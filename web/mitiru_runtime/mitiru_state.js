/*!
 * mitiru_state.js — シーン跨ぎの state store (F-03)
 *
 * シーン遷移をまたいで生き残る軽量な key/value store。
 * CEF では永続化操作の backing channel は `mitiru.dispatch` (G-05 bridge)。
 * 素のブラウザでは全データはメモリ内のみ。
 *
 * 提供 API:
 *   window.mitiru.state.set(key, value)         — immutable な置換、subscriber へ通知
 *   window.mitiru.state.get(key, fallback?)      — 現在値、または fallback
 *   window.mitiru.state.subscribe(key, fn)       — BehaviorSubject パターン (即時発火)
 *   window.mitiru.state.unsubscribe(key, fn)
 *   window.mitiru.state.reset(key)               — key を削除し undefined で通知
 *   window.mitiru.state.keys()                   — 生存中の key のスナップショット配列
 *   window.mitiru.state.snapshot()               — store 全体の plain-object コピー
 *   window.mitiru.state.save(key, slotId)        — dispatch 経由で永続化 (CEF 経路)
 *   window.mitiru.state.load(key, slotId)        — dispatch から load + commit (CEF 経路)
 *   window.mitiru.state.listSlots()              — dispatch 経由でセーブスロット一覧 (CEF 経路)
 *
 * 設計メモ:
 *   - 値は書き込み時に構造的に freeze される (immutability ルール)。
 *   - subscriber は diff ではなく新しい値を受け取る — 必要なら自分で比較する。
 *   - `save` / `load` / `listSlots` は Promise を返す。CEF 外では reject する。
 *   - store は意図的に global-singleton (ページコンテキストごとに 1 つ)。
 *
 * 仕様: docs/feedback-from-kaerucrape/2026-04-24.md F-03
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.state) { return; }  // 既に読み込み済み

	// ── 内部ストレージ ─────────────────────────────────────────
	const _store     = Object.create(null);  // key -> freeze 済みの値
	const _listeners = Object.create(null);  // key -> [fn, ...]

	// ── ヘルパ ───────────────────────────────────────────────────
	function _freeze(v)
	{
		if (v === null || typeof v !== 'object') { return v; }
		if (Object.isFrozen(v)) { return v; }
		// ルートを浅く freeze し、ネストした object は再帰的に freeze。
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

	// ── 公開 API ────────────────────────────────────────────────
	const state = mitiru.state = Object.create(null);

	state.set = function(key, value)
	{
		if (typeof key !== 'string' || key === '')
		{
			throw new Error('mitiru.state.set: key must be a non-empty string');
		}
		// 新しい freeze 済みの値を作る — 既存 object は決して mutate しない。
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

		// BehaviorSubject パターン — 既に値があれば即時発火する。
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

	// ── CEF 永続化 (G-05 dispatch channel 経由) ───────────────
	// 3 メソッドとも cefQuery をラップする `mitiru.dispatch` に委譲する。
	// CEF 外では reject する — caller は reject を適切に処理すること。

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
