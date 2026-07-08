/*!
 * mitiru_chat.js — chat バブル surface ヘルパー (F-10)
 *
 * mitiru_components.css の `.mitiru-chat*` CSS class の薄い wrapper。
 * バブルの DOM 生成、typing indicator の表示/非表示、choice 描画、
 * 末尾への auto-scroll anchor、クリックで scroll back する操作を扱う。
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.chat.mount(rootEl, opts?)        surface を attach; handle を返す
 *   mitiru.chat.send(handle, bubble)        バブルを 1 件追加 (element を返す)
 *   mitiru.chat.typing(handle, heroine?)    typing indicator を表示;
 *                                            stop() を返す — 呼ぶと非表示
 *   mitiru.chat.choice(handle, options, onPick)
 *                                           choice-prompt ボタンを描画;
 *                                            dismiss() を返す — 呼ぶと除去
 *   mitiru.chat.clear(handle)               全バブル/indicator を除去
 *   mitiru.chat.scrollToBottom(handle, smooth?)
 *   mitiru.chat.isAtBottom(handle)          boolean (8px の許容内か)
 *   mitiru.chat.unmount(handle)             listener を外す; DOM は残す
 *
 * ── Bubble shape ────────────────────────────────────────────────────────────
 *   {
 *     kind:      'incoming' | 'outgoing' | 'system',    // default 'incoming'
 *     text:      'message body',                         // system 以外は必須
 *     speaker:   'マリア',                               // 任意 (incoming)
 *     heroine:   'maria',                                // 任意 → data-heroine
 *     time:      '06:43',                                // 任意
 *     html:      false,                                  // text を HTML 扱い (default false)
 *   }
 *
 * ── Auto-scroll ─────────────────────────────────────────────────────────────
 *   default では `send()` は user が既に末尾に張り付いている時だけ末尾へ
 *   scroll する (古典的な chat UX)。手動で scroll back すると scroll 位置は
 *   固定され、以降の送信で user を前へ引き戻すことはない。
 *   常に send 時 scroll させたい場合は opts に `{forceScroll: true}` を渡す。
 *
 * ── Events ──────────────────────────────────────────────────────────────────
 *   handle.root が CustomEvent を dispatch する:
 *     'chat:append'   { bubble, element }
 *     'chat:scroll'   { atBottom }
 *     'chat:choice:pick' { option, index }
 *
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.chat) { return; }  // ロード済み

	const SCROLL_TOLERANCE = 8;

	// ── ヘルパー ─────────────────────────────────────────────────
	function _mkEl(tag, cls, attrs)
	{
		const el = document.createElement(tag);
		if (cls)   { el.className = cls; }
		if (attrs) { for (const k in attrs) { if (attrs[k] != null) { el.setAttribute(k, attrs[k]); } } }
		return el;
	}

	function _emit(handle, name, detail)
	{
		try
		{
			handle.root.dispatchEvent(new CustomEvent(name, { detail: detail }));
		}
		catch (_e) { /* IE/legacy ガード */ }
	}

	function _atBottom(el)
	{
		return (el.scrollHeight - el.scrollTop - el.clientHeight) <= SCROLL_TOLERANCE;
	}

	function _scrollToBottom(el, smooth)
	{
		if (typeof el.scrollTo === 'function')
		{
			el.scrollTo({ top: el.scrollHeight, behavior: smooth ? 'smooth' : 'auto' });
		}
		else
		{
			el.scrollTop = el.scrollHeight;
		}
	}

	function _buildBubble(bubble)
	{
		const kind = (bubble && bubble.kind) || 'incoming';
		if (kind !== 'incoming' && kind !== 'outgoing' && kind !== 'system')
		{
			throw new Error('mitiru.chat: unknown bubble kind: ' + kind);
		}

		const wrap = _mkEl('div',
			'mitiru-chat-bubble mitiru-chat-bubble--' + kind,
			bubble && bubble.heroine ? { 'data-heroine': bubble.heroine } : null);

		if (bubble && bubble.speaker && kind === 'incoming')
		{
			const sp = _mkEl('span', 'mitiru-chat-bubble__speaker');
			sp.textContent = bubble.speaker;
			wrap.appendChild(sp);
		}

		const txt = _mkEl('span', 'mitiru-chat-bubble__text');
		const body = bubble && bubble.text != null ? String(bubble.text) : '';
		if (bubble && bubble.html) { txt.innerHTML = body; }
		else                       { txt.textContent = body; }
		wrap.appendChild(txt);

		if (bubble && bubble.time)
		{
			const tm = _mkEl('span', 'mitiru-chat-bubble__time');
			tm.textContent = String(bubble.time);
			wrap.appendChild(tm);
		}
		return wrap;
	}

	function _buildTyping(heroine)
	{
		const el = _mkEl('div', 'mitiru-chat-typing', heroine ? { 'data-heroine': heroine } : null);
		el.appendChild(_mkEl('span'));
		el.appendChild(_mkEl('span'));
		el.appendChild(_mkEl('span'));
		return el;
	}

	// ── public API ──────────────────────────────────────────────
	const chat = {};  // 公開 API

	chat.mount = function(rootEl, opts)
	{
		if (!rootEl || rootEl.nodeType !== 1)
		{
			throw new Error('mitiru.chat.mount: rootEl must be an element');
		}
		opts = opts || {};

		rootEl.classList.add('mitiru-chat');
		rootEl.setAttribute('data-mitiru-chat', '');

		const handle = {
			root:          rootEl,
			forceScroll:   !!opts.forceScroll,
			_userScrolled: false,
		};

		handle._onScroll = function()
		{
			const atBot = _atBottom(rootEl);
			handle._userScrolled = !atBot;
			_emit(handle, 'chat:scroll', { atBottom: atBot });
		};
		rootEl.addEventListener('scroll', handle._onScroll, { passive: true });

		return handle;
	};

	chat.unmount = function(handle)
	{
		if (!handle) { return; }
		if (handle._onScroll) { handle.root.removeEventListener('scroll', handle._onScroll); }
		handle._onScroll = null;
	};

	chat.send = function(handle, bubble)
	{
		if (!handle || !handle.root) { throw new Error('mitiru.chat.send: invalid handle'); }
		const wasAtBottom = _atBottom(handle.root);
		const el = _buildBubble(bubble);
		handle.root.appendChild(el);
		_emit(handle, 'chat:append', { bubble: bubble, element: el });
		if (handle.forceScroll || wasAtBottom) { _scrollToBottom(handle.root); }
		return el;
	};

	chat.typing = function(handle, heroine)
	{
		if (!handle || !handle.root) { throw new Error('mitiru.chat.typing: invalid handle'); }
		const wasAtBottom = _atBottom(handle.root);
		const el = _buildTyping(heroine);
		handle.root.appendChild(el);
		if (handle.forceScroll || wasAtBottom) { _scrollToBottom(handle.root); }
		return function stop()
		{
			if (el.parentNode) { el.parentNode.removeChild(el); }
		};
	};

	chat.choice = function(handle, options, onPick)
	{
		if (!handle || !handle.root) { throw new Error('mitiru.chat.choice: invalid handle'); }
		if (!Array.isArray(options)) { throw new Error('mitiru.chat.choice: options must be an array'); }

		const wrap = _mkEl('div', 'mitiru-chat-choice');
		const btns = [];
		const dismiss = function()
		{
			if (wrap.parentNode) { wrap.parentNode.removeChild(wrap); }
		};
		for (let i = 0; i < options.length; ++i)
		{
			const opt = options[i];
			const btn = _mkEl('button', 'mitiru-chat-choice__btn');
			btn.type        = 'button';
			btn.textContent = (opt && opt.label != null) ? String(opt.label) : String(opt);
			(function(capturedOpt, capturedIdx)
			{
				btn.addEventListener('click', function()
				{
					_emit(handle, 'chat:choice:pick', { option: capturedOpt, index: capturedIdx });
					if (typeof onPick === 'function')
					{
						try { onPick(capturedOpt, capturedIdx); }
						catch (e) { console.error('[mitiru.chat] onPick threw:', e); }
					}
					dismiss();
				});
			})(opt, i);
			wrap.appendChild(btn);
			btns.push(btn);
		}

		const wasAtBottom = _atBottom(handle.root);
		handle.root.appendChild(wrap);
		if (handle.forceScroll || wasAtBottom) { _scrollToBottom(handle.root); }
		return dismiss;
	};

	chat.clear = function(handle)
	{
		if (!handle || !handle.root) { return; }
		while (handle.root.firstChild) { handle.root.removeChild(handle.root.firstChild); }
	};

	chat.scrollToBottom = function(handle, smooth)
	{
		if (!handle || !handle.root) { return; }
		_scrollToBottom(handle.root, !!smooth);
	};

	chat.isAtBottom = function(handle)
	{
		if (!handle || !handle.root) { return false; }
		return _atBottom(handle.root);
	};

	// ── export ──────────────────────────────────────────────────
	mitiru.chat = chat;  // 公開

})(typeof window !== 'undefined' ? window : globalThis);
