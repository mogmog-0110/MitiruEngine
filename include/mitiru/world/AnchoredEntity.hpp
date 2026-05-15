#pragma once

/*!
 * @file AnchoredEntity.hpp
 * @brief Anchor-based entity positioning — sprite and hitbox share one origin.
 *
 * Both `getSpriteWorldRect()` and `getHitboxWorldRect()` are derived from
 * `anchor` plus their respective offset.  Moving the anchor via `setAnchor()`
 * moves both rects in lock-step; there is no separate stored world rect.
 *
 * Usage (Mode A / C++ gameplay):
 * @code
 *   mitiru::world::AnchoredEntity crepe{
 *       .anchor       = {100.f, 200.f},
 *       .spriteOffset = {0.f, 0.f, 64.f, 64.f},
 *       .hitboxOffset = {16.f, 16.f, 32.f, 32.f},
 *       .name         = "crepe",
 *   };
 *   auto sprite = crepe.getSpriteWorldRect();  // {100, 200, 64, 64}
 *   auto hitbox = crepe.getHitboxWorldRect();  // {116, 216, 32, 32}
 *   crepe.setAnchor({150.f, 200.f});           // both rects shift by +50 x
 * @endcode
 */

#include <cassert>
#include <cmath>
#include <string>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru::world {

/*!
 * @struct AnchoredEntity
 * @brief Positional descriptor that binds a sprite bbox and hitbox bbox to a
 *        single world-space anchor point.
 *
 * **Invariant**: `anchor`, `spriteOffset`, and `hitboxOffset` must always
 * contain finite (non-NaN, non-Inf) values.  The assertion helpers enforce
 * this on construction and mutation.
 */
struct AnchoredEntity
{
	/// World-space anchor position — the single source of truth for location.
	sgc::Vec2f anchor{0.f, 0.f};

	/// Rect of the visible sprite, *relative* to `anchor`.
	/// x/y may be negative (e.g. centred sprites).
	sgc::Rectf spriteOffset{0.f, 0.f, 0.f, 0.f};

	/// Rect of the collision hitbox, *relative* to `anchor`.
	/// Typically a smaller inset of spriteOffset.
	sgc::Rectf hitboxOffset{0.f, 0.f, 0.f, 0.f};

	/// Human-readable label used by the inspector overlay and debug logs.
	std::string name;

	// ── Derived accessors ─────────────────────────────────────────────────

	/*!
	 * @brief Returns the world-space sprite bounding rect.
	 *
	 * Always computed from `anchor + spriteOffset`.  No separate stored rect.
	 */
	[[nodiscard]] sgc::Rectf getSpriteWorldRect() const noexcept
	{
		return sgc::Rectf{
			anchor.x + spriteOffset.position.x,
			anchor.y + spriteOffset.position.y,
			spriteOffset.size.x,
			spriteOffset.size.y,
		};
	}

	/*!
	 * @brief Returns the world-space hitbox bounding rect.
	 *
	 * Always computed from `anchor + hitboxOffset`.  No separate stored rect.
	 */
	[[nodiscard]] sgc::Rectf getHitboxWorldRect() const noexcept
	{
		return sgc::Rectf{
			anchor.x + hitboxOffset.position.x,
			anchor.y + hitboxOffset.position.y,
			hitboxOffset.size.x,
			hitboxOffset.size.y,
		};
	}

	/*!
	 * @brief Sets the anchor to a new world-space position.
	 *
	 * Both `getSpriteWorldRect()` and `getHitboxWorldRect()` will reflect the
	 * new position immediately after this call.
	 *
	 * @param pos  New world-space anchor.  Must contain finite values.
	 */
	void setAnchor(const sgc::Vec2f& pos) noexcept
	{
		assert(isFiniteVec2(pos) && "AnchoredEntity::setAnchor: pos must be finite");
		anchor = pos;
	}

	/*!
	 * @brief Returns the current world-space anchor position.
	 */
	[[nodiscard]] sgc::Vec2f getAnchorWorld() const noexcept
	{
		return anchor;
	}

private:
	[[nodiscard]] static bool isFiniteVec2(const sgc::Vec2f& v) noexcept
	{
		return std::isfinite(v.x) && std::isfinite(v.y);
	}
};

} // namespace mitiru::world
