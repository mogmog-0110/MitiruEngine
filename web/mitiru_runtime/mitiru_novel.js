/*!
 * mitiru_novel.js — JSON-driven ADV novel VM (F-04)
 *
 * Implements window.mitiru.novel — a lightweight visual-novel runtime
 * that interprets the mitiru narrative JSON schema (see
 * web/mitiru_runtime/mitiru_novel/schema.json and docs/NARRATIVE_SCRIPT.md).
 *
 * Attaches to: window.mitiru.novel
 * Depends on:  mitiru_state.js (optional — for save/restore)
 *
 * // Scope (approved 2026-04-24)
 * //   INCLUDED : voice (line.voice -> new Audio(...).play()), ~10 lines
 * //   INCLUDED : backlog with jumpTo (pc rewind + full re-render from scratch)
 * //   INCLUDED : NF-10 skip-unread tracking & readline stats
 * //   INCLUDED : NF-11 effect primitives (shake/flash/tint/zoom/blur/slide/fade-sprite)
 * //   DEFERRED : backlog "time-travel fidelity" — sprite positions before a
 * //              jumpTo target may not match in-between mutation state (Phase 2)
 * //   DEFERRED : text interpolation, conditional branches beyond choice.next,
 * //              loops, localisation keys (per NARRATIVE_SCRIPT.md v2 deferrals)
 *
 * // Naming deviation from spec (approved):
 * //   novel.load(script)      = load JSON script (URL string or object)
 * //   novel.save(slotId)      = persist runtime state via mitiru.state
 * //   novel.restore(slotId)   = restore runtime state via mitiru.state
 * //   ("load" overloaded for script loading; "save"/"restore" avoid ambiguity
 * //    with the spec text that had save/load for persistence)
 *
 * Events (CustomEvent on containerEl):
 *   novel:script-loaded   — after load() resolves; detail: { scriptId }
 *   novel:line:start      — before a line is displayed; detail: { index, line }
 *   novel:line:end        — after typewriter finishes; detail: { index }
 *   novel:choice:open     — when choices are shown; detail: { options: [{label,next}] }
 *   novel:choice:pick     — when a choice is committed; detail: { label, next }
 *   novel:script:end      — script exhausted (no more lines); detail: {}
 *   novel:effect:start    — before an NF-11 effect runs; detail: { type, line, durationMs }
 *   novel:effect:end      — after an NF-11 effect completes; detail: { type }
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-04, NF-10, NF-11
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.novel) { return; }

	// ── constants ──────────────────────────────────────────────────
	const Z_BG = 0, Z_SPRITE = 10, Z_TEXTBOX = 20, Z_BACKLOG = 30;
	const TYPEWRITER_DEFAULT_CPS = 40;

	// ── private runtime state ──────────────────────────────────────
	var _containerEl = null, _bgLayer = null, _spriteLayer = null;
	var _textboxEl   = null, _backlogEl = null, _toolbarEl = null;
	var _script      = null, _pc = -1, _playing = false, _opts = {};
	// typewriter
	var _twTimer = null, _twFull = '', _twPos = 0;
	var _twLastTs = 0, _twActive = false;
	// backlog
	var _log = [];

	// ── NF-10: skip-unread tracking ───────────────────────────────
	// _readSets: { [scriptId]: Set<number> } — in-memory read index sets
	var _readSets  = Object.create(null);
	// _skipMode: 'off' | 'all' | 'read-only'
	var _skipMode  = 'off';
	// _skipTimer: RAF/timer handle for auto-advance loop
	var _skipTimer = null;
	// _warnedSprites: { [id]: true } — suppress repeat console.warn
	var _warnedSprites = Object.create(null);
	// H-02: background fit mode (cover | contain | fill). Default keeps
	// pre-H-02 behaviour; per-bg-line `fit` overrides this.
	var _bgFit = 'cover';
	// Bonus: input lockout. _inputLockout holds the configured windows
	// (0 = disabled). _inputLockUntil is the absolute timestamp at which
	// input becomes responsive again.
	var _inputLockout = { sceneTransitionMs: 0, perLineMs: 0 };
	var _inputLockUntil = 0;

	// ── helpers ────────────────────────────────────────────────────

	function _emit(name, detail)
	{
		if (!_containerEl) { return; }
		var ev;
		if (typeof CustomEvent === 'function')
		{
			ev = new CustomEvent(name, { bubbles: true, detail: detail || {} });
		}
		else
		{
			// IE/Node shim path
			ev = _containerEl.ownerDocument.createEvent('CustomEvent');
			ev.initCustomEvent(name, true, false, detail || {});
		}
		_containerEl.dispatchEvent(ev);
	}

	function _deepClone(v) { return (v == null) ? v : JSON.parse(JSON.stringify(v)); }

	function _setLayerStyle(el, z)
	{
		el.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;z-index:' + z + ';';
	}

	// H-02: allowed background-size values.
	function _isValidFit(value)
	{
		return value === 'cover' || value === 'contain' || value === 'fill';
	}

	// H-05: textbox defaults are defined in mitiru_components.css (section 19)
	// so consumer stylesheets loaded AFTER mitiru_components.css can override
	// them with normal cascade rules. Previously this module injected a
	// <style> block at mount time, which appended after consumer <link>s and
	// forced the consumer to use `!important`. The shim below stays as a
	// no-op for backwards compat; remove in a future major version.
	function _ensureStyleBlock() { /* moved to mitiru_components.css §19 */ }

	function _createDiv(cls)
	{
		var el = global.document.createElement('div');
		if (cls) { el.className = cls; }
		return el;
	}

	// ── NF-10: read-set persistence helpers ───────────────────────

	function _readKey(scriptId) { return 'novel:read:' + (scriptId || ''); }

	function _loadReadSet(scriptId)
	{
		if (_readSets[scriptId]) { return; }
		var arr = null;
		if (mitiru.state) { arr = mitiru.state.get(_readKey(scriptId)); }
		_readSets[scriptId] = new Set(Array.isArray(arr) ? arr : []);
	}

	function _saveReadSet(scriptId)
	{
		if (!mitiru.state) { return; }
		var set = _readSets[scriptId];
		if (!set) { return; }
		var sorted = Array.from(set).sort(function(a, b) { return a - b; });
		mitiru.state.set(_readKey(scriptId), sorted);
	}

	function _isTextLine(line)
	{
		var t = line.type;
		return !t || t === 'text' || t === 'dialogue';
	}

	// ── NF-10: skip-mode tick ──────────────────────────────────────

	function _cancelSkipTick()
	{
		if (_skipTimer !== null)
		{
			clearTimeout(_skipTimer);
			_skipTimer = null;
		}
	}

	function _scheduleSkipTick(durationMs)
	{
		_cancelSkipTick();
		_skipTimer = setTimeout(function()
		{
			_skipTimer = null;
			_runSkipIfNeeded();
		}, durationMs || 0);
	}

	function _runSkipIfNeeded()
	{
		if (_skipMode === 'off' || !_script) { return; }
		if (_pc < 0 || _pc >= _script.lines.length) { return; }
		var line = _script.lines[_pc];
		// Never skip choice lines.
		if (line.type === 'choice') { return; }
		var isText = _isTextLine(line);
		if (_skipMode === 'all')
		{
			// Auto-advance everything that isn't a choice.
			if (_twActive) { _twShowFull(); return; }
			_advance();
		}
		else if (_skipMode === 'read-only' && isText)
		{
			var sid   = _script.id || '';
			_loadReadSet(sid);
			var isAlreadyRead = _readSets[sid] && _readSets[sid].has(_pc);
			if (isAlreadyRead)
			{
				if (_twActive) { _twShowFull(); return; }
				_advance();
			}
		}
	}

	// ── typewriter ─────────────────────────────────────────────────

	function _twStop()
	{
		if (_twTimer !== null && typeof cancelAnimationFrame === 'function')
		{
			cancelAnimationFrame(_twTimer);
		}
		_twTimer = null;
		_twActive = false;
	}

	function _twShowFull()
	{
		_twStop();
		_twPos = _twFull.length;
		if (_textboxEl)
		{
			var el = _textboxEl.querySelector('[data-novel-text]');
			if (el) { el.textContent = _twFull; }
		}
	}

	function _twTick(ts)
	{
		if (!_twActive) { return; }
		var dt   = (ts - _twLastTs) / 1000;
		_twLastTs = ts;
		var cps  = (_opts.cps !== undefined) ? _opts.cps : TYPEWRITER_DEFAULT_CPS;
		_twPos   = Math.min(_twPos + cps * dt, _twFull.length);

		if (_textboxEl)
		{
			var textEl = _textboxEl.querySelector('[data-novel-text]');
			if (textEl) { textEl.textContent = _twFull.slice(0, Math.round(_twPos)); }
		}

		if (_twPos >= _twFull.length)
		{
			_twStop();
			_emit('novel:line:end', { index: _pc });
			if (_playing) { _advance(); return; }
			// NF-10: schedule skip-mode tick after typewriter finishes.
			if (_skipMode !== 'off') { _scheduleSkipTick(0); }
		}
		else
		{
			_twTimer = requestAnimationFrame(_twTick);
		}
	}

	function _twStart(text)
	{
		_twStop();
		_twFull    = text;
		_twPos     = 0;
		_twActive  = true;
		_twLastTs  = 0;
		_twTimer   = requestAnimationFrame(function(ts)
		{
			_twLastTs = ts;
			_twTimer  = requestAnimationFrame(_twTick);
		});
	}

	// ── background crossfade ───────────────────────────────────────

	function _setBg(path, fit)
	{
		if (!_bgLayer || !path) { return; }
		// H-02: per-line fit overrides the mount-default; default keeps
		// backward-compatible 'cover' behaviour. 'fill' is a novel-level
		// alias that maps to the CSS `background-size: 100% 100%` stretch.
		var resolvedFit = fit || _bgFit;
		if (!_isValidFit(resolvedFit))
		{
			throw new Error('novel: invalid bg fit "' + resolvedFit
				+ '" (expected cover | contain | fill)');
		}
		var cssSize = (resolvedFit === 'fill') ? '100% 100%' : resolvedFit;
		// Create a new layer on top, fade in, then remove old one.
		var doc    = global.document;
		var newImg = doc.createElement('div');
		newImg.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;'
		                     + 'background-size:' + cssSize + ';background-position:center;'
		                     + 'background-repeat:no-repeat;'
		                     + 'background-image:url("' + path + '");'
		                     + 'opacity:0;transition:opacity 0.4s ease;';
		_bgLayer.appendChild(newImg);
		// Force reflow then fade in.
		void newImg.offsetWidth;
		newImg.style.opacity = '1';

		// Remove all siblings after transition.
		var siblings = Array.prototype.slice.call(_bgLayer.children, 0, _bgLayer.children.length - 1);
		setTimeout(function()
		{
			for (var i = 0; i < siblings.length; ++i)
			{
				if (siblings[i].parentNode === _bgLayer) { _bgLayer.removeChild(siblings[i]); }
			}
		}, 450);
	}

	// ── sprites ────────────────────────────────────────────────────

	function _showSprite(id, path, pos)
	{
		if (!_spriteLayer) { return; }
		var doc = global.document;
		var el  = _spriteLayer.querySelector('[data-sprite-id="' + id + '"]');
		if (!el)
		{
			el = doc.createElement('img');
			el.setAttribute('data-sprite-id', id);
			el.style.position = 'absolute';
			el.style.bottom   = '0';
			_spriteLayer.appendChild(el);
		}
		el.src = path;
		// Position: left/center/right or custom percent string.
		var posMap = { left: '15%', center: '50%', right: '75%' };
		var left   = (pos && posMap[pos]) ? posMap[pos] : (pos || '50%');
		el.style.left      = left;
		el.style.transform = 'translateX(-50%)';
		el.style.maxHeight = '100%';
	}

	function _hideSprite(id)
	{
		if (!_spriteLayer) { return; }
		var el = _spriteLayer.querySelector('[data-sprite-id="' + id + '"]');
		if (el) { _spriteLayer.removeChild(el); }
	}

	// ── choices ────────────────────────────────────────────────────

	function _showChoices(options)
	{
		if (!_textboxEl) { return; }
		var doc      = global.document;
		var choiceEl = _createDiv('novel-choices');
		choiceEl.setAttribute('data-novel-choices', '');
		choiceEl.style.cssText = 'position:absolute;bottom:0;left:0;right:0;'
		                       + 'display:flex;flex-direction:column;gap:8px;padding:16px;';

		for (var i = 0; i < options.length; ++i)
		{
			(function(opt)
			{
				var btn = doc.createElement('button');
				btn.textContent = opt.label;
				btn.setAttribute('data-novel-choice', opt.next || '');
				btn.style.cssText = 'padding:12px 16px;cursor:pointer;font-size:1rem;';
				btn.addEventListener('click', function()
				{
					_commitChoice(opt);
				});
				choiceEl.appendChild(btn);
			})(options[i]);
		}

		_textboxEl.appendChild(choiceEl);
		_emit('novel:choice:open', { options: options });
	}

	function _clearChoices()
	{
		if (!_textboxEl) { return; }
		var el = _textboxEl.querySelector('[data-novel-choices]');
		if (el) { _textboxEl.removeChild(el); }
	}

	function _commitChoice(opt)
	{
		_clearChoices();
		_emit('novel:choice:pick', { label: opt.label, next: opt.next });
		// Jump to the labelled line if one exists in this script.
		var target = opt.next;
		if (target && _script)
		{
			for (var i = 0; i < _script.lines.length; ++i)
			{
				if (_script.lines[i].label === target)
				{
					_pc = i - 1;   // _advance() will increment
					_advance();
					return;
				}
			}
		}
		// No matching label — treat as script end.
		_emit('novel:script:end', {});
	}

	// ── backlog ────────────────────────────────────────────────────

	function _logLine(speaker, text)
	{
		_log.push({ speaker: speaker || '', text: text });
	}

	function _buildBacklogDOM()
	{
		if (!_backlogEl) { return; }
		var doc = global.document;
		// Clear and rebuild.
		_backlogEl.innerHTML = '';
		var list = doc.createElement('ul');
		list.style.cssText   = 'list-style:none;margin:0;padding:16px;overflow-y:auto;max-height:100%;';

		for (var i = 0; i < _log.length; ++i)
		{
			var entry = _log[i];
			var li    = doc.createElement('li');
			li.style.cssText = 'margin-bottom:8px;cursor:pointer;';
			li.setAttribute('data-backlog-index', String(i));
			li.innerHTML = (entry.speaker ? '<b>' + _escHtml(entry.speaker) + '</b>: ' : '')
			             + _escHtml(entry.text);
			// jumpTo on click — rewind pc and re-render from scratch.
			(function(idx) {
				li.addEventListener('click', function() { novel.jumpTo(idx); });
			})(i);
			list.appendChild(li);
		}
		_backlogEl.appendChild(list);
	}

	function _escHtml(s)
	{
		return String(s).replace(/&/g, '&amp;')
		                .replace(/</g, '&lt;')
		                .replace(/>/g, '&gt;');
	}

	// ── NF-11: effect helpers ──────────────────────────────────────

	function _resolveTarget(target)
	{
		if (target === 'stage') { return _containerEl; }
		if (target === 'bg')    { return _bgLayer; }
		if (typeof target === 'string' && target.indexOf('sprite:') === 0)
		{
			var spriteId = target.slice(7);
			if (!_spriteLayer) { return null; }
			var el = _spriteLayer.querySelector('[data-sprite-id="' + spriteId + '"]');
			if (!el)
			{
				if (!_warnedSprites[spriteId])
				{
					_warnedSprites[spriteId] = true;
					if (typeof console !== 'undefined') { console.warn('novel effect: sprite not found: ' + spriteId); }
				}
				return null;
			}
			return el;
		}
		return null;
	}

	function _animateOrFallback(el, keyframes, opts, onDone)
	{
		if (el && typeof el.animate === 'function')
		{
			var anim = el.animate(keyframes, opts);
			anim.onfinish = onDone;
		}
		else
		{
			setTimeout(onDone, opts.duration || 0);
		}
	}

	function _execEffect(line)
	{
		var type       = line.type;
		var durationMs = typeof line.durationMs === 'number' ? line.durationMs : 300;

		_emit('novel:effect:start', { type: type, line: _deepClone(line), durationMs: durationMs });

		function done()
		{
			_emit('novel:effect:end', { type: type });
		}

		if (type === 'shake')
		{
			var target = _resolveTarget(line.target || 'stage');
			if (target)
			{
				target.classList.add('mitiru-novel-shake');
				setTimeout(function()
				{
					target.classList.remove('mitiru-novel-shake');
					done();
				}, durationMs);
			}
			else { done(); }
		}
		else if (type === 'flash')
		{
			var overlay  = _createDiv('novel-flash-overlay');
			var color    = line.color    || '#ffffff';
			var peak     = typeof line.peakAlpha === 'number' ? line.peakAlpha : 1;
			overlay.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;'
			                      + 'pointer-events:none;background:' + color
			                      + ';opacity:' + peak + ';z-index:99;';
			if (_containerEl) { _containerEl.appendChild(overlay); }
			_animateOrFallback(overlay,
				[{ opacity: peak }, { opacity: 0 }],
				{ duration: durationMs, fill: 'forwards', easing: 'ease-out' },
				function()
				{
					if (overlay.parentNode) { overlay.parentNode.removeChild(overlay); }
					done();
				}
			);
		}
		else if (type === 'tint')
		{
			var tintEl  = _createDiv('novel-tint-overlay');
			var tcolor  = line.color    || '#ff0000';
			var tpeak   = typeof line.peakAlpha === 'number' ? line.peakAlpha : 0.4;
			tintEl.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;'
			                     + 'pointer-events:none;background:' + tcolor
			                     + ';opacity:0;z-index:99;';
			if (_containerEl) { _containerEl.appendChild(tintEl); }
			_animateOrFallback(tintEl,
				[{ opacity: 0 }, { opacity: tpeak }, { opacity: 0 }],
				{ duration: durationMs, fill: 'forwards', easing: 'ease-in-out' },
				function()
				{
					if (tintEl.parentNode) { tintEl.parentNode.removeChild(tintEl); }
					done();
				}
			);
		}
		else if (type === 'zoom')
		{
			var zel   = _resolveTarget(line.target || 'stage');
			var scale = typeof line.scale === 'number' ? line.scale : 1.2;
			if (zel)
			{
				_animateOrFallback(zel,
					[{ transform: 'scale(1)' }, { transform: 'scale(' + scale + ')' }, { transform: 'scale(1)' }],
					{ duration: durationMs, fill: 'none', easing: 'ease-in-out' },
					done
				);
			}
			else { done(); }
		}
		else if (type === 'blur')
		{
			var bel    = _resolveTarget(line.target || 'stage');
			var radius = typeof line.radius === 'number' ? line.radius : 8;
			if (bel)
			{
				_animateOrFallback(bel,
					[
						{ filter: 'blur(0px)' },
						{ filter: 'blur(' + radius + 'px)' },
						{ filter: 'blur(0px)' }
					],
					{ duration: durationMs, fill: 'none', easing: 'ease-in-out' },
					done
				);
			}
			else { done(); }
		}
		else if (type === 'slide')
		{
			var sel  = _resolveTarget(line.target || 'stage');
			var dist = typeof line.distance === 'number' ? line.distance : 32;
			var dir  = line.direction || 'left';
			var dx   = 0, dy = 0;
			if (dir === 'left')  { dx = -dist; }
			if (dir === 'right') { dx = dist;  }
			if (dir === 'up')    { dy = -dist; }
			if (dir === 'down')  { dy = dist;  }
			var txStart = 'translate(' + dx + 'px,' + dy + 'px)';
			if (sel)
			{
				_animateOrFallback(sel,
					[{ transform: 'translate(0,0)' }, { transform: txStart }, { transform: 'translate(0,0)' }],
					{ duration: durationMs, fill: 'none', easing: 'ease-in-out' },
					done
				);
			}
			else { done(); }
		}
		else if (type === 'fade-sprite')
		{
			var fsel   = _resolveTarget('sprite:' + (line.id || ''));
			var toOpac = typeof line.to === 'number' ? line.to : 1;
			if (fsel)
			{
				_animateOrFallback(fsel,
					[{ opacity: fsel.style.opacity || 1 }, { opacity: toOpac }],
					{ duration: durationMs, fill: 'forwards', easing: 'ease-in-out' },
					function()
					{
						fsel.style.opacity = String(toOpac);
						done();
					}
				);
			}
			else { done(); }
		}
		else
		{
			done();
		}

		setTimeout(_advance, durationMs);
	}

	// ── line execution ─────────────────────────────────────────────

	var _EFFECT_TYPES = { shake: 1, flash: 1, tint: 1, zoom: 1, blur: 1, slide: 1, 'fade-sprite': 1 };

	function _execLine(line)
	{
		var type = line.type;

		if (type === 'text' || type === 'dialogue' || !type)
		{
			// Plain dialogue / narration line.
			var speaker = line.speaker || '';
			var text    = line.text    || '';

			// NF-10: mark this line as read.
			if (_script)
			{
				var sid = _script.id || '';
				_loadReadSet(sid);
				_readSets[sid].add(_pc);
				_saveReadSet(sid);
			}

			// Update speaker display.
			if (_textboxEl)
			{
				var speakerEl = _textboxEl.querySelector('[data-novel-speaker]');
				if (speakerEl) { speakerEl.textContent = speaker; }
			}
			// Typewriter.
			_twStart(text);
			// Log.
			_logLine(speaker, text);
			// Voice.
			if (line.voice && typeof Audio !== 'undefined')
			{
				try { new Audio(line.voice).play(); } catch (_e) { /* non-fatal */ }
			}
		}
		else if (type === 'bg')
		{
			_setBg(line.path, line.fit);
			_advance();
		}
		else if (type === 'sprite')
		{
			if (line.hide) { _hideSprite(line.id); }
			else           { _showSprite(line.id, line.path, line.pos); }
			_advance();
		}
		else if (type === 'choice')
		{
			_twStop();
			var options = Array.isArray(line.options) ? line.options : [];
			_showChoices(options);
			// Execution pauses; resumes via _commitChoice.
		}
		else if (type === 'wait')
		{
			var ms = typeof line.ms === 'number' ? line.ms : 0;
			setTimeout(_advance, ms);
		}
		else if (_EFFECT_TYPES[type])
		{
			// NF-11: effect primitive — auto-advances after durationMs.
			_execEffect(line);
		}
		else
		{
			// Unknown type — skip.
			_advance();
		}
	}

	// ── public advance ─────────────────────────────────────────────

	function _advance()
	{
		if (!_script) { return; }
		_pc += 1;
		if (_pc >= _script.lines.length)
		{
			_emit('novel:script:end', {});
			return;
		}
		var line = _script.lines[_pc];
		_emit('novel:line:start', { index: _pc, line: _deepClone(line) });
		_execLine(line);
	}

	// ── schema validation ──────────────────────────────────────────

	function _validateScript(obj)
	{
		if (typeof obj !== 'object' || obj === null) { throw new Error('novel: script must be an object'); }
		if (!Array.isArray(obj.lines))               { throw new Error('novel: script.lines must be an array'); }
		for (var i = 0; i < obj.lines.length; ++i)
		{
			var line = obj.lines[i];
			if (typeof line !== 'object' || line === null)
			{
				throw new Error('novel: lines[' + i + '] must be an object');
			}
			// text/dialogue lines need .text
			var t = line.type || 'text';
			if ((t === 'text' || t === 'dialogue') && typeof line.text !== 'string')
			{
				throw new Error('novel: lines[' + i + '].text (string) required for type "' + t + '"');
			}
		}
	}

	// ── public API ─────────────────────────────────────────────────

	var novel = mitiru.novel = Object.create(null);

	/**
	 * mount(containerEl, opts)
	 *
	 * Builds the layer stack inside containerEl. If opts.textBox is provided
	 * the element is adopted (moved) into the novel root. If opts.toolbar is
	 * provided it is likewise adopted.
	 *
	 * opts: {
	 *   textBox     : HTMLElement  (existing element — will be adopted)
	 *   toolbar     : HTMLElement  (optional — will be adopted)
	 *   cps         : number       (typewriter chars/sec, default 40)
	 *   autoAdvance : boolean      (default false)
	 * }
	 */
	novel.mount = function(containerEl, opts)
	{
		if (!containerEl) { throw new Error('novel.mount: containerEl required'); }
		_containerEl = containerEl;
		_opts        = opts || {};
		_playing     = !!_opts.autoAdvance;

		// H-02: accept bgFit option (default 'cover' preserves legacy).
		if (typeof _opts.bgFit !== 'undefined')
		{
			if (!_isValidFit(_opts.bgFit))
			{
				throw new Error('novel.mount: invalid bgFit "' + _opts.bgFit
					+ '" (expected cover | contain | fill)');
			}
			_bgFit = _opts.bgFit;
		}

		// H-05: ensure CSS defaults are reachable by external stylesheets.
		_ensureStyleBlock();

		var doc = global.document;

		// Novel root — fills container.
		var root = _createDiv('novel-root');
		root.setAttribute('data-novel-root', '');
		root.style.cssText = 'position:relative;width:100%;height:100%;overflow:hidden;';

		// Layer: background
		_bgLayer = _createDiv('novel-bg-layer');
		_setLayerStyle(_bgLayer, Z_BG);
		root.appendChild(_bgLayer);

		// Layer: sprites
		_spriteLayer = _createDiv('novel-sprite-layer');
		_setLayerStyle(_spriteLayer, Z_SPRITE);
		root.appendChild(_spriteLayer);

		// Layer: text-box — adopt existing element or create one.
		if (_opts.textBox && _opts.textBox.nodeType === 1)
		{
			_textboxEl = _opts.textBox;
		}
		else
		{
			_textboxEl = _createDiv('novel-textbox');
			_textboxEl.setAttribute('data-novel-textbox', '');
			_textboxEl.innerHTML =
				'<div data-novel-speaker></div>'
				+ '<div data-novel-text></div>';
			// H-05: position, z-index, and all visual defaults live in
			// mitiru_components.css §19 (.novel-textbox). External CSS
			// loaded after mitiru_components.css overrides with plain
			// selectors — no `!important` needed.
		}
		root.appendChild(_textboxEl);

		// Layer: backlog overlay (hidden).
		_backlogEl = _createDiv('novel-backlog');
		_backlogEl.setAttribute('data-novel-backlog', '');
		_backlogEl.style.cssText =
			'position:absolute;top:0;left:0;width:100%;height:100%;'
			+ 'background:rgba(0,0,0,0.85);color:#fff;overflow-y:auto;display:none;'
			+ 'z-index:' + Z_BACKLOG + ';';
		root.appendChild(_backlogEl);

		// Optional toolbar — adopt.
		if (_opts.toolbar && _opts.toolbar.nodeType === 1)
		{
			_toolbarEl = _opts.toolbar;
			root.appendChild(_toolbarEl);
		}

		containerEl.appendChild(root);
	};

	/**
	 * load(scriptOrUrl)
	 *
	 * Loads a script. Accepts a plain object or a URL string.
	 * Returns a Promise that resolves once the script is ready.
	 * Resets playback state but does NOT auto-advance.
	 */
	novel.load = function(scriptOrUrl)
	{
		_twStop();
		_pc      = -1;
		_log     = [];
		_playing = !!(_opts && _opts.autoAdvance);

		// H-01: clear UI so the previous script's final textbox content,
		// background image, and sprites don't flash before the new script's
		// first line renders. Guarded for pre-mount calls.
		if (_textboxEl)
		{
			var _speakerEl = _textboxEl.querySelector('[data-novel-speaker]');
			var _textEl    = _textboxEl.querySelector('[data-novel-text]');
			if (_speakerEl) { _speakerEl.textContent = ''; }
			if (_textEl)    { _textEl.textContent    = ''; }
		}
		if (_bgLayer)
		{
			while (_bgLayer.firstChild) { _bgLayer.removeChild(_bgLayer.firstChild); }
		}
		if (_spriteLayer)
		{
			while (_spriteLayer.firstChild) { _spriteLayer.removeChild(_spriteLayer.firstChild); }
		}
		_clearChoices();
		_warnedSprites = Object.create(null);

		// Bonus: arm scene-transition input lockout so clicks from the
		// previous scene don't leak into the newly-loaded script.
		if (_inputLockout.sceneTransitionMs > 0)
		{
			var _now = Date.now();
			if (_now + _inputLockout.sceneTransitionMs > _inputLockUntil)
			{
				_inputLockUntil = _now + _inputLockout.sceneTransitionMs;
			}
		}

		if (typeof scriptOrUrl === 'string')
		{
			var url = scriptOrUrl;
			var fetchFn = (mitiru.fetch) ? mitiru.fetch : global.fetch;
			return fetchFn(url).then(function(r)
			{
				if (!r.ok) { throw new Error('novel.load: HTTP ' + r.status + ' for ' + url); }
				return r.json();
			}).then(function(obj)
			{
				_validateScript(obj);
				_script = obj;
				_emit('novel:script-loaded', { scriptId: obj.id || url });
				return obj;
			});
		}

		// Inline object.
		return Promise.resolve().then(function()
		{
			_validateScript(scriptOrUrl);
			_script = scriptOrUrl;
			_emit('novel:script-loaded', { scriptId: _script.id || '' });
			return _script;
		});
	};

	/**
	 * advance()
	 *
	 * Move to the next line.
	 * - If the typewriter is still running, skip to the end first (double-tap).
	 * - If the typewriter has finished, proceed to the next line.
	 */
	novel.advance = function()
	{
		// Bonus: refresh per-line input lockout on every advance() so
		// rhythmic clicking can't chain-skip lines. Games read the state
		// via novel.inputLocked() in their own click handlers.
		if (_inputLockout.perLineMs > 0)
		{
			var _now = Date.now();
			if (_now + _inputLockout.perLineMs > _inputLockUntil)
			{
				_inputLockUntil = _now + _inputLockout.perLineMs;
			}
		}

		if (_twActive)
		{
			_twShowFull();
			return;
		}
		_clearChoices();
		_advance();
	};

	/**
	 * jumpTo(logIndex)
	 *
	 * Rewind to a logged dialogue entry (backlog click).
	 * Re-renders from pc=logIndex; sprite positions before that point are
	 * NOT restored (Phase 2 deferral — documented in module header).
	 */
	novel.jumpTo = function(logIndex)
	{
		if (!_script) { return; }
		// Find the script line whose logged position matches logIndex.
		// We count only text/dialogue lines when matching to log indices.
		var logCount = 0;
		for (var i = 0; i < _script.lines.length; ++i)
		{
			var t = _script.lines[i].type || 'text';
			if (t === 'text' || t === 'dialogue' || !_script.lines[i].type)
			{
				if (logCount === logIndex)
				{
					_twStop();
					_clearChoices();
					_pc = i - 1;   // _advance() will increment
					_advance();
					_hideBacklog();
					return;
				}
				logCount++;
			}
		}
	};

	/** showBacklog() / hideBacklog() */
	novel.showBacklog = function()
	{
		if (!_backlogEl) { return; }
		_buildBacklogDOM();
		_backlogEl.style.display = 'block';
	};

	function _hideBacklog()
	{
		if (_backlogEl) { _backlogEl.style.display = 'none'; }
	}
	novel.hideBacklog = _hideBacklog;

	/**
	 * save(slotId)
	 *
	 * Persist runtime state to mitiru.state under key 'novel:save:<slotId>'.
	 * Returns a Promise. Rejects if mitiru.state is unavailable.
	 */
	novel.save = function(slotId)
	{
		return new Promise(function(resolve, reject)
		{
			if (!mitiru.state) { reject(new Error('novel.save: mitiru.state not loaded')); return; }
			if (!_script)      { reject(new Error('novel.save: no script loaded')); return; }
			var payload = {
				scriptId : _script.id || '',
				pc       : _pc,
				log      : _log.slice()
			};
			mitiru.state.set('novel:save:' + slotId, payload);
			resolve(payload);
		});
	};

	/**
	 * restore(slotId)
	 *
	 * Restore previously saved state from mitiru.state.
	 * Returns a Promise. Rejects if no save data found.
	 */
	novel.restore = function(slotId)
	{
		return new Promise(function(resolve, reject)
		{
			if (!mitiru.state) { reject(new Error('novel.restore: mitiru.state not loaded')); return; }
			var saved = mitiru.state.get('novel:save:' + slotId);
			if (!saved)        { reject(new Error('novel.restore: no save in slot ' + slotId)); return; }
			if (!_script || _script.id !== saved.scriptId)
			{
				reject(new Error('novel.restore: script mismatch (saved=' + saved.scriptId + ')'));
				return;
			}
			_twStop();
			_clearChoices();
			_log = (saved.log || []).slice();
			_pc  = typeof saved.pc === 'number' ? saved.pc : -1;
			// Re-display current line without typewriter.
			if (_pc >= 0 && _pc < _script.lines.length)
			{
				var line     = _script.lines[_pc];
				var speaker  = line.speaker || '';
				var text     = line.text    || '';
				if (_textboxEl)
				{
					var sp = _textboxEl.querySelector('[data-novel-speaker]');
					var tx = _textboxEl.querySelector('[data-novel-text]');
					if (sp) { sp.textContent = speaker; }
					if (tx) { tx.textContent = text; }
				}
			}
			resolve(saved);
		});
	};

	/** pc() — read the current program counter (0-based line index, -1 = before start). */
	novel.pc = function() { return _pc; };

	/** script() — returns a shallow copy of the loaded script object, or null. */
	novel.script = function() { return _script ? _deepClone(_script) : null; };

	/** log() — snapshot of the backlog entries. */
	novel.log = function() { return _log.slice(); };

	/**
	 * Bonus (hato H-*): configure input-lockout windows used by click-heavy
	 * callers. Both windows are in milliseconds and default to 0 (disabled).
	 *   - sceneTransitionMs: armed on novel.load() — prevents clicks from the
	 *     previous scene from leaking into the freshly loaded script.
	 *   - perLineMs: armed on each novel.advance() — prevents rhythmic
	 *     double-clicks from chain-skipping lines.
	 * Games consult novel.inputLocked() from their own click handlers.
	 */
	novel.setInputLockout = function(opts)
	{
		opts = opts || {};
		if (typeof opts.sceneTransitionMs === 'number' && opts.sceneTransitionMs >= 0)
		{
			_inputLockout.sceneTransitionMs = opts.sceneTransitionMs;
		}
		if (typeof opts.perLineMs === 'number' && opts.perLineMs >= 0)
		{
			_inputLockout.perLineMs = opts.perLineMs;
		}
	};

	novel.inputLocked = function()
	{
		return Date.now() < _inputLockUntil;
	};

	/** destroy() — tear down all DOM and internal state. */
	novel.destroy = function()
	{
		_twStop();
		_cancelSkipTick();
		_skipMode = 'off';
		if (_containerEl)
		{
			var root = _containerEl.querySelector('[data-novel-root]');
			if (root) { _containerEl.removeChild(root); }
		}
		_containerEl   = null;
		_bgLayer       = null;
		_spriteLayer   = null;
		_textboxEl     = null;
		_backlogEl     = null;
		_toolbarEl     = null;
		_script        = null;
		_pc            = -1;
		_playing       = false;
		_log           = [];
		_warnedSprites = Object.create(null);
		_bgFit         = 'cover';
		_inputLockout  = { sceneTransitionMs: 0, perLineMs: 0 };
		_inputLockUntil = 0;
	};

	// ── NF-10 public API ──────────────────────────────────────────

	/**
	 * isRead(scriptId, lineIdx)
	 *
	 * Returns true if the text/dialogue line at lineIdx in scriptId has been read.
	 * @param {string} scriptId
	 * @param {number} lineIdx — array index in script.lines (not log index)
	 * @returns {boolean}
	 */
	novel.isRead = function(scriptId, lineIdx)
	{
		var id = scriptId || '';
		_loadReadSet(id);
		return !!(_readSets[id] && _readSets[id].has(lineIdx));
	};

	/**
	 * markRead(scriptId, lineIdx)
	 *
	 * Marks a text/dialogue line as read. Persists via mitiru.state if available.
	 * Non-text lines (bg/sprite/wait/choice) are silently ignored.
	 * @param {string} scriptId
	 * @param {number} lineIdx
	 */
	novel.markRead = function(scriptId, lineIdx)
	{
		var id = scriptId || '';
		_loadReadSet(id);
		_readSets[id].add(lineIdx);
		_saveReadSet(id);
	};

	/**
	 * chapterProgress(scriptId)
	 *
	 * Returns read statistics for a script.
	 * total counts only text/dialogue lines; read counts those in the read set.
	 * @param {string} scriptId
	 * @returns {{ read: number, total: number, fraction: number }}
	 */
	novel.chapterProgress = function(scriptId)
	{
		var id  = scriptId || '';
		_loadReadSet(id);
		var set = _readSets[id] || new Set();

		// If the current script matches, count from it.
		var total = 0;
		if (_script && (_script.id || '') === id)
		{
			for (var i = 0; i < _script.lines.length; ++i)
			{
				if (_isTextLine(_script.lines[i])) { total++; }
			}
		}

		// read = intersection of set with valid text indices.
		var read = 0;
		set.forEach(function(idx)
		{
			if (!_script || (_script.id || '') !== id) { read++; return; }
			var line = _script.lines[idx];
			if (line && _isTextLine(line)) { read++; }
		});

		var fraction = total > 0 ? read / total : 0;
		return { read: read, total: total, fraction: fraction };
	};

	/**
	 * setSkipMode(mode)
	 *
	 * Set the auto-advance skip mode.
	 * @param {'off'|'all'|'read-only'} mode
	 */
	novel.SkipMode = Object.freeze({
		OFF:       'off',
		ALL:       'all',
		READ_ONLY: 'read-only'
	});

	novel.setSkipMode = function(mode)
	{
		// H-03: accept 'none' as an ergonomic alias for 'off' — callers
		// often reach for it and the silent rejection is hard to diagnose.
		if (mode === 'none') { mode = 'off'; }
		if (mode !== 'off' && mode !== 'all' && mode !== 'read-only')
		{
			throw new Error('novel.setSkipMode: invalid mode "' + mode + '"');
		}
		_skipMode = mode;
		_cancelSkipTick();
		if (mode !== 'off') { _scheduleSkipTick(0); }
	};

	/**
	 * skipMode()
	 *
	 * Returns the current skip mode.
	 * @returns {'off'|'all'|'read-only'}
	 */
	novel.skipMode = function() { return _skipMode; };

	/**
	 * totalReadLines()
	 *
	 * Returns the total number of read lines across all tracked script ids.
	 * @returns {number}
	 */
	novel.totalReadLines = function()
	{
		var total = 0;
		var ids   = Object.keys(_readSets);
		for (var i = 0; i < ids.length; ++i)
		{
			total += _readSets[ids[i]].size;
		}
		return total;
	};

	/**
	 * clearReadHistory(scriptId?)
	 *
	 * Clears the read history for one script id (if given) or all scripts.
	 * Also removes the persisted state key(s).
	 * @param {string} [scriptId]
	 */
	novel.clearReadHistory = function(scriptId)
	{
		if (scriptId !== undefined)
		{
			var id = scriptId || '';
			_readSets[id] = new Set();
			if (mitiru.state) { mitiru.state.set(_readKey(id), []); }
		}
		else
		{
			var ids = Object.keys(_readSets);
			for (var i = 0; i < ids.length; ++i)
			{
				if (mitiru.state) { mitiru.state.set(_readKey(ids[i]), []); }
			}
			_readSets = Object.create(null);
		}
	};

	// ── dev hooks ──────────────────────────────────────────────────
	// Exposed for unit tests so tests can drive the VM without a real clock.
	novel._forceAdvance  = _advance;
	novel._twShowFull    = _twShowFull;
	novel._getLog        = function() { return _log; };
	novel._getPc         = function() { return _pc; };
	novel._getReadSets   = function() { return _readSets; };
	novel._getSkipMode   = function() { return _skipMode; };

}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));
