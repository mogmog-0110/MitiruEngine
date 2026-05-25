/*!
 * mitiru_router.js — single-page シーンルーター (F-01)
 *
 * CEF/ブラウザの single-page application 内のシーン遷移を管理する。
 * シーンは route key で register され、navigation はシーンの HTML を
 * <iframe> (または host 要素) に読み込んでライフサイクルフックを実行する。
 *
 * 提供 API:
 *   window.mitiru.router.register(key, descriptor)  — シーンを登録 / 差し替え
 *   window.mitiru.router.navigate(key, params?)      — シーンへ遷移
 *   window.mitiru.router.current()                   — { key, params } または null
 *   window.mitiru.router.back()                      — 履歴スタックを 1 段 pop
 *   window.mitiru.router.onSceneReady()              — シーンページが DOMContentLoaded で呼ぶ
 *
 * scene descriptor:
 *   {
 *     url:     string,           // シーン HTML へのパス (ゲームルート相対)
 *     preload: boolean,          // 予約 — console.warn を出すのみ、未実装
 *     in?:     function(params), // transition-in フック (シーンの DOMContentLoaded 後に実行)
 *     out?:    function(),       // transition-out フック (iframe src 変更前に実行)
 *   }
 *
 * 遷移シーケンス:
 *   1. 現在シーンの `out()` フックを呼ぶ (あれば)
 *   2. document.body を opacity 0 へフェード (CSS transition、FADE_MS)
 *   3. iframe src を新シーン URL に設定
 *   4. シーンページが自身の DOMContentLoaded で `mitiru.router.onSceneReady()` を呼ぶ
 *   5. document.body を opacity 1 へ戻すフェード
 *   6. 新シーンの `in(params)` フックを呼ぶ (あれば)
 *
 * 履歴:
 *   navigate() は内部スタックに push する (最大 HISTORY_MAX 件)。
 *   back() は pop して前の entry へ navigate する — back() はスタックに push しないので
 *   無限ループ防止ガードは不要。
 *
 * 仕様: docs/feedback-from-kaerucrape/2026-04-24.md F-01
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.router) { return; }  // 既に読み込み済み

	// ── 定数 ─────────────────────────────────────────────────
	const FADE_MS     = 200;   // body opacity フェード時間 (ms)
	const HISTORY_MAX = 50;    // navigation 履歴の最大件数

	// ── 内部状態 ────────────────────────────────────────────
	const _registry = Object.create(null);  // key -> descriptor
	let   _current  = null;                 // { key, params, descriptor }
	const _history  = [];                   // [{ key, params }, ...]  (古い → 新しい)
	let   _pending  = null;                 // シーンが ready を通知したとき resolve する Promise
	let   _pendingResolve = null;           // _pending の resolve 関数

	// ── ヘルパ ───────────────────────────────────────────────────
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
		// シーンページが mitiru.router.onSceneReady() を呼んでこれを resolve する。
		_pending = new Promise(function(resolve)
		{
			_pendingResolve = resolve;
		});
		// タイムアウト安全策 — シーンが onSceneReady を呼ばなければ 5 秒後に解除。
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

	// ── 公開 API ────────────────────────────────────────────────
	const router = mitiru.router = Object.create(null);

	/**
	 * シーンを登録、または差し替える。
	 * 同じ key で 2 回目を呼ぶと descriptor を差し替える (セマンティクスが明快)。
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
		// immutable なコピーを保存する。
		_registry[key] = Object.freeze({
			url:     descriptor.url,
			preload: descriptor.preload || false,
			in:      typeof descriptor.in  === 'function' ? descriptor.in  : null,
			out:     typeof descriptor.out === 'function' ? descriptor.out : null,
		});
	};

	/**
	 * 登録済みシーンへ navigate する。
	 * シーンの遷移インが完了したとき resolve する Promise を返す。
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
				// 1. 現在シーンの out() フックを実行。
				if (_current && _current.descriptor.out)
				{
					try { return _current.descriptor.out(); }
					catch (e) { console.error('[mitiru.router] out() threw:', e); }
				}
			})
			.then(function()
			{
				// 2. フェードアウト。
				_setFade(0, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				// 3. 履歴に push (前進ナビゲーション時のみ。back() 経由では push しない)。
				if (_current)
				{
					_history.push({ key: _current.key, params: _current.params });
					if (_history.length > HISTORY_MAX)
					{
						_history.splice(0, _history.length - HISTORY_MAX);
					}
				}

				// 4. 新しい current を設定し iframe src を更新。
				_current = { key: key, params: params, descriptor: descriptor };
				const iframe = _getIframe();
				const readyPromise = _waitSceneReady();
				iframe.src = descriptor.url;
				return readyPromise;
			})
			.then(function()
			{
				// 5. フェードイン。
				_setFade(1, FADE_MS);
				return _waitMs(FADE_MS);
			})
			.then(function()
			{
				// 6. 新シーンの in() フックを実行。
				if (descriptor.in)
				{
					try { return descriptor.in(params); }
					catch (e) { console.error('[mitiru.router] in() threw:', e); }
				}
			});
	};

	/**
	 * 現在のシーン記述 { key, params }、または null を返す。
	 */
	router.current = function()
	{
		if (!_current) { return null; }
		return { key: _current.key, params: _current.params };
	};

	/**
	 * 履歴スタックを 1 段戻る。
	 * 履歴が空なら no-op (即 resolve)。
	 */
	router.back = function()
	{
		if (_history.length === 0) { return Promise.resolve(); }
		const prev = _history.pop();

		// 履歴に再 push せずに navigate する。
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
	 * シーンページが自身の DOMContentLoaded ハンドラから呼ぶ。
	 * シーンが mount され in() フックの準備が整ったことを router に通知する。
	 *
	 * 各シーンの HTML 内のパターン:
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

	// テスト / デバッグ用に履歴の長さを公開する。
	Object.defineProperty(router, '_historyLength', {
		get: function() { return _history.length; },
		enumerable: false,
	});

})(typeof window !== 'undefined' ? window : globalThis);
