#pragma once

/// @file Dx12SwapChain.hpp
/// @brief DirectX 12スワップチェーン実装
/// @details IDXGISwapChain3をラップし、トリプルバッファリングとフレーム同期を管理する
///          ISwapChain実装。

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
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12スワップチェーン実装
/// @details IDXGISwapChain3によるトリプルバッファリングを管理する。
///          present()でフェンスシグナルとフレームインデックスの更新を行う。
class Dx12SwapChain final : public ISwapChain
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief トリプルバッファリングのフレーム数
	static constexpr uint32_t FRAME_COUNT = 3;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param commandQueue コマンドキュー
	/// @param factory DXGIファクトリ
	/// @param hwnd ターゲットウィンドウハンドル
	/// @param width バッファ幅
	/// @param height バッファ高さ
	Dx12SwapChain(ID3D12Device* device,
	              ID3D12CommandQueue* commandQueue,
	              IDXGIFactory4* factory,
	              HWND hwnd,
	              int width, int height)
		: m_device(device)
		, m_commandQueue(commandQueue)
		, m_width(width)
		, m_height(height)
	{
		if (!device || !commandQueue || !factory || !hwnd)
		{
			throw std::runtime_error(
				"Dx12SwapChain: invalid parameters");
		}

		createSwapChain(factory, hwnd, width, height);
		createRenderTargets();
	}

	/// @brief バックバッファを画面に表示する
	void present() override
	{
		HRESULT hr = m_swapChain->Present(1, 0);
		if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING)
		{
			/// デバイスロスト時はログのみ（クラッシュさせない）
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
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

		/// 既存のレンダーターゲットを解放する
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			m_renderTargets[i].release();
			m_backBuffers[i].Reset();
		}

		/// バッファのリサイズ
		HRESULT hr = m_swapChain->ResizeBuffers(
			FRAME_COUNT,
			static_cast<UINT>(width),
			static_cast<UINT>(height),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12SwapChain: ResizeBuffers failed");
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		/// レンダーターゲットを再生成する
		createRenderTargets();
	}

	/// @brief 現在のバックバッファのレンダーターゲットを取得する
	/// @return レンダーターゲットへのポインタ
	[[nodiscard]] IRenderTarget* backBuffer() noexcept override
	{
		return &m_renderTargets[m_frameIndex];
	}

	/// @brief 現在のバックバッファインデックスを取得する
	/// @return バックバッファインデックス
	[[nodiscard]] uint32_t currentBackBufferIndex() const override
	{
		return m_frameIndex;
	}

	/// @brief バックバッファ数を取得する
	/// @return トリプルバッファリングのバッファ数（3）
	[[nodiscard]] uint32_t bufferCount() const override
	{
		return FRAME_COUNT;
	}

	/// @brief 指定インデックスのバックバッファリソースを取得する
	/// @param index バッファインデックス
	/// @return ID3D12Resourceへのポインタ
	[[nodiscard]] ID3D12Resource* getBackBufferResource(uint32_t index) const noexcept
	{
		if (index < FRAME_COUNT)
		{
			return m_backBuffers[index].Get();
		}
		return nullptr;
	}

	/// @brief 内部のIDXGISwapChain3を取得する
	/// @return スワップチェーンへのポインタ
	[[nodiscard]] IDXGISwapChain3* getSwapChain() const noexcept
	{
		return m_swapChain.Get();
	}

private:
	/// @brief スワップチェーンを生成する
	/// @param factory DXGIファクトリ
	/// @param hwnd ターゲットウィンドウ
	/// @param width バッファ幅
	/// @param height バッファ高さ
	void createSwapChain(IDXGIFactory4* factory, HWND hwnd,
	                     int width, int height)
	{
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = FRAME_COUNT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		ComPtr<IDXGISwapChain1> swapChain1;
		HRESULT hr = factory->CreateSwapChainForHwnd(
			m_commandQueue,
			hwnd,
			&desc,
			nullptr,
			nullptr,
			swapChain1.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12SwapChain: CreateSwapChainForHwnd failed");
		}

		/// ALT+Enterのフルスクリーン切り替えを無効化する
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

		/// IDXGISwapChain3にキャストする
		hr = swapChain1.As(&m_swapChain);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12SwapChain: QueryInterface IDXGISwapChain3 failed");
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	/// @brief バックバッファからレンダーターゲットを生成する
	void createRenderTargets()
	{
		/// RTVデスクリプタヒープの生成
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FRAME_COUNT;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT hr = m_device->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(m_rtvHeap.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12SwapChain: CreateDescriptorHeap (RTV) failed");
		}

		const UINT rtvDescriptorSize =
			m_device->GetDescriptorHandleIncrementSize(
				D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		auto rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			hr = m_swapChain->GetBuffer(
				i, IID_PPV_ARGS(m_backBuffers[i].GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"Dx12SwapChain: GetBuffer failed");
			}

			m_renderTargets[i] = Dx12RenderTarget::createFromBackBuffer(
				m_device,
				m_backBuffers[i].Get(),
				rtvHandle,
				m_width, m_height);

			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	ID3D12Device* m_device = nullptr;                    ///< D3D12デバイス（非所有）
	ID3D12CommandQueue* m_commandQueue = nullptr;         ///< コマンドキュー（非所有）
	ComPtr<IDXGISwapChain3> m_swapChain;                  ///< DXGIスワップチェーン
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;              ///< RTVデスクリプタヒープ
	ComPtr<ID3D12Resource> m_backBuffers[FRAME_COUNT];   ///< バックバッファリソース
	Dx12RenderTarget m_renderTargets[FRAME_COUNT];       ///< レンダーターゲット
	uint32_t m_frameIndex = 0;                           ///< 現在のフレームインデックス
	int m_width = 0;                                      ///< バッファ幅
	int m_height = 0;                                     ///< バッファ高さ
};

} // namespace mitiru::gfx

#endif // _WIN32
