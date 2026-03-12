#pragma once

/// @file Dx11RenderTarget.hpp
/// @brief DirectX 11レンダーターゲット
/// @details ID3D11RenderTargetViewをComPtrで管理する。
///          スワップチェーンのバックバッファまたは独立テクスチャから生成する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <stdexcept>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11レンダーターゲット
/// @details スワップチェーンバックバッファまたはテクスチャベースのレンダーターゲット。
class Dx11RenderTarget final : public IRenderTarget
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief スワップチェーンバックバッファからレンダーターゲットを生成する
	/// @param device D3D11デバイス
	/// @param backBuffer バックバッファテクスチャ
	/// @param width バッファ幅
	/// @param height バッファ高さ
	/// @return 生成されたレンダーターゲット
	[[nodiscard]] static Dx11RenderTarget createFromBackBuffer(
		ID3D11Device* device,
		ID3D11Texture2D* backBuffer,
		int width, int height)
	{
		Dx11RenderTarget rt;
		rt.m_width = width;
		rt.m_height = height;

		HRESULT hr = device->CreateRenderTargetView(
			backBuffer, nullptr, rt.m_rtv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateRenderTargetView failed");
		}

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
	/// @return バックバッファベースの場合はnullptr
	[[nodiscard]] ITexture* texture() noexcept override
	{
		return nullptr;
	}

	/// @brief 内部のID3D11RenderTargetViewを取得する
	/// @return RTV へのポインタ
	[[nodiscard]] ID3D11RenderTargetView* getRTV() const noexcept
	{
		return m_rtv.Get();
	}

	/// @brief RTVを解放する（リサイズ前に必要）
	void release() noexcept
	{
		m_rtv.Reset();
	}

private:
	ComPtr<ID3D11RenderTargetView> m_rtv;  ///< レンダーターゲットビュー
	int m_width = 0;                        ///< 幅
	int m_height = 0;                       ///< 高さ
};

} // namespace mitiru::gfx

#endif // _WIN32
