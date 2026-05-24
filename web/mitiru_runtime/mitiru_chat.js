/*!
 * mitiru_chat.js — chat bubble surface helper (F-10)
 *
 * Small wrapper around the `.mitiru-chat*` CSS classes in mitiru_components.css.
 * Handles bubble DOM creation, typing indicator show/hide, choice rendering,
 * auto-scroll-to-bottom anchor, and click-to-scroll-back interaction.
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.chat.mount(rootEl, opts?)        attach the surface; returns a handle
 *   mitiru.chat.send(handle, bubble)        append one bubble (returns element)
 *   mitiru.chat.typing(handle, heroine?)    show typing indicator; returns
 *                                            stop() — call to hide
 *   mitiru.chat.choice(handle, options, onPick)
 *                                           render choice-prompt buttons;
 *                                            returns dismiss() — call to remove
 *   mitiru.chat.clear(handle)               remove all bubbles/indicators
 *   mitiru.chat.scrollToBottom(handle, smooth?)
 *   mitiru.chat.isAtBottom(handle)          boolean (within 8px tolerance)
 *   mitiru.chat.unmount(handle)             detach listeners; DOM stays
 *
 * ── Bubble shape ────────────────────────────────────────────────────────────
 *   {
 *     kind:      'incoming' | 'outgoing' | 'system',    // default 'incoming'
 *     text:      'message body',                         // required for non-system
 *     speaker:   'マリア',                               // optional (incoming)
 *     heroine:   'maria',                                // optional → data-heroine
 *     time:      '06:43',                                // optional
 *     html:      false,                                  // treat text as HTML (default false)
 *   }
 *
 * ── Auto-scroll ─────────────────────────────────────────────────────────────
 *   By default, `send()` scrolls to the bottom only when the user is already
 *   pinned at the bottom (classic chat UX). Manual scroll back freezes the
 *   scroll position; sending more messages does NOT yank the user forward.
 *   Pass `{forceScroll: true}` in opts to always scroll on send.
 *
 * ── Events ──────────────────────────────────────────────────────────────────
 *   handle.root dispatches CustomEvent:
 *     'chat:append'   { bubble, element }
 *     'chat:scroll'   { atBottom }
 *     'chat:choice:pick' { option, index }
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-10
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.chat) { return; }  // already loaded

	const SCROLL_TOLERANCE = 8;

	// ── helpers ─────────────────────────────────────────────────
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
		catch (_e) { /* IE/legacy guard */ }
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
	const chat = {};

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
	mitiru.chat = chat;

})(typeof window !== 'undefined' ? window : globalThis);
