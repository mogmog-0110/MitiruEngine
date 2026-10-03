#pragma once

/// @file Dx12TextureUpload.hpp
/// @brief mip 連鎖つき画像 (RGBA8 / BC4 / BC5 / BC7) を DX12 の DEFAULT heap へ上げる
/// @details `uploadMipImage` が唯一の転送経路で、clod のテクスチャも forward のアルベド
///          (Dx12Texture2D) もここを通る。skybox 用の TextureCube とは別経路。

#ifdef _WIN32

#include <cstdint>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/render/DdsFile.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/TextureMips.hpp>

namespace mitiru::render::dx12
{

namespace detail
{

[[nodiscard]] inline D3D12_RESOURCE_DESC textureDesc(const MipImage& image) noexcept
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = image.width;
	desc.Height = image.height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = static_cast<UINT16>(image.mips.size());
	desc.Format = static_cast<DXGI_FORMAT>(image.format);
	desc.SampleDesc.Count = 1;
	return desc;
}

/// staging の各段を tex へコピーする命令を積む。COPY キューのリストにも積める (遷移は積まない)
inline void recordMipCopies(ID3D12GraphicsCommandList* cmd, ID3D12Resource* tex, ID3D12Resource* staging,
                            const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts)
{
	for (UINT lv = 0; lv < static_cast<UINT>(layouts.size()); ++lv)
	{
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = lv;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = staging;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = layouts[lv];
		cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}
}

inline void recordTransition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES before,
                             D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &b);
}

} // namespace detail

/// @brief GPU へ送る前のテクスチャ。DEFAULT heap の本体と、中身を詰めた UPLOAD heap を持つ
/// @details 作るのはどのスレッドでもよい (装置と D3D12MA は複数のスレッドから呼べる)。
struct StagedTexture
{
	gfx::GpuResource                                 texture;
	gfx::GpuResource                                 staging;
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>  layouts;
	std::uint32_t                                    width = 0;
	std::uint32_t                                    height = 0;
	DXGI_FORMAT                                      format = DXGI_FORMAT_UNKNOWN;

	[[nodiscard]] bool valid() const noexcept { return texture && staging; }
	[[nodiscard]] std::uint64_t bytes() const noexcept { return staging ? staging->GetDesc().Width : 0; }
};

/// @param initialState 本体の最初の状態。COPY キューで写すなら COMMON (COPY キューは COMMON から写す)
/// @return 失敗 (寸法 0・mip 不足・確保失敗) は false で out は空
[[nodiscard]] inline bool stageMipImage(ID3D12Device* device, const MipImage& image, StagedTexture& out,
                                        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST)
{
	out = {};
	if (!device || image.width == 0 || image.height == 0 || image.mips.empty()) { return false; }

	const D3D12_RESOURCE_DESC desc = detail::textureDesc(image);
	// BC は行が 4x4 ブロック単位で数え方が変わるので、配置はドライバに出させる
	const UINT levels = desc.MipLevels;
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(levels);
	std::vector<UINT> rows(levels);
	std::vector<UINT64> rowBytes(levels);
	UINT64 total = 0;
	device->GetCopyableFootprints(&desc, 0, levels, 0, layouts.data(), rows.data(), rowBytes.data(), &total);
	for (UINT lv = 0; lv < levels; ++lv)
	{
		if (image.mips[lv].size() < static_cast<std::size_t>(rowBytes[lv]) * rows[lv]) { return false; }
	}

	StagedTexture staged;
	if (FAILED(gfx::createGpuResource(device, D3D12_HEAP_TYPE_DEFAULT, desc, initialState, nullptr, staged.texture)) ||
	    FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ,
	                                staged.staging)))
	{
		return false;
	}
	std::uint8_t* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(staged.staging->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))) { return false; }
	for (UINT lv = 0; lv < levels; ++lv)
	{
		for (UINT r = 0; r < rows[lv]; ++r)
		{
			std::memcpy(mapped + layouts[lv].Offset + static_cast<UINT64>(r) * layouts[lv].Footprint.RowPitch,
			            image.mips[lv].data() + static_cast<std::size_t>(r) * rowBytes[lv],
			            static_cast<std::size_t>(rowBytes[lv]));
		}
	}
	const D3D12_RANGE wrote = {0, static_cast<SIZE_T>(total)};
	staged.staging->Unmap(0, &wrote);
	staged.layouts = std::move(layouts);
	staged.width = image.width;
	staged.height = image.height;
	staged.format = desc.Format;
	out = std::move(staged);
	return true;
}

/// @brief 詰めた中身を本体へ写す命令を積む。COPY キューのリストでもよい
inline void recordStagedCopies(ID3D12GraphicsCommandList* cmd, const StagedTexture& staged)
{
	detail::recordMipCopies(cmd, staged.texture.Get(), staged.staging.Get(), staged.layouts);
}

/// @brief image の全 mip を DEFAULT heap のテクスチャへコピーし、afterState へ遷移させる命令を cmd に積む
/// @param uploadOwner  使い捨ての UPLOAD heap を GPU が読み終わるまで持っておく入れ物
/// @return 失敗 (寸法 0・mip 不足・確保失敗) は false で out は空
[[nodiscard]] inline bool uploadMipImage(ID3D12Device* device, ID3D12GraphicsCommandList* cmd,
                                         const MipImage& image,
                                         std::vector<gfx::GpuResource>& uploadOwner,
                                         gfx::GpuResource& out, D3D12_RESOURCE_STATES afterState)
{
	out.Reset();
	if (!cmd) { return false; }
	StagedTexture staged;
	if (!stageMipImage(device, image, staged)) { return false; }
	recordStagedCopies(cmd, staged);
	detail::recordTransition(cmd, staged.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, afterState);
	uploadOwner.push_back(std::move(staged.staging));
	out = std::move(staged.texture);
	return true;
}

