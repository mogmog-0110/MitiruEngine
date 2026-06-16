/*!
 * mitiru_drag.js — pointer-event drag engine (E-03)
 *
 * KaeruCrape `cooking_drag.js` (257 LOC) を game-agnostic にポートしたもの。
 * ADR-006 "pointer-event DnD" の rationale 通り、HTML5 native drag は
 * capturing phase で preventDefault → pointer-events ベースで完結する。
 *
 * 使い方 (games/<name>/assets/ui/index.html):
 *
 *   <script src="../mitiru_runtime/mitiru_drag.js"></script>
 *   <script>
 *     mitiru.drag.init({
 *         // デフォルトは [data-drag-id] / [data-drop-id] だけで動く
 *         onDrop: (dragId, dropId, ctx) => {
 *             // 判定ロジック。true (or {accepted:true}) を返すと snap-forward、
 *             // false / undefined を返すと snap-back + reject flash。
 *             return bridge.tryDrop(dragId, dropId);
 *         },
 *     });
 *   </script>
 *
 * HTML 側:
 *   <div data-drag-id="cherry1" class="item">🍒</div>
 *   <div data-drop-id="bucket" class="slot">bucket</div>
 *
 * init(options) の options:
 *   - sourceSelector (string, default '[data-drag-id]')
 *   - targetSelector (string, default '[data-drop-id]')
 *   - threshold (px, default 8)
 *   - ghostZIndex (int, default 500)     // --mitiru-z-drag と揃えるのを推奨
 *   - snapBackMs / snapForwardMs (int, default 180 / 120)
 *   - bodyClass (string, default 'mitiru-dragging')
 *   - rejectClass (string, default 'mitiru-drop-rejected')
 *   - onDrop (fn): (dragId, dropId, {event, source, dropTarget}) =>
 *                  boolean | {accepted: boolean} — 必須ではないが通常必要
 *
 * lifecycle:
 *   - mitiru.drag.init(opts): listeners をアタッチ (べき等)
 *   - mitiru.drag.destroy(): アタッチを剥がし、進行中の drag を cancel
 *   - mitiru.drag.isDragging(): 現在 ghost が存在するか
 *
 * ghost element の装飾は mitiru_base.css の `.mitiru-drag-ghost` を参照。
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};

	const DEFAULTS = {
		sourceSelector: '[data-drag-id]',
		targetSelector: '[data-drop-id]',
		threshold: 8,
		ghostZIndex: 500,
		snapBackMs: 180,
		snapForwardMs: 120,
		bodyClass: 'mitiru-dragging',
		rejectClass: 'mitiru-drop-rejected',
		onDrop: null,
	};

	let _attached = false;
	let _cfg = null;
	let _pending = null;     // {dragId, source, startX, startY, pointerId}
	let _drag = null;        // {dragId, source, ghost, srcRect}
	let _bound = {};

	// ── helpers ────────────────────────────────────────────────
	function isPrimary(e)
	{
		if (e.pointerType === 'mouse') { return e.button === 0; }
		return e.isPrimary !== false;
	}

	function findDragSource(target)
	{
		if (!target || !target.closest) { return null; }
		return target.closest(_cfg.sourceSelector);
	}

	function findDropTarget(x, y)
	{
		const el = document.elementFromPoint(x, y);
		if (!el || !el.closest) { return null; }
		return el.closest(_cfg.targetSelector);
	}

	function makeGhost(source)
	{
		const visualRoot = source.parentElement || source;
		const rect = visualRoot.getBoundingClientRect();
		const ghost = visualRoot.cloneNode(true);
		ghost.classList.add('mitiru-drag-ghost');

		// ネストしている drag ソースを ghost から除去 (pointer 干渉防止)
		const nested = ghost.querySelectorAll
			? ghost.querySelectorAll(_cfg.sourceSelector)
			: [];
		for (let i = 0; i < nested.length; ++i)
		{
			if (nested[i].parentNode) { nested[i].parentNode.removeChild(nested[i]); }
		}

		// ステージが CSS transform: scale(...) で拡縮されている場合、ghost を body 直下に
		// 置くと scale が外れて中身が等倍描画され、見た目が実画面より大きく/小さく出る。
		// 論理サイズ (offset*) と実サイズ (rect) の比で effective scale を求め、ghost にも
		// 同じ scale をかけて見た目を一致させる。scale 無しのゲームでは offset==rect なので
		// scale=1 となり従来と完全に同じ挙動 (後方互換)。
		const logicalW = visualRoot.offsetWidth || rect.width;
		const logicalH = visualRoot.offsetHeight || rect.height;
		const scale = (logicalW > 0) ? (rect.width / logicalW) : 1;

		// inline style で固定配置 (CSS 特異度に依存しない)
		const st = ghost.style;
		st.position = 'fixed';
		st.pointerEvents = 'none';
		st.zIndex = String(_cfg.ghostZIndex);
		st.left = '0';
		st.top = '0';
		st.margin = '0';
		st.width = logicalW + 'px';
		st.height = logicalH + 'px';
		st.transformOrigin = 'center center';
		st.transform = 'translate(-50%, -50%) scale(' + scale + ')';
		st.opacity = '0.88';
		st.filter = 'drop-shadow(0 6px 12px rgba(0, 0, 0, 0.5))';
		st.transition = '';

		document.body.appendChild(ghost);
		return { node: ghost, srcRect: rect };
	}

	function moveGhost(ghost, x, y)
	{
		ghost.style.left = x + 'px';
		ghost.style.top = y + 'px';
	}

	function removeGhost(ghost)
	{
		if (ghost && ghost.parentNode) { ghost.parentNode.removeChild(ghost); }
	}

	function animateGhostTo(ghost, tx, ty, ms)
	{
		ghost.style.transition =
			'left ' + ms + 'ms ease-out,' +
			' top ' + ms + 'ms ease-out,' +
			' opacity ' + ms + 'ms ease-out';
		ghost.style.left = tx + 'px';
		ghost.style.top = ty + 'px';
		ghost.style.opacity = '0';
		window.setTimeout(function() { removeGhost(ghost); }, ms + 40);
	}

	function flashReject(source)
	{
		if (!source || !_cfg.rejectClass) { return; }
		const visual = source.parentElement || source;
		visual.classList.add(_cfg.rejectClass);
		window.setTimeout(function() {
			visual.classList.remove(_cfg.rejectClass);
		}, 320);
	}

	// ── handlers ───────────────────────────────────────────────
	function onPointerDown(e)
	{
		if (!isPrimary(e)) { return; }
		const source = findDragSource(e.target);
		if (!source) { return; }
		// setPointerCapture は使わない (ADR-006 v1 の footgun)
		_pending = {
			dragId: source.getAttribute('data-drag-id'),
			source: source,
			startX: e.clientX,
			startY: e.clientY,
			pointerId: e.pointerId,
		};
	}

	function startDrag(e)
	{
		const made = makeGhost(_pending.source);
		_drag = {
			dragId: _pending.dragId,
			source: _pending.source,
			ghost: made.node,
			srcRect: made.srcRect,
		};
		document.body.classList.add(_cfg.bodyClass);
		moveGhost(_drag.ghost, e.clientX, e.clientY);
	}

	function onPointerMove(e)
	{
		if (_drag)
		{
			moveGhost(_drag.ghost, e.clientX, e.clientY);
			return;
		}
		if (!_pending || e.pointerId !== _pending.pointerId) { return; }
		const dx = e.clientX - _pending.startX;
		const dy = e.clientY - _pending.startY;
		const t = _cfg.threshold;
		if (dx * dx + dy * dy < t * t) { return; }
		startDrag(e);
	}

	function onPointerUp(e)
	{
		if (!_drag) { _pending = null; return; }

		const dropTarget = findDropTarget(e.clientX, e.clientY);
		const ghost = _drag.ghost;
		const dragId = _drag.dragId;
		const source = _drag.source;
		const srcRect = _drag.srcRect;

		// state は post の前にリセット (re-render での上書き防止)
		document.body.classList.remove(_cfg.bodyClass);
		_drag = null;
		_pending = null;

		if (dropTarget)
		{
			const dropId = dropTarget.getAttribute('data-drop-id');
			let verdict = null;
			if (typeof _cfg.onDrop === 'function')
			{
				try
				{
					verdict = _cfg.onDrop(dragId, dropId, {
						event: e,
						source: source,
						dropTarget: dropTarget,
					});
				}
				catch (err)
				{
					console.error('mitiru.drag.onDrop threw:', err);
				}
			}
			const accepted = verdict === true
			              || (verdict && typeof verdict === 'object' && verdict.accepted === true);
			if (accepted)
			{
				const r = dropTarget.getBoundingClientRect();
				animateGhostTo(ghost,
					r.left + r.width / 2, r.top + r.height / 2,
					_cfg.snapForwardMs);
			}
			else
			{
				animateGhostTo(ghost,
					srcRect.left + srcRect.width / 2,
					srcRect.top + srcRect.height / 2,
					_cfg.snapBackMs);
				flashReject(source);
			}
		}
		else
		{
			animateGhostTo(ghost,
				srcRect.left + srcRect.width / 2,
				srcRect.top + srcRect.height / 2,
				_cfg.snapBackMs);
			flashReject(source);
		}
	}

	function onPointerCancel(/*e*/)
	{
		if (_drag && _drag.ghost) { removeGhost(_drag.ghost); }
		if (_cfg && _cfg.bodyClass) { document.body.classList.remove(_cfg.bodyClass); }
		_pending = null;
		_drag = null;
	}

	function onDragStart(e)
	{
		// drag engine が attach されている間は native HTML5 drag を常に kill
		e.preventDefault();
	}

	function onSelectStart(e)
	{
		if (_drag || _pending) { e.preventDefault(); }
	}

	// ── public API ────────────────────────────────────────────
	mitiru.drag = {
		init: function(options)
		{
			if (_attached) { return; }
			_cfg = Object.assign({}, DEFAULTS, options || {});
			_bound = {
				down: onPointerDown,
				move: onPointerMove,
				up: onPointerUp,
				cancel: onPointerCancel,
				dragstart: onDragStart,
				selectstart: onSelectStart,
			};
			document.addEventListener('pointerdown', _bound.down, true);
			document.addEventListener('pointermove', _bound.move, true);
			document.addEventListener('pointerup', _bound.up, true);
			document.addEventListener('pointercancel', _bound.cancel, true);
			document.addEventListener('dragstart', _bound.dragstart, true);
			document.addEventListener('selectstart', _bound.selectstart, true);
			_attached = true;
		},

		destroy: function()
		{
			if (!_attached) { return; }
			document.removeEventListener('pointerdown', _bound.down, true);
			document.removeEventListener('pointermove', _bound.move, true);
			document.removeEventListener('pointerup', _bound.up, true);
			document.removeEventListener('pointercancel', _bound.cancel, true);
			document.removeEventListener('dragstart', _bound.dragstart, true);
			document.removeEventListener('selectstart', _bound.selectstart, true);
			if (_drag && _drag.ghost) { removeGhost(_drag.ghost); }
			if (_cfg && _cfg.bodyClass) { document.body.classList.remove(_cfg.bodyClass); }
			_bound = {};
			_pending = null;
			_drag = null;
			_cfg = null;
			_attached = false;
		},

		isDragging: function() { return _drag !== null; },

		/// @brief 進行中の drag を cancel (テスト / プログラム的キャンセル用)
		cancel: function() { onPointerCancel(); },
	};
})(typeof window !== 'undefined' ? window : globalThis);
