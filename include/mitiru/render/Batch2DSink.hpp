#pragma once

/// @file Batch2DSink.hpp
/// @brief RenderPipeline2D から、自前の D3D 経路を持たない描画層 (Dawn) へ 2D 頂点バッチを渡す窓口

#include <cstdint>
#include <vector>

#include <mitiru/render/Vertex2D.hpp>

namespace mitiru::render
{

class IBatch2DSink
{
public:
	virtual ~IBatch2DSink() = default;

	/// @brief 頂点色の三角形を描く。座標は resize に渡した論理画面の大きさを基準とする。色は指定した sRGB のまま使う。通常のアルファ合成を使う
	virtual void submit(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices) = 0;

	virtual void resize(float width, float height) = 0;
};

} // namespace mitiru::render
