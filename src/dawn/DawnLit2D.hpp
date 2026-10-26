#pragma once

// Dawn の 2D ライト (R6)。光を使わない 2D は DawnBatch2D の頂点色のバッチのまま変えず、光が有効なフレームだけ、
// こちらが「テクスチャ付きの頂点バッチ + 光」を最終色へ重ねる。式は include/mitiru/render/Light2DMath.hpp が正本で、
// shaders/dawn/batch2dlit.slang が同じ式を持つ (DX12 は DefaultShaders2DLit.hpp)。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <mitiru/gfx/dawn/DawnDevice.hpp>
#include <mitiru/render/Light2DMath.hpp>
#include <mitiru/render/Vertex2D.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

/// 論理画面 → クリップ空間の列優先行列。RenderPipeline2D の OrthoMatrix と同じ値
void orthoMatrix2D(float width, float height, float out[16]);

class DawnLit2D
{
public:
	explicit DawnLit2D(gfx::DawnDevice& device);

	/// @brief パイプライン・サンプラー・空の画像を作る。作れなければ false (光は当たらない扱い)
	[[nodiscard]] bool initialize();

	void resize(float logicalWidth, float logicalHeight);

	/// @brief 以後のサブミットに当てる光。nullptr は光なし
	void setLighting(const Lighting2D* lighting);
	[[nodiscard]] bool active() const noexcept { return m_active; }

	/// @brief key の画像を GPU に置く。同じ key・大きさ・pixels の先頭なら再利用する (contentMayChange は内容の変化も見る)
	[[nodiscard]] std::uint32_t ensureTexture(const void* key, int w, int h, const std::uint8_t* rgba, bool contentMayChange);
	void setDistanceRange(const void* key, float range);
	void updateRows(const void* key, int firstRow, int rowCount, const std::uint8_t* rgba);
	void setNormalMap(const void* colorKey, const void* normalKey, int w, int h, const std::uint8_t* rgba);

	/// @brief 頂点バッチを光つきで最終色へ重ねる。texHandle 0 は頂点色だけ
	void submit(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices, std::uint32_t texHandle);

private:
	struct Texture
	{
		wgpu::Texture texture;
		wgpu::TextureView view;
		int w = 0;
		int h = 0;
		const void* key = nullptr;
		const std::uint8_t* src = nullptr;
		std::uint64_t hash = 0;
		float distanceRange = 0.0f;
		std::uint32_t version = 0;   ///< 画像を作り直すたびに増やす。bind group の作り直しの目印
	};

	struct NormalRef
	{
		const void* key = nullptr;
		int w = 0;
		int h = 0;
		const std::uint8_t* rgba = nullptr;
	};

	struct GroupEntry
	{
		wgpu::BindGroup group;
		std::uint32_t colorVersion = 0;
		std::uint32_t normalVersion = 0;
	};

	[[nodiscard]] wgpu::Texture makeTexture(int w, int h) const;
	void upload(const wgpu::Texture& texture, int firstRow, int rowCount, int w, const std::uint8_t* rgba) const;
	[[nodiscard]] const wgpu::BindGroup* groupFor(std::uint32_t colorHandle, std::uint32_t normalHandle);
	[[nodiscard]] const wgpu::TextureView& viewOf(std::uint32_t handle) const;

	gfx::DawnDevice& m_device;
	gfx::DawnContext& m_ctx;
	wgpu::Buffer m_uniform;
	wgpu::Buffer m_ring;
	wgpu::BindGroupLayout m_layout;
	wgpu::Sampler m_linear;
	wgpu::Sampler m_point;
	gfx::AsyncRenderPipeline m_pipeline;
	Texture m_blank;       ///< 色を読まない描画用の 1 x 1
	Texture m_flatNormal;  ///< 法線マップが無い描画用の 1 x 1
	std::vector<Texture> m_textures;   ///< 添字 + 1 が handle
	std::unordered_map<const void*, std::uint32_t> m_lookup;
	std::unordered_map<const void*, NormalRef> m_normals;
	std::unordered_map<const void*, float> m_ranges;   ///< 距離場の文字の幅 (画像の key ごと)
	std::unordered_map<std::uint64_t, GroupEntry> m_groups;
	Lighting2D m_lighting;
	light2d::PackedLight m_packed[kMaxLights2D] = {};
	float m_ortho[16] = {};
	float m_logicalW = 1.0f;
	float m_logicalH = 1.0f;
	std::uint64_t m_cursor = 0;
	bool m_active = false;
};

} // namespace mitiru::render::dawn_detail
