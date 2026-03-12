#pragma once

/// @file Vertex2D.hpp
/// @brief 2D頂点構造体
/// @details 2Dスプライト・シェイプ描画に使用する頂点データ型を定義する。

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief 2D描画用の頂点データ
/// @details 位置・テクスチャ座標・色を持つ頂点。
///          SpriteBatch や ShapeRenderer が内部で生成する。
///
/// @code
/// mitiru::render::Vertex2D v;
/// v.position = {100.0f, 200.0f};
/// v.texCoord = {0.0f, 0.0f};
/// v.color = sgc::Colorf::white();
/// @endcode
struct Vertex2D
{
	sgc::Vec2f position{};  ///< スクリーン座標位置
	sgc::Vec2f texCoord{};  ///< テクスチャ座標 [0,1]
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};  ///< 頂点色（デフォルト白）

	/// @brief デフォルトコンストラクタ
	constexpr Vertex2D() noexcept = default;

	/// @brief 全フィールド指定コンストラクタ
	/// @param position 位置
	/// @param texCoord テクスチャ座標
	/// @param color 頂点色
	constexpr Vertex2D(const sgc::Vec2f& position,
	                   const sgc::Vec2f& texCoord,
	                   const sgc::Colorf& color) noexcept
		: position(position)
		, texCoord(texCoord)
		, color(color)
	{
	}

	/// @brief 位置と色のみ指定するコンストラクタ（テクスチャ座標はゼロ）
	/// @param position 位置
	/// @param color 頂点色
	constexpr Vertex2D(const sgc::Vec2f& position,
	                   const sgc::Colorf& color) noexcept
		: position(position)
		, color(color)
	{
	}
};

} // namespace mitiru::render
