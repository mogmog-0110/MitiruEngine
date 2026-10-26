#pragma once

/// @file Batch2DSink.hpp
/// @brief RenderPipeline2D から、自前の D3D 経路を持たない描画層 (Dawn) へ 2D 頂点バッチを渡す窓口

#include <cstdint>
#include <vector>

#include <mitiru/render/Light2DMath.hpp>
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

	/// @brief 2D ライトを当てられる描画先か。既定は当てられない
	[[nodiscard]] virtual bool supportsLighting2D() const { return false; }

	/// @brief 以後の submit に当てる光。nullptr は光なし。渡した物は次の呼び出しまで読める
	virtual void setLighting2D(const Lighting2D* /*lighting*/) {}

	// ── 光の間だけ使えるテクスチャ付きのサブミット (法線マップのために)。光を使わない間は呼ばれない ──

	/// @brief 光が有効で、テクスチャ付きの頂点バッチを受け取れるか
	[[nodiscard]] virtual bool supportsTextured() const { return false; }
	/// @brief key の画像を GPU に置いて handle (1 以上) を返す。同じ key・大きさ・pixels の先頭なら置き直さない。失敗は 0
	[[nodiscard]] virtual std::uint32_t ensureTexture(const void* /*key*/, int /*w*/, int /*h*/, const std::uint8_t* /*rgba*/, bool /*contentMayChange*/) { return 0; }
	virtual void setDistanceRange(const void* /*key*/, float /*pixelRange*/) {}
	virtual void updateTextureRows(const void* /*key*/, int /*firstRow*/, int /*rowCount*/, const std::uint8_t* /*rgba*/) {}
	virtual void setNormalMap(const void* /*colorKey*/, const void* /*normalKey*/, int /*w*/, int /*h*/, const std::uint8_t* /*rgba*/) {}
	virtual void submitTextured(const std::vector<Vertex2D>& /*vertices*/, const std::vector<std::uint32_t>& /*indices*/, std::uint32_t /*texHandle*/) {}
	/// @brief RGBA8 の画像 1 枚を、論理画面の矩形 (x, y, w, h) へ重ねる (動画の 1 コマ、Screen::drawPixelGrid)。色は掛けず、画像のままの値を
	///        通常のアルファ合成で重ねる。呼び出しより前に submit した頂点バッチの上に描く。対応しない描画の層は false を返す
	virtual bool submitImage(float /*x*/, float /*y*/, float /*w*/, float /*h*/, const std::uint8_t* /*rgba*/, int /*pixelWidth*/, int /*pixelHeight*/)
	{
		return false;
	}
};

} // namespace mitiru::render
