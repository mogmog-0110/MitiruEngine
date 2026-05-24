/*!
 * mitiru_hud.js — persistent HUD widget (F-08)
 *
 * Persistent overlay that lives in main.html and survives scene transitions.
 * Slots subscribe to mitiru.state keys and re-render on change.
 *
 * API: mount(container, opts) / unmount() / slots() / update()
 * opts: { stateKeys:string[], slots:[{id, render}], className:string }
 * render return type dispatch:
 *   string → textContent | Node → replaceChildren | {html:string} → innerHTML
 *
 * stateKeys:[] (default) = static snapshot, no subscriptions.
 * Double-mount: silently unmounts previous instance first.
 * F-02 absent: falls back to minimal inline style.
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-08
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.hud) { return; }  // already loaded

	const document = global.document;

	// ── internal state ────────────────────────────────────────────
	let _root    = null;   // HTMLElement — the mounted HUD root
	let _slots   = [];     // [{ id, el, render }]
	let _unsubs  = [];     // [unsubscribe fn, ...]  from mitiru.state.subscribe
	let _opts    = null;   // last mount options (for update())

	// ── helpers ───────────────────────────────────────────────────

	// Render one slot; dispatch on render() return type.
	function _renderSlot(slot, snapshot)
	{
		var value;
		try
		{
			value = slot.render(snapshot);
		}
		catch (e)
		{
			console.error('[mitiru.hud] slot "' + slot.id + '" render threw:', e);
			return;
		}

		var el = slot.el;
		if (value === null || value === undefined)
		{
			el.textContent = '';
		}
		else if (typeof value === 'string')
		{
			el.textContent = value;
		}
		else if (value instanceof global.Node)
		{
			el.replaceChildren(value);
		}
		else if (typeof value === 'object' && typeof value.html === 'string')
		{
			el.innerHTML = value.html;
		}
		else
		{
			// Fallback: coerce to string safely.
			el.textContent = String(value);
		}
	}

	// Build snapshot of subscribed keys and render all slots.
	function _renderAll()
	{
		if (!_root || !_opts) { return; }

		var snapshot = _buildSnapshot(_opts.stateKeys || []);
		for (var i = 0; i < _slots.length; ++i)
		{
			_renderSlot(_slots[i], snapshot);
		}
	}

	/**
	 * Build a plain-object snapshot of the given state keys.
	 * Falls back gracefully when mitiru.state is not loaded.
	 */
	function _buildSnapshot(keys)
	{
		var snap = {};
		var s = mitiru.state;
		if (!s) { return snap; }
		for (var i = 0; i < keys.length; ++i)
		{
			snap[keys[i]] = s.get(keys[i]);
		}
		return snap;
	}

	/**
	 * Check whether the F-02 component class is available in any loaded stylesheet.
	 * Returns true if `mitiru-hud-note` resolves to a non-empty rule set.
	 */
	function _f02Available()
	{
		try
		{
			var sheets = document.styleSheets;
			for (var i = 0; i < sheets.length; ++i)
			{
				var rules;
				try { rules = sheets[i].cssRules || sheets[i].rules; }
				catch (_e) { continue; }  // cross-origin sheet
				if (!rules) { continue; }
				for (var j = 0; j < rules.length; ++j)
				{
					var r = rules[j];
					if (r.selectorText && r.selectorText.indexOf('mitiru-hud-note') >= 0)
					{
						return true;
					}
				}
			}
		}
		catch (_e) { /* ignore */ }
		return false;
	}

	/**
	 * Apply inline fallback styles to the HUD root when F-02 is not available.
	 */
	function _applyFallbackStyle(el)
	{
		el.style.cssText = [
			'position:fixed',
			'top:8px',
			'left:8px',
			'display:flex',
			'flex-direction:row',
			'gap:8px',
			'z-index:var(--mitiru-z-hud,200)',
			'pointer-events:none',
		].join(';');
	}

	// ── public API ────────────────────────────────────────────────
	const hud = mitiru.hud = Object.create(null);

	/**
	 * Mount the HUD into `container`.
	 * If already mounted, the previous instance is unmounted first (double-mount protection).
	 *
	 * @param {HTMLElement} container
	 * @param {object}      opts
	 * @param {string[]}    [opts.stateKeys=[]]
	 * @param {Array}       [opts.slots=[]]
	 * @param {string}      [opts.className='']
	 */
	hud.mount = function(container, opts)
	{
		if (!container || typeof container.appendChild !== 'function')
		{
			throw new Error('mitiru.hud.mount: container must be a DOM element');
		}

		// Double-mount protection: tear down any existing instance.
		if (_root) { hud.unmount(); }

		opts = opts || {};
		var stateKeys = Array.isArray(opts.stateKeys) ? opts.stateKeys : [];
		var slotDefs  = Array.isArray(opts.slots)     ? opts.slots     : [];
		var extraClass = typeof opts.className === 'string' ? opts.className : '';

		_opts  = { stateKeys: stateKeys, slots: slotDefs, className: extraClass };
		_slots = [];
		_unsubs = [];

		// ── build root element ────────────────────────────────────
		_root = document.createElement('div');
		_root.setAttribute('data-mitiru-hud', '');
		_root.classList.add('mitiru-hud');
		if (extraClass) { _root.classList.add(extraClass); }

		// Apply F-02 class if available, otherwise inline fallback.
		if (_f02Available())
		{
			_root.classList.add('mitiru-hud-note');
		}
		else
		{
			_applyFallbackStyle(_root);
		}

		// ── build slot elements ───────────────────────────────────
		for (var i = 0; i < slotDefs.length; ++i)
		{
			var def = slotDefs[i];
			if (!def || typeof def.id !== 'string' || typeof def.render !== 'function')
			{
				console.warn('[mitiru.hud] slot at index ' + i + ' missing id or render — skipped');
				continue;
			}

			var slotEl = document.createElement('div');
			slotEl.setAttribute('data-hud-slot', def.id);

			_root.appendChild(slotEl);
			_slots.push({ id: def.id, el: slotEl, render: def.render });
		}

		container.appendChild(_root);

		// ── initial render ────────────────────────────────────────
		_renderAll();

		// ── subscriptions ─────────────────────────────────────────
		// Single shared handler — all key changes trigger a full re-render.
		// This avoids N separate closures and simplifies teardown via _unsubs[].
		var s = mitiru.state;
		if (s && stateKeys.length > 0)
		{
			var _handler = function() { _renderAll(); };

			for (var k = 0; k < stateKeys.length; ++k)
			{
				// subscribe fires immediately — suppress the redundant initial render
				// by temporarily wrapping; we already rendered above.
				var unsub = (function(key)
				{
					var fired = false;
					var off = s.subscribe(key, function()
					{
						if (!fired) { fired = true; return; }  // skip immediate fire
						_handler();
					});
					return off;
				})(stateKeys[k]);

				_unsubs.push(unsub);
			}
		}
	};

	/**
	 * Unmount: unsubscribe all state listeners and remove the HUD element from the DOM.
	 * Safe to call when not mounted (no-op).
	 */
	hud.unmount = function()
	{
		// Tear down subscriptions.
		for (var i = 0; i < _unsubs.length; ++i)
		{
			try { _unsubs[i](); } catch (_e) { /* ignore */ }
		}
		_unsubs = [];

		// Remove element.
		if (_root && _root.remove) { _root.remove(); }
		_root  = null;
		_slots = [];
		_opts  = null;
	};

	/**
	 * Returns the current live slot descriptors: [{ id: string, el: HTMLElement }].
	 * Returns [] when not mounted.
	 */
	hud.slots = function()
	{
		return _slots.map(function(s) { return { id: s.id, el: s.el }; });
	};

	/**
	 * Force re-render all slots from the current state snapshot.
	 * Useful after batch state mutations when you want a single synchronous update.
	 */
	hud.update = function()
	{
		_renderAll();
	};

})(typeof window !== 'undefined' ? window : globalThis);
