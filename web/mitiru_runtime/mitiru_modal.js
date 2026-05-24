/*!
 * mitiru_modal.js — reusable modal / dialog primitive (NF-13)
 *
 * Provides alert / confirm / prompt / custom modals with full accessibility,
 * focus trapping, Escape / backdrop dismissal, and nested stacking support.
 *
 * API:
 *   mitiru.modal.alert(opts)    → Promise<void>
 *   mitiru.modal.confirm(opts)  → Promise<boolean>
 *   mitiru.modal.prompt(opts)   → Promise<string|null>
 *   mitiru.modal.custom(opts)   → Promise<any>
 *   mitiru.modal.close(value?)  — close topmost programmatically
 *   mitiru.modal.isOpen()       → boolean
 *   mitiru.modal.count()        → number
 *   mitiru.modal.on(event, cb) / off(event, cb)
 *
 * Implements spec: docs/feedback-from-engine/2026-04-24b NF-13
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.modal) { return; }  // already loaded

	const document = global.document;

	// ── internal state ────────────────────────────────────────────
	var _stack    = [];   // [{root, resolve, kind, opts}]  topmost = last
	var _zBase    = 1000; // z-index base; each modal adds 10
	var _idSeq    = 0;    // monotonic ID for aria-labelledby uniqueness
	var _listeners = {};  // { 'open': [fn,...], 'close': [fn,...] }

	// ── event emitter helpers ─────────────────────────────────────

	function _emit(event, payload)
	{
		var handlers = _listeners[event];
		if (!handlers) { return; }
		for (var i = 0; i < handlers.length; ++i)
		{
			try { handlers[i](payload); } catch (_e) { /* isolate */ }
		}
	}

	// ── focus trap ───────────────────────────────────────────────

	var _FOCUSABLE = [
		'a[href]',
		'area[href]',
		'button:not([disabled])',
		'input:not([disabled])',
		'select:not([disabled])',
		'textarea:not([disabled])',
		'[tabindex]:not([tabindex="-1"])',
	].join(',');

	function _getFocusable(root)
	{
		var all = root.querySelectorAll(_FOCUSABLE);
		var out = [];
		for (var i = 0; i < all.length; ++i) { out.push(all[i]); }
		return out;
	}

	function _trapFocus(e, root)
	{
		var focusable = _getFocusable(root);
		if (focusable.length === 0) { e.preventDefault(); return; }

		var first = focusable[0];
		var last  = focusable[focusable.length - 1];

		if (e.shiftKey)
		{
			if (document.activeElement === first)
			{
				e.preventDefault();
				last.focus();
			}
		}
		else
		{
			if (document.activeElement === last)
			{
				e.preventDefault();
				first.focus();
			}
		}
	}

	// ── DOM builder ───────────────────────────────────────────────

	function _normalizeOpts(kind, opts)
	{
		opts = opts || {};
		var defaults = {
			title:           opts.title           || '',
			body:            opts.body            !== undefined ? opts.body : '',
			html:            opts.html            === true,
			okLabel:         opts.okLabel         || 'OK',
			cancelLabel:     opts.cancelLabel     || 'Cancel',
			okKind:          opts.okKind          || 'primary',
			closeOnBackdrop: opts.closeOnBackdrop !== false,
			closeOnEscape:   opts.closeOnEscape   !== false,
			dismissable:     opts.dismissable     !== false,
			defaultValue:    opts.defaultValue    !== undefined ? opts.defaultValue : '',
			placeholder:     opts.placeholder     || '',
		};

		if (opts.initialFocus !== undefined)
		{
			defaults.initialFocus = opts.initialFocus;
		}
		else if (kind === 'alert')    { defaults.initialFocus = 'ok'; }
		else if (kind === 'confirm')  { defaults.initialFocus = 'cancel'; }
		else if (kind === 'prompt')   { defaults.initialFocus = 'input'; }
		else                          { defaults.initialFocus = 'ok'; }

		return defaults;
	}

	function _buildRoot(kind, opts, resolve)
	{
		_idSeq += 1;
		var labelId = 'mitiru-modal-title-' + _idSeq;
		var zIndex  = _zBase + _stack.length * 10;

		// ── root wrapper ──────────────────────────────────────────
		var root = document.createElement('div');
		root.className = 'mitiru-modal-root';
		root.setAttribute('data-mitiru-modal', '');
		root.setAttribute('data-mitiru-modal-type', kind);
		root.style.zIndex = String(zIndex);

		// ── backdrop ──────────────────────────────────────────────
		var backdrop = document.createElement('div');
		backdrop.className = 'mitiru-modal-backdrop';
		root.appendChild(backdrop);

		// ── dialog box ───────────────────────────────────────────
		var box = document.createElement('div');
		box.className = 'mitiru-modal';
		box.setAttribute('role', 'dialog');
		box.setAttribute('aria-modal', 'true');
		if (opts.title) { box.setAttribute('aria-labelledby', labelId); }
		root.appendChild(box);

		// × button
		if (opts.dismissable)
		{
			var closeBtn = document.createElement('button');
			closeBtn.className = 'mitiru-modal__close';
			closeBtn.setAttribute('aria-label', 'Close');
			closeBtn.setAttribute('type', 'button');
			closeBtn.textContent = '×';
			box.appendChild(closeBtn);

			closeBtn.addEventListener('click', function()
			{
				_resolveTop(null, true);
			});
		}

		// title
		if (opts.title)
		{
			var titleEl = document.createElement('h2');
			titleEl.className = 'mitiru-modal__title';
			titleEl.id = labelId;
			titleEl.textContent = opts.title;
			box.appendChild(titleEl);
		}

		// body
		var bodyEl = document.createElement('div');
		bodyEl.className = 'mitiru-modal__body';
		_applyBody(bodyEl, opts.body, opts.html);
		box.appendChild(bodyEl);

		// prompt input
		var inputEl = null;
		if (kind === 'prompt')
		{
			inputEl = document.createElement('input');
			inputEl.type = 'text';
			inputEl.className = 'mitiru-modal__input';
			inputEl.value = opts.defaultValue;
			inputEl.placeholder = opts.placeholder;
			box.appendChild(inputEl);

			inputEl.addEventListener('keydown', function(e)
			{
				if (e.key === 'Enter') { _resolveTop(inputEl.value, false); }
			});
		}

		// actions
		var hasCancel = (kind === 'confirm' || kind === 'prompt');
		var actionsEl = document.createElement('div');
		actionsEl.className = 'mitiru-modal__actions';

		var cancelBtn = null;
		if (hasCancel)
		{
			cancelBtn = document.createElement('button');
			cancelBtn.className = 'mitiru-modal__btn mitiru-modal__btn--cancel';
			cancelBtn.setAttribute('type', 'button');
			cancelBtn.textContent = opts.cancelLabel;
			actionsEl.appendChild(cancelBtn);

			cancelBtn.addEventListener('click', function()
			{
				_resolveTop(kind === 'prompt' ? null : false, true);
			});
		}

		var okBtn = document.createElement('button');
		var okClass = 'mitiru-modal__btn mitiru-modal__btn--ok mitiru-modal__btn--' + opts.okKind;
		okBtn.className = okClass;
		okBtn.setAttribute('type', 'button');
		okBtn.textContent = opts.okLabel;
		actionsEl.appendChild(okBtn);

		okBtn.addEventListener('click', function()
		{
			var value;
			if (kind === 'confirm') { value = true; }
			else if (kind === 'prompt') { value = inputEl ? inputEl.value : ''; }
			else { value = undefined; }
			_resolveTop(value, false);
		});

		box.appendChild(actionsEl);

		// keyboard events
		root.addEventListener('keydown', function(e)
		{
			if (_stack.length === 0) { return; }
			var top = _stack[_stack.length - 1];
			if (top.root !== root) { return; }

			if (e.key === 'Tab')
			{
				_trapFocus(e, box);
				return;
			}

			if (e.key === 'Escape' && opts.closeOnEscape)
			{
				e.preventDefault();
				_resolveTop(kind === 'prompt' ? null : (kind === 'confirm' ? false : undefined), true);
			}
		});

		// backdrop click
		backdrop.addEventListener('click', function()
		{
			if (opts.closeOnBackdrop)
			{
				_resolveTop(kind === 'prompt' ? null : (kind === 'confirm' ? false : undefined), true);
			}
		});

		return { root: root, box: box, okBtn: okBtn, cancelBtn: cancelBtn, inputEl: inputEl };
	}

	function _applyBody(el, body, asHtml)
	{
		if (body instanceof global.Node)
		{
			el.appendChild(body);
		}
		else if (typeof body === 'string')
		{
			if (asHtml) { el.innerHTML = body; }
			else        { el.textContent = body; }
		}
		else if (body !== null && body !== undefined)
		{
			el.textContent = String(body);
		}
	}

	function _setInitialFocus(focusHint, elements)
	{
		if (focusHint === 'ok' && elements.okBtn)
		{
			elements.okBtn.focus();
			return;
		}
		if (focusHint === 'cancel' && elements.cancelBtn)
		{
			elements.cancelBtn.focus();
			return;
		}
		if (focusHint === 'input' && elements.inputEl)
		{
			elements.inputEl.focus();
			elements.inputEl.select();
			return;
		}
		// CSS selector
		if (typeof focusHint === 'string')
		{
			var target = elements.box.querySelector(focusHint);
			if (target && typeof target.focus === 'function')
			{
				target.focus();
				return;
			}
		}
		// fallback
		if (elements.okBtn) { elements.okBtn.focus(); }
	}

	// ── open / close lifecycle ────────────────────────────────────

	function _open(kind, opts)
	{
		opts = _normalizeOpts(kind, opts);
		var savedFocus = document.activeElement;

		return new Promise(function(resolve)
		{
			var elements = _buildRoot(kind, opts, resolve);
			var entry = {
				root:       elements.root,
				resolve:    resolve,
				kind:       kind,
				opts:       opts,
				savedFocus: savedFocus,
			};

			_stack.push(entry);
			document.body.appendChild(elements.root);
			_setInitialFocus(opts.initialFocus, elements);
			_emit('open', { kind: kind, options: opts });
		});
	}

	function _resolveTop(value, dismissed)
	{
		if (_stack.length === 0) { return; }
		var entry = _stack.pop();
		_teardown(entry, value, dismissed);
	}

	function _teardown(entry, value, dismissed)
	{
		if (entry.root && entry.root.parentNode)
		{
			entry.root.parentNode.removeChild(entry.root);
		}

		// Restore focus only if this was the topmost modal remaining
		// (after pop, the stack no longer contains this entry).
		if (_stack.length === 0 && entry.savedFocus && typeof entry.savedFocus.focus === 'function')
		{
			try { entry.savedFocus.focus(); } catch (_e) { /* ignore */ }
		}
		else if (_stack.length > 0)
		{
			// Return focus to previous modal's primary button.
			var prev = _stack[_stack.length - 1];
			var prevFocusable = _getFocusable(prev.root);
			if (prevFocusable.length > 0) { prevFocusable[0].focus(); }
		}

		_emit('close', { kind: entry.kind, value: value, dismissed: dismissed === true });
		entry.resolve(value);
	}

	// ── public API ────────────────────────────────────────────────

	var modal = mitiru.modal = Object.create(null);

	modal.alert = function(opts)
	{
		return _open('alert', opts);
	};

	modal.confirm = function(opts)
	{
		return _open('confirm', opts);
	};

	modal.prompt = function(opts)
	{
		return _open('prompt', opts);
	};

	modal.custom = function(opts)
	{
		return _open('custom', opts);
	};

	modal.close = function(resolveValue)
	{
		if (_stack.length === 0) { return; }
		_resolveTop(resolveValue, false);
	};

	modal.isOpen = function()
	{
		return _stack.length > 0;
	};

	modal.count = function()
	{
		return _stack.length;
	};

	modal.on = function(event, cb)
	{
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
		return function() { modal.off(event, cb); };
	};

	modal.off = function(event, cb)
	{
		var handlers = _listeners[event];
		if (!handlers) { return; }
		var idx = handlers.indexOf(cb);
		if (idx >= 0) { handlers.splice(idx, 1); }
	};

})(typeof window !== 'undefined' ? window : globalThis);
