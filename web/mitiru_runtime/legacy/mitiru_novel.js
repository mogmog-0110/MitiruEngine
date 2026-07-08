/*!
 * [DEPRECATED 2026-07-05] legacy — 新規使用禁止 (docs/adr/0022)。
 * 理由: pc / 分岐 / 既読を JS が所有する novel VM で、ADR 0005/0017
 * (gameplay state は C++ GameMemory 単一源) とレイヤー表「novel VM は C++」に違反。
 * 移行先: novel VM の C++ 移管 (script は純データ JSON のまま)。移管まで現状凍結。
 * 既存 consumer 互換のためファイルは出荷物に残す。
 */
/*!
 * mitiru_novel.js — JSON 駆動の ADV novel VM (F-04)
 *
 * window.mitiru.novel を実装。mitiru narrative JSON schema を解釈する
 * 軽量な visual-novel runtime (schema は
 * web/mitiru_runtime/mitiru_novel/schema.json と docs/NARRATIVE_SCRIPT.md 参照)。
 *
 * Attaches to: window.mitiru.novel
 * Depends on:  mitiru_state.js (optional — save/restore 用)
 *
 * // Scope (2026-04-24 承認)
 * //   INCLUDED : voice (line.voice -> new Audio(...).play())、約 10 行
 * //   INCLUDED : jumpTo 付き backlog (pc 巻き戻し + 全 re-render)
 * //   INCLUDED : NF-10 未読 skip 追跡 & readline 統計
 * //   INCLUDED : NF-11 effect primitive (shake/flash/tint/zoom/blur/slide/fade-sprite)
 * //   DEFERRED : backlog の「time-travel 忠実度」 — jumpTo target 以前の sprite
 * //              位置は途中の mutation state と一致しない場合あり (Phase 2)
 * //   DEFERRED : text interpolation、choice.next を超える条件分岐、
 * //              loop、localisation key (NARRATIVE_SCRIPT.md v2 の deferral 通り)
 *
 * // spec からの命名逸脱 (承認済):
 * //   novel.load(script)      = JSON script を読み込む (URL string か object)
 * //   novel.save(slotId)      = mitiru.state 経由で runtime state を永続化
 * //   novel.restore(slotId)   = mitiru.state 経由で runtime state を復元
 * //   ("load" は script 読み込み用にオーバーロード。永続化に save/load を使う
 * //    spec 文面との曖昧さを "save"/"restore" で回避)
 *
 * Events (containerEl 上の CustomEvent):
 *   novel:script-loaded   — load() resolve 後; detail: { scriptId }
 *   novel:line:start      — line 表示前; detail: { index, line }
 *   novel:line:end        — typewriter 完了後; detail: { index }
 *   novel:choice:open     — 選択肢表示時; detail: { options: [{label,next}] }
 *   novel:choice:pick     — 選択肢確定時; detail: { label, next }
 *   novel:script:end      — script 末尾到達 (line 残無し); detail: {}
 *   novel:effect:start    — NF-11 effect 実行前; detail: { type, line, durationMs }
 *   novel:effect:end      — NF-11 effect 完了後; detail: { type }
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-04, NF-10, NF-11
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.novel) { return; }

	// ── 定数 ──────────────────────────────────────────────────
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

	// ── NF-10: 未読 skip 追跡 ───────────────────────────────
	// _readSets: { [scriptId]: Set<number> } — メモリ上の既読 index 集合
	var _readSets  = Object.create(null);
	// _skipMode: 'off' | 'all' | 'read-only'
	var _skipMode  = 'off';
	// _skipTimer: auto-advance loop 用 RAF/timer ハンドル
	var _skipTimer = null;
	// _warnedSprites: { [id]: true } — console.warn の重複抑制
	var _warnedSprites = Object.create(null);
	// H-02: background fit mode (cover | contain | fill)。default は
	// H-02 以前の挙動を維持。per-bg-line の `fit` がこれを上書き。
	var _bgFit = 'cover';
	// Bonus: input lockout。_inputLockout は設定済みの window 幅を保持
	// (0 = 無効)。_inputLockUntil は入力が再び応答する絶対 timestamp。
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
			// IE/Node 用 shim 経路
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

	// H-02: 許可する background-size 値。
	function _isValidFit(value)
	{
		return value === 'cover' || value === 'contain' || value === 'fill';
	}

	// H-05: textbox の default は mitiru_components.css (section 19) に定義する。
	// こうすれば mitiru_components.css の後に読み込まれた consumer stylesheet が
	// 通常の cascade 規則で上書きできる。以前はこの module が mount 時に
	// <style> block を注入していたが、consumer の <link> 群の後に追加されるため
	// consumer 側に `!important` を強いていた。下の shim は後方互換のための
	// no-op として残す。将来の major version で削除する。
	function _ensureStyleBlock() { /* mitiru_components.css §19 へ移動 */ }

	function _createDiv(cls)
	{
		var el = global.document.createElement('div');
		if (cls) { el.className = cls; }
		return el;
	}

	// ── NF-10: 既読集合の永続化 helper ───────────────────────

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
		// choice line は決して skip しない。
		if (line.type === 'choice') { return; }
		var isText = _isTextLine(line);
		if (_skipMode === 'all')
		{
			// choice 以外は全て auto-advance する。
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
			// NF-10: typewriter 完了後に skip-mode tick を予約。
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
		// H-02: per-line fit が mount-default を上書きする。default は
		// 後方互換の 'cover' 挙動を維持。'fill' は CSS の
		// `background-size: 100% 100%` stretch に対応する novel レベルの alias。
		var resolvedFit = fit || _bgFit;
		if (!_isValidFit(resolvedFit))
		{
			throw new Error('novel: invalid bg fit "' + resolvedFit
				+ '" (expected cover | contain | fill)');
		}
		var cssSize = (resolvedFit === 'fill') ? '100% 100%' : resolvedFit;
		// 上に新 layer を作り fade in、その後で古い layer を削除。
		var doc    = global.document;
		var newImg = doc.createElement('div');
		newImg.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;'
		                     + 'background-size:' + cssSize + ';background-position:center;'
		                     + 'background-repeat:no-repeat;'
		                     + 'background-image:url("' + path + '");'
		                     + 'opacity:0;transition:opacity 0.4s ease;';
		_bgLayer.appendChild(newImg);
		// reflow を強制してから fade in。
		void newImg.offsetWidth;
		newImg.style.opacity = '1';

		// transition 後に sibling を全削除。
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
		// 位置: left/center/right またはカスタムの percent 文字列。
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
		// この script に該当 label の line があればそこへ jump。
		var target = opt.next;
		if (target && _script)
		{
			for (var i = 0; i < _script.lines.length; ++i)
			{
				if (_script.lines[i].label === target)
				{
					_pc = i - 1;   // _advance() で increment される
					_advance();
					return;
				}
			}
		}
		// 一致する label 無し — script 終了として扱う。
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
		// clear して再構築。
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
			// click で jumpTo — pc を巻き戻して最初から re-render。
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

	// ── NF-11: effect helper ──────────────────────────────────────

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

	// ── line 実行 ─────────────────────────────────────────────

	var _EFFECT_TYPES = { shake: 1, flash: 1, tint: 1, zoom: 1, blur: 1, slide: 1, 'fade-sprite': 1 };

	function _execLine(line)
	{
		var type = line.type;

		if (type === 'text' || type === 'dialogue' || !type)
		{
			// 通常の dialogue / narration line。
			var speaker = line.speaker || '';
			var text    = line.text    || '';

			// NF-10: この line を既読としてマーク。
			if (_script)
			{
				var sid = _script.id || '';
				_loadReadSet(sid);
				_readSets[sid].add(_pc);
				_saveReadSet(sid);
			}

			// speaker 表示を更新。
			if (_textboxEl)
			{
				var speakerEl = _textboxEl.querySelector('[data-novel-speaker]');
				if (speakerEl) { speakerEl.textContent = speaker; }
			}
			// Typewriter。
			_twStart(text);
			// Log。
			_logLine(speaker, text);
			// Voice。
			if (line.voice && typeof Audio !== 'undefined')
			{
				try { new Audio(line.voice).play(); } catch (_e) { /* 致命的でない */ }
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
			// 実行を一時停止; _commitChoice で再開する。
		}
		else if (type === 'wait')
		{
			var ms = typeof line.ms === 'number' ? line.ms : 0;
			setTimeout(_advance, ms);
		}
		else if (_EFFECT_TYPES[type])
		{
			// NF-11: effect primitive — durationMs 後に auto-advance。
			_execEffect(line);
		}
		else
		{
			// 未知の type — skip。
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
			// text/dialogue line には .text が必要
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
	 * containerEl 内に layer stack を構築する。opts.textBox が渡されれば
	 * その element を novel root に取り込む (移動する)。opts.toolbar が
	 * 渡された場合も同様に取り込む。
	 *
	 * opts: {
	 *   textBox     : HTMLElement  (既存 element — 取り込まれる)
	 *   toolbar     : HTMLElement  (optional — 取り込まれる)
	 *   cps         : number       (typewriter chars/sec、default 40)
	 *   autoAdvance : boolean      (default false)
	 * }
	 */
	novel.mount = function(containerEl, opts)
	{
		if (!containerEl) { throw new Error('novel.mount: containerEl required'); }
		_containerEl = containerEl;
		_opts        = opts || {};
		_playing     = !!_opts.autoAdvance;

		// H-02: bgFit option を受理 (default 'cover' で従来挙動を維持)。
		if (typeof _opts.bgFit !== 'undefined')
		{
			if (!_isValidFit(_opts.bgFit))
			{
				throw new Error('novel.mount: invalid bgFit "' + _opts.bgFit
					+ '" (expected cover | contain | fill)');
			}
			_bgFit = _opts.bgFit;
		}

		// H-05: 外部 stylesheet から CSS default に到達できるようにする。
		_ensureStyleBlock();

		var doc = global.document;

		// Novel root — container を埋める。
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

		// Layer: text-box — 既存 element を取り込むか新規作成。
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
			// H-05: position、z-index、視覚的 default は全て
			// mitiru_components.css §19 (.novel-textbox) にある。
			// mitiru_components.css の後に読み込まれた外部 CSS は素の
			// selector で上書きできる — `!important` は不要。
		}
		root.appendChild(_textboxEl);

		// Layer: backlog overlay (非表示)。
		_backlogEl = _createDiv('novel-backlog');
		_backlogEl.setAttribute('data-novel-backlog', '');
		_backlogEl.style.cssText =
			'position:absolute;top:0;left:0;width:100%;height:100%;'
			+ 'background:rgba(0,0,0,0.85);color:#fff;overflow-y:auto;display:none;'
			+ 'z-index:' + Z_BACKLOG + ';';
		root.appendChild(_backlogEl);

		// Optional toolbar — 取り込む。
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
	 * script を読み込む。plain object か URL string を受理する。
	 * script 準備完了で resolve する Promise を返す。
	 * 再生状態は reset するが auto-advance はしない。
	 */
	novel.load = function(scriptOrUrl)
	{
		_twStop();
		_pc      = -1;
		_log     = [];
		_playing = !!(_opts && _opts.autoAdvance);

		// H-01: UI を clear し、新 script の最初の line が render される前に
		// 前 script の最終 textbox 内容・background・sprite が一瞬出ないように
		// する。mount 前の呼び出しに備えてガード済み。
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

		// Bonus: scene-transition の input lockout を armed にし、前 scene の
		// click が新規読み込み script に漏れないようにする。
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

		// Inline object。
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
	 * 次の line へ進む。
	 * - typewriter がまだ動作中なら、まず末尾まで skip する (double-tap)。
	 * - typewriter が完了済みなら、次の line へ進む。
	 */
	novel.advance = function()
	{
		// Bonus: advance() の度に per-line input lockout を更新し、
		// リズミカルな click で line を連鎖 skip できないようにする。game は
		// 自身の click handler で novel.inputLocked() から状態を読む。
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
	 * log 済みの dialogue entry へ巻き戻す (backlog click)。
	 * pc=logIndex から re-render する; その地点以前の sprite 位置は
	 * 復元されない (Phase 2 deferral — module header に記載)。
	 */
	novel.jumpTo = function(logIndex)
	{
		if (!_script) { return; }
		// logged 位置が logIndex に一致する script line を探す。
		// log index への対応付けでは text/dialogue line のみを数える。
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
					_pc = i - 1;   // _advance() で increment される
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
	 * runtime state を key 'novel:save:<slotId>' で mitiru.state に永続化する。
	 * Promise を返す。mitiru.state が使えない場合は reject する。
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
	 * 以前 save した状態を mitiru.state から復元する。
	 * Promise を返す。save data が無ければ reject する。
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
			// typewriter 無しで現在の line を再表示。
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

	/** pc() — 現在の program counter を読む (0 始まりの line index、-1 = 開始前)。 */
	novel.pc = function() { return _pc; };

	/** script() — 読み込み済み script object の shallow copy を返す、無ければ null。 */
	novel.script = function() { return _script ? _deepClone(_script) : null; };

	/** log() — backlog entry の snapshot。 */
	novel.log = function() { return _log.slice(); };

	/**
	 * Bonus (hato H-*): click 多用の caller 向けに input-lockout window を設定。
	 * 両 window は milliseconds 単位で default は 0 (無効)。
	 *   - sceneTransitionMs: novel.load() で armed — 前 scene の click が
	 *     新規読み込み script に漏れるのを防ぐ。
	 *   - perLineMs: novel.advance() ごとに armed — リズミカルな double-click で
	 *     line を連鎖 skip するのを防ぐ。
	 * game は自身の click handler から novel.inputLocked() を参照する。
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

	/** destroy() — DOM と内部状態を全て破棄する。 */
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
	 * scriptId の lineIdx にある text/dialogue line が既読なら true を返す。
	 * @param {string} scriptId
	 * @param {number} lineIdx — script.lines の array index (log index ではない)
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
	 * text/dialogue line を既読としてマーク。可能なら mitiru.state で永続化。
	 * text 以外の line (bg/sprite/wait/choice) は黙って無視する。
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
	 * script の既読統計を返す。
	 * total は text/dialogue line のみ数える; read は既読集合内の数。
	 * @param {string} scriptId
	 * @returns {{ read: number, total: number, fraction: number }}
	 */
	novel.chapterProgress = function(scriptId)
	{
		var id  = scriptId || '';
		_loadReadSet(id);
		var set = _readSets[id] || new Set();

		// 現在の script が一致するなら、そこから数える。
		var total = 0;
		if (_script && (_script.id || '') === id)
		{
			for (var i = 0; i < _script.lines.length; ++i)
			{
				if (_isTextLine(_script.lines[i])) { total++; }
			}
		}

		// read = 集合と有効な text index との積集合。
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
	 * auto-advance の skip mode を設定する。
	 * @param {'off'|'all'|'read-only'} mode
	 */
	novel.SkipMode = Object.freeze({
		OFF:       'off',
		ALL:       'all',
		READ_ONLY: 'read-only'
	});

	novel.setSkipMode = function(mode)
	{
		// H-03: 'none' を 'off' の使いやすい alias として受理 — caller が
		// よく使う割に、黙って reject されると原因究明が難しいため。
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
	 * 現在の skip mode を返す。
	 * @returns {'off'|'all'|'read-only'}
	 */
	novel.skipMode = function() { return _skipMode; };

	/**
	 * totalReadLines()
	 *
	 * 追跡中の全 script id を跨いだ既読 line の総数を返す。
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
	 * 1 つの script id (指定時) または全 script の既読履歴を消去する。
	 * 永続化された state key も削除する。
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
	// unit test 用に公開。実時計なしで VM を駆動できるようにするため。
	novel._forceAdvance  = _advance;
	novel._twShowFull    = _twShowFull;
	novel._getLog        = function() { return _log; };
	novel._getPc         = function() { return _pc; };
	novel._getReadSets   = function() { return _readSets; };
	novel._getSkipMode   = function() { return _skipMode; };

}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));
