#pragma once

/// @file CollisionUtils.hpp
/// @brief 衝突法線の分析と接地状態トラッカー
///
/// 物理エンジンの衝突法線を分析して、接地判定・壁判定・天井判定を行う
/// フレームワーク非依存のユーティリティ群。
///
/// @code
/// using namespace sgc::physics;
///
/// // 法線分析
/// Vec2f normal{0.0f, 0.8f};
/// if (isGroundContact(normal)) { /* 接地 */ }
///
/// // 接地トラッカー
/// GroundTracker tracker;
/// // 物理ステップ毎:
/// tracker.reset();
/// tracker.onContact(collisionNormal);
/// if (tracker.isOnGround()) { allowJump(); }
/// @endcode

#include "sgc/math/Vec2.hpp"

namespace sgc::physics
{

/// @brief constexpr対応の絶対値
/// @note std::absはC++23までconstexprではない（MSVC等で非constexpr）
template <typename T>
[[nodiscard]] constexpr T absVal(T x) noexcept
{
	return x < T{0} ? -x : x;
}

/// @brief 衝突法線が接地（下方向の接触面）を示すか判定する
///
/// 法線のY成分の絶対値が閾値を超えるかで判定する。
/// 画面座標系（Y軸下向き）を前提とし、法線の方向（A→BかB→Aか）に
/// 依存しない安全な判定を行う。
///
/// @param normal 衝突法線ベクトル
/// @param threshold Y成分の閾値（デフォルト: 0.5）
/// @return 接地判定の法線ならtrue
[[nodiscard]] constexpr bool isGroundContact(
	const Vec2f& normal, float threshold = 0.5f) noexcept
{
	return absVal(normal.y) > threshold;
}

/// @brief 衝突法線が壁接触を示すか判定する
///
/// 法線のX成分の絶対値が閾値を超えるかで判定する。
///
/// @param normal 衝突法線ベクトル
/// @param threshold X成分の閾値（デフォルト: 0.5）
/// @return 壁接触の法線ならtrue
[[nodiscard]] constexpr bool isWallContact(
	const Vec2f& normal, float threshold = 0.5f) noexcept
{
	return absVal(normal.x) > threshold;
}

/// @brief 接地状態を物理ステップ単位で追跡する
///
/// 物理シミュレーションの各ステップで以下の手順で使用する:
/// 1. reset() で前フレームの状態をクリア
/// 2. 衝突コールバック内で onContact() を呼ぶ
/// 3. isOnGround() で現在の接地状態を取得
///
/// @code
/// sgc::physics::GroundTracker groundTracker;
///
/// // 物理ステップのコールバック内:
/// groundTracker.reset();
/// for (auto& contact : contacts) {
///     groundTracker.onContact({contact.normal.x, contact.normal.y});
/// }
///
/// if (groundTracker.isOnGround() && jumpPressed) {
///     applyJumpImpulse();
/// }
/// @endcode
class GroundTracker
{
public:
	/// @brief 接地フラグをリセットする（物理ステップの先頭で呼ぶ）
	void reset() noexcept { m_isOnGround = false; }

	/// @brief 衝突法線を受け取り、接地判定を更新する
	/// @param normal 衝突法線ベクトル
	/// @param threshold Y成分の閾値（デフォルト: 0.5）
	void onContact(const Vec2f& normal, float threshold = 0.5f) noexcept
	{
		if (isGroundContact(normal, threshold))
		{
			m_isOnGround = true;
		}
	}

	/// @brief 現在接地しているか
	[[nodiscard]] bool isOnGround() const noexcept { return m_isOnGround; }

	/// @brief 接地フラグを強制的に設定する
	/// @param grounded 接地状態
	void setOnGround(bool grounded) noexcept { m_isOnGround = grounded; }

private:
	bool m_isOnGround = false;
};

/// @brief ワンウェイプラットフォームの通過判定
///
/// プレイヤーが上方から降りてきた場合のみ着地し、
/// 下方からジャンプした場合は通過するワンウェイプラットフォームの判定。
///
/// @param velocity プレイヤーの速度ベクトル
/// @param platformNormal プラットフォームの法線（通常は上向き {0, -1}）
/// @param dotThreshold 内積の閾値（デフォルト: 0.0）
/// @return 通過可能ならtrue（着地不可）
[[nodiscard]] constexpr bool isOneWayPassable(
	const Vec2f& velocity, const Vec2f& platformNormal,
	float dotThreshold = 0.0f) noexcept
{
	// 速度と法線の内積: 負なら衝突方向、正なら離れる方向
	const float dot = velocity.x * platformNormal.x + velocity.y * platformNormal.y;
	return dot > dotThreshold;
}

/// @brief 天井接触の判定
///
/// 法線のY成分が負（画面座標系で上向き）かつ閾値以上であれば天井。
///
/// @param normal 衝突法線ベクトル
/// @param threshold Y成分の閾値（デフォルト: 0.5）
/// @return 天井接触の法線ならtrue
[[nodiscard]] constexpr bool isCeilingContact(
	const Vec2f& normal, float threshold = 0.5f) noexcept
{
	return normal.y < -threshold;
}

} // namespace sgc::physics
