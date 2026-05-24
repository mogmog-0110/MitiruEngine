/*!
 * mitiru_preloader.js — boot-time asset preloader + progress UI (NF-04)
 *
 * Registered manifest list, progress bar mount, fade-out on completion.
 * Every scene can opt in or skip independently.
 *
 * Public API:
 *   mitiru.preloader.register(items)         Register asset items to load.
 *   mitiru.preloader.mount(containerEl, opts) Build progress-bar DOM.
 *   mitiru.preloader.unmount()               Remove from DOM; safe to re-mount.
 *   mitiru.preloader.start()                 → Promise<{loaded, failed}>
 *   mitiru.preloader.get(key)                Resolved value after start().
 *   mitiru.preloader.progress()              { done, total, fraction, currentKey }
 *   mitiru.preloader.on(event, cb)           Subscribe: 'progress'|'item:done'|'item:fail'|'complete'
 *   mitiru.preloader.off(event, cb)          Unsubscribe.
 *   mitiru.preloader.clear()                 Reset registry + resolved map.
 *
 * Events:
 *   'progress'   { done, total, fraction, currentKey }
 *   'item:done'  { key, kind, value }
 *   'item:fail'  { key, kind, error }
 *   'complete'   { loaded: string[], failed: string[] }
 *
 * Depends on mitiru.fetch (E-15) when available; falls back to global fetch.
 *
 * Implements spec: NF-04
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.preloader) { return; }  // already loaded

	const document = global.document;

	// ── capability checks (warn once) ────────────────────────────
	var _warnedFetch = false;
	var _warnedImage = false;
	var _warnedAudio = false;

	function _hasFetch()
	{
		if (typeof (mitiru.fetch || global.fetch) !== 'function')
		{
			if (!_warnedFetch)
			{
				_warnedFetch = true;
				console.warn('[mitiru.preloader] fetch unavailable; fetch/json/text items will be skipped');
			}
			return false;
		}
		return true;
	}

	function _hasImage()
	{
		if (typeof global.Image !== 'function')
		{
			if (!_warnedImage)
			{
				_warnedImage = true;
				console.warn('[mitiru.preloader] Image unavailable; image items will be skipped');
			}
			return false;
		}
		return true;
	}

	function _hasAudio()
	{
		if (typeof global.Audio !== 'function')
		{
			if (!_warnedAudio)
			{
				_warnedAudio = true;
				console.warn('[mitiru.preloader] Audio unavailable; audio items will be skipped');
			}
			return false;
		}
		return true;
	}

	// ── internal state ────────────────────────────────────────────
	var _items    = [];    // registered items: [{kind, path, key}]
	var _resolved = {};    // key → loaded value
	var _listeners = {};   // event → [cb, ...]

	var _done    = 0;
	var _total   = 0;
	var _current = '';
	var _running = false;

	// DOM state
	var _root     = null;  // mounted container div
	var _fill     = null;  // .mitiru-preloader__fill
	var _label    = null;  // .mitiru-preloader__label
	var _mountOpts = null;

	// ── event helpers ─────────────────────────────────────────────

	function _emit(event, data)
	{
		var cbs = _listeners[event];
		if (!cbs) { return; }
		for (var i = 0; i < cbs.length; ++i)
		{
			try { cbs[i](data); } catch (e) { console.error('[mitiru.preloader] event handler threw:', e); }
		}
	}

	// ── loader helpers ────────────────────────────────────────────

	function _doFetch(path)
	{
		var fn = mitiru.fetch || global.fetch;
		return fn(path);
	}

	function _loadItem(item)
	{
		var kind = item.kind;

		if (kind === 'json')
		{
			if (!_hasFetch()) { return Promise.reject(new Error('fetch unavailable')); }
			return _doFetch(item.path).then(function(r) { return r.json(); });
		}

		if (kind === 'text')
		{
			if (!_hasFetch()) { return Promise.reject(new Error('fetch unavailable')); }
			return _doFetch(item.path).then(function(r) { return r.text(); });
		}

		if (kind === 'fetch')
		{
			if (!_hasFetch()) { return Promise.reject(new Error('fetch unavailable')); }
			return _doFetch(item.path);
		}

		if (kind === 'image')
		{
			if (!_hasImage()) { return Promise.reject(new Error('Image unavailable')); }
			return new Promise(function(resolve, reject)
			{
				var img = new global.Image();
				img.onload  = function() { resolve(img); };
				img.onerror = function(e) { reject(new Error('image load failed: ' + item.path)); };
				img.src = item.path;
			});
		}

		if (kind === 'audio')
		{
			if (!_hasAudio()) { return Promise.reject(new Error('Audio unavailable')); }
			return new Promise(function(resolve, reject)
			{
				var audio = new global.Audio();
				audio.addEventListener('canplaythrough', function() { resolve(audio); });
				audio.addEventListener('error', function() { reject(new Error('audio load failed: ' + item.path)); });
				audio.src = item.path;
				audio.load();
			});
		}

		return Promise.reject(new Error('unknown kind: ' + kind));
	}

	// ── concurrency pool ─────────────────────────────────────────

	function _runPool(items, concurrency, onItem)
	{
		return new Promise(function(resolve)
		{
			var idx     = 0;
			var active  = 0;
			var total   = items.length;
			var settled = 0;

			if (total === 0) { resolve(); return; }

			function next()
			{
				while (active < concurrency && idx < total)
				{
					(function(item)
					{
						active++;
						onItem(item).then(function()
						{
							active--;
							settled++;
							if (settled === total) { resolve(); }
							else { next(); }
						});
					})(items[idx++]);
				}
			}

			next();
		});
	}

	// ── UI helpers ───────────────────────────────────────────────

	function _updateUI()
	{
		if (!_fill || !_label) { return; }

		var opts      = _mountOpts || {};
		var frac      = _total > 0 ? (_done / _total) : 0;
		var pct       = (frac * 100).toFixed(1) + '%';

		_fill.style.width = pct;

		var base  = typeof opts.label === 'string' ? opts.label : 'Loading…';
		var show  = opts.showCount !== false;
		_label.textContent = base + (show ? (' ' + _done + ' / ' + _total) : '');
	}

	function _triggerFadeOut()
	{
		if (!_root) { return; }
		var opts  = _mountOpts || {};
		var ms    = typeof opts.fadeMs === 'number' ? opts.fadeMs : 400;
		_root.style.transition = 'opacity ' + ms + 'ms ease';
		_root.style.opacity    = '0';
		var captured = _root;
		setTimeout(function()
		{
			if (captured && captured.parentNode) { captured.parentNode.removeChild(captured); }
			// Clear internal DOM refs only if this is still the active root.
			if (_root === captured)
			{
				_root  = null;
				_fill  = null;
				_label = null;
			}
		}, ms);
	}

	// ── public API ────────────────────────────────────────────────
	var preloader = mitiru.preloader = Object.create(null);

	/**
	 * Register asset items to load.
	 * Calling register() multiple times concatenates to the existing list.
	 *
	 * @param {Array<{kind: string, path: string, key: string}>} items
	 */
	preloader.register = function(items)
	{
		if (!Array.isArray(items)) { throw new Error('mitiru.preloader.register: items must be an array'); }
		for (var i = 0; i < items.length; ++i)
		{
			var item = items[i];
			if (!item || typeof item.key !== 'string' || typeof item.path !== 'string' || typeof item.kind !== 'string')
			{
				console.warn('[mitiru.preloader] register: item at index ' + i + ' missing key/path/kind — skipped');
				continue;
			}
			_items.push({ kind: item.kind, path: item.path, key: item.key });
		}
	};

	/**
	 * Build the progress-bar DOM into container.
	 * If already mounted, the prior instance is removed first (idempotent).
	 *
	 * @param {HTMLElement} containerEl
	 * @param {object}      [opts]
	 * @param {string}      [opts.label='Loading…']
	 * @param {boolean}     [opts.showCount=true]
	 * @param {number}      [opts.fadeMs=400]
	 * @param {number}      [opts.concurrency=4]
	 */
	preloader.mount = function(containerEl, opts)
	{
		if (!containerEl || typeof containerEl.appendChild !== 'function')
		{
			throw new Error('mitiru.preloader.mount: containerEl must be a DOM element');
		}

		// Double-mount protection.
		if (_root) { preloader.unmount(); }

		_mountOpts = opts || {};

		_root = document.createElement('div');
		_root.className = 'mitiru-preloader';
		_root.setAttribute('data-mitiru-preloader', '');

		// Inline fallback styles when mitiru_components.css is not loaded.
		if (!_isCssLoaded())
		{
			_root.style.cssText = [
				'box-sizing:border-box',
				'width:100%',
				'padding:8px 16px',
			].join(';');
		}

		var track = document.createElement('div');
		track.className = 'mitiru-preloader__track';

		if (!_isCssLoaded())
		{
			track.style.cssText = [
				'width:100%',
				'height:8px',
				'background:#e0e0e0',
				'border-radius:4px',
				'overflow:hidden',
			].join(';');
		}

		_fill = document.createElement('div');
		_fill.className = 'mitiru-preloader__fill';
		_fill.style.width = '0%';

		if (!_isCssLoaded())
		{
			_fill.style.cssText = [
				'height:100%',
				'width:0%',
				'background:var(--mitiru-accent,#e07a5f)',
				'border-radius:4px',
				'transition:width 80ms ease',
			].join(';');
		}

		_label = document.createElement('div');
		_label.className = 'mitiru-preloader__label';
		_label.setAttribute('data-preloader-label', '');

		if (!_isCssLoaded())
		{
			_label.style.cssText = [
				'margin-top:6px',
				'font-size:12px',
				'color:var(--mitiru-text-2,#666)',
			].join(';');
		}

		track.appendChild(_fill);
		_root.appendChild(track);
		_root.appendChild(_label);
		containerEl.appendChild(_root);

		_updateUI();
	};

	/**
	 * Check if the CSS component sheet is loaded (heuristic on class name).
	 */
	function _isCssLoaded()
	{
		try
		{
			var sheets = document.styleSheets;
			for (var i = 0; i < sheets.length; ++i)
			{
				var rules;
				try { rules = sheets[i].cssRules || sheets[i].rules; } catch (_e) { continue; }
				if (!rules) { continue; }
				for (var j = 0; j < rules.length; ++j)
				{
					var r = rules[j];
					if (r.selectorText && r.selectorText.indexOf('mitiru-preloader') >= 0) { return true; }
				}
			}
		}
		catch (_e) { /* ignore */ }
		return false;
	}

	/**
	 * Remove the preloader DOM. Safe to call when not mounted.
	 */
	preloader.unmount = function()
	{
		if (_root && _root.parentNode) { _root.parentNode.removeChild(_root); }
		_root  = null;
		_fill  = null;
		_label = null;
	};

	/**
	 * Start loading all registered items.
	 * Returns a Promise that resolves with { loaded: string[], failed: string[] }.
	 *
	 * @param {object} [opts]  Same options as mount(); concurrency applies here.
	 * @returns {Promise<{loaded: string[], failed: string[]}>}
	 */
	preloader.start = function(opts)
	{
		if (_running) { return Promise.reject(new Error('mitiru.preloader.start: already running')); }

		var runOpts     = opts || _mountOpts || {};
		var concurrency = typeof runOpts.concurrency === 'number' && runOpts.concurrency > 0
			? runOpts.concurrency
			: 4;

		_running = true;
		_done    = 0;
		_total   = _items.length;
		_current = '';

		var loaded = [];
		var failed = [];

		if (_total === 0)
		{
			_running = false;
			_emit('complete', { loaded: loaded, failed: failed });
			return Promise.resolve({ loaded: loaded, failed: failed });
		}

		_updateUI();

		var self = this;

		return _runPool(_items, concurrency, function(item)
		{
			_current = item.key;
			return _loadItem(item).then(
				function(value)
				{
					_resolved[item.key] = value;
					_done++;
					loaded.push(item.key);
					_emit('item:done', { key: item.key, kind: item.kind, value: value });
					var frac = _total > 0 ? (_done / _total) : 1;
					_updateUI();
					_emit('progress', { done: _done, total: _total, fraction: frac, currentKey: item.key });
				},
				function(error)
				{
					_done++;
					failed.push(item.key);
					_emit('item:fail', { key: item.key, kind: item.kind, error: error });
					var frac = _total > 0 ? (_done / _total) : 1;
					_updateUI();
					_emit('progress', { done: _done, total: _total, fraction: frac, currentKey: item.key });
				}
			);
		}).then(function()
		{
			_running = false;
			_current = '';
			_emit('complete', { loaded: loaded, failed: failed });
			_triggerFadeOut();
			return { loaded: loaded, failed: failed };
		});
	};

	/**
	 * Get the resolved value for a key after start() completes.
	 *
	 * @param  {string} key
	 * @returns {*}  image / HTMLAudioElement / parsed JSON / text string / Response
	 */
	preloader.get = function(key)
	{
		return _resolved[key];
	};

	/**
	 * Current loading progress snapshot.
	 *
	 * @returns {{ done: number, total: number, fraction: number, currentKey: string }}
	 */
	preloader.progress = function()
	{
		var frac = _total > 0 ? (_done / _total) : 0;
		return { done: _done, total: _total, fraction: frac, currentKey: _current };
	};

	/**
	 * Subscribe to a preloader event.
	 * Unknown event types are silently accepted (no-op until that event fires).
	 *
	 * @param  {string}   event  'progress'|'item:done'|'item:fail'|'complete'
	 * @param  {Function} cb
	 * @returns {Function} unsubscribe
	 */
	preloader.on = function(event, cb)
	{
		if (typeof event !== 'string' || typeof cb !== 'function') { return function() {}; }
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
		return function() { preloader.off(event, cb); };
	};

	/**
	 * Unsubscribe a previously registered event listener.
	 *
	 * @param {string}   event
	 * @param {Function} cb
	 */
	preloader.off = function(event, cb)
	{
		var cbs = _listeners[event];
		if (!cbs) { return; }
		for (var i = cbs.length - 1; i >= 0; --i)
		{
			if (cbs[i] === cb) { cbs.splice(i, 1); }
		}
	};

	/**
	 * Reset registry and resolved map. Does not affect mounted DOM.
	 */
	preloader.clear = function()
	{
		_items    = [];
		_resolved = {};
		_done     = 0;
		_total    = 0;
		_current  = '';
		_running  = false;
	};

})(typeof window !== 'undefined' ? window : globalThis);
