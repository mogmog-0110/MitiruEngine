#pragma once

/// @file IMesh.hpp
/// @brief 3Dメッシュインターフェースと頂点構造体
///
/// フレームワーク非依存の3Dメッシュ描画APIを定義する。
///
/// @code
/// class MyMesh : public sgc::graphics3d::IMesh {
///     void setVertices(std::span<const Vertex3D> verts) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <cstddef>
#include <cstdint>
#include <span>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

/// @brief 3D頂点データ
struct Vertex3D
{
	Vec3f position;                          ///< 頂点位置
	Vec3f normal;                            ///< 法線ベクトル
	Vec2f texCoord;                          ///< テクスチャ座標
	Colorf color{1.0f, 1.0f, 1.0f, 1.0f};   ///< 頂点カラー
};

/// @brief 3Dメッシュの抽象インターフェース
///
/// 頂点・インデックスデータの設定と描画を抽象化する。
class IMesh
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IMesh() = default;

	/// @brief 頂点データを設定する
	/// @param vertices 頂点配列
	virtual void setVertices(std::span<const Vertex3D> vertices) = 0;

	/// @brief インデックスデータを設定する
	/// @param indices インデックス配列
	virtual void setIndices(std::span<const uint32_t> indices) = 0;

	/// @brief 頂点数を返す
	/// @return 頂点数
	[[nodiscard]] virtual std::size_t vertexCount() const = 0;

	/// @brief インデックス数を返す
	/// @return インデックス数
	[[nodiscard]] virtual std::size_t indexCount() const = 0;

	/// @brief メッシュを描画する
	virtual void draw() const = 0;
};

} // namespace sgc::graphics3d
