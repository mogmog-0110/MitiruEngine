#pragma once

/// @file Dx12OffscreenRT.hpp
/// @brief DX12オフスクリーンレンダーターゲット
/// @details バックバッファの内容をコピーし、
///          Viewportに表示するためのSRV付きテクスチャを管理する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace mitiru::gfx
{

/// @brief DX12オフスクリーンレンダーターゲット
/// @details バックバッファからCopyResourceでコピーし、
///          SRVを通じてViewportに表示する。
class Dx12OffscreenRT
{
public:
	/// @brief オフスクリーンRTを作成する
	/// @param device D3D12デバイス
	/// @param w テクスチャ幅
	/// @param h テクスチャ高さ
	/// @param format テクスチャフォーマット
	/// @param srvCpuHandle SRV作成先のCPUハンドル
	/// @param srvGpuHandle SRV作成先のGPUハンドル
	/// @return 成功時 true
	bool create(ID3D12Device* device, UINT w, UINT h,
	            DXGI_FORMAT format,
	            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
	            D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle)
	{
		if (!device || w == 0 || h == 0)
		{
			return false;
		}

		// サイズ変更不要ならスキップ
		if (m_texture && m_width == w && m_height == h)
		{
			return true;
		}

		release();
		m_width = w;
		m_height = h;
		m_srvGpuHandle = srvGpuHandle;

		// テクスチャ作成（コピー先 + SRV読み取り用）
		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Alignment = 0;
		texDesc.Width = w;
		texDesc.Height = h;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(m_texture.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}

		// SRVを作成する
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		device->CreateShaderResourceView(
			m_texture.Get(), &srvDesc, srvCpuHandle);

		m_valid = true;
		return true;
	}

	/// @brief バックバッファからオフスクリーンテクスチャにコピーする
	/// @param cmdList コマンドリスト（開いた状態であること）
	/// @param src コピー元リソース（バックバッファ、RT状態であること）
	/// @details ソース: RT → COPY_SRC → RT
	///          デスト: COMMON → COPY_DEST → PIXEL_SHADER_RESOURCE
	void copyFrom(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* src)
	{
		if (!cmdList || !src || !m_texture)
		{
			return;
		}

		D3D12_RESOURCE_BARRIER barriers[2] = {};

		// ソース: RENDER_TARGET → COPY_SOURCE
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Transition.pResource = src;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		// デスト: COMMON/PIXEL_SHADER_RESOURCE → COPY_DEST
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Transition.pResource = m_texture.Get();
		barriers[1].Transition.StateBefore = m_currentState;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		cmdList->ResourceBarrier(2, barriers);

		// コピー実行
		cmdList->CopyResource(m_texture.Get(), src);

		// ソース: COPY_SOURCE → RENDER_TARGET に戻す
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

		// デスト: COPY_DEST → PIXEL_SHADER_RESOURCE
		barriers[1].Transition.pResource = m_texture.Get();
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		cmdList->ResourceBarrier(2, barriers);

		m_currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	/// @brief 有効かどうか
	[[nodiscard]] bool isValid() const noexcept { return m_valid; }

	/// @brief テクスチャ幅
	[[nodiscard]] UINT width() const noexcept { return m_width; }

	/// @brief テクスチャ高さ
	[[nodiscard]] UINT height() const noexcept { return m_height; }

	/// @brief リソースを解放する
	void release()
	{
		m_texture.Reset();
		m_width = 0;
		m_height = 0;
		m_valid = false;
		m_currentState = D3D12_RESOURCE_STATE_COMMON;
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
	D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuHandle = {};
	UINT m_width = 0;
	UINT m_height = 0;
	bool m_valid = false;
	D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;
};

} // namespace mitiru::gfx

#endif // _WIN32