/// @brief GPU へ送る前のバッファ。本体 (DEFAULT heap) と中身を詰めた UPLOAD heap
struct StagedBuffer
{
	gfx::GpuResource buffer;
	gfx::GpuResource staging;
	std::uint64_t    bytes = 0;

	[[nodiscard]] bool valid() const noexcept { return buffer && staging; }
};

[[nodiscard]] inline bool stageBuffer(ID3D12Device* device, const void* data, std::size_t bytes, StagedBuffer& out)
{
	out = {};
	if (!device || data == nullptr || bytes == 0) { return false; }
	StagedBuffer staged;
	if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COMMON,
	                                staged.buffer)) ||
	    FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ,
	                                staged.staging)))
	{
		return false;
	}
	void* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(staged.staging->Map(0, &noRead, &mapped))) { return false; }
	std::memcpy(mapped, data, bytes);
	const D3D12_RANGE wrote = {0, static_cast<SIZE_T>(bytes)};
	staged.staging->Unmap(0, &wrote);
	staged.bytes = bytes;
	out = std::move(staged);
	return true;
}

inline void recordStagedCopies(ID3D12GraphicsCommandList* cmd, const StagedBuffer& staged)
{
	cmd->CopyBufferRegion(staged.buffer.Get(), 0, staged.staging.Get(), 0, staged.bytes);
}

/// @brief forward パスのテクスチャ (アルベドは sRGB の RGBA8、glTF の材質のマップは圧縮形式もある) + mip 連鎖
/// @details `uploadFrom(...)` で 1 回 GPU に転送したら、SRV を CPU ハンドル経由で
///          外部の shader-visible heap にコピーして使う。
class Dx12Texture2D
{
public:
	Dx12Texture2D() = default;
	~Dx12Texture2D() = default;

	Dx12Texture2D(const Dx12Texture2D&) = delete;
	Dx12Texture2D& operator=(const Dx12Texture2D&) = delete;
	Dx12Texture2D(Dx12Texture2D&&) noexcept = default;
	Dx12Texture2D& operator=(Dx12Texture2D&&) noexcept = default;

	/// @brief Texture (RGBA8) を mip 連鎖ごと GPU へ上げる
	/// @param uploadOwner upload heap の寿命管理用 (frame 完了まで保持してね)
	/// @return true で成功
	bool uploadFrom(ID3D12Device* device,
	                ID3D12GraphicsCommandList* cmdList,
	                const Texture& src,
	                std::vector<gfx::GpuResource>& uploadOwner)
	{
		if (src.width() <= 0 || src.height() <= 0) return false;
		const auto& px = src.pixels();
		if (px.size() < static_cast<std::size_t>(src.width()) * src.height() * 4) return false;

		MipImage image;
		image.format = TextureFormat::Rgba8Srgb;
		image.width = static_cast<std::uint32_t>(src.width());
		image.height = static_cast<std::uint32_t>(src.height());
		image.mips = buildMipChain(px.data(), src.width(), src.height(), MipFilter::Srgb);
		return uploadMip(device, cmdList, image, uploadOwner);
	}

	/// @brief mip 連鎖つき画像 (RGBA8 / BC4 / BC5 / BC7) をそのままの形式で GPU へ上げる。SRV も同じ形式で読む
	bool uploadMip(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const MipImage& image,
	               std::vector<gfx::GpuResource>& uploadOwner)
	{
		if (!uploadMipImage(device, cmdList, image, uploadOwner, m_texture,
		                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
		{
			return false;
		}
		m_width = static_cast<int>(image.width);
		m_height = static_cast<int>(image.height);
		m_mipLevels = static_cast<UINT>(image.mips.size());
		m_format = static_cast<DXGI_FORMAT>(image.format);
		m_ready = true;
		return true;
	}

	/// @brief COPY キューで写し終えた本体を受け取る。本体を読む状態への遷移は呼び手が先に積む
	void adoptStaged(StagedTexture&& staged)
	{
		m_texture = std::move(staged.texture);
		m_width = static_cast<int>(staged.width);
		m_height = static_cast<int>(staged.height);
		m_mipLevels = static_cast<UINT>(staged.layouts.size());
		m_format = staged.format;
		m_ready = static_cast<bool>(m_texture);
		staged.staging.Reset();
	}

	/// @brief 描画先 (副ビューの出力など) をそのまま色のテクスチャとして使う。資源は描画先と共有し、mip は 1 段
	void adopt(const gfx::GpuResource& texture, DXGI_FORMAT srvFormat, int width, int height)
	{
		m_texture = texture;
		m_width = width;
		m_height = height;
		m_mipLevels = 1;
		m_format = srvFormat;
		m_ready = static_cast<bool>(m_texture);
	}

	/// @brief 既存の descriptor heap に SRV を生成する
	void createSRV(ID3D12Device* device,
	               D3D12_CPU_DESCRIPTOR_HANDLE dst) const
	{
		if (!m_texture) return;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                  = m_format;
		srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels     = m_mipLevels;
		device->CreateShaderResourceView(m_texture.Get(), &srv, dst);
	}

	[[nodiscard]] ID3D12Resource* nativeResource() const noexcept { return m_texture.Get(); }
	[[nodiscard]] int width() const noexcept  { return m_width; }
	[[nodiscard]] int height() const noexcept { return m_height; }
	[[nodiscard]] UINT mipLevels() const noexcept { return m_mipLevels; }
	[[nodiscard]] bool isReady() const noexcept { return m_ready; }

private:
	gfx::GpuResource m_texture;
	int  m_width     = 0;
	int  m_height    = 0;
	UINT m_mipLevels = 1;
	DXGI_FORMAT m_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	bool m_ready     = false;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
