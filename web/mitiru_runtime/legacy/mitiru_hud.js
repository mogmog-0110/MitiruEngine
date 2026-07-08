/*!
 * [DEPRECATED 2026-07-05] legacy — 新規使用禁止 (docs/adr/0022)。
 * 理由: JS render callback + innerHTML 直注入で HUD logic を JS が所有し、
 * ADR 0007 (HUD の既定は zero-JS binder) と衝突。mitiru_state.js (legacy) に依存。
 * 移行先: mitiru_bind.js (data-m-text / show / repeat 等) + C++ state push。
 * 既存 consumer 互換のためファイルは出荷物に残す。
 */
/*!
 * mitiru_hud.js — 常駐 HUD ウィジェット (F-08)
 *
 * main.html に常駐しシーン遷移をまたいで生き残るオーバーレイ。
 * slot は mitiru.state の key を購読し、変化時に再レンダリングする。
 *
 * API: mount(container, opts) / unmount() / slots() / update()
 * opts: { stateKeys:string[], slots:[{id, render}], className:string }
 * render の戻り値型による振り分け:
 *   string → textContent | Node → replaceChildren | {html:string} → innerHTML
 *
 * stateKeys:[] (既定) = 静的スナップショット、購読なし。
 * 二重 mount: 先に前のインスタンスを黙って unmount する。
 * F-02 不在時: 最小限の inline style に fallback する。
 *
 * 仕様: docs/feedback-from-kaerucrape/2026-04-24.md F-08
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.hud) { return; }  // 既に読み込み済み

	const document = global.document;

	// ── 内部状態 ────────────────────────────────────────────
	let _root    = null;   // HTMLElement — mount された HUD ルート
	let _slots   = [];     // [{ id, el, render }]
	let _unsubs  = [];     // mitiru.state.subscribe からの [unsubscribe fn, ...]
	let _opts    = null;   // 直近の mount オプション (update() 用)

	// ── ヘルパ ───────────────────────────────────────────────────

	// slot を 1 つレンダリング。render() の戻り値型で振り分ける。
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
			// fallback: 安全に文字列化する。
			el.textContent = String(value);
		}
	}

	// 購読 key のスナップショットを作り、全 slot をレンダリングする。
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
	 * 指定した state key の plain-object スナップショットを作る。
	 * mitiru.state が未読み込みでも穏当に fallback する。
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
	 * F-02 のコンポーネントクラスが読み込み済みの stylesheet にあるか確認する。
	 * `mitiru-hud-note` が空でないルールセットに解決できれば true を返す。
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
				catch (_e) { continue; }  // cross-origin の sheet
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
		catch (_e) { /* 無視 */ }
		return false;
	}

	/**
	 * F-02 が利用できないとき、HUD ルートに inline fallback スタイルを適用する。
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

	// ── 公開 API ────────────────────────────────────────────────
	const hud = mitiru.hud = Object.create(null);

	/**
	 * HUD を `container` に mount する。
	 * 既に mount 済みなら先に前のインスタンスを unmount する (二重 mount 防止)。
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

		// 二重 mount 防止: 既存インスタンスがあれば破棄する。
		if (_root) { hud.unmount(); }

		opts = opts || {};
		var stateKeys = Array.isArray(opts.stateKeys) ? opts.stateKeys : [];
		var slotDefs  = Array.isArray(opts.slots)     ? opts.slots     : [];
		var extraClass = typeof opts.className === 'string' ? opts.className : '';

		_opts  = { stateKeys: stateKeys, slots: slotDefs, className: extraClass };
		_slots = [];
		_unsubs = [];

		// ── ルート要素を構築 ────────────────────────────────────
		_root = document.createElement('div');
		_root.setAttribute('data-mitiru-hud', '');
		_root.classList.add('mitiru-hud');
		if (extraClass) { _root.classList.add(extraClass); }

		// F-02 クラスがあれば適用、無ければ inline fallback。
		if (_f02Available())
		{
			_root.classList.add('mitiru-hud-note');
		}
		else
		{
			_applyFallbackStyle(_root);
		}

		// ── slot 要素を構築 ───────────────────────────────────
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

		// ── 初回レンダリング ────────────────────────────────────────
		_renderAll();

		// ── 購読 ─────────────────────────────────────────
		// 共有ハンドラ 1 つ — どの key の変化でも全体を再レンダリングする。
		// N 個の別クロージャを避け、_unsubs[] での後始末も簡潔になる。
		var s = mitiru.state;
		if (s && stateKeys.length > 0)
		{
			var _handler = function() { _renderAll(); };

			for (var k = 0; k < stateKeys.length; ++k)
			{
				// subscribe は即時発火する — 上で既にレンダリング済みなので、
				// 一時的にラップして冗長な初回レンダリングを抑制する。
				var unsub = (function(key)
				{
					var fired = false;
					var off = s.subscribe(key, function()
					{
						if (!fired) { fired = true; return; }  // 即時発火はスキップ
						_handler();
					});
					return off;
				})(stateKeys[k]);

				_unsubs.push(unsub);
			}
		}
	};

	/**
	 * unmount: 全 state listener を解除し、HUD 要素を DOM から取り除く。
	 * mount されていなくても安全に呼べる (no-op)。
	 */
	hud.unmount = function()
	{
		// 購読を破棄する。
		for (var i = 0; i < _unsubs.length; ++i)
		{
			try { _unsubs[i](); } catch (_e) { /* 無視 */ }
		}
		_unsubs = [];

		// 要素を取り除く。
		if (_root && _root.remove) { _root.remove(); }
		_root  = null;
		_slots = [];
		_opts  = null;
	};

	/**
	 * 現在生きている slot の記述 [{ id: string, el: HTMLElement }] を返す。
	 * mount されていなければ [] を返す。
	 */
	hud.slots = function()
	{
		return _slots.map(function(s) { return { id: s.id, el: s.el }; });
	};

	/**
	 * 現在の state スナップショットから全 slot を強制的に再レンダリングする。
	 * バッチで state を変更した後、同期的に 1 回だけ更新したいときに便利。
	 */
	hud.update = function()
	{
		_renderAll();
	};

})(typeof window !== 'undefined' ? window : globalThis);
