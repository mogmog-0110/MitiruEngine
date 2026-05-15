#pragma once

/// @file Dx12RenderTarget.hpp
/// @brief DirectX 12レンダーターゲット実装
/// @details ID3D12Resourceをレンダーターゲットとして管理し、
///          RTVデスクリプタとの関連付けを行うIRenderTarget実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/ITexture.hpp>
#include <mitiru/gfx/dx12/Dx12Texture.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12レンダーターゲット実装
/// @details スワップチェーンバックバッファまたはテクスチャベースのレンダーターゲット。
///          RTVデスクリプタハンドルを保持し、コマンドリストからの参照を提供する。
class Dx12RenderTarget final : public IRenderTarget
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	Dx12RenderTarget() = default;

	/// @brief スワップチェーンバックバッファからレンダーターゲットを生成する
	/// @param device D3D12デバイス
	/// @param resource バックバッファリソース
	/// @param rtvHandle RTVデスクリプタハンドル
	/// @param width バッファ幅
	/// @param height バッファ高さ
	/// @return 生成されたレンダーターゲット
	[[nodiscard]] static Dx12RenderTarget createFromBackBuffer(
		ID3D12Device* device,
		ID3D12Resource* resource,
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		int width, int height)
	{
		if (!device || !resource)
		{
			throw std::runtime_error(
				"Dx12RenderTarget: invalid device or resource");
		}

		Dx12RenderTarget rt;
		rt.m_resource = resource;
		rt.m_rtvHandle = rtvHandle;
		rt.m_width = width;
		rt.m_height = height;

		/// RTVを生成する
		device->CreateRenderTargetView(resource, nullptr, rtvHandle);

		return rt;
	}

	/// @brief レンダーターゲット幅を取得する
	[[nodiscard]] int width() const noexcept override
	{
		return m_width;
	}

	/// @brief レンダーターゲット高さを取得する
	[[nodiscard]] int height() const noexcept override
	{
		return m_height;
	}

	/// @brief 関連付けられたテクスチャを取得する
	/// @return テクスチャへのポインタ（バックバッファの場合はnullptr）
	[[nodiscard]] ITexture* texture() noexcept override
	{
		return m_texture;
	}

	/// @brief RTVデスクリプタハンドルを取得する
	/// @return D3D12 CPUデスクリプタハンドル
	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle() const noexcept
	{
		return m_rtvHandle;
	}

	/// @brief 内部のID3D12Resourceを取得する
	/// @return リソースへのポインタ（非所有）
	[[nodiscard]] ID3D12Resource* nativeResource() const noexcept
	{
		return m_resource;
	}

	/// @brief リソースをクリアする（リサイズ前に必要）
	void release() noexcept
	{
		m_resource = nullptr;
	}

private:
	ID3D12Resource* m_resource = nullptr;             ///< バックバッファリソース（非所有）
	D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle = {};     ///< RTVデスクリプタハンドル
	ITexture* m_texture = nullptr;                     ///< 関連テクスチャ（非所有）
	int m_width = 0;                                   ///< 幅
	int m_height = 0;                                  ///< 高さ
};

} // namespace mitiru::gfx

#endif // _WIN32
