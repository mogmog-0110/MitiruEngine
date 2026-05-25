/*!
 * mitiru_i18n.js — locale / 翻訳 runtime (F-07)
 *
 * UI 文字列用の極小な翻訳 layer。locale ごとの JSON table を読み込み、
 * dot-path で key を引き、named parameter を補間し、`data-i18n` を付けた
 * DOM element を live-bind する。
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.i18n.load(locale, source)        Promise<void>
 *                                           source: URL string | inline object
 *                                           省略時は baseDir + locale + '.json' を使う
 *   mitiru.i18n.addLocale(locale, data)     void — fetch せずに登録
 *   mitiru.i18n.setLocale(locale)           void — active locale を切り替える
 *   mitiru.i18n.locale()                    string —現在 active な locale
 *   mitiru.i18n.locales()                   string[] — 読み込み済み locale
 *   mitiru.i18n.t(key, params?)             string — 翻訳 + 補間
 *   mitiru.i18n.has(key, locale?)           boolean
 *   mitiru.i18n.onLocaleChange(cb)          unsubscribe fn
 *   mitiru.i18n.bindDOM(root?)              [data-i18n] / [data-i18n-attr] を走査
 *   mitiru.i18n.unbindDOM(root?)            `root` の registry を clear
 *   mitiru.i18n.setFontMap(map)             { ja: 'Noto Sans JP', en: 'Nunito' }
 *   mitiru.i18n.fontFamily(locale?)         string | null
 *   mitiru.i18n.setBaseDir(path)            default 'locales/'
 *   mitiru.i18n.setFallback(locale)         例 'en' — key 欠落時に使う
 *
 * ── Locale JSON 形式 ──────────────────────────────────────────────────────
 *   flat ("menu.title": "…") か nested ({"menu": {"title": "…"}}) のどちらか。
 *   lookup はまず literal な flat key を試し、次に dot-path 走査する。
 *
 *   補間: "{name}" placeholder を params から置換する。
 *     t('day.counter', {n: 3, total: 12})   → "DAY 3 / 12"
 *
 * ── DOM binding ─────────────────────────────────────────────────────────────
 *   <span data-i18n="menu.title"></span>                    → textContent
 *   <button data-i18n="tooltip"
 *           data-i18n-attr="title"></button>                → element.title
 *   <span data-i18n="day.counter"
 *         data-i18n-params='{"n":3,"total":12}'></span>     → 補間される
 *
 *   bind した element は root ごとに追跡され、再 bind は冪等。以降の
 *   setLocale() 呼び出しで翻訳が再適用される。
 *
 * ── Font swap ───────────────────────────────────────────────────────────────
 *   setFontMap({ ja: '"Noto Sans JP", sans-serif', en: '"Nunito", sans-serif' })
 *   setLocale() 時に document.documentElement へ CSS custom property
 *   --mitiru-locale-font を書く。mitiru_tokens.css / app CSS は次で参照する:
 *       body { font-family: var(--mitiru-locale-font, inherit); }
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-07
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.i18n) { return; }  // 読み込み済み

	// ── internal state ──────────────────────────────────────────
	const _tables    = Object.create(null);  // locale -> table object (flat か nested)
	let   _active    = '';
	let   _fallback  = '';
	let   _baseDir   = 'locales/';
	const _fontMap   = Object.create(null);  // locale -> css font-family
	const _listeners = [];                   // onLocaleChange callback
	const _bindings  = [];                   // [{root, records: [{el, key, attr, params}]}]

	// ── helpers ─────────────────────────────────────────────────
	function _lookup(table, key)
	{
		// flat lookup を優先する (dot 付き key の flat JSON に対応)。
		if (Object.prototype.hasOwnProperty.call(table, key))
		{
			const v = table[key];
			if (typeof v === 'string') { return v; }
		}
		// Fallback: dot-path 走査。
		const parts = key.split('.');
		let cur = table;
		for (let i = 0; i < parts.length; ++i)
		{
			if (cur === null || typeof cur !== 'object') { return undefined; }
			cur = cur[parts[i]];
		}
		return typeof cur === 'string' ? cur : undefined;
	}

	function _interpolate(str, params)
	{
		if (!params || typeof params !== 'object') { return str; }
		return str.replace(/\{(\w+)\}/g, function(_m, name)
		{
			const v = params[name];
			return (v === undefined || v === null) ? ('{' + name + '}') : String(v);
		});
	}

	function _applyFont()
	{
		if (typeof document === 'undefined') { return; }
		const family = _fontMap[_active];
		const el = document.documentElement;
		if (!el || !el.style) { return; }
		if (family) { el.style.setProperty('--mitiru-locale-font', family); }
		else        { el.style.removeProperty('--mitiru-locale-font'); }
	}

	function _notifyLocaleChange(prev, next)
	{
		const copy = _listeners.slice();
		for (let i = 0; i < copy.length; ++i)
		{
			try { copy[i](next, prev); }
			catch (e) { console.error('[mitiru.i18n] onLocaleChange threw:', e); }
		}
	}

	function _applyRecord(rec)
	{
		if (!rec.el || !rec.el.isConnected) { return; }
		const text = i18n.t(rec.key, rec.params);
		if (rec.attr) { rec.el.setAttribute(rec.attr, text); }
		else          { rec.el.textContent = text; }
	}

	function _applyAll()
	{
		for (let i = 0; i < _bindings.length; ++i)
		{
			const slot = _bindings[i];
			const live = [];
			for (let j = 0; j < slot.records.length; ++j)
			{
				const rec = slot.records[j];
				if (rec.el && rec.el.isConnected) { _applyRecord(rec); live.push(rec); }
			}
			slot.records = live;
		}
	}

	function _parseParams(raw)
	{
		if (!raw) { return null; }
		try { return JSON.parse(raw); }
		catch (_e) { return null; }
	}

	// ── public API ──────────────────────────────────────────────
	const i18n = {};

	i18n.setBaseDir = function(path)
	{
		_baseDir = typeof path === 'string' ? path : 'locales/';
		if (_baseDir && _baseDir[_baseDir.length - 1] !== '/') { _baseDir += '/'; }
	};

	i18n.setFallback = function(locale)
	{
		_fallback = typeof locale === 'string' ? locale : '';
	};

	i18n.addLocale = function(locale, data)
	{
		if (typeof locale !== 'string' || !locale) { throw new Error('mitiru.i18n.addLocale: locale required'); }
		if (data === null || typeof data !== 'object') { throw new Error('mitiru.i18n.addLocale: data must be an object'); }
		_tables[locale] = data;
		if (!_active) { _active = locale; _applyFont(); }
	};

	i18n.load = function(locale, source)
	{
		if (typeof locale !== 'string' || !locale)
		{
			return Promise.reject(new Error('mitiru.i18n.load: locale required'));
		}
		// Inline data path — fetch なし。
		if (source && typeof source === 'object')
		{
			i18n.addLocale(locale, source);
			return Promise.resolve();
		}
		// URL path — 可能なら mitiru.fetch 経由で取得。
		const url = (typeof source === 'string' && source)
			? source
			: (_baseDir + locale + '.json');
		const fetchFn = (mitiru.fetch) ? mitiru.fetch : (typeof fetch !== 'undefined' ? fetch : null);
		if (!fetchFn)
		{
			return Promise.reject(new Error('mitiru.i18n.load: fetch is not available'));
		}
		return fetchFn(url).then(function(r)
		{
			if (!r.ok) { throw new Error('mitiru.i18n.load: HTTP ' + r.status + ' for ' + url); }
			return r.json();
		}).then(function(data)
		{
			i18n.addLocale(locale, data);
		});
	};

	i18n.setLocale = function(locale)
	{
		if (typeof locale !== 'string' || !locale) { throw new Error('mitiru.i18n.setLocale: locale required'); }
		if (!_tables[locale])                      { throw new Error('mitiru.i18n.setLocale: locale not loaded: ' + locale); }
		if (_active === locale)                    { return; }
		const prev = _active;
		_active = locale;
		_applyFont();
		_applyAll();
		_notifyLocaleChange(prev, locale);
	};

	i18n.locale   = function() { return _active; };
	i18n.locales  = function() { return Object.keys(_tables); };

	i18n.has = function(key, locale)
	{
		const loc   = locale || _active;
		const table = _tables[loc];
		if (!table) { return false; }
		return _lookup(table, key) !== undefined;
	};

	i18n.t = function(key, params)
	{
		if (typeof key !== 'string' || !key) { return ''; }
		let str;
		if (_active && _tables[_active])       { str = _lookup(_tables[_active], key); }
		if (str === undefined && _fallback && _tables[_fallback])
		{
			str = _lookup(_tables[_fallback], key);
		}
		if (str === undefined) { return key; }    // key 欠落時の fallback
		return _interpolate(str, params);
	};

	i18n.onLocaleChange = function(cb)
	{
		if (typeof cb !== 'function') { throw new Error('mitiru.i18n.onLocaleChange: cb must be a function'); }
		_listeners.push(cb);
		return function unsubscribe()
		{
			const idx = _listeners.indexOf(cb);
			if (idx >= 0) { _listeners.splice(idx, 1); }
		};
	};

	i18n.bindDOM = function(root)
	{
		if (typeof document === 'undefined') { return 0; }
		const host = root || document;
		// この root の既存 slot を破棄する (再 bind は冪等)。
		for (let i = _bindings.length - 1; i >= 0; --i)
		{
			if (_bindings[i].root === host) { _bindings.splice(i, 1); }
		}
		const nodes = host.querySelectorAll('[data-i18n]');
		const records = [];
		for (let i = 0; i < nodes.length; ++i)
		{
			const el  = nodes[i];
			const key = el.getAttribute('data-i18n');
			if (!key) { continue; }
			const attr   = el.getAttribute('data-i18n-attr') || '';
			const params = _parseParams(el.getAttribute('data-i18n-params'));
			const rec    = { el: el, key: key, attr: attr, params: params };
			records.push(rec);
			_applyRecord(rec);
		}
		_bindings.push({ root: host, records: records });
		return records.length;
	};

	i18n.unbindDOM = function(root)
	{
		const host = root || (typeof document !== 'undefined' ? document : null);
		if (!host) { return 0; }
		let removed = 0;
		for (let i = _bindings.length - 1; i >= 0; --i)
		{
			if (_bindings[i].root === host)
			{
				removed += _bindings[i].records.length;
				_bindings.splice(i, 1);
			}
		}
		return removed;
	};

	i18n.setFontMap = function(map)
	{
		if (map === null || typeof map !== 'object') { throw new Error('mitiru.i18n.setFontMap: map must be an object'); }
		// map 全体を置き換える。
		const keys = Object.keys(_fontMap);
		for (let i = 0; i < keys.length; ++i) { delete _fontMap[keys[i]]; }
		const nk = Object.keys(map);
		for (let i = 0; i < nk.length; ++i) { _fontMap[nk[i]] = map[nk[i]]; }
		_applyFont();
	};

	i18n.fontFamily = function(locale)
	{
		const loc = locale || _active;
		return _fontMap[loc] || null;
	};

	// ── export ──────────────────────────────────────────────────
	mitiru.i18n = i18n;

})(typeof window !== 'undefined' ? window : globalThis);
