#pragma once

/// @file Dx11OffscreenRT.hpp
/// @brief DX11オフスクリーンレンダーターゲット
/// @details ゲーム描画をオフスクリーンテクスチャにリダイレクトし、
///          そのSRVをImGui::Image()で表示するために使用する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <wrl/client.h>

namespace mitiru::gfx
{

/// @brief DX11オフスクリーンレンダーターゲット
/// @details Texture2D + RTV + SRV を管理する。
///          create()でリソース作成、bind()でRTVをセット、
///          srv()でImGui::Image()に渡すSRVを取得する。
class Dx11OffscreenRT
{
public:
	/// @brief オフスクリーンRTを作成する
	/// @param device DX11デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @return 成功時 true
	bool create(ID3D11Device* device, unsigned int width, unsigned int height)
	{
		if (!device || width == 0 || height == 0)
		{
			return false;
		}

		// サイズ変更不要ならスキップ
		if (m_texture && m_width == width && m_height == height)
		{
			return true;
		}

		release();
		m_width = width;
		m_height = height;

		// テクスチャ作成
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = width;
		texDesc.Height = height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, m_texture.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}

		// RTV作成
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		hr = device->CreateRenderTargetView(m_texture.Get(), &rtvDesc, m_rtv.GetAddressOf());
		if (FAILED(hr))
		{
			release();
			return false;
		}

		// SRV作成
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srv.GetAddressOf());
		if (FAILED(hr))
		{
			release();
			return false;
		}

		// 深度バッファ作成
		D3D11_TEXTURE2D_DESC depthDesc = {};
		depthDesc.Width = width;
		depthDesc.Height = height;
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 1;
		depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.SampleDesc.Quality = 0;
		depthDesc.Usage = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTex;
		hr = device->CreateTexture2D(&depthDesc, nullptr, depthTex.GetAddressOf());
		if (FAILED(hr))
		{
			release();
			return false;
		}

		hr = device->CreateDepthStencilView(depthTex.Get(), nullptr, m_dsv.GetAddressOf());
		if (FAILED(hr))
		{
			release();
			return false;
		}

		return true;
	}

	/// @brief RTVをコンテキストにバインドし、クリアする
	/// @param context DX11デバイスコンテキスト
	/// @param clearColor クリア色 (RGBA)
	void bind(ID3D11DeviceContext* context, const float clearColor[4] = nullptr)
	{
		if (!context || !m_rtv)
		{
			return;
		}

		const float defaultClear[4] = {0.08f, 0.08f, 0.12f, 1.0f};
		const float* color = clearColor ? clearColor : defaultClear;

		context->ClearRenderTargetView(m_rtv.Get(), color);
		context->ClearDepthStencilView(m_dsv.Get(),
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		ID3D11RenderTargetView* rtvs[] = {m_rtv.Get()};
		context->OMSetRenderTargets(1, rtvs, m_dsv.Get());

		// ビューポート設定
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
	}

	/// @brief RTVバインドを解除する（バックバッファに戻す前に呼ぶ）
	/// @param context DX11デバイスコンテキスト
	void unbind(ID3D11DeviceContext* context)
	{
		if (!context)
		{
			return;
		}

		// SRVとして使用する前にRTV参照を解除する
		ID3D11RenderTargetView* nullRTV[] = {nullptr};
		context->OMSetRenderTargets(1, nullRTV, nullptr);
	}

	/// @brief SRVを取得する（ImGui::Image()に渡す用）
	/// @return ID3D11ShaderResourceView* をvoid*にキャストしたもの
	[[nodiscard]] void* srv() const noexcept
	{
		return static_cast<void*>(m_srv.Get());
	}

	/// @brief RTVを取得する
	[[nodiscard]] ID3D11RenderTargetView* rtv() const noexcept
	{
		return m_rtv.Get();
	}

	/// @brief テクスチャ幅
	[[nodiscard]] unsigned int width() const noexcept { return m_width; }

	/// @brief テクスチャ高さ
	[[nodiscard]] unsigned int height() const noexcept { return m_height; }

	/// @brief 有効かどうか
	[[nodiscard]] bool isValid() const noexcept
	{
		return m_texture && m_rtv && m_srv;
	}

	/// @brief リソースを解放する
	void release()
	{
		m_srv.Reset();
		m_rtv.Reset();
		m_dsv.Reset();
		m_texture.Reset();
		m_width = 0;
		m_height = 0;
	}

private:
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;
	unsigned int m_width = 0;
	unsigned int m_height = 0;
};

} // namespace mitiru::gfx

#endif // _WIN32
