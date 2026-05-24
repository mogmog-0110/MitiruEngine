/*!
 * mitiru_state.js — cross-scene state store (F-03)
 *
 * Lightweight key/value store that survives scene navigation.
 * In CEF the backing channel is `mitiru.dispatch` (G-05 bridge) for
 * persistence operations; in a plain browser all data stays in memory.
 *
 * Implements:
 *   window.mitiru.state.set(key, value)         — immutable replace, notifies subscribers
 *   window.mitiru.state.get(key, fallback?)      — current value or fallback
 *   window.mitiru.state.subscribe(key, fn)       — BehaviorSubject pattern (fires immediately)
 *   window.mitiru.state.unsubscribe(key, fn)
 *   window.mitiru.state.reset(key)               — remove key, notify with undefined
 *   window.mitiru.state.keys()                   — snapshot array of live keys
 *   window.mitiru.state.snapshot()               — plain-object copy of entire store
 *   window.mitiru.state.save(key, slotId)        — persist via dispatch (CEF path)
 *   window.mitiru.state.load(key, slotId)        — load + commit from dispatch (CEF path)
 *   window.mitiru.state.listSlots()              — list save slots via dispatch (CEF path)
 *
 * Design notes:
 *   - Values are structurally frozen on write (immutability rule).
 *   - Subscribers receive the new value, not a diff — compare yourself if needed.
 *   - `save` / `load` / `listSlots` return Promises; they reject outside CEF.
 *   - The store is intentionally global-singleton (one per page context).
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-03
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.state) { return; }  // already loaded

	// ── internal storage ─────────────────────────────────────────
	const _store     = Object.create(null);  // key -> frozen value
	const _listeners = Object.create(null);  // key -> [fn, ...]

	// ── helpers ───────────────────────────────────────────────────
	function _freeze(v)
	{
		if (v === null || typeof v !== 'object') { return v; }
		if (Object.isFrozen(v)) { return v; }
		// Shallow freeze the root; nested objects frozen recursively.
		const keys = Object.keys(v);
		for (let i = 0; i < keys.length; ++i) { v[keys[i]] = _freeze(v[keys[i]]); }
		return Object.freeze(v);
	}

	function _notify(key, value)
	{
		const arr = _listeners[key];
		if (!arr || arr.length === 0) { return; }
		const copy = arr.slice();
		for (let i = 0; i < copy.length; ++i)
		{
			try { copy[i](value); }
			catch (e) { console.error('[mitiru.state] subscriber threw (key=' + key + '):', e); }
		}
	}

	// ── public API ────────────────────────────────────────────────
	const state = mitiru.state = Object.create(null);

	state.set = function(key, value)
	{
		if (typeof key !== 'string' || key === '')
		{
			throw new Error('mitiru.state.set: key must be a non-empty string');
		}
		// Create a new frozen value — never mutate existing objects.
		const frozen = _freeze(Array.isArray(value) ? value.slice() :
		               (value !== null && typeof value === 'object')
		                   ? Object.assign(Object.create(null), value)
		                   : value);
		_store[key] = frozen;
		_notify(key, frozen);
	};

	state.get = function(key, fallback)
	{
		if (!Object.prototype.hasOwnProperty.call(_store, key))
		{
			return arguments.length >= 2 ? fallback : undefined;
		}
		return _store[key];
	};

	state.subscribe = function(key, fn)
	{
		if (typeof key !== 'string' || typeof fn !== 'function')
		{
			throw new Error('mitiru.state.subscribe: (string, function) required');
		}
		if (!_listeners[key]) { _listeners[key] = []; }
		_listeners[key].push(fn);

		// BehaviorSubject pattern — fire immediately if a value already exists.
		if (Object.prototype.hasOwnProperty.call(_store, key))
		{
			try { fn(_store[key]); }
			catch (e) { console.error('[mitiru.state] subscribe initial fire threw:', e); }
		}
		return function unsubscribe() { state.unsubscribe(key, fn); };
	};

	state.unsubscribe = function(key, fn)
	{
		const arr = _listeners[key];
		if (!arr) { return; }
		const i = arr.indexOf(fn);
		if (i >= 0) { arr.splice(i, 1); }
	};

	state.reset = function(key)
	{
		if (!Object.prototype.hasOwnProperty.call(_store, key)) { return; }
		delete _store[key];
		_notify(key, undefined);
	};

	state.keys = function()
	{
		return Object.keys(_store);
	};

	state.snapshot = function()
	{
		const out = {};
		const keys = Object.keys(_store);
		for (let i = 0; i < keys.length; ++i) { out[keys[i]] = _store[keys[i]]; }
		return out;
	};

	// ── CEF persistence (via G-05 dispatch channel) ───────────────
	// All three methods delegate to `mitiru.dispatch` which wraps cefQuery.
	// Outside CEF they reject — callers must handle the rejection gracefully.

	state.save = function(key, slotId)
	{
		if (typeof key !== 'string') { return Promise.reject(new Error('mitiru.state.save: key must be string')); }
		if (typeof mitiru.dispatch !== 'function')
		{
			return Promise.reject(new Error('mitiru.state.save: mitiru.dispatch not available'));
		}
		const value = state.get(key);
		return mitiru.dispatch('state.save', { key: key, slotId: slotId || 'default', value: value });
	};

	state.load = function(key, slotId)
	{
		if (typeof key !== 'string') { return Promise.reject(new Error('mitiru.state.load: key must be string')); }
		if (typeof mitiru.dispatch !== 'function')
		{
			return Promise.reject(new Error('mitiru.state.load: mitiru.dispatch not available'));
		}
		return mitiru.dispatch('state.load', { key: key, slotId: slotId || 'default' })
			.then(function(resp)
			{
				if (resp !== null && resp !== undefined)
				{
					state.set(key, resp);
				}
				return resp;
			});
	};

	state.listSlots = function()
	{
		if (typeof mitiru.dispatch !== 'function')
		{
			return Promise.reject(new Error('mitiru.state.listSlots: mitiru.dispatch not available'));
		}
		return mitiru.dispatch('state.listSlots', null);
	};

})(typeof window !== 'undefined' ? window : globalThis);
