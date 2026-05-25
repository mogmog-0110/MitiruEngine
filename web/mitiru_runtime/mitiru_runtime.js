/*!
 * mitiru_runtime.js — MitiruEngine web runtime helpers
 *
 * CEF / ブラウザ 両方で動く、ゲーム UI が共通で必要とするユーティリティ。
 * IIFE で `window.mitiru` (グローバル) に機能を足すだけ。ESM 不使用 —
 * 利用元ゲーム (KaeruCrape ほか) が生スクリプトで読み込んでいるため。
 *
 * 使い方 (games/<name>/assets/ui/index.html):
 *
 *   <script src="../mitiru_runtime/mitiru_runtime.js"></script>
 *   <script>
 *     const balance = await mitiru.loadJson('data/balance.json', {
 *         schema: '1.0.0',
 *         required: ['cook.duration', 'combo.step'],
 *         freeze: true,
 *     });
 *   </script>
 *
 * 機能:
 *   mitiru.fetch(url, opts)       — CEF file:// 失敗に自動 XHR fallback (E-15)
 *   mitiru.loadJson(path, opts)   — schema_version + required + freeze (E-04)
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};

	// ── E-15: CEF-aware fetch shim ───────────────────────────────
	// CEF 128 の fetch() は file:// URL に対して稀に TypeError を投げる
	// (net::ERR_FAILED)。GET についてのみ XHR に自動 fallback する。
	// それ以外 (POST/PUT/DELETE や stream body) は素の fetch に委譲。
	mitiru.fetch = async function mitiruFetch(url, options)
	{
		options = options || {};
		const method = (options.method || 'GET').toUpperCase();
		const isGet = method === 'GET';

		try
		{
			const r = await fetch(url, options);
			return r;
		}
		catch (fetchErr)
		{
			// fetch throw (CEF file:// 特有、またはネットワーク障害)
			if (!isGet) { throw fetchErr; }   // non-GET は fallback 危険、throw

			return new Promise(function(resolve, reject)
			{
				const xhr = new XMLHttpRequest();
				xhr.open('GET', url, true);
				if (options.responseType) { xhr.responseType = options.responseType; }
				xhr.onload = function()
				{
					const ok = (xhr.status >= 200 && xhr.status < 300)
					        || (xhr.status === 0 && xhr.responseText);  // file:// は 0
					resolve({
						ok: ok,
						status: xhr.status || (ok ? 200 : 0),
						statusText: xhr.statusText,
						url: url,
						headers: { get: function() { return null; } },
						text: function() { return Promise.resolve(xhr.responseText); },
						json: function() { return Promise.resolve(JSON.parse(xhr.responseText)); },
						arrayBuffer: function()
						{
							if (xhr.response instanceof ArrayBuffer) { return Promise.resolve(xhr.response); }
							return Promise.reject(new Error(
								'mitiru.fetch: pass options.responseType="arraybuffer" for binary'));
						},
					});
				};
				xhr.onerror = function() { reject(new Error('mitiru.fetch XHR failed: ' + url)); };
				xhr.send(options.body || null);
			});
		}
	};

	// ── E-04: JSON loader with schema + required + freeze ───────
	// KaeruCrape cooking_balance.js / cooking_recipes.js / cooking_style.js の
	// 共通パターン:
	//   - fetch (または XHR fallback) で GET
	//   - data.schema_version が opts.schema と一致するか確認
	//   - opts.required の dot-path がすべて存在するか確認
	//   - deep freeze (デフォルト true) して意図しない mutation を防ぐ
	mitiru.loadJson = async function mitiruLoadJson(path, opts)
	{
		opts = opts || {};
		const r = await mitiru.fetch(path);
		if (!r.ok)
		{
			throw new Error('mitiru.loadJson: HTTP ' + r.status + ' loading ' + path);
		}
		let data;
		try { data = await r.json(); }
		catch (e)
		{
			throw new Error('mitiru.loadJson: invalid JSON in ' + path + ' — ' + e.message);
		}

		if (opts.schema !== undefined && data && data.schema_version !== opts.schema)
		{
			throw new Error('mitiru.loadJson: ' + path + ' schema_version mismatch '
			              + '(want "' + opts.schema + '", got "' + data.schema_version + '")');
		}

		if (opts.required && Array.isArray(opts.required))
		{
			for (let i = 0; i < opts.required.length; ++i)
			{
				const p = opts.required[i];
				if (_pathLookup(data, p) === undefined)
				{
					throw new Error('mitiru.loadJson: ' + path
					              + ' missing required path "' + p + '"');
				}
			}
		}

		if (opts.freeze !== false) { _deepFreeze(data); }
		return data;
	};

	function _pathLookup(obj, dotPath)
	{
		const parts = dotPath.split('.');
		let cur = obj;
		for (let i = 0; i < parts.length; ++i)
		{
			if (cur === null || cur === undefined) { return undefined; }
			cur = cur[parts[i]];
		}
		return cur;
	}

	function _deepFreeze(obj)
	{
		if (obj === null || typeof obj !== 'object') { return obj; }
		if (Object.isFrozen(obj)) { return obj; }
		Object.freeze(obj);
		const keys = Object.keys(obj);
		for (let i = 0; i < keys.length; ++i) { _deepFreeze(obj[keys[i]]); }
		return obj;
	}

	// ── NF-01: manifest 相対 URL 解決 ─────────────────
	// 問題: /ui/novel.html のシーンが /ui/data/script_manifest.json を fetch し、
	// その manifest が列挙する entry パスは「シーンではなく manifest の位置」に
	// 相対している。これらを document.baseURI に対して解決すると無言で 404 になる。
	// 定石: 各 entry を `new URL(entry, manifestUrl).href` で解決する。
	//
	// 絶対 URL (http://…, file://…, /absolute, data:…) はそのまま通る —
	// `new URL()` が正しく処理する。
	mitiru.resolveUrl = function mitiruResolveUrl(rel, base)
	{
		if (typeof rel !== 'string' || !rel)
		{
			throw new TypeError('mitiru.resolveUrl: rel must be a non-empty string');
		}
		var baseRef = base;
		if (!baseRef)
		{
			baseRef = (typeof document !== 'undefined' && document.baseURI) || undefined;
		}
		try { return new URL(rel, baseRef).href; }
		catch (e)
		{
			throw new Error('mitiru.resolveUrl: cannot resolve "' + rel
			              + '" against "' + baseRef + '" — ' + e.message);
		}
	};
})(typeof window !== 'undefined' ? window : globalThis);
