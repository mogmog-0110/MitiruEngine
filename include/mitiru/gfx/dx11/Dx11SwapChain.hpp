#pragma once

/// @file Dx11SwapChain.hpp
/// @brief DirectX 11スワップチェーンラッパー
/// @details IDXGISwapChainをComPtrで管理し、画面表示とリサイズを提供する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <stdexcept>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/dx11/Dx11RenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11スワップチェーンラッパー
/// @details IDXGISwapChainの生成・プレゼント・リサイズを管理する。
class Dx11SwapChain final : public ISwapChain
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param hwnd ターゲットウィンドウハンドル
	/// @param width バッファ幅
	/// @param height バッファ高さ
	Dx11SwapChain(
		ID3D11Device* device,
		HWND hwnd,
		int width, int height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		/// DXGIファクトリの取得
		ComPtr<IDXGIDevice> dxgiDevice;
		HRESULT hr = device->QueryInterface(
			__uuidof(IDXGIDevice),
			reinterpret_cast<void**>(dxgiDevice.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: IDXGIDevice QueryInterface failed");
		}

		ComPtr<IDXGIAdapter> adapter;
		hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: GetAdapter failed");
		}

		ComPtr<IDXGIFactory> factory;
		hr = adapter->GetParent(
			__uuidof(IDXGIFactory),
			reinterpret_cast<void**>(factory.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: GetParent(IDXGIFactory) failed");
		}

		/// スワップチェーン記述子
		DXGI_SWAP_CHAIN_DESC desc = {};
		desc.BufferDesc.Width = static_cast<UINT>(width);
		desc.BufferDesc.Height = static_cast<UINT>(height);
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.OutputWindow = hwnd;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		hr = factory->CreateSwapChain(
			device, &desc, m_swapChain.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: CreateSwapChain failed");
		}

		/// バックバッファからレンダーターゲットを生成
		createRenderTarget();
	}

	/// @brief バックバッファを画面に表示する
	void present() override
	{
		/// VSync有効（引数1）でプレゼント
		HRESULT hr = m_swapChain->Present(1, 0);
		if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING)
		{
			/// デバイスロスト時はログのみ（クラッシュさせない）
		}
	}

	/// @brief スワップチェーンのサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(int width, int height) override
	{
		if (width <= 0 || height <= 0)
		{
			return;
		}

		m_width = width;
		m_height = height;

		/// 既存のレンダーターゲットを解放
		m_renderTarget.release();

		/// バッファのリサイズ
		HRESULT hr = m_swapChain->ResizeBuffers(
			0,
			static_cast<UINT>(width),
			static_cast<UINT>(height),
			DXGI_FORMAT_UNKNOWN,
			0);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: ResizeBuffers failed");
		}

		/// レンダーターゲットを再生成
		createRenderTarget();
	}

	/// @brief 現在のバックバッファのレンダーターゲットを取得する
	/// @return レンダーターゲットへのポインタ
	[[nodiscard]] IRenderTarget* backBuffer() noexcept override
	{
		return &m_renderTarget;
	}

	/// @brief レンダーターゲットビューを取得する
	/// @return RTV へのポインタ
	[[nodiscard]] ID3D11RenderTargetView* getRenderTargetView() const noexcept
	{
		return m_renderTarget.getRTV();
	}

	/// @brief 内部のIDXGISwapChainを取得する
	/// @return スワップチェーンへのポインタ
	[[nodiscard]] IDXGISwapChain* getSwapChain() const noexcept
	{
		return m_swapChain.Get();
	}

private:
	/// @brief バックバッファからレンダーターゲットを生成する
	void createRenderTarget()
	{
		ComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = m_swapChain->GetBuffer(
			0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(backBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11SwapChain: GetBuffer failed");
		}

		m_renderTarget = Dx11RenderTarget::createFromBackBuffer(
			m_device, backBuffer.Get(), m_width, m_height);
	}

	ID3D11Device* m_device = nullptr;               ///< D3D11デバイス（非所有）
	ComPtr<IDXGISwapChain> m_swapChain;             ///< DXGIスワップチェーン
	Dx11RenderTarget m_renderTarget;                 ///< バックバッファレンダーターゲット
	int m_width = 0;                                 ///< バッファ幅
	int m_height = 0;                                ///< バッファ高さ
};

} // namespace mitiru::gfx

#endif // _WIN32
