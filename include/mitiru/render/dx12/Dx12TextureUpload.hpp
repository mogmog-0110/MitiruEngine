#pragma once

/// @file Dx12TextureUpload.hpp
/// @brief DX12 用 2D テクスチャアップロードユーティリティ
/// @details `mitiru::render::Texture` (RGBA8) を ID3D12Resource (DEFAULT heap) に
///          アップロードして SRV を作るヘルパー。skybox 用の TextureCube とは別経路
///          で、シェーダーで `Texture2D : register(t0)` として読み取れる単一画像を扱う。
///
///          典型的な使い方:
///          ```cpp
///          Dx12Texture2D tex;
///          tex.uploadFrom(device, cmdList, srcTexture);
///          // 後で SRV を descriptor heap にコピーして root descriptor table に bind
///          device->CopyDescriptorsSimple(1, dstHandle, tex.srvCpuHandle(),
///                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
///          ```

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

#include <mitiru/render/Texture.hpp>

namespace mitiru::render::dx12
{

/// @brief DX12 上の 2D テクスチャリソース（DEFAULT heap）
/// @details `uploadFrom(...)` で 1 回 GPU に転送したら、SRV を CPU ハンドル経由で
///          外部の shader-visible heap にコピーして使う。
class Dx12Texture2D
{
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	Dx12Texture2D() = default;
	~Dx12Texture2D() = default;

	Dx12Texture2D(const Dx12Texture2D&) = delete;
	Dx12Texture2D& operator=(const Dx12Texture2D&) = delete;
	Dx12Texture2D(Dx12Texture2D&&) noexcept = default;
	Dx12Texture2D& operator=(Dx12Texture2D&&) noexcept = default;

	/// @brief Texture (RGBA8) を GPU にアップロードして SRV を作る
	/// @param device       DX12 デバイス
	/// @param cmdList      recording 中の graphics command list
	/// @param src          RGBA8 ソース
	/// @param uploadOwner  upload heap の寿命管理用コンテナ
	///                     (frame 完了まで保持してね)
	/// @return true で成功
	bool uploadFrom(ID3D12Device* device,
	                ID3D12GraphicsCommandList* cmdList,
	                const Texture& src,
	                std::vector<ComPtr<ID3D12Resource>>& uploadOwner)
	{
		if (!device || !cmdList) return false;
		if (src.width() <= 0 || src.height() <= 0) return false;
		const auto& px = src.pixels();
		if (px.empty()) return false;

		m_width  = src.width();
		m_height = src.height();

		// ── 1. DEFAULT heap に 2D テクスチャを作る ───────────────────
		D3D12_HEAP_PROPERTIES texHp = {};
		texHp.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width            = static_cast<UINT64>(m_width);
		texDesc.Height           = static_cast<UINT>(m_height);
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels        = 1;
		texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		if (FAILED(device->CreateCommittedResource(
				&texHp, D3D12_HEAP_FLAG_NONE, &texDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
				IID_PPV_ARGS(m_texture.GetAddressOf()))))
		{
			return false;
		}

		// ── 2. UPLOAD heap (使い捨て) ───────────────────────────────
		const UINT rawRow = static_cast<UINT>(m_width) * 4u;
		const UINT alignedRow =
			(rawRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT64 uploadSize =
			static_cast<UINT64>(alignedRow) * static_cast<UINT64>(m_height);

		D3D12_HEAP_PROPERTIES upHp = {};
		upHp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC upDesc = {};
		upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		upDesc.Width            = uploadSize;
		upDesc.Height           = 1;
		upDesc.DepthOrArraySize = 1;
		upDesc.MipLevels        = 1;
		upDesc.Format           = DXGI_FORMAT_UNKNOWN;
		upDesc.SampleDesc.Count = 1;
		upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> upload;
		if (FAILED(device->CreateCommittedResource(
				&upHp, D3D12_HEAP_FLAG_NONE, &upDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(upload.GetAddressOf()))))
		{
			return false;
		}

		void* mapped = nullptr;
		D3D12_RANGE noRead{0, 0};
		if (FAILED(upload->Map(0, &noRead, &mapped))) return false;
		auto* dst = static_cast<std::uint8_t*>(mapped);
		const auto* srcBytes = px.data();
		for (int row = 0; row < m_height; ++row)
		{
			std::memcpy(dst + row * alignedRow,
			            srcBytes + row * rawRow, rawRow);
		}
		const D3D12_RANGE wroteAll{0, uploadSize};
		upload->Unmap(0, &wroteAll);

		// ── 3. CopyTextureRegion ────────────────────────────────────
		D3D12_TEXTURE_COPY_LOCATION cdst = {};
		cdst.pResource        = m_texture.Get();
		cdst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		cdst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION csrc = {};
		csrc.pResource                          = upload.Get();
		csrc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		csrc.PlacedFootprint.Offset             = 0;
		csrc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
		csrc.PlacedFootprint.Footprint.Width    = static_cast<UINT>(m_width);
		csrc.PlacedFootprint.Footprint.Height   = static_cast<UINT>(m_height);
		csrc.PlacedFootprint.Footprint.Depth    = 1;
		csrc.PlacedFootprint.Footprint.RowPitch = alignedRow;

		cmdList->CopyTextureRegion(&cdst, 0, 0, 0, &csrc, nullptr);

		// ── 4. barrier: COPY_DEST → PIXEL_SHADER_RESOURCE ────────────
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = m_texture.Get();
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmdList->ResourceBarrier(1, &b);

		// ── 5. upload heap を呼び出し側に保管してもらう ──────────────
		uploadOwner.push_back(std::move(upload));

		m_ready = true;
		return true;
	}

	/// @brief 既存の descriptor heap に SRV を生成する
	void createSRV(ID3D12Device* device,
	               D3D12_CPU_DESCRIPTOR_HANDLE dst) const
	{
		if (!m_texture) return;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels     = 1;
		device->CreateShaderResourceView(m_texture.Get(), &srv, dst);
	}

	[[nodiscard]] ID3D12Resource* nativeResource() const noexcept { return m_texture.Get(); }
	[[nodiscard]] int width() const noexcept  { return m_width; }
	[[nodiscard]] int height() const noexcept { return m_height; }
	[[nodiscard]] bool isReady() const noexcept { return m_ready; }

private:
	ComPtr<ID3D12Resource> m_texture;
	int  m_width  = 0;
	int  m_height = 0;
	bool m_ready  = false;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
