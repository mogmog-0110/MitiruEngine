#pragma once

/*!
 * @file AnchoredEntity.hpp
 * @brief anchor を基準にした entity の位置決め — sprite と hitbox が 1 つの原点を共有する。
 *
 * `getSpriteWorldRect()` と `getHitboxWorldRect()` はどちらも `anchor` と
 * それぞれの offset から導出される。`setAnchor()` で anchor を動かすと両 rect が
 * 連動して動く。独立した world rect を別に持つことはしない。
 *
 * 使い方 (Mode A / C++ gameplay):
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
 * @brief sprite bbox と hitbox bbox を 1 つの world 空間 anchor 点に
 *        束ねる位置 descriptor。
 *
 * **不変条件**: `anchor`、`spriteOffset`、`hitboxOffset` は常に有限値
 * (non-NaN, non-Inf) を持たねばならない。assertion helper が構築時と変更時に
 * これを強制する。
 */
struct AnchoredEntity
{
	/// world 空間の anchor 位置 — 位置の単一の source of truth。
	sgc::Vec2f anchor{0.f, 0.f};

	/// 可視 sprite の rect。`anchor` に対する*相対*値。
	/// x/y は負になりうる (例: 中心揃え sprite)。
	sgc::Rectf spriteOffset{0.f, 0.f, 0.f, 0.f};

	/// 当たり判定 hitbox の rect。`anchor` に対する*相対*値。
	/// 通常は spriteOffset を少し内側に inset したもの。
	sgc::Rectf hitboxOffset{0.f, 0.f, 0.f, 0.f};

	/// inspector overlay と debug log で使う人間可読の label。
	std::string name;

	// ── 導出 accessor ─────────────────────────────────────────────────

	/*!
	 * @brief world 空間の sprite bounding rect を返す。
	 *
	 * 常に `anchor + spriteOffset` から計算する。別に保持した rect は無い。
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
	 * @brief world 空間の hitbox bounding rect を返す。
	 *
	 * 常に `anchor + hitboxOffset` から計算する。別に保持した rect は無い。
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
	 * @brief anchor を新しい world 空間位置に設定する。
	 *
	 * 呼び出し直後に `getSpriteWorldRect()` と `getHitboxWorldRect()` の両方が
	 * 新しい位置を反映する。
	 *
	 * @param pos  新しい world 空間 anchor。有限値を持たねばならない。
	 */
	void setAnchor(const sgc::Vec2f& pos) noexcept
	{
		assert(isFiniteVec2(pos) && "AnchoredEntity::setAnchor: pos must be finite");
		anchor = pos;
	}

	/*!
	 * @brief 現在の world 空間 anchor 位置を返す。
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
