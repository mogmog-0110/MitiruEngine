/*!
 * anchored_entity.js — MitiruEngine の anchor ベース entity 配置 (Mode B)
 *
 * sprite bbox と hitbox bbox はどちらも単一の anchor 点から導出される。
 * anchor を動かすと両方が同時に動く; 個別の offset は struct 自身に
 * 明示的に記述・ドキュメント化される。
 *
 * ── Shape ───────────────────────────────────────────────────────────────────
 *   anchor        {x, y}           World 座標の位置 (single source of truth)
 *   spriteOffset  {x, y, w, h}     anchor からの相対 (負も可)
 *   hitboxOffset  {x, y, w, h}     anchor からの相対 (負も可)
 *   name          string           debug / inspector ラベル
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   getSpriteWorldRect()            → {x, y, w, h}  常に anchor から導出
 *   getHitboxWorldRect()            → {x, y, w, h}  常に anchor から導出
 *   setAnchor(x, y)                 anchor を移動; 両 rect が追従する
 *   getAnchorWorld()                → {x, y}
 *   toJSON()                        → plain object (serialise 可能)
 *   static fromJSON(obj)            → AnchoredEntity
 *
 * ── Inspector ───────────────────────────────────────────────────────────────
 *   toInspectorEntry(anchored)      → {name, spriteRect, hitboxRect, anchor}
 */

/**
 * anchor ベースの entity。全ての位置データは `anchor` を経由する; offset は
 * その単一原点に対する visual / collision shape の immutable な記述子である。
 */
export class AnchoredEntity {
	/**
	 * @param {Object} opts
	 * @param {{x: number, y: number}} opts.anchor
	 * @param {{x: number, y: number, w: number, h: number}} opts.spriteOffset
	 * @param {{x: number, y: number, w: number, h: number}} opts.hitboxOffset
	 * @param {string} [opts.name]
	 */
	constructor({ anchor, spriteOffset, hitboxOffset, name = '' }) {
		if (!isFiniteRect(spriteOffset)) {
			throw new RangeError(`AnchoredEntity "${name}": spriteOffset contains non-finite value`);
		}
		if (!isFiniteRect(hitboxOffset)) {
			throw new RangeError(`AnchoredEntity "${name}": hitboxOffset contains non-finite value`);
		}
		if (!isFiniteVec2(anchor)) {
			throw new RangeError(`AnchoredEntity "${name}": anchor contains non-finite value`);
		}

		/** @type {{x: number, y: number}} */
		this._anchor = { x: anchor.x, y: anchor.y };

		/** @type {{x: number, y: number, w: number, h: number}} */
		this.spriteOffset = Object.freeze({ ...spriteOffset });

		/** @type {{x: number, y: number, w: number, h: number}} */
		this.hitboxOffset = Object.freeze({ ...hitboxOffset });

		/** @type {string} */
		this.name = name;
	}

	/**
	 * anchor から導出した sprite の world 座標 bounding rect を返す。
	 * @returns {{x: number, y: number, w: number, h: number}}
	 */
	getSpriteWorldRect() {
		return {
			x: this._anchor.x + this.spriteOffset.x,
			y: this._anchor.y + this.spriteOffset.y,
			w: this.spriteOffset.w,
			h: this.spriteOffset.h,
		};
	}

	/**
	 * anchor から導出した hitbox の world 座標 bounding rect を返す。
	 * @returns {{x: number, y: number, w: number, h: number}}
	 */
	getHitboxWorldRect() {
		return {
			x: this._anchor.x + this.hitboxOffset.x,
			y: this._anchor.y + this.hitboxOffset.y,
			w: this.hitboxOffset.w,
			h: this.hitboxOffset.h,
		};
	}

	/**
	 * anchor を移動する。sprite と hitbox の world rect は自動で追従する。
	 * @param {number} x
	 * @param {number} y
	 */
	setAnchor(x, y) {
		if (!Number.isFinite(x) || !Number.isFinite(y)) {
			throw new RangeError(`AnchoredEntity "${this.name}": setAnchor received non-finite value`);
		}
		this._anchor = { x, y };
	}

	/**
	 * 現在の world 座標 anchor 位置を返す。
	 * @returns {{x: number, y: number}}
	 */
	getAnchorWorld() {
		return { x: this._anchor.x, y: this._anchor.y };
	}

	/**
	 * JSON.stringify に適した plain object に serialise する。
	 * @returns {Object}
	 */
	toJSON() {
		return {
			name: this.name,
			anchor: { ...this._anchor },
			spriteOffset: { ...this.spriteOffset },
			hitboxOffset: { ...this.hitboxOffset },
		};
	}

	/**
	 * toJSON() が生成した plain object から deserialise する。
	 * @param {Object} obj
	 * @returns {AnchoredEntity}
	 */
	static fromJSON(obj) {
		return new AnchoredEntity({
			anchor: obj.anchor,
			spriteOffset: obj.spriteOffset,
			hitboxOffset: obj.hitboxOffset,
			name: obj.name ?? '',
		});
	}
}

// ── Inspector adapter ────────────────────────────────────────────────────────

/**
 * AnchoredEntity を inspector overlay が期待する shape に変換する。
 *
 * Shape: { name: string, spriteRect: {x,y,w,h}, hitboxRect: {x,y,w,h}, anchor: {x,y} }
 *
 * inspector_overlay.js が未ロードなら、戻り値の object を配列に溜めておき、
 * overlay がロードされ次第 registerEntities() に渡す。
 *
 * @param {AnchoredEntity} anchored
 * @returns {{ name: string, spriteRect: {x:number,y:number,w:number,h:number}, hitboxRect: {x:number,y:number,w:number,h:number}, anchor: {x:number,y:number} }}
 */
export function toInspectorEntry(anchored) {
	return {
		name: anchored.name,
		spriteRect: anchored.getSpriteWorldRect(),
		hitboxRect: anchored.getHitboxWorldRect(),
		anchor: anchored.getAnchorWorld(),
	};
}

// ── 内部ヘルパー ─────────────────────────────────────────────────────────

/** @param {{x:number,y:number,w:number,h:number}} r */
function isFiniteRect(r) {
	return (
		r != null &&
		Number.isFinite(r.x) &&
		Number.isFinite(r.y) &&
		Number.isFinite(r.w) &&
		Number.isFinite(r.h)
	);
}

/** @param {{x:number,y:number}} v */
function isFiniteVec2(v) {
	return v != null && Number.isFinite(v.x) && Number.isFinite(v.y);
}
