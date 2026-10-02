#pragma once

/// @file Dx11RenderTarget.hpp
/// @brief DirectX 11 レンダーターゲット
/// @details ID3D11RenderTargetView を ComPtr で管理する。
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

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief gfx::PixelFormat を DXGI_FORMAT に変換する
/// @return 対応フォーマットが無い場合は DXGI_FORMAT_UNKNOWN
/// @details Depth24Stencil8 は RTV+SRV 兼用テクスチャとして作れない（D3D11_BIND_DEPTH_STENCIL が要る）ため未対応。
[[nodiscard]] inline DXGI_FORMAT toDxgiFormat(PixelFormat format) noexcept
{
	switch (format)
	{
	case PixelFormat::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case PixelFormat::BGRA8: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case PixelFormat::R8: return DXGI_FORMAT_R8_UNORM;
	case PixelFormat::Depth24Stencil8: return DXGI_FORMAT_UNKNOWN;
	case PixelFormat::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
	}
	return DXGI_FORMAT_UNKNOWN;
}

/// @brief DirectX 11 レンダーターゲット
/// @details スワップチェーンバックバッファまたはテクスチャベースのレンダーターゲット。
class Dx11RenderTarget final : public IRenderTarget
{
public:
	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief スワップチェーンバックバッファからレンダーターゲットを生成する
	/// @param device D3D11 デバイス
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

	/// @brief 独立テクスチャからレンダーターゲットを生成する（RenderTargetPool/IDevice::createRenderTarget 用）
	/// @details バックバッファ版と違い RTV に加えて SRV も持つ（ポストプロセス等の中間 RT として読み戻せるように）。
	/// @param device D3D11 デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param format DXGI ピクセルフォーマット
	/// @return 生成されたレンダーターゲット
	[[nodiscard]] static Dx11RenderTarget createTexture(
		ID3D11Device* device, int width, int height, DXGI_FORMAT format)
	{
		Dx11RenderTarget rt;
		rt.m_width = width;
		rt.m_height = height;

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = static_cast<UINT>(width);
		texDesc.Height = static_cast<UINT>(height);
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		HRESULT hr = device->CreateTexture2D(
			&texDesc, nullptr, rt.m_ownedTexture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateTexture2D failed");
		}

		hr = device->CreateRenderTargetView(
			rt.m_ownedTexture.Get(), nullptr, rt.m_rtv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateRenderTargetView failed");
		}

		hr = device->CreateShaderResourceView(
			rt.m_ownedTexture.Get(), nullptr, rt.m_srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateShaderResourceView failed");
		}

		return rt;
	}

	/// @brief depth-only レンダーターゲット（DSV + SRV）を生成する（`PixelFormat::Depth24Stencil8` 用）
	/// @details D3D11 では DSV と RTV を同一テクスチャで兼用できないため、typeless
	///          テクスチャ (`R24G8_TYPELESS`) から DSV (`D24_UNORM_S8_UINT`) と
	///          深度チャンネルのみの SRV (`R24_UNORM_X8_TYPELESS`) を別ビューとして作る。
	/// @param device D3D11 デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @return 生成されたレンダーターゲット（`getRTV()` は常に nullptr、`getDSV()`/`getSRV()` が有効）
	[[nodiscard]] static Dx11RenderTarget createDepthTexture(
		ID3D11Device* device, int width, int height)
	{
		Dx11RenderTarget rt;
		rt.m_width = width;
		rt.m_height = height;

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = static_cast<UINT>(width);
		texDesc.Height = static_cast<UINT>(height);
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

		HRESULT hr = device->CreateTexture2D(
			&texDesc, nullptr, rt.m_ownedTexture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateTexture2D (depth) failed");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		hr = device->CreateDepthStencilView(
			rt.m_ownedTexture.Get(), &dsvDesc, rt.m_dsv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateDepthStencilView failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		hr = device->CreateShaderResourceView(
			rt.m_ownedTexture.Get(), &srvDesc, rt.m_srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11RenderTarget: CreateShaderResourceView (depth) failed");
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
	/// @return バックバッファベースの場合は nullptr
	[[nodiscard]] ITexture* texture() noexcept override
	{
		return nullptr;
	}

	/// @brief バックエンド固有の RTV を取得する（IRenderTarget 経由）
	/// @return createTexture 由来なら ID3D11RenderTargetView*、バックバッファ版も含め常に有効
	[[nodiscard]] void* nativeRtv() noexcept override
	{
		return m_rtv.Get();
	}

	/// @brief バックエンド固有の SRV を取得する（IRenderTarget 経由）
	/// @return createTexture / createDepthTexture 由来のみ有効。バックバッファ版は nullptr（SRV を持たない）
	[[nodiscard]] void* nativeSrv() noexcept override
	{
		return m_srv.Get();
	}

	/// @brief バックエンド固有の DSV を取得する（IRenderTarget 経由）
	/// @return createDepthTexture 由来のみ有効
	[[nodiscard]] void* nativeDsv() noexcept override
	{
		return m_dsv.Get();
	}

	/// @brief 内部の ID3D11RenderTargetView を取得する
	/// @return RTV へのポインタ
	[[nodiscard]] ID3D11RenderTargetView* getRTV() const noexcept
	{
		return m_rtv.Get();
	}

	/// @brief 内部の ID3D11ShaderResourceView を取得する
	/// @return SRV へのポインタ（バックバッファ版は nullptr）
	[[nodiscard]] ID3D11ShaderResourceView* getSRV() const noexcept
	{
		return m_srv.Get();
	}

	/// @brief 内部の ID3D11DepthStencilView を取得する
	/// @return createDepthTexture 由来のみ有効、それ以外は nullptr
	[[nodiscard]] ID3D11DepthStencilView* getDSV() const noexcept
	{
		return m_dsv.Get();
	}

	/// @brief RTV/DSV を解放する（リサイズ前に必要）
	void release() noexcept
	{
		m_rtv.Reset();
		m_srv.Reset();
		m_dsv.Reset();
		m_ownedTexture.Reset();
	}

private:
	ComPtr<ID3D11Texture2D> m_ownedTexture;      ///< createTexture/createDepthTexture 由来のテクスチャ実体（バックバッファ版では未使用）
	ComPtr<ID3D11RenderTargetView> m_rtv;        ///< レンダーターゲットビュー（depth-only RT では未使用）
	ComPtr<ID3D11ShaderResourceView> m_srv;      ///< シェーダーリソースビュー（バックバッファ版では未使用）
	ComPtr<ID3D11DepthStencilView> m_dsv;        ///< 深度ステンシルビュー（createDepthTexture 由来のみ）
	int m_width = 0;                              ///< 幅
	int m_height = 0;                             ///< 高さ
};

} // namespace mitiru::gfx

#endif // _WIN32
