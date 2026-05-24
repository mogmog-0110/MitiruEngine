/*!
 * mitiru_crash_reporter.js — uncaught-error capture + export (NF-05)
 *
 * Hooks window.onerror and unhandledrejection to capture crash reports,
 * persists them to localStorage, and provides export / download helpers
 * for player → developer handoff from the Title screen.
 *
 * Implements spec: docs/engine-feedback-20260424b NF-05
 *
 * ── API ──────────────────────────────────────────────────────────────────────
 *   mitiru.crashReporter.install(opts?)        hooks onerror + unhandledrejection
 *   mitiru.crashReporter.uninstall()           removes listeners
 *   mitiru.crashReporter.record(error, extra?) manually record a report
 *   mitiru.crashReporter.list()                array of captured reports (newest first)
 *   mitiru.crashReporter.clear()               drop all reports
 *   mitiru.crashReporter.count()               number of reports
 *   mitiru.crashReporter.exportBlob()          Blob (application/json)
 *   mitiru.crashReporter.exportString()        plain JSON string
 *   mitiru.crashReporter.triggerDownload(filename?)  <a download> trigger
 *   mitiru.crashReporter.setStateSnapshotFn(fn)  register state snapshot fn
 *   mitiru.crashReporter.setMaxReports(n)      default 20; FIFO cap
 *   mitiru.crashReporter.on(event, cb)         'capture' event listener
 *   mitiru.crashReporter.off(event, cb)        remove listener
 *
 * ── install() defaults ───────────────────────────────────────────────────────
 *   autoLoad:   true
 *   persist:    true
 *   maxReports: 20
 *   key:        'mitiru.crashReports'
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.crashReporter) { return; }  // already loaded

	// ── constants ──────────────────────────────────────────────────
	var DEFAULT_KEY        = 'mitiru.crashReports';
	var DEFAULT_MAX        = 20;

	// ── private state ──────────────────────────────────────────────
	var _reports       = [];          // newest first
	var _maxReports    = DEFAULT_MAX;
	var _snapshotFn    = null;
	var _listeners     = { capture: [] };

	// install config (nulled on uninstall)
	var _installed     = false;
	var _persistKey    = DEFAULT_KEY;
	var _doPersist     = true;

	// saved prior handlers for chain-call on uninstall
	var _prevOnerror   = null;
	var _rejHandler    = null;
	var _onerrorBound  = null;

	// ── helpers ────────────────────────────────────────────────────
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

	// Returns the JSON string, or null if serialisation fails (circular ref etc.)
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
		// Verify it is JSON-serialisable (catches circular refs, BigInt, etc.)
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
			catch (_e) { /* listener errors must not crash the reporter */ }
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
			/* localStorage full or unavailable — silently skip */
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
		// Prepend (newest first).
		_reports = [report].concat(_reports);
		// Enforce FIFO cap: drop from the tail (oldest).
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

	// ── onerror / unhandledrejection handlers ──────────────────────
	function _onerrorHandler(msg, src, lineno, colno, errorObj)
	{
		var report = _buildReport('error', errorObj || msg, src, lineno, colno, null);
		_pushReport(report);
		// Chain to prior handler if any.
		if (typeof _prevOnerror === 'function')
		{
			return _prevOnerror(msg, src, lineno, colno, errorObj);
		}
		return false;  // do not suppress default browser handling
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
	 * Install window.onerror and unhandledrejection hooks.
	 * Safe to call multiple times — uninstalls old hooks first.
	 *
	 * @param {object} [opts]
	 * @param {boolean} [opts.autoLoad=true]   load prior reports from localStorage
	 * @param {boolean} [opts.persist=true]    write to localStorage on each capture
	 * @param {number}  [opts.maxReports=20]   FIFO cap
	 * @param {string}  [opts.key]             localStorage key
	 */
	crashReporter.install = function(opts)
	{
		// Idempotent: remove old handlers before re-installing.
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
			// Merge: stored are already newest-first; current in-memory prepend.
			_reports = _reports.concat(stored);
			if (_reports.length > _maxReports)
			{
				_reports = _reports.slice(0, _maxReports);
			}
		}

		// Save and replace window.onerror.
		_prevOnerror = global.onerror || null;
		_onerrorBound = _onerrorHandler;
		global.onerror = _onerrorBound;

		// Add unhandledrejection listener.
		_rejHandler = _rejectionHandler;
		global.addEventListener('unhandledrejection', _rejHandler);

		_installed = true;
	};

	/**
	 * Remove installed hooks.
	 */
	crashReporter.uninstall = function()
	{
		if (!_installed) { return; }

		// Restore prior onerror.
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
	 * Manually record an error (for non-throwing code paths).
	 *
	 * @param {Error|string} error
	 * @param {*} [extra]  any JSON-serialisable extra context
	 */
	crashReporter.record = function(error, extra)
	{
		var report = _buildReport('manual', error, '', 0, 0, extra);
		_pushReport(report);
	};

	/** Return all captured reports, newest first. @returns {Array} */
	crashReporter.list = function() { return _reports.slice(); };

	/** Drop all in-memory reports and clear localStorage entry. */
	crashReporter.clear = function()
	{
		_reports = [];
		try { global.localStorage.removeItem(_persistKey); }
		catch (_e) { /* ignore */ }
	};

	/** Number of captured reports. @returns {number} */
	crashReporter.count = function() { return _reports.length; };

	/** Export all reports as a Blob (application/json). @returns {Blob} */
	crashReporter.exportBlob = function()
	{
		return new Blob([crashReporter.exportString()], { type: 'application/json' });
	};

	/** Export all reports as a plain JSON string. @returns {string} */
	crashReporter.exportString = function()
	{
		return JSON.stringify(_reports, null, 2);
	};

	/**
	 * Trigger a browser file-download of the crash log.
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
	 * Register a function called at capture time to attach game state.
	 * fn() must return a JSON-serialisable object.
	 * If fn throws, report.state is set to { __snapshotError: message }.
	 * If the return value is not serialisable, { __nonSerialisable: true }.
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
	 * Set the FIFO cap on stored reports.  Older reports beyond n are dropped immediately.
	 * @param {number} n  positive integer
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
	 * Subscribe to an event.
	 * @param {'capture'} event
	 * @param {function}  cb    called with the report object
	 */
	crashReporter.on = function(event, cb)
	{
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
	};

	/**
	 * Unsubscribe from an event.
	 * @param {'capture'} event
	 * @param {function}  cb
	 */
	crashReporter.off = function(event, cb)
	{
		if (!_listeners[event]) { return; }
		_listeners[event] = _listeners[event].filter(function(fn) { return fn !== cb; });
	};

})(typeof window !== 'undefined' ? window : globalThis);
