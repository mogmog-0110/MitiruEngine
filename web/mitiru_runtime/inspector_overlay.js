/*!
 * inspector_overlay.js — Visual debug inspector for entity positions, sprite
 * bounds, hitbox bounds, and anchor points.
 *
 * Solves the "AI can't see the screen" problem by rendering overlay data onto
 * a Canvas2D context so the result can be captured as a PNG and inspected.
 *
 * API:
 *   const overlay = new InspectorOverlay();
 *   overlay.register(entityId, getter)   — getter returns EntityDebugInfo
 *   overlay.unregister(entityId)
 *   overlay.setEnabled(bool)
 *   overlay.render(ctx2d)               — call each frame after game rendering
 *   overlay.bindKeyToggle(window, 'F3') — optional keyboard toggle
 *
 * EntityDebugInfo shape:
 *   {
 *     name:       string,
 *     spriteRect: { x, y, w, h },   // canvas-space pixel rect
 *     hitboxRect: { x, y, w, h },   // canvas-space pixel rect
 *     anchor:     { x, y },          // canvas-space pixel point
 *   }
 */
(function (global) {
  'use strict';

  // ── colour constants ────────────────────────────────────────────────────────
  const SPRITE_STROKE  = 'rgba(0,128,255,1)';
  const HITBOX_STROKE  = 'rgba(255,0,0,1)';
  const HITBOX_FILL    = 'rgba(255,0,0,0.15)';
  const ANCHOR_FILL    = 'rgba(255,0,255,1)';
  const LABEL_TEXT     = 'rgba(255,255,255,1)';
  const LABEL_SHADOW   = 'rgba(0,0,0,1)';

  // ── helpers ─────────────────────────────────────────────────────────────────

  /** Draw a 1 px stroked rectangle. */
  function strokeRect(ctx, rect, color) {
    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w, rect.h);
    ctx.restore();
  }

  /** Draw a filled + stroked rectangle. */
  function fillStrokeRect(ctx, rect, fillColor, strokeColor) {
    ctx.save();
    ctx.fillStyle = fillColor;
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1;
    ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w, rect.h);
    ctx.restore();
  }

  /** Draw a 4×4 filled square centred on (px, py). */
  function drawAnchorSquare(ctx, px, py, color) {
    ctx.save();
    ctx.fillStyle = color;
    ctx.fillRect(Math.round(px) - 2, Math.round(py) - 2, 4, 4);
    ctx.restore();
  }

  /**
   * Draw text with a 1 px black outline for contrast.
   * Position is clamped so the label stays within the canvas bounds.
   */
  function drawLabel(ctx, text, px, py, canvasWidth, canvasHeight) {
    ctx.save();
    ctx.font = '11px monospace';
    ctx.textBaseline = 'bottom';

    const metrics = ctx.measureText(text);
    const textW = metrics.width;
    const textH = 12; // approximate ascent for 11px mono

    // clamp so both the label and its 1 px outline stay inside the canvas
    const clampedX = Math.max(2, Math.min(px, canvasWidth  - textW - 2));
    const clampedY = Math.max(textH + 2, Math.min(py, canvasHeight - 2));

    // 1 px black outline (8-direction)
    ctx.fillStyle = LABEL_SHADOW;
    for (let dx = -1; dx <= 1; dx++) {
      for (let dy = -1; dy <= 1; dy++) {
        if (dx !== 0 || dy !== 0) {
          ctx.fillText(text, clampedX + dx, clampedY + dy);
        }
      }
    }

    ctx.fillStyle = LABEL_TEXT;
    ctx.fillText(text, clampedX, clampedY);
    ctx.restore();
  }

  // ── InspectorOverlay class ──────────────────────────────────────────────────

  /**
   * @class InspectorOverlay
   * Manages a registry of entity debug getters and renders their spatial info
   * to a CanvasRenderingContext2D.
   */
  function InspectorOverlay() {
    this._enabled  = false;
    this._entities = Object.create(null); // entityId → getter fn
  }

  /**
   * Register an entity getter.
   * @param {string}   entityId  Unique identifier for the entity.
   * @param {Function} getter    Returns EntityDebugInfo each frame.
   */
  InspectorOverlay.prototype.register = function (entityId, getter) {
    if (typeof entityId !== 'string' || entityId.length === 0) {
      throw new TypeError('InspectorOverlay.register: entityId must be a non-empty string');
    }
    if (typeof getter !== 'function') {
      throw new TypeError('InspectorOverlay.register: getter must be a function');
    }
    this._entities[entityId] = getter;
  };

  /**
   * Unregister an entity getter.
   * @param {string} entityId
   */
  InspectorOverlay.prototype.unregister = function (entityId) {
    delete this._entities[entityId];
  };

  /**
   * Enable or disable the overlay.
   * @param {boolean} enabled
   */
  InspectorOverlay.prototype.setEnabled = function (enabled) {
    this._enabled = Boolean(enabled);
  };

  /** @returns {boolean} */
  InspectorOverlay.prototype.isEnabled = function () {
    return this._enabled;
  };

  /**
   * Render all registered entities onto the provided 2D canvas context.
   * No-ops when disabled.
   * @param {CanvasRenderingContext2D} ctx
   */
  InspectorOverlay.prototype.render = function (ctx) {
    if (!this._enabled) { return; }

    const canvasWidth  = ctx.canvas ? ctx.canvas.width  : 9999;
    const canvasHeight = ctx.canvas ? ctx.canvas.height : 9999;

    const ids = Object.keys(this._entities);
    for (let i = 0; i < ids.length; i++) {
      const id     = ids[i];
      const getter = this._entities[id];

      let info;
      try {
        info = getter();
      } catch (e) {
        continue; // skip broken getters silently
      }

      if (!info) { continue; }

      const { name, spriteRect, hitboxRect, anchor } = info;

      // sprite bounds — blue outline
      if (spriteRect) {
        strokeRect(ctx, spriteRect, SPRITE_STROKE);
      }

      // hitbox bounds — red fill + outline
      if (hitboxRect) {
        fillStrokeRect(ctx, hitboxRect, HITBOX_FILL, HITBOX_STROKE);
      }

      // anchor point — 4×4 magenta square
      if (anchor) {
        drawAnchorSquare(ctx, anchor.x, anchor.y, ANCHOR_FILL);
      }

      // label "name @ (x,y)" above sprite bbox
      if (name && spriteRect) {
        const label  = name + ' @ (' + Math.round(spriteRect.x) + ',' + Math.round(spriteRect.y) + ')';
        const labelX = spriteRect.x;
        const labelY = spriteRect.y - 1; // 1 px above top edge
        drawLabel(ctx, label, labelX, labelY, canvasWidth, canvasHeight);
      }
    }
  };

  /**
   * Bind an F3 (or any key) toggle to window.
   * @param {Window}  win       The window object to attach to.
   * @param {string}  key       Key to listen for (e.g. 'F3').
   */
  InspectorOverlay.prototype.bindKeyToggle = function (win, key) {
    const self = this;
    win.addEventListener('keydown', function (e) {
      if (e.key === key) {
        self.setEnabled(!self._enabled);
      }
    });
  };

  // ── export ──────────────────────────────────────────────────────────────────

  const mitiru = global.mitiru = global.mitiru || {};
  mitiru.InspectorOverlay = InspectorOverlay;

}(typeof globalThis !== 'undefined' ? globalThis : this));
