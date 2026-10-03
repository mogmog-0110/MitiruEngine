#pragma once

/// @file DX12WorldPrepare.hpp
/// @brief world.json を描ける形にする前半。ワーカースレッドで走り、描画側の表には触れない
/// @details world.json を読み (高さマップ、splat、草の密度、撒いた物、LOD の四分木)、画像を DEFAULT heap へ詰めて COPY キューで
///          写し、影のための粗い三角形を作る。描画側 (DX12World.hpp) はできた物を受け取り、読む状態への遷移を積んで登録する。

#ifdef _WIN32

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <stb_image.h>

#include <sgc/math/Mat4.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/MaterialTextures.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/TextureMips.hpp>
#include <mitiru/render/dx12/Dx12CopyQueue.hpp>
#include <mitiru/render/dx12/Dx12TextureUpload.hpp>
#include <mitiru/terrain/OutdoorWorldLoad.hpp>

namespace mitiru::render::dx12
{

struct PreparedOutdoorWorld
{
	std::string                                 error;    ///< 空でなければ読めなかった理由
	std::unique_ptr<terrain::OutdoorWorld>      world;
	StagedTexture                               height;
	std::array<StagedTexture, 2>                splat;
	StagedTexture                               density;
	std::array<StagedTexture, terrain::kMaxTerrainLayers> albedo;
	std::array<StagedTexture, terrain::kMaxTerrainLayers> normal;
	std::vector<std::unique_ptr<Mesh>>          shadowChunks;
	std::vector<sgc::Mat4f>                     shadowChunkWorld;
	std::vector<std::vector<MeshInstance>>      scatter;
	bool                                        copied = false;  ///< COPY キューで写し終えた
	std::uint64_t                               gpuBytes = 0;

