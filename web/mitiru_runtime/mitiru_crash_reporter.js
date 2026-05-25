/*!
 * mitiru_crash_reporter.js — uncaught-error の capture + export (NF-05)
 *
 * window.onerror と unhandledrejection を hook して crash report を capture し、
 * localStorage に永続化する。Title 画面から player → developer へ渡すための
 * export / download ヘルパーを提供する。
 *
 * 仕様: docs/engine-feedback-20260424b NF-05
 *
 * ── API ──────────────────────────────────────────────────────────────────────
 *   mitiru.crashReporter.install(opts?)        onerror + unhandledrejection を hook
 *   mitiru.crashReporter.uninstall()           listener を除去
 *   mitiru.crashReporter.record(error, extra?) report を手動記録
 *   mitiru.crashReporter.list()                capture した report の配列 (新しい順)
 *   mitiru.crashReporter.clear()               全 report を破棄
 *   mitiru.crashReporter.count()               report 数
 *   mitiru.crashReporter.exportBlob()          Blob (application/json)
 *   mitiru.crashReporter.exportString()        plain JSON 文字列
 *   mitiru.crashReporter.triggerDownload(filename?)  <a download> を起動
 *   mitiru.crashReporter.setStateSnapshotFn(fn)  state snapshot fn を登録
 *   mitiru.crashReporter.setMaxReports(n)      default 20; FIFO cap
 *   mitiru.crashReporter.on(event, cb)         'capture' event listener
 *   mitiru.crashReporter.off(event, cb)        listener を除去
 *
 * ── install() の default ───────────────────────────────────────────────────────
 *   autoLoad:   true
 *   persist:    true
 *   maxReports: 20
 *   key:        'mitiru.crashReports'
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.crashReporter) { return; }  // ロード済み

	// ── 定数 ──────────────────────────────────────────────────
	var DEFAULT_KEY        = 'mitiru.crashReports';
	var DEFAULT_MAX        = 20;

	// ── private 状態 ──────────────────────────────────────────────
	var _reports       = [];          // 新しい順
	var _maxReports    = DEFAULT_MAX;
	var _snapshotFn    = null;
	var _listeners     = { capture: [] };

	// install 設定 (uninstall 時に null 化)
	var _installed     = false;
	var _persistKey    = DEFAULT_KEY;
	var _doPersist     = true;

	// uninstall 時に chain-call するため保存した旧 handler
	var _prevOnerror   = null;
	var _rejHandler    = null;
	var _onerrorBound  = null;

	// ── ヘルパー ────────────────────────────────────────────────────
	function _hex(n)
	{
		return n.toString(16).padStart(4, '0');
	}

	function _makeId()
	{
		var ts  = Date.now();
		var rnd = Math.floor(Math.random() * 0xffff);
		return 'crash-' + ts + '-' + _hex(rnd);
	}

	// JSON 文字列を返す。serialise 失敗時 (循環参照等) は null。
	function _safeStringify(obj)
	{
		try
		{
			return JSON.stringify(obj);
		}
		catch (_e)
		{
			return null;
		}
	}

	function _captureSnapshot()
	{
		if (typeof _snapshotFn !== 'function') { return null; }
		var snap;
		try
		{
			snap = _snapshotFn();
		}
		catch (e)
		{
			return { __snapshotError: (e && e.message) ? e.message : String(e) };
		}
		// JSON-serialise 可能か検証 (循環参照, BigInt 等を捕捉)
		var str = _safeStringify(snap);
		if (str === null) { return { __nonSerialisable: true }; }
		return snap;
	}

	function _stackFrom(error)
	{
		if (error && typeof error.stack === 'string') { return error.stack; }
		return '';
	}

	function _messageFrom(error)
	{
		if (error === null || error === undefined) { return String(error); }
		if (typeof error === 'string') { return error; }
		if (typeof error.message === 'string') { return error.message; }
		return String(error);
	}

	function _emit(event, data)
	{
		var cbs = _listeners[event];
		if (!cbs) { return; }
		for (var i = 0; i < cbs.length; ++i)
		{
			try { cbs[i](data); }
			catch (_e) { /* listener の error で reporter を crash させない */ }
		}
	}

	function _persist()
	{
		if (!_doPersist) { return; }
		try
		{
			global.localStorage.setItem(_persistKey, JSON.stringify(_reports));
		}
		catch (_e)
		{
			/* localStorage が満杯 or 利用不可 — 黙って skip */
		}
	}

	function _loadFromStorage(key)
	{
		try
		{
			var raw = global.localStorage.getItem(key);
			if (!raw) { return []; }
			var parsed = JSON.parse(raw);
			return Array.isArray(parsed) ? parsed : [];
		}
		catch (_e)
		{
			return [];
		}
	}

	function _pushReport(report)
	{
		// 先頭に追加 (新しい順)。
		_reports = [report].concat(_reports);
		// FIFO cap を適用: 末尾 (最古) から落とす。
		if (_reports.length > _maxReports)
		{
			_reports = _reports.slice(0, _maxReports);
		}
		_persist();
		_emit('capture', report);
	}

	function _buildReport(kind, error, source, line, col, extra)
	{
		var now = Date.now();
		return {
			id:        _makeId(),
			ts:        now,
			when:      new Date(now).toISOString(),
			kind:      kind,
			message:   _messageFrom(error),
			stack:     _stackFrom(error),
			source:    source  || '',
			line:      line    || 0,
			column:    col     || 0,
			userAgent: (global.navigator && global.navigator.userAgent) ? global.navigator.userAgent : '',
			url:       (global.location  && global.location.href)       ? global.location.href       : '',
			state:     _captureSnapshot(),
			extra:     (extra !== undefined) ? extra : null,
		};
	}

	// ── onerror / unhandledrejection の handler ──────────────────────
	function _onerrorHandler(msg, src, lineno, colno, errorObj)
	{
		var report = _buildReport('error', errorObj || msg, src, lineno, colno, null);
		_pushReport(report);
		// 旧 handler があれば chain する。
		if (typeof _prevOnerror === 'function')
		{
			return _prevOnerror(msg, src, lineno, colno, errorObj);
		}
		return false;  // browser の default 処理を抑制しない
	}

	function _rejectionHandler(evt)
	{
		var reason = evt && evt.reason;
		var report = _buildReport('unhandled-rejection', reason, '', 0, 0, null);
		_pushReport(report);
	}

	// ── public API ─────────────────────────────────────────────────
	var crashReporter = mitiru.crashReporter = {};

	/**
	 * window.onerror と unhandledrejection の hook を install する。
	 * 複数回呼んでも安全 — 先に旧 hook を uninstall する。
	 *
	 * @param {object} [opts]
	 * @param {boolean} [opts.autoLoad=true]   過去の report を localStorage からロード
	 * @param {boolean} [opts.persist=true]    capture ごとに localStorage へ書き込む
	 * @param {number}  [opts.maxReports=20]   FIFO cap
	 * @param {string}  [opts.key]             localStorage key
	 */
	crashReporter.install = function(opts)
	{
		// 冪等: 再 install 前に旧 handler を除去する。
		if (_installed) { crashReporter.uninstall(); }

		var o = (opts && typeof opts === 'object') ? opts : {};
		var autoLoad  = (o.autoLoad  !== undefined) ? !!o.autoLoad  : true;
		var persist   = (o.persist   !== undefined) ? !!o.persist   : true;
		var maxRep    = (typeof o.maxReports === 'number' && o.maxReports > 0)
		                ? Math.floor(o.maxReports) : DEFAULT_MAX;
		var key       = (typeof o.key === 'string' && o.key) ? o.key : DEFAULT_KEY;

		_maxReports = maxRep;
		_doPersist  = persist;
		_persistKey = key;

		if (autoLoad)
		{
			var stored = _loadFromStorage(key);
			// マージ: stored は既に新しい順; 現在の in-memory 分を前に付ける。
			_reports = _reports.concat(stored);
			if (_reports.length > _maxReports)
			{
				_reports = _reports.slice(0, _maxReports);
			}
		}

		// window.onerror を保存して差し替える。
		_prevOnerror = global.onerror || null;
		_onerrorBound = _onerrorHandler;
		global.onerror = _onerrorBound;

		// unhandledrejection listener を追加する。
		_rejHandler = _rejectionHandler;
		global.addEventListener('unhandledrejection', _rejHandler);

		_installed = true;
	};

	/**
	 * install 済みの hook を除去する。
	 */
	crashReporter.uninstall = function()
	{
		if (!_installed) { return; }

		// 旧 onerror を復元する。
		if (global.onerror === _onerrorBound)
		{
			global.onerror = _prevOnerror;
		}
		_onerrorBound = null;
		_prevOnerror  = null;

		if (_rejHandler)
		{
			global.removeEventListener('unhandledrejection', _rejHandler);
			_rejHandler = null;
		}

		_installed = false;
	};

	/**
	 * error を手動記録する (throw しない code path 向け)。
	 *
	 * @param {Error|string} error
	 * @param {*} [extra]  JSON-serialise 可能な追加コンテキスト
	 */
	crashReporter.record = function(error, extra)
	{
		var report = _buildReport('manual', error, '', 0, 0, extra);
		_pushReport(report);
	};

	/** capture した全 report を新しい順で返す。 @returns {Array} */
	crashReporter.list = function() { return _reports.slice(); };

	/** in-memory の全 report を破棄し localStorage entry をクリアする。 */
	crashReporter.clear = function()
	{
		_reports = [];
		try { global.localStorage.removeItem(_persistKey); }
		catch (_e) { /* 無視 */ }
	};

	/** capture した report 数。 @returns {number} */
	crashReporter.count = function() { return _reports.length; };

	/** 全 report を Blob (application/json) として export する。 @returns {Blob} */
	crashReporter.exportBlob = function()
	{
		return new Blob([crashReporter.exportString()], { type: 'application/json' });
	};

	/** 全 report を plain JSON 文字列として export する。 @returns {string} */
	crashReporter.exportString = function()
	{
		return JSON.stringify(_reports, null, 2);
	};

	/**
	 * crash log の browser file-download を起動する。
	 * @param {string} [filename='mitiru_crash.json']
	 */
	crashReporter.triggerDownload = function(filename)
	{
		var name = (typeof filename === 'string' && filename)
		           ? filename : 'mitiru_crash.json';
		var blob = crashReporter.exportBlob();
		var url  = URL.createObjectURL(blob);
		var a    = document.createElement('a');
		a.href     = url;
		a.download = name;
		a.style.display = 'none';
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
		setTimeout(function() { URL.revokeObjectURL(url); }, 100);
	};

	/**
	 * capture 時に game state を添付するため呼ばれる関数を登録する。
	 * fn() は JSON-serialise 可能な object を返す必要がある。
	 * fn が throw した場合、report.state は { __snapshotError: message } になる。
	 * 戻り値が serialise 不可なら { __nonSerialisable: true } になる。
	 *
	 * @param {function|null} fn
	 */
	crashReporter.setStateSnapshotFn = function(fn)
	{
		if (fn !== null && typeof fn !== 'function')
		{
			throw new TypeError('mitiru.crashReporter.setStateSnapshotFn: fn must be a function or null');
		}
		_snapshotFn = fn;
	};

	/**
	 * 保存 report の FIFO cap を設定する。n を超える古い report は即座に破棄される。
	 * @param {number} n  正の整数
	 */
	crashReporter.setMaxReports = function(n)
	{
		if (typeof n !== 'number' || !Number.isInteger(n) || n < 1)
		{
			throw new RangeError('mitiru.crashReporter.setMaxReports: n must be a positive integer');
		}
		_maxReports = n;
		if (_reports.length > _maxReports)
		{
			_reports = _reports.slice(0, _maxReports);
		}
	};

	/**
	 * event を購読する。
	 * @param {'capture'} event
	 * @param {function}  cb    report object を引数に呼ばれる
	 */
	crashReporter.on = function(event, cb)
	{
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
	};

	/**
	 * event を購読解除する。
	 * @param {'capture'} event
	 * @param {function}  cb
	 */
	crashReporter.off = function(event, cb)
	{
		if (!_listeners[event]) { return; }
		_listeners[event] = _listeners[event].filter(function(fn) { return fn !== cb; });
	};

})(typeof window !== 'undefined' ? window : globalThis);
