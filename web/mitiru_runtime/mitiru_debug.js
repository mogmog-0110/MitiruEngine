/*!
 * mitiru_debug.js — engine-side debug bridge (E-08 / E-09)
 *
 * KaeruCrape feedback:
 *   E-08  `window.mitiru.debug.snapshot()`  — 構造化 state snapshot
 *   E-09  `window.mitiru.debug.postLog()`   — post() ring buffer
 *
 * どちらもゲームごとに再発明されていたもの。
 *
 * 使い方 (games/<name>/assets/ui/index.html):
 *
 *   <script src="../mitiru_runtime/mitiru_debug.js"></script>
 *   <script>
 *     // モジュール毎に slice を register する。
 *     // 戻り値 (plain object / JSON-safe) が snapshot() に組み込まれる。
 *     mitiru.debug.register('cooking', () => ({
 *         state:   CookingState.current(),
 *         queue:   OrderQueue.toJSON(),
 *         selected: Cursor.selectedDragId,
 *     }));
 *
 *     // post() を裏で intercept したいだけなら install は自動。
 *     // 明示的に wrap したい送信関数があれば wrapSender で:
 *     const bridge = mitiru.debug.wrapSender('cefQuery', (payload) =>
 *         window.cefQuery({ request: 'bridge|' + JSON.stringify(payload) }));
 *   </script>
 *
 * MCP / Playwright 側からは:
 *
 *   const s = await page.evaluate(() => window.mitiru.debug.snapshot());
 *   const log = await page.evaluate(() => window.mitiru.debug.postLog());
 *
 * API:
 *   mitiru.debug.register(name, fn)    — slice 提供 (idempotent、同名は上書き)
 *   mitiru.debug.unregister(name)
 *   mitiru.debug.snapshot()            — { ts, slices: { name: value, ... } }
 *   mitiru.debug.postLog()             — [{ts, kind, payload, durationMs?}, ...]
 *   mitiru.debug.clearPostLog()
 *   mitiru.debug.logPost(kind, payload) — 手動で 1 件記録
 *   mitiru.debug.wrapSender(kind, fn)  — 任意の送信関数を wrap (自動で logPost)
 *   mitiru.debug.setRingSize(n)         — ring buffer サイズ変更 (default 200)
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};

	const _slices = Object.create(null);   // name → fn
	let   _ring   = [];                     // [{ts, kind, payload, durationMs?}]
	let   _ringMax = 200;

	// ── slice registry ────────────────────────────────────────
	function register(name, fn)
	{
		if (typeof name !== 'string' || !name)
		{
			throw new Error('mitiru.debug.register: name must be non-empty string');
		}
		if (typeof fn !== 'function')
		{
			throw new Error('mitiru.debug.register: fn must be function');
		}
		_slices[name] = fn;
	}

	function unregister(name)
	{
		delete _slices[name];
	}

	function snapshot()
	{
		const out = {
			ts:     _now(),
			slices: Object.create(null),
		};
		const names = Object.keys(_slices);
		for (let i = 0; i < names.length; ++i)
		{
			const n = names[i];
			try
			{
				out.slices[n] = _toJsonSafe(_slices[n]());
			}
			catch (e)
			{
				out.slices[n] = { __error: String((e && e.message) || e) };
			}
		}
		return out;
	}

	// ── post() ring buffer ───────────────────────────────────
	function setRingSize(n)
	{
		_ringMax = Math.max(1, (n | 0));
		if (_ring.length > _ringMax) { _ring = _ring.slice(-_ringMax); }
	}

	function logPost(kind, payload, durationMs)
	{
		const entry = {
			ts:      _now(),
			kind:    String(kind || 'unknown'),
			payload: _toJsonSafe(payload),
		};
		if (typeof durationMs === 'number') { entry.durationMs = +durationMs.toFixed(3); }
		_ring.push(entry);
		if (_ring.length > _ringMax) { _ring.shift(); }
	}

	function postLog()
	{
		// defensive copy — 呼び出し側で push しても ring が汚れないように
		return _ring.slice();
	}

	function clearPostLog() { _ring.length = 0; }

	function wrapSender(kind, fn)
	{
		if (typeof fn !== 'function')
		{
			throw new Error('mitiru.debug.wrapSender: fn must be function');
		}
		return function wrapped()
		{
			const started = _now();
			// 引数を配列として snapshot する。1 引数なら値そのものを payload に。
			const payload = arguments.length === 1
				? arguments[0]
				: Array.prototype.slice.call(arguments);
			try
			{
				const result = fn.apply(this, arguments);
				if (result && typeof result.then === 'function')
				{
					return result.then(
						function(v) { logPost(kind, payload, _now() - started); return v; },
						function(e)
						{
							logPost(kind, { __error: String((e && e.message) || e), payload: payload },
								_now() - started);
							throw e;
						});
				}
				logPost(kind, payload, _now() - started);
				return result;
			}
			catch (e)
			{
				logPost(kind, { __error: String((e && e.message) || e), payload: payload },
					_now() - started);
				throw e;
			}
		};
	}

	// ── helpers ────────────────────────────────────────────────
	function _now()
	{
		if (typeof performance !== 'undefined' && performance.now)
		{
			return performance.now();
		}
		return Date.now();
	}

	/// @brief JSON-serializable な deep clone を返す (循環参照対策)
	function _toJsonSafe(v)
	{
		try { return JSON.parse(JSON.stringify(v)); }
		catch (e) { return { __unserializable: String(v) }; }
	}

	// ── public API ────────────────────────────────────────────
	mitiru.debug = {
		register:     register,
		unregister:   unregister,
		snapshot:     snapshot,
		postLog:      postLog,
		clearPostLog: clearPostLog,
		logPost:      logPost,
		wrapSender:   wrapSender,
		setRingSize:  setRingSize,
	};
})(typeof window !== 'undefined' ? window : globalThis);