	template <typename Fn>
	void forEachTexture(Fn&& fn)
	{
		fn(height);
		for (auto& t : splat) { fn(t); }
		fn(density);
		for (auto& t : albedo) { fn(t); }
		for (auto& t : normal) { fn(t); }
	}
};

namespace detail
{

/// @brief 1 段の生画像 (R16、R8、RGBA8)
inline void stageRawTexture(ID3D12Device* device, StagedTexture& out, DXGI_FORMAT format, std::uint32_t w,
                            std::uint32_t h, const std::uint8_t* data, std::size_t bytes, bool mips)
{
	MipImage img;
	img.format = static_cast<TextureFormat>(format);
	img.width = w;
	img.height = h;
	if (mips) { img.mips = buildMipChain(data, static_cast<int>(w), static_cast<int>(h), MipFilter::Linear); }
	else { img.mips.emplace_back(data, data + bytes); }
	(void)stageMipImage(device, img, out, D3D12_RESOURCE_STATE_COMMON);
}

/// @brief 層の画像を読む。辺が 4 の倍数なら BC 圧縮し、隣に DDS を置く
inline void stageLayerTexture(ID3D12Device* device, StagedTexture& out, const std::string& world,
                              const std::string& image, std::size_t layer, TextureKind kind)
{
	if (image.empty()) { return; }
	CpuTexture cpu;
	if (const auto bytes = vfs::readAsset(image))
	{
		int w = 0, h = 0, c = 0;
		if (stbi_uc* px = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &c, 4))
		{
			cpu.width = w;
			cpu.height = h;
			cpu.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
			stbi_image_free(px);
		}
	}
	if (!cpu.valid())
	{
		debug::warnOnce("dx12.world.layer." + image, "地形の層の画像を読めない: " + image);
		return;
	}
	const auto sidecar = materialTextureSidecar(world, layer, kind == TextureKind::Normal ? "normal" : "base", image);
	const MipImage img = prepareMaterialTexture(cpu, kind, &sidecar);
	(void)stageMipImage(device, img, out, D3D12_RESOURCE_STATE_COMMON);
}

inline void stageWorldTextures(ID3D12Device* device, PreparedOutdoorWorld& p)
{
	const terrain::OutdoorWorld& w = *p.world;
	const auto& s = w.terrain.samples();
	stageRawTexture(device, p.height, DXGI_FORMAT_R16_UNORM, w.terrain.width(), w.terrain.depth(),
	                reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 2, false);
	for (std::size_t k = 0; k < 2; ++k)
	{
		const auto& img = w.splat[k];
		if (img.valid())
		{
			stageRawTexture(device, p.splat[k], DXGI_FORMAT_R8G8B8A8_UNORM, img.width, img.height, img.rgba.data(),
			                img.rgba.size(), true);
		}
	}
	const std::uint8_t one = 255;
	const auto& d = w.grassDensity;
	if (d.valid()) { stageRawTexture(device, p.density, DXGI_FORMAT_R8_UNORM, d.width, d.height, d.values.data(), d.values.size(), false); }
	else { stageRawTexture(device, p.density, DXGI_FORMAT_R8_UNORM, 1, 1, &one, 1, false); }
	for (std::size_t k = 0; k < w.layers.size(); ++k)
	{
		stageLayerTexture(device, p.albedo[k], w.path, w.layers[k].albedo, k, TextureKind::Color);
		stageLayerTexture(device, p.normal[k], w.path, w.layers[k].normal, k, TextureKind::Normal);
	}
	p.forEachTexture([&](const StagedTexture& t) { p.gpuBytes += t.bytes(); });
}

/// @brief 地形のチャンク (x0..x1, z0..z1 の標本) を stride おきに間引いた三角形にする
[[nodiscard]] inline std::unique_ptr<Mesh> terrainShadowChunk(const terrain::Heightfield& h, std::uint32_t x0,
                                                              std::uint32_t z0, std::uint32_t x1, std::uint32_t z1,
                                                              std::uint32_t stride, const sgc::Vec3f& base)
{
	std::vector<Vertex3D> verts;
	for (std::uint32_t iz = z0; iz <= z1; iz = (iz == z1) ? z1 + 1 : std::min(iz + stride, z1))
	{
		for (std::uint32_t ix = x0; ix <= x1; ix = (ix == x1) ? x1 + 1 : std::min(ix + stride, x1))
		{
			const sgc::Vec3f p = h.vertex(ix, iz);
			verts.emplace_back(sgc::Vec3f{p.x - base.x, p.y, p.z - base.z}, sgc::Vec3f{0, 1, 0});
		}
	}
	const std::uint32_t cols = (x1 - x0 + stride - 1) / stride + 1;
	const auto rows = static_cast<std::uint32_t>(verts.size() / cols);
	std::vector<std::uint32_t> idx;
	for (std::uint32_t r = 0; r + 1 < rows; ++r)
	{
		for (std::uint32_t c = 0; c + 1 < cols; ++c)
		{
			const std::uint32_t a = r * cols + c, b = a + 1, cc = a + cols, d = cc + 1;
			idx.insert(idx.end(), {a, cc, b, b, cc, d});
		}
	}
	auto mesh = std::make_unique<Mesh>();
	mesh->setVertices(std::move(verts));
	mesh->setIndices(std::move(idx));
	return mesh;
}

/// @brief 影のパス用に、地形を 64 マス角のチャンクの三角形にする。512 標本を超える辺は間引く
inline void buildTerrainShadowChunks(PreparedOutdoorWorld& p)
{
	const terrain::Heightfield& h = p.world->terrain;
	const std::uint32_t stride = std::max(1u, (std::max(h.width(), h.depth()) + 511u) / 512u);
	const std::uint32_t chunk = 64u * stride;
	for (std::uint32_t z0 = 0; z0 + 1 < h.depth(); z0 += chunk)
	{
		for (std::uint32_t x0 = 0; x0 + 1 < h.width(); x0 += chunk)
		{
			const std::uint32_t x1 = std::min(x0 + chunk, h.width() - 1);
			const std::uint32_t z1 = std::min(z0 + chunk, h.depth() - 1);
			const sgc::Vec3f base = h.vertex(x0, z0);
			p.shadowChunks.push_back(terrainShadowChunk(h, x0, z0, x1, z1, stride, base));
			p.shadowChunkWorld.push_back(sgc::Mat4f::translation(sgc::Vec3f{base.x, 0.0f, base.z}));
		}
	}
}

inline void copyWorldTextures(Dx12CopyQueue* copy, PreparedOutdoorWorld& p)
{
	if (copy == nullptr || !copy->ready()) { return; }
	const std::uint64_t fence = copy->submit([&](ID3D12GraphicsCommandList* list) {
		p.forEachTexture([&](const StagedTexture& t) { if (t.valid()) { recordStagedCopies(list, t); } });
	});
	p.copied = fence != 0 && copy->wait(fence);
}

} // namespace detail

/// @brief ワーカーで走る前半。copy が null なら写しは描画側に任せる
[[nodiscard]] inline PreparedOutdoorWorld prepareOutdoorWorld(const std::string& path, ID3D12Device* device,
                                                              Dx12CopyQueue* copy)
{
	PreparedOutdoorWorld p;
	auto result = terrain::loadOutdoorWorld(path);
	if (!result.world) { p.error = result.error; return p; }
	p.world = std::move(result.world);
	for (const auto& w : p.world->warnings) { debug::warnOnce("dx12.world.warn." + path + w, w); }
	detail::stageWorldTextures(device, p);
	detail::buildTerrainShadowChunks(p);
	for (const auto& set : p.world->scatter)
	{
		std::vector<MeshInstance> inst(set.instances.size());
		for (std::size_t i = 0; i < inst.size(); ++i) { std::memcpy(inst[i].world, set.instances[i].world, sizeof(inst[i].world)); }
		p.scatter.push_back(std::move(inst));
	}
	detail::copyWorldTextures(copy, p);
	return p;
}

} // namespace mitiru::render::dx12

#endif // _WIN32
