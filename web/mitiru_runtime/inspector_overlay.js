/*!
 * inspector_overlay.js — entity 位置・sprite 境界・hitbox 境界・anchor 点の
 * visual debug inspector。
 *
 * overlay データを Canvas2D context に描画して結果を PNG として capture・
 * 検査できるようにし、「AI が画面を見られない」問題を解決する。
 *
 * API:
 *   const overlay = new InspectorOverlay();
 *   overlay.register(entityId, getter)   — getter は EntityDebugInfo を返す
 *   overlay.unregister(entityId)
 *   overlay.setEnabled(bool)
 *   overlay.render(ctx2d)               — ゲーム描画後に毎 frame 呼ぶ
 *
 * toggle の入力割当はゲーム窓に持たせない — host C++ (ToolRegistry) か
 * ページ内 UI から setEnabled() を呼ぶ。
 *
 * EntityDebugInfo shape:
 *   {
 *     name:       string,
 *     spriteRect: { x, y, w, h },   // canvas 座標の pixel rect
 *     hitboxRect: { x, y, w, h },   // canvas 座標の pixel rect
 *     anchor:     { x, y },          // canvas 座標の pixel 点
 *   }
 */
(function (global) {
  'use strict';

  // ── 色定数 ────────────────────────────────────────────────────────
  const SPRITE_STROKE  = 'rgba(0,128,255,1)';
  const HITBOX_STROKE  = 'rgba(255,0,0,1)';
  const HITBOX_FILL    = 'rgba(255,0,0,0.15)';
  const ANCHOR_FILL    = 'rgba(255,0,255,1)';
  const LABEL_TEXT     = 'rgba(255,255,255,1)';
  const LABEL_SHADOW   = 'rgba(0,0,0,1)';

  // ── ヘルパー ─────────────────────────────────────────────────────────────────

  /** 1 px の枠線の矩形を描く。 */
  function strokeRect(ctx, rect, color) {
    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w, rect.h);
    ctx.restore();
  }

  /** 塗りつぶし + 枠線の矩形を描く。 */
  function fillStrokeRect(ctx, rect, fillColor, strokeColor) {
    ctx.save();
    ctx.fillStyle = fillColor;
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1;
    ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w, rect.h);
    ctx.restore();
  }

  /** (px, py) を中心とする 4×4 の塗りつぶし正方形を描く。 */
  function drawAnchorSquare(ctx, px, py, color) {
    ctx.save();
    ctx.fillStyle = color;
    ctx.fillRect(Math.round(px) - 2, Math.round(py) - 2, 4, 4);
    ctx.restore();
  }

  /**
   * コントラストのため 1 px の黒 outline 付きでテキストを描く。
   * label が canvas 境界内に収まるよう位置を clamp する。
   */
  function drawLabel(ctx, text, px, py, canvasWidth, canvasHeight) {
    ctx.save();
    ctx.font = '18px monospace'; // 18px+ ルール (可読最小サイズ)
    ctx.textBaseline = 'bottom';

    const metrics = ctx.measureText(text);
    const textW = metrics.width;
    const textH = 19; // 18px mono の ascent 近似値

    // label と 1 px outline の両方が canvas 内に収まるよう clamp する
    const clampedX = Math.max(2, Math.min(px, canvasWidth  - textW - 2));
    const clampedY = Math.max(textH + 2, Math.min(py, canvasHeight - 2));

    // 1 px の黒 outline (8 方向)
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
   * entity debug getter の registry を管理し、その空間情報を
   * CanvasRenderingContext2D に描画する。
   */
  function InspectorOverlay() {
    this._enabled  = false;
    this._entities = Object.create(null); // entityId → getter fn
  }

  /**
   * entity getter を登録する。
   * @param {string}   entityId  entity の一意な識別子。
   * @param {Function} getter    毎 frame EntityDebugInfo を返す。
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
   * entity getter を登録解除する。
   * @param {string} entityId
   */
  InspectorOverlay.prototype.unregister = function (entityId) {
    delete this._entities[entityId];
  };

  /**
   * overlay を有効/無効にする。
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
   * 登録済みの全 entity を渡された 2D canvas context に描画する。
   * 無効時は no-op。
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
        continue; // 壊れた getter は黙って skip
      }

      if (!info) { continue; }

      const { name, spriteRect, hitboxRect, anchor } = info;

      // sprite 境界 — 青い枠線
      if (spriteRect) {
        strokeRect(ctx, spriteRect, SPRITE_STROKE);
      }

      // hitbox 境界 — 赤の塗りつぶし + 枠線
      if (hitboxRect) {
        fillStrokeRect(ctx, hitboxRect, HITBOX_FILL, HITBOX_STROKE);
      }

      // anchor 点 — 4×4 のマゼンタ正方形
      if (anchor) {
        drawAnchorSquare(ctx, anchor.x, anchor.y, ANCHOR_FILL);
      }

      // sprite bbox の上に "name @ (x,y)" ラベル
      if (name && spriteRect) {
        const label  = name + ' @ (' + Math.round(spriteRect.x) + ',' + Math.round(spriteRect.y) + ')';
        const labelX = spriteRect.x;
        const labelY = spriteRect.y - 1; // 上辺の 1 px 上
        drawLabel(ctx, label, labelX, labelY, canvasWidth, canvasHeight);
      }
    }
  };

  // ── export ──────────────────────────────────────────────────────────────────

  const mitiru = global.mitiru = global.mitiru || {};
  mitiru.InspectorOverlay = InspectorOverlay;

}(typeof globalThis !== 'undefined' ? globalThis : this));
