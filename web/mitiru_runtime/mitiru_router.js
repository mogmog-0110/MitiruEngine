/*!
 * mitiru_router.js — single-page scene router (F-01)
 *
 * Manages scene transitions within a CEF/browser single-page application.
 * Scenes are registered with a route key; navigation loads the scene's HTML
 * into an <iframe> (or a host element) and runs lifecycle hooks.
 *
 * Implements:
 *   window.mitiru.router.register(key, descriptor)  — register / replace a scene
 *   window.mitiru.router.navigate(key, params?)      — transition to a scene
 *   window.mitiru.router.current()                   — { key, params } or null
 *   window.mitiru.router.back()                      — pop history stack (1 level)
 *   window.mitiru.router.onSceneReady()              — called by scene page on DOMContentLoaded
 *
 * Scene descriptor:
 *   {
 *     url:     string,           // path to scene's HTML (relative to the game root)
 *     preload: boolean,          // reserved — emits console.warn, not yet implemented
 *     in?:     function(params), // transition-in hook (runs after DOMContentLoaded in scene)
 *     out?:    function(),       // transition-out hook (runs before iframe src changes)
 *   }
 *
 * Transition sequence:
 *   1. Call current scene's `out()` hook (if any)
 *   2. Fade document.body to opacity 0 (CSS transition, FADE_MS)
 *   3. Set iframe src to new scene URL
 *   4. Scene page calls `mitiru.router.onSceneReady()` on its DOMContentLoaded
 *   5. Fade document.body back to opacity 1
 *   6. Call new scene's `in(params)` hook (if any)
 *
 * History:
 *   navigate() pushes onto an internal stack (max HISTORY_MAX entries).
 *   back() pops and navigates to the previous entry — no infinite loop guard
 *   needed because back() does not push onto the stack.
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-01
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.router) { return; }  // already loaded

	// ── constants ─────────────────────────────────────────────────
	const FADE_MS     = 200;   // body opacity fade duration (ms)
	const HISTORY_MAX = 50;    // max navigation history entries

	// ── internal state ────────────────────────────────────────────
	const _registry = Object.create(null);  // key -> descriptor
	let   _current  = null;                 // { key, params, descriptor }
	const _history  = [];                   // [{ key, params }, ...]  (oldest → newest)
	let   _pending  = null;                 // Promise resolving when scene signals ready
	let   _pendingResolve = null;           // resolve fn for _pending

	// ── helpers ───────────────────────────────────────────────────
	function _getIframe()
	{
		let el = document.getElementById('mitiru-scene-frame');
		if (!el)
		{
			el = document.createElement('iframe');
			el.id = 'mitiru-scene-frame';
			el.style.cssText = [
				'position:fixed', 'inset:0', 'width:100%', 'height:100%',
				'border:none', 'background:transparent',
			].join(';');
			document.body.appendChild(el);
		}
		return el;
	}

	function _setFade(opacity, ms)
	{
		document.body.style.transition = 'opacity ' + ms + 'ms ease';
		document.body.style.opacity    = String(opacity);
	}

	function _waitMs(ms)
	{
		return new Promise(function(resolve) { setTimeout(resolve, ms); });
	}

	function _waitSceneReady()
	{
		// The scene page calls mitiru.router.onSceneReady() to resolve this.
		_pending = new Promise(function(resolve)
		{
			_pendingResolve = resolve;
		});
		// Timeout safety — if scene never calls onSceneReady, unblock after 5 s.
		const timeout = setTimeout(function()
		{
			if (_pendingResolve)
			{
				console.warn('[mitiru.router] scene ready timeout — did the scene call onSceneReady()?');
				_pendingResolve();
				_pendingResolve = null;
			}
		}, 5000);
		return _pending.then(function(v)
		{
			clearTimeout(timeout);
			return v;
		});
	}

	// ── public API ────────────────────────────────────────────────
	const router = mitiru.router = Object.create(null);

	/**
	 * Register or replace a scene.
	 * Second call with same key replaces the descriptor (cleaner semantics).
	 */
	router.register = function(key, descriptor)
	{
		if (typeof key !== 'string' || key === '')
		{
			throw new Error('mitiru.router.register: key must be a non-empty string');
		}
		if (!descriptor || typeof descriptor.url !== 'string')
		{
			throw new Error('mitiru.router.register: descriptor.url (string) required');
		}
		if (descriptor.preload)
		{
			console.warn('[mitiru.router] preload: true is reserved and not yet implemented for key "' + key + '"');
		}
		// Store an immutable copy.
		_registry[key] = Object.freeze({
			url:     descriptor.url,
			preload: descriptor.preload || false,
			in:      typeof descriptor.in  === 'function' ? descriptor.in  : null,
			out:     typeof descriptor.out === 'function' ? descriptor.out : null,
		});
	};

	/**
	 * Navigate to a registered scene.
	 * Returns a Promise that resolves once the scene is fully transitioned in.
	 */
	router.navigate = function(key, params)
	{
		if (typeof key !== 'string' || !_registry[key])
		{
			return Promise.reject(new Error('mitiru.router.navigate: unknown scene "' + key + '"'));
		}
		const descriptor = _registry[key];
		params = params || null;

		return Promise.resolve()
			.then(function()
			{
				// 1. Run out() hook of the current scene.
				if (_current && _current.descriptor.out)
				{
					try { return _current.descriptor.out(); }
					catch (e) { console.error('[mitiru.router] out() threw:', e); }
				}
			})
			.then(function()
			{
				// 2. Fade out.
				_setFade(0, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				// 3. Push history (only when navigating forward, not via back()).
				if (_current)
				{
					_history.push({ key: _current.key, params: _current.params });
					if (_history.length > HISTORY_MAX)
					{
						_history.splice(0, _history.length - HISTORY_MAX);
					}
				}

				// 4. Set new current and update iframe src.
				_current = { key: key, params: params, descriptor: descriptor };
				const iframe = _getIframe();
				const readyPromise = _waitSceneReady();
				iframe.src = descriptor.url;
				return readyPromise;
			})
			.then(function()
			{
				// 5. Fade in.
				_setFade(1, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				// 6. Run in() hook of the new scene.
				if (descriptor.in)
				{
					try { return descriptor.in(params); }
					catch (e) { console.error('[mitiru.router] in() threw:', e); }
				}
			});
	};

	/**
	 * Return the current scene descriptor { key, params } or null.
	 */
	router.current = function()
	{
		if (!_current) { return null; }
		return { key: _current.key, params: _current.params };
	};

	/**
	 * Navigate back one level in the history stack.
	 * No-op (resolves immediately) if history is empty.
	 */
	router.back = function()
	{
		if (_history.length === 0) { return Promise.resolve(); }
		const prev = _history.pop();

		// Navigate without pushing onto history again.
		const descriptor = _registry[prev.key];
		if (!descriptor)
		{
			console.warn('[mitiru.router] back(): scene "' + prev.key + '" no longer registered');
			return Promise.resolve();
		}
		const params = prev.params;

		return Promise.resolve()
			.then(function()
			{
				if (_current && _current.descriptor.out)
				{
					try { return _current.descriptor.out(); }
					catch (e) { console.error('[mitiru.router] back out() threw:', e); }
				}
			})
			.then(function()
			{
				_setFade(0, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				_current = { key: prev.key, params: params, descriptor: descriptor };
				const iframe = _getIframe();
				const readyPromise = _waitSceneReady();
				iframe.src = descriptor.url;
				return readyPromise;
			})
			.then(function()
			{
				_setFade(1, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				if (descriptor.in)
				{
					try { return descriptor.in(params); }
					catch (e) { console.error('[mitiru.router] back in() threw:', e); }
				}
			});
	};

	/**
	 * Called by a scene page from its own DOMContentLoaded handler.
	 * Signals the router that the scene is mounted and ready for its in() hook.
	 *
	 * Pattern inside each scene's HTML:
	 *
	 *   <script>
	 *     document.addEventListener('DOMContentLoaded', function() {
	 *         // scene init here ...
	 *         if (window.parent && window.parent.mitiru && window.parent.mitiru.router) {
	 *             window.parent.mitiru.router.onSceneReady();
	 *         }
	 *     });
	 *   </script>
	 */
	router.onSceneReady = function()
	{
		if (_pendingResolve)
		{
			_pendingResolve();
			_pendingResolve = null;
		}
	};

	// Expose history length for testing / debugging.
	Object.defineProperty(router, '_historyLength', {
		get: function() { return _history.length; },
		enumerable: false,
	});

})(typeof window !== 'undefined' ? window : globalThis);
