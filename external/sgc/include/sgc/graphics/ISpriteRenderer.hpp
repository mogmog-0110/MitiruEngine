#pragma once

/// @file ISpriteRenderer.hpp
/// @brief スプライト/テクスチャ描画の抽象インターフェース
///
/// フレームワーク非依存のスプライト描画APIを定義する。
/// テクスチャの読み込み・解放、矩形/回転/スケール付き描画、
/// ブレンドモード設定を抽象化する。
///
/// @code
/// class MySpriteRenderer : public sgc::ISpriteRenderer {
///     void drawSprite(const AABB2f& dest, const AABB2f& src,
///         int textureId, const Colorf& tint) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <cstdint>
#include <string>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/types/Color.hpp"

namespace sgc
{

/// @brief ブレンドモード
enum class BlendMode : int32_t
{
	Normal,    ///< 通常（アルファブレンド）
	Additive,  ///< 加算合成
	Multiply,  ///< 乗算合成
	Screen     ///< スクリーン合成
};

/// @brief スプライト/テクスチャ描画の抽象インターフェース
///
/// テクスチャの読み込み・解放と、各種スプライト描画を提供する。
/// ブレンドモードの切り替えにも対応する。
class ISpriteRenderer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISpriteRenderer() = default;

	// ── テクスチャ管理 ─────────────────────────────────────

	/// @brief テクスチャを読み込む
	/// @param path テクスチャファイルパス
	/// @return テクスチャID（失敗時は負の値）
	[[nodiscard]] virtual int loadTexture(const std::string& path) = 0;

	/// @brief テクスチャを解放する
	/// @param textureId テクスチャID
	virtual void unloadTexture(int textureId) = 0;

	/// @brief テクスチャのサイズを取得する
	/// @param textureId テクスチャID
	/// @return テクスチャサイズ（幅, 高さ）
	[[nodiscard]] virtual Vec2f getTextureSize(int textureId) const = 0;

	// ── 描画 ────────────────────────────────────────────────

	/// @brief スプライトを描画する（ソース矩形→デスティネーション矩形）
	/// @param dest 描画先矩形
	/// @param src テクスチャ上のソース矩形
	/// @param textureId テクスチャID
	/// @param tint 色合い（デフォルト: 白）
	virtual void drawSprite(const AABB2f& dest, const AABB2f& src,
		int textureId, const Colorf& tint = Colorf::white()) = 0;

	/// @brief 回転付きスプライトを描画する
	/// @param dest 描画先矩形
	/// @param src テクスチャ上のソース矩形
	/// @param textureId テクスチャID
	/// @param angle 回転角度（ラジアン）
	/// @param origin 回転原点（デスティネーション矩形内のオフセット）
	/// @param tint 色合い（デフォルト: 白）
	virtual void drawSpriteRotated(const AABB2f& dest, const AABB2f& src,
		int textureId, float angle, const Vec2f& origin,
		const Colorf& tint = Colorf::white()) = 0;

	/// @brief スケール付きスプライトを描画する
	/// @param pos 描画位置（左上座標）
	/// @param scale 拡大率（X, Y）
	/// @param textureId テクスチャID
	/// @param tint 色合い（デフォルト: 白）
	virtual void drawSpriteScaled(const Vec2f& pos, const Vec2f& scale,
		int textureId, const Colorf& tint = Colorf::white()) = 0;

	// ── ブレンドモード ──────────────────────────────────────

	/// @brief ブレンドモードを設定する
	/// @param mode ブレンドモード
	virtual void setBlendMode(BlendMode mode) = 0;
};

} // namespace sgc
