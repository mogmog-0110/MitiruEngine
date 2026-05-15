#pragma once

/// @file SdfEffects.hpp
/// @brief SDFテキストエフェクト定義（アウトライン、シャドウ、グロー）

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief アウトラインエフェクト設定
struct SdfOutlineEffect
{
	bool enabled = false;           ///< アウトライン有効化
	sgc::Colorf color{0, 0, 0, 1}; ///< アウトライン色
	float width = 0.1f;            ///< アウトライン幅（SDF空間、0.0〜0.5）
};

/// @brief シャドウエフェクト設定
struct SdfShadowEffect
{
	bool enabled = false;            ///< シャドウ有効化
	sgc::Colorf color{0, 0, 0, 0.6f}; ///< シャドウ色
	float offsetX = 2.0f;           ///< X方向オフセット（ピクセル）
	float offsetY = 2.0f;           ///< Y方向オフセット（ピクセル）
	float softness = 0.15f;         ///< シャドウのぼかし度（SDF空間）
};

/// @brief グローエフェクト設定
struct SdfGlowEffect
{
	bool enabled = false;                ///< グロー有効化
	sgc::Colorf color{1, 1, 0.5f, 0.8f}; ///< グロー色
	float radius = 0.2f;                ///< グロー範囲（SDF空間、0.0〜0.5）
};

/// @brief テキストエフェクトの組み合わせ
/// @details 複数のエフェクトを同時に適用可能。描画順は: グロー → シャドウ → アウトライン → 本体
struct SdfTextEffect
{
	SdfOutlineEffect outline; ///< アウトラインエフェクト
	SdfShadowEffect shadow;  ///< シャドウエフェクト
	SdfGlowEffect glow;      ///< グローエフェクト

	/// @brief エフェクトなしのデフォルト設定を返す
	[[nodiscard]] static SdfTextEffect none() noexcept { return {}; }

	/// @brief アウトライン付きエフェクトを返す
	[[nodiscard]] static SdfTextEffect withOutline(
		const sgc::Colorf& color, float width = 0.1f)
	{
		SdfTextEffect effect;
		effect.outline.enabled = true;
		effect.outline.color = color;
		effect.outline.width = width;
		return effect;
	}

	/// @brief シャドウ付きエフェクトを返す
	[[nodiscard]] static SdfTextEffect withShadow(
		const sgc::Colorf& color, float offsetX = 2.0f, float offsetY = 2.0f)
	{
		SdfTextEffect effect;
		effect.shadow.enabled = true;
		effect.shadow.color = color;
		effect.shadow.offsetX = offsetX;
		effect.shadow.offsetY = offsetY;
		return effect;
	}

	/// @brief グロー付きエフェクトを返す
	[[nodiscard]] static SdfTextEffect withGlow(
		const sgc::Colorf& color, float radius = 0.2f)
	{
		SdfTextEffect effect;
		effect.glow.enabled = true;
		effect.glow.color = color;
		effect.glow.radius = radius;
		return effect;
	}
};

} // namespace mitiru::render
