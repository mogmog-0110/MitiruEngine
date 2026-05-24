/*!
 * mitiru_cef_state.js — JS side of the CEF state bridge (G-05)
 *
 * Pair with C++ `mitiru::cef::StateStore` (include/mitiru/cef/StateStore.hpp).
 *
 * Exposes:
 *   window.mitiru.onStateChange(key, fn)   — reactive subscription to C++ set()
 *   window.mitiru.offStateChange(key, fn)
 *   window.mitiru.on(event, fn)            — one-shot event listener
 *   window.mitiru.off(event, fn)
 *   window.mitiru.getState(key)            — current retained value
 *   window.mitiru.dispatch(action, payload)— JS → C++ typed action; returns Promise
 *
 * Retained semantics: `onStateChange` fires immediately with the most recent
 * retained value if one exists (common reactive pattern, matches RxJS
 * BehaviorSubject). `on` does not — events are fire-and-forget.
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};

	// Internal sinks — the C++ side only touches `_state._onChange` and `_onEvent`.
	const _state = mitiru._state = mitiru._state || {};
	const _stateListeners = Object.create(null);  // key -> [fn, ...]
	const _eventListeners = Object.create(null);  // name -> [fn, ...]
	const _retained       = Object.create(null);  // key -> last value

	// ── C++ → JS sinks ────────────────────────────────────────
	_state._onChange = function(key, value)
	{
		_retained[key] = value;
		_invokeAll(_stateListeners[key], value, 'onStateChange:' + key);
	};

	_state._onEvent = function(name, payload)
	{
		_invokeAll(_eventListeners[name], payload, 'on:' + name);
	};

	// ── subscription APIs ────────────────────────────────────
	mitiru.onStateChange = function(key, fn)
	{
		if (typeof key !== 'string' || typeof fn !== 'function')
		{
			throw new Error('mitiru.onStateChange: (string, function) required');
		}
		if (!_stateListeners[key]) { _stateListeners[key] = []; }
		_stateListeners[key].push(fn);

		// Immediate fire for reactive pattern — late subscribers still see
		// the current value. No-op if the key was never set.
		if (Object.prototype.hasOwnProperty.call(_retained, key))
		{
			try { fn(_retained[key]); }
			catch (e) { console.error('[mitiru.onStateChange] initial fire threw:', e); }
		}
		return function unsubscribe() { mitiru.offStateChange(key, fn); };
	};

	mitiru.offStateChange = function(key, fn)
	{
		const arr = _stateListeners[key];
		if (!arr) { return; }
		const i = arr.indexOf(fn);
		if (i >= 0) { arr.splice(i, 1); }
	};

	mitiru.on = function(name, fn)
	{
		if (typeof name !== 'string' || typeof fn !== 'function')
		{
			throw new Error('mitiru.on: (string, function) required');
		}
		if (!_eventListeners[name]) { _eventListeners[name] = []; }
		_eventListeners[name].push(fn);
		return function unsubscribe() { mitiru.off(name, fn); };
	};

	mitiru.off = function(name, fn)
	{
		const arr = _eventListeners[name];
		if (!arr) { return; }
		const i = arr.indexOf(fn);
		if (i >= 0) { arr.splice(i, 1); }
	};

	mitiru.getState = function(key)
	{
		return _retained[key];
	};

	// ── JS → C++ dispatch (wraps cefQuery) ───────────────────
	mitiru.dispatch = function(action, payload)
	{
		if (typeof action !== 'string')
		{
			return Promise.reject(new Error('mitiru.dispatch: action must be string'));
		}
		if (typeof window.cefQuery !== 'function')
		{
			console.warn('[mitiru.dispatch] cefQuery missing; running outside CEF?');
			return Promise.reject(new Error('cefQuery unavailable'));
		}
		const request = 'state.dispatch|' + JSON.stringify({
			action:  action,
			payload: payload === undefined ? null : payload,
		});
		return new Promise(function(resolve, reject)
		{
			window.cefQuery({
				request: request,
				onSuccess: function(resp)
				{
					let parsed = null;
					if (resp)
					{
						try { parsed = JSON.parse(resp); }
						catch (_e) { parsed = resp; }
					}
					if (parsed && typeof parsed === 'object' && parsed.error)
					{
						reject(new Error(parsed.error));
						return;
					}
					resolve(parsed);
				},
				onFailure: function(code, msg)
				{
					reject(new Error('[' + code + '] ' + msg));
				},
			});
		});
	};

	// ── helpers ───────────────────────────────────────────────
	function _invokeAll(listeners, arg, label)
	{
		if (!listeners || listeners.length === 0) { return; }
		// Defensive copy — listeners may unsubscribe during callback.
		const copy = listeners.slice();
		for (let i = 0; i < copy.length; ++i)
		{
			try { copy[i](arg); }
			catch (e) { console.error('[mitiru.' + label + '] threw:', e); }
		}
	}
})(typeof window !== 'undefined' ? window : globalThis);
