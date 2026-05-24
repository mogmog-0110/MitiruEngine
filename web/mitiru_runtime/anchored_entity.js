/*!
 * anchored_entity.js — Anchor-based entity positioning for MitiruEngine (Mode B)
 *
 * Sprite bbox and hitbox bbox both derive from a single anchor point.
 * Moving the anchor moves both simultaneously; independent offsets are
 * explicit and documented on the struct itself.
 *
 * ── Shape ───────────────────────────────────────────────────────────────────
 *   anchor        {x, y}           World-space position (single source of truth)
 *   spriteOffset  {x, y, w, h}     Relative to anchor (may be negative)
 *   hitboxOffset  {x, y, w, h}     Relative to anchor (may be negative)
 *   name          string           Debug / inspector label
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   getSpriteWorldRect()            → {x, y, w, h}  always derived from anchor
 *   getHitboxWorldRect()            → {x, y, w, h}  always derived from anchor
 *   setAnchor(x, y)                 moves anchor; both rects follow
 *   getAnchorWorld()                → {x, y}
 *   toJSON()                        → plain object (serialisable)
 *   static fromJSON(obj)            → AnchoredEntity
 *
 * ── Inspector ───────────────────────────────────────────────────────────────
 *   toInspectorEntry(anchored)      → {name, spriteRect, hitboxRect, anchor}
 */

/**
 * Anchor-based entity.  All positional data flows through `anchor`; offsets
 * are immutable descriptors of visual and collision shape relative to that
 * single origin.
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
	 * Returns the world-space bounding rect of the sprite, derived from anchor.
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
	 * Returns the world-space bounding rect of the hitbox, derived from anchor.
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
	 * Moves the anchor. Both sprite and hitbox world rects follow automatically.
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
	 * Returns the current world-space anchor position.
	 * @returns {{x: number, y: number}}
	 */
	getAnchorWorld() {
		return { x: this._anchor.x, y: this._anchor.y };
	}

	/**
	 * Serialises to a plain object suitable for JSON.stringify.
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
	 * Deserialises from a plain object produced by toJSON().
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
 * Converts an AnchoredEntity to the shape expected by the inspector overlay.
 *
 * Shape: { name: string, spriteRect: {x,y,w,h}, hitboxRect: {x,y,w,h}, anchor: {x,y} }
 *
 * If inspector_overlay.js is not yet present, store the returned object in an
 * array and pass it to the overlay's registerEntities() once it loads.
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

// ── Internal helpers ─────────────────────────────────────────────────────────

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
