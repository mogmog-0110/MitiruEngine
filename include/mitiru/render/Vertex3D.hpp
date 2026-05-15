#pragma once

/// @file Vertex3D.hpp
/// @brief 3D頂点構造体
/// @details 3Dメッシュ描画に使用する頂点データ型を定義する。

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief 3D描画用の頂点データ
/// @details 位置・法線・テクスチャ座標・色を持つ頂点。
///          Mesh クラスが内部で保持する。
///
/// @code
/// mitiru::render::Vertex3D v;
/// v.position = {1.0f, 2.0f, 3.0f};
/// v.normal = {0.0f, 1.0f, 0.0f};
/// v.texCoord = {0.5f, 0.5f};
/// v.color = sgc::Colorf::white();
/// @endcode
struct Vertex3D
{
	sgc::Vec3f position{};  ///< ワールド空間位置
	sgc::Vec3f normal{};    ///< 法線ベクトル
	sgc::Vec2f texCoord{};  ///< テクスチャ座標 [0,1]
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};  ///< 頂点色（デフォルト白）

	/// @brief デフォルトコンストラクタ
	constexpr Vertex3D() noexcept = default;

	/// @brief 全フィールド指定コンストラクタ
	/// @param position 位置
	/// @param normal 法線
	/// @param texCoord テクスチャ座標
	/// @param color 頂点色
	constexpr Vertex3D(const sgc::Vec3f& position,
	                   const sgc::Vec3f& normal,
	                   const sgc::Vec2f& texCoord,
	                   const sgc::Colorf& color) noexcept
		: position(position)
		, normal(normal)
		, texCoord(texCoord)
		, color(color)
	{
	}

	/// @brief 位置と法線のみ指定するコンストラクタ
	/// @param position 位置
	/// @param normal 法線
	constexpr Vertex3D(const sgc::Vec3f& position,
	                   const sgc::Vec3f& normal) noexcept
		: position(position)
		, normal(normal)
	{
	}
};

} // namespace mitiru::render
