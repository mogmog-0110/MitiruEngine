#pragma once

/// @file GameViewRT.hpp
/// @brief ゲーム描画結果をオフスクリーンRTにキャプチャする

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace mitiru::render
{

/// @brief オフスクリーンレンダーターゲット（ゲーム描画キャプチャ用）
/// @details ゲームのdraw()出力をオフスクリーンRTにリダイレクトし、
///          SRVを通じてViewportパネルに表示する。
///
/// @code
/// mitiru::render::GameViewRT rt;
/// rt.create(device, 1280, 720);
/// rt.beginCapture(ctx);
/// game->draw(screen);
/// rt.endCapture(ctx);
/// @endcode
class GameViewRT
{
public:
	/// @brief レンダーターゲットを作成する
	/// @param device DX11デバイス
	/// @param width 幅（ピクセル）
	/// @param height 高さ（ピクセル）
	/// @return 成功時true（サイズ変更なしで既に作成済みならtrueを即返却）
	bool create(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
	{
		if (m_width == width && m_height == height && m_rtv) return true;
		release();
		m_width = width;
		m_height = height;

		// レンダーターゲットテクスチャ作成
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf())))
		{
			return false;
		}
		if (FAILED(device->CreateRenderTargetView(tex.Get(), nullptr, m_rtv.GetAddressOf())))
		{
			return false;
		}
		if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, m_srv.GetAddressOf())))
		{
			return false;
		}
		return true;
	}

	/// @brief リソースを解放する
	void release()
	{
		m_rtv.Reset();
		m_srv.Reset();
	}

	/// @brief ゲーム描画のキャプチャを開始する
	/// @param ctx DX11デバイスコンテキスト
	/// @details 現在のRTを保存し、このRTに描画をリダイレクトする
	void beginCapture(ID3D11DeviceContext* ctx)
	{
		// 現在のRTを保存
		ctx->OMGetRenderTargets(1, m_prevRTV.GetAddressOf(), nullptr);
		// このRTに切り替え・クリア
		float clearColor[4] = {0.1f, 0.1f, 0.15f, 1.0f};
		ctx->ClearRenderTargetView(m_rtv.Get(), clearColor);
		ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
	}

	/// @brief ゲーム描画のキャプチャを終了する
	/// @param ctx DX11デバイスコンテキスト
	/// @details 元のRTを復元する
	void endCapture(ID3D11DeviceContext* ctx)
	{
		ctx->OMSetRenderTargets(1, m_prevRTV.GetAddressOf(), nullptr);
		m_prevRTV.Reset();
	}

	/// @brief SRVを取得する
	/// @return シェーダーリソースビュー
	[[nodiscard]] ID3D11ShaderResourceView* srv() const { return m_srv.Get(); }

	/// @brief 幅を取得する
	[[nodiscard]] std::uint32_t width() const noexcept { return m_width; }

	/// @brief 高さを取得する
	[[nodiscard]] std::uint32_t height() const noexcept { return m_height; }

private:
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_prevRTV;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
};

} // namespace mitiru::render

#endif // _WIN32
