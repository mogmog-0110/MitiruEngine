/*!
 * mitiru_cef_state.js — CEF state bridge の JS 側 (G-05)
 *
 * C++ 側 `mitiru::cef::StateStore` (include/mitiru/cef/StateStore.hpp) と対になる。
 *
 * 提供 API:
 *   window.mitiru.onStateChange(key, fn)   — C++ の set() へのリアクティブ購読
 *   window.mitiru.offStateChange(key, fn)
 *   window.mitiru.on(event, fn)            — one-shot イベントリスナ
 *   window.mitiru.off(event, fn)
 *   window.mitiru.getState(key)            — 現在の retained 値
 *   window.mitiru.dispatch(action, payload)— JS → C++ の型付き action。Promise を返す
 *
 * Retained セマンティクス: `onStateChange` は retained 値が存在すれば直近の値で
 * 即時発火する (よくあるリアクティブパターン、RxJS の BehaviorSubject に相当)。
 * `on` は発火しない — イベントは fire-and-forget。
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};

	// 内部 sink — C++ 側は `_state._onChange` と `_onEvent` だけに触れる。
	const _state = mitiru._state = mitiru._state || {};
	const _stateListeners = Object.create(null);  // key -> [fn, ...]
	const _eventListeners = Object.create(null);  // name -> [fn, ...]
	const _retained       = Object.create(null);  // key -> 直近の値

	// ── C++ → JS の sink ────────────────────────────────────────
	_state._onChange = function(key, value)
	{
		_retained[key] = value;
		_invokeAll(_stateListeners[key], value, 'onStateChange:' + key);
	};

	// 1 回の IPC で複数 key を受ける batch 版 (C++ StateStore::flushBatch と対)。
	// pairs = [[key, value], ...]。配列順に per-key 適用するので N 回の _onChange と等価。
	_state._onChangeBatch = function(pairs)
	{
		if (!pairs) { return; }
		for (let i = 0; i < pairs.length; ++i)
		{
			const key   = pairs[i][0];
			const value = pairs[i][1];
			_retained[key] = value;
			_invokeAll(_stateListeners[key], value, 'onStateChange:' + key);
		}
	};

	_state._onEvent = function(name, payload)
	{
		_invokeAll(_eventListeners[name], payload, 'on:' + name);
	};

	// ── 購読 API ────────────────────────────────────
	mitiru.onStateChange = function(key, fn)
	{
		if (typeof key !== 'string' || typeof fn !== 'function')
		{
			throw new Error('mitiru.onStateChange: (string, function) required');
		}
		if (!_stateListeners[key]) { _stateListeners[key] = []; }
		_stateListeners[key].push(fn);

		// リアクティブパターンのための即時発火 — 後から購読しても現在値を見られる。
		// key が一度も set されていなければ no-op。
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

	// ── JS → C++ dispatch (cefQuery をラップ) ───────────────────
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

	// ── ヘルパ ───────────────────────────────────────────────
	function _invokeAll(listeners, arg, label)
	{
		if (!listeners || listeners.length === 0) { return; }
		// 防御的コピー — コールバック中に listener が unsubscribe する場合がある。
		const copy = listeners.slice();
		for (let i = 0; i < copy.length; ++i)
		{
			try { copy[i](arg); }
			catch (e) { console.error('[mitiru.' + label + '] threw:', e); }
		}
	}
})(typeof window !== 'undefined' ? window : globalThis);
