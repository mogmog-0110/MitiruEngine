/*!
 * mitiru_test_harness.js — 共通テストハーネス (E-05)
 *
 * 各ゲームの `*.test.html` で再発明されていた
 * IIFE + <pre id="results"> + assert(name, cond) 構造を engine 側に持ち上げる。
 *
 * 使い方 (games/<name>/assets/tests/foo.test.html):
 *
 *   <!doctype html>
 *   <meta charset="utf-8">
 *   <title>foo.test</title>
 *   <link rel="stylesheet" href="../mitiru_runtime/mitiru_base.css">
 *   <script src="../mitiru_runtime/mitiru_runtime.js"></script>
 *   <script src="../mitiru_runtime/mitiru_test_harness.js"></script>
 *   <pre id="results">(running...)</pre>
 *   <script type="module">
 *     import {foo} from './foo.js';
 *     mitiru.test.case('foo returns 1', () => mitiru.test.assertEq(foo(), 1));
 *     mitiru.test.case('foo handles null', () => mitiru.test.assertNoThrow(() => foo(null)));
 *     mitiru.test.run();
 *   </script>
 *
 * 実行完了後:
 *   - <pre id="results"> に pass/fail 集計がテキストでレンダリングされる
 *   - `window.__testResult = { pass, fail, tests: [...] }` に machine-readable
 *     な結果が代入される → Playwright / MCP が regex scrape せず直接 読める
 *   - `window.__testDone = true` が set される → 外部 driver が polling で検出可
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	const testApi = mitiru.test = mitiru.test || {};

	const _cases = [];
	const _async = [];

	// ── case 登録 ─────────────────────────────────────────────
	testApi.case = function(name, fn)
	{
		if (typeof name !== 'string') { throw new Error('mitiru.test.case: name must be string'); }
		if (typeof fn !== 'function') { throw new Error('mitiru.test.case: fn must be function'); }
		_cases.push({ name: name, fn: fn });
	};

	// 旧 API 互換: mitiru.test('name', fn)
	const _callable = function(name, fn) { testApi.case(name, fn); };
	Object.setPrototypeOf(testApi, Function.prototype);  /* noop */

	// ── アサーション群 ──────────────────────────────────────
	testApi.assert = function(cond, msg)
	{
		if (!cond) { throw new Error('assert failed' + (msg ? ': ' + msg : '')); }
	};

	testApi.assertEq = function(actual, expected, msg)
	{
		if (!_deepEqual(actual, expected))
		{
			throw new Error('assertEq: got ' + _show(actual)
			              + ', want ' + _show(expected)
			              + (msg ? ' — ' + msg : ''));
		}
	};

	testApi.assertNotEq = function(actual, expected, msg)
	{
		if (_deepEqual(actual, expected))
		{
			throw new Error('assertNotEq: both values equal ' + _show(actual)
			              + (msg ? ' — ' + msg : ''));
		}
	};

	testApi.assertClose = function(actual, expected, tol, msg)
	{
		tol = (tol === undefined) ? 1e-6 : tol;
		if (Math.abs(actual - expected) > tol)
		{
			throw new Error('assertClose: ' + actual + ' !≈ ' + expected
			              + ' (tol ' + tol + ')' + (msg ? ' — ' + msg : ''));
		}
	};

	testApi.assertThrow = function(fn, msg)
	{
		let threw = false;
		try { fn(); } catch (_e) { threw = true; }
		if (!threw) { throw new Error('assertThrow: no exception' + (msg ? ' — ' + msg : '')); }
	};

	testApi.assertNoThrow = function(fn, msg)
	{
		try { fn(); }
		catch (e) { throw new Error('assertNoThrow: threw ' + e.message + (msg ? ' — ' + msg : '')); }
	};

	// ── run ──────────────────────────────────────────────────
	testApi.run = async function()
	{
		const results = { pass: 0, fail: 0, tests: [] };
		for (let i = 0; i < _cases.length; ++i)
		{
			const c = _cases[i];
			const started = performance.now();
			try
			{
				const maybePromise = c.fn();
				if (maybePromise && typeof maybePromise.then === 'function')
				{
					await maybePromise;
				}
				results.tests.push({
					name: c.name, ok: true,
					durationMs: +(performance.now() - started).toFixed(2),
				});
				results.pass++;
			}
			catch (e)
			{
				results.tests.push({
					name: c.name, ok: false,
					error: String((e && e.message) || e),
					stack: (e && e.stack) ? String(e.stack) : '',
					durationMs: +(performance.now() - started).toFixed(2),
				});
				results.fail++;
			}
		}

		global.__testResult = results;
		global.__testDone = true;
		_render(results);
		return results;
	};

	// ── レンダリング ────────────────────────────────────────
	function _render(results)
	{
		let el = document.getElementById('results');
		if (!el)
		{
			el = document.createElement('pre');
			el.id = 'results';
			document.body.appendChild(el);
		}
		const total = results.pass + results.fail;
		const head = 'PASS ' + results.pass + '  FAIL ' + results.fail
		           + '  TOTAL ' + total + '\n\n';
		const lines = results.tests.map(function(t)
		{
			const prefix = t.ok ? '  ✓ ' : '  ✗ ';
			const tail = t.ok ? '' : '\n      ' + t.error;
			return prefix + t.name + '  (' + t.durationMs + 'ms)' + tail;
		});
		el.textContent = head + lines.join('\n');
	}

	// ── deep equal (循環参照はサポートしない) ──────────────
	function _deepEqual(a, b)
	{
		if (a === b) { return true; }
		if (a === null || b === null) { return a === b; }
		if (typeof a !== typeof b) { return false; }
		if (typeof a !== 'object') { return a === b || (a !== a && b !== b); /* NaN */ }
		if (Array.isArray(a))
		{
			if (!Array.isArray(b) || a.length !== b.length) { return false; }
			for (let i = 0; i < a.length; ++i) { if (!_deepEqual(a[i], b[i])) { return false; } }
			return true;
		}
		const ka = Object.keys(a), kb = Object.keys(b);
		if (ka.length !== kb.length) { return false; }
		for (let i = 0; i < ka.length; ++i)
		{
			if (!Object.prototype.hasOwnProperty.call(b, ka[i])) { return false; }
			if (!_deepEqual(a[ka[i]], b[ka[i]])) { return false; }
		}
		return true;
	}

	function _show(v)
	{
		try { return JSON.stringify(v); } catch (_e) { return String(v); }
	}
})(typeof window !== 'undefined' ? window : globalThis);
