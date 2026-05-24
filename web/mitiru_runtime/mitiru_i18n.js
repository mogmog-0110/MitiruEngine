/*!
 * mitiru_i18n.js — locale / translation runtime (F-07)
 *
 * Tiny translation layer for UI strings. Loads per-locale JSON tables,
 * looks up keys via dot-path, interpolates named parameters, and live-binds
 * DOM elements tagged with `data-i18n`.
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.i18n.load(locale, source)        Promise<void>
 *                                           source: URL string | inline object
 *                                           If omitted, uses baseDir + locale + '.json'
 *   mitiru.i18n.addLocale(locale, data)     void — register without fetching
 *   mitiru.i18n.setLocale(locale)           void — switch active locale
 *   mitiru.i18n.locale()                    string — current active locale
 *   mitiru.i18n.locales()                   string[] — loaded locales
 *   mitiru.i18n.t(key, params?)             string — translate + interpolate
 *   mitiru.i18n.has(key, locale?)           boolean
 *   mitiru.i18n.onLocaleChange(cb)          unsubscribe fn
 *   mitiru.i18n.bindDOM(root?)              scans [data-i18n] / [data-i18n-attr]
 *   mitiru.i18n.unbindDOM(root?)            clears the registry for `root`
 *   mitiru.i18n.setFontMap(map)             { ja: 'Noto Sans JP', en: 'Nunito' }
 *   mitiru.i18n.fontFamily(locale?)         string | null
 *   mitiru.i18n.setBaseDir(path)            default 'locales/'
 *   mitiru.i18n.setFallback(locale)         e.g. 'en' — used when key missing
 *
 * ── Locale JSON format ──────────────────────────────────────────────────────
 *   Either flat ("menu.title": "…") or nested ({"menu": {"title": "…"}}).
 *   Lookup tries the literal flat key first, then dot-path traversal.
 *
 *   Interpolation: "{name}" placeholders replaced from params.
 *     t('day.counter', {n: 3, total: 12})   → "DAY 3 / 12"
 *
 * ── DOM binding ─────────────────────────────────────────────────────────────
 *   <span data-i18n="menu.title"></span>                    → textContent
 *   <button data-i18n="tooltip"
 *           data-i18n-attr="title"></button>                → element.title
 *   <span data-i18n="day.counter"
 *         data-i18n-params='{"n":3,"total":12}'></span>     → interpolated
 *
 *   Bound elements are tracked per-root so re-binding is idempotent and
 *   subsequent setLocale() calls re-apply translations.
 *
 * ── Font swap ───────────────────────────────────────────────────────────────
 *   setFontMap({ ja: '"Noto Sans JP", sans-serif', en: '"Nunito", sans-serif' })
 *   On setLocale(), writes CSS custom property --mitiru-locale-font on
 *   document.documentElement.  mitiru_tokens.css / app CSS reference it via
 *       body { font-family: var(--mitiru-locale-font, inherit); }
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-07
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.i18n) { return; }  // already loaded

	// ── internal state ──────────────────────────────────────────
	const _tables    = Object.create(null);  // locale -> table object (flat or nested)
	let   _active    = '';
	let   _fallback  = '';
	let   _baseDir   = 'locales/';
	const _fontMap   = Object.create(null);  // locale -> css font-family
	const _listeners = [];                   // onLocaleChange callbacks
	const _bindings  = [];                   // [{root, records: [{el, key, attr, params}]}]

	// ── helpers ─────────────────────────────────────────────────
	function _lookup(table, key)
	{
		// Flat lookup takes priority (supports flat JSON with dotted keys).
		if (Object.prototype.hasOwnProperty.call(table, key))
		{
			const v = table[key];
			if (typeof v === 'string') { return v; }
		}
		// Fallback: dot-path traversal.
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
		// Inline data path — no fetch.
		if (source && typeof source === 'object')
		{
			i18n.addLocale(locale, source);
			return Promise.resolve();
		}
		// URL path — fetch via mitiru.fetch if available.
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
		if (str === undefined) { return key; }    // missing-key fallback
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
		// Drop any existing slot for this root (re-binding is idempotent).
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
		// Replace entire map.
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
