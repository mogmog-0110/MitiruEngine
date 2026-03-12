#pragma once

/// @file Dx11Device.hpp
/// @brief DirectX 11デバイス実装
/// @details ID3D11DeviceとID3D11DeviceContextを管理し、
///          フレーム制御・ピクセル読み戻しを提供するIDevice実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11CommandList.hpp>
#include <mitiru/gfx/dx11/Dx11SwapChain.hpp>
#include <mitiru/gfx/dx11/Dx11Texture.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11デバイス実装
/// @details D3D11デバイス・コンテキスト・スワップチェーンを統合管理する。
///          beginFrame()でクリア、endFrame()でプレゼントを行う。
class Dx11Device final : public IDevice
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param window Win32ウィンドウ（HWNDの取得に使用）
	explicit Dx11Device(mitiru::Win32Window* window)
	{
		if (!window)
		{
			throw std::runtime_error(
				"Dx11Device: Win32Window is null");
		}

		createDevice();
		m_swapChain = std::make_unique<Dx11SwapChain>(
			m_device.Get(),
			window->getHandle(),
			window->width(),
			window->height());
	}

	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		if (!m_swapChain)
		{
			return {};
		}

		/// バックバッファの取得
		ComPtr<ID3D11Texture2D> backBuffer;
		HRESULT hr = m_swapChain->getSwapChain()->GetBuffer(
			0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(backBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			return {};
		}

		/// ステージングテクスチャの生成
		auto staging = Dx11Texture::createStaging(
			m_device.Get(), width, height);

		/// バックバッファからステージングテクスチャへコピー
		m_context->CopyResource(
			staging.getTexture(), backBuffer.Get());

		/// ステージングテクスチャをマップしてCPUから読み取る
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = m_context->Map(
			staging.getTexture(), 0,
			D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr))
		{
			return {};
		}

		/// ピクセルデータをコピー
		const auto pixelCount =
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height);
		std::vector<std::uint8_t> pixels(pixelCount * 4);

		const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
		const auto rowBytes = static_cast<std::size_t>(width) * 4;

		for (int y = 0; y < height; ++y)
		{
			std::memcpy(
				pixels.data() + static_cast<std::size_t>(y) * rowBytes,
				src + static_cast<std::size_t>(y) * mapped.RowPitch,
				rowBytes);
		}

		m_context->Unmap(staging.getTexture(), 0);

		return pixels;
	}

	/// @brief アクティブなバックエンドを取得する
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::Dx11;
	}

	/// @brief フレーム開始処理
	/// @details レンダーターゲットをクリアする。
	void beginFrame() override
	{
		if (!m_swapChain)
		{
			return;
		}

		auto* rtv = m_swapChain->getRenderTargetView();
		if (rtv)
		{
			/// コーンフラワーブルーでクリア
			constexpr float clearColor[4] = {
				0.392f, 0.584f, 0.929f, 1.0f
			};
			m_context->ClearRenderTargetView(rtv, clearColor);
			m_context->OMSetRenderTargets(1, &rtv, nullptr);
		}
	}

	/// @brief フレーム終了・プレゼント処理
	void endFrame() override
	{
		if (m_swapChain)
		{
			m_swapChain->present();
		}
	}

	/// @brief 内部のD3D11デバイスを取得する
	/// @return ID3D11Deviceへのポインタ
	[[nodiscard]] ID3D11Device* getD3DDevice() const noexcept
	{
		return m_device.Get();
	}

	/// @brief 内部のD3D11デバイスコンテキストを取得する
	/// @return ID3D11DeviceContextへのポインタ
	[[nodiscard]] ID3D11DeviceContext* getD3DContext() const noexcept
	{
		return m_context.Get();
	}

	/// @brief スワップチェーンを取得する
	/// @return Dx11SwapChainへのポインタ
	[[nodiscard]] Dx11SwapChain* getSwapChain() const noexcept
	{
		return m_swapChain.get();
	}

	/// @brief GPUバッファを生成する
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptrで初期化なし）
	/// @return 生成されたバッファ
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override
	{
		return std::make_unique<Dx11Buffer>(
			m_device.Get(), bufferType, sizeBytes,
			dynamic, initialData);
	}

	/// @brief コマンドリストを生成する
	/// @return 生成されたコマンドリスト
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<Dx11CommandList>(m_context.Get());
	}

private:
	/// @brief D3D11デバイスとコンテキストを生成する
	void createDevice()
	{
		/// フィーチャーレベル（DX11.0を要求）
		constexpr D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		UINT createFlags = 0;
#ifdef _DEBUG
		createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_FEATURE_LEVEL actualLevel = {};
		HRESULT hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createFlags,
			featureLevels,
			static_cast<UINT>(std::size(featureLevels)),
			D3D11_SDK_VERSION,
			m_device.GetAddressOf(),
			&actualLevel,
			m_context.GetAddressOf());

		/// ハードウェアが失敗した場合はWARPにフォールバック
		if (FAILED(hr))
		{
			hr = D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				createFlags,
				featureLevels,
				static_cast<UINT>(std::size(featureLevels)),
				D3D11_SDK_VERSION,
				m_device.GetAddressOf(),
				&actualLevel,
				m_context.GetAddressOf());
		}

		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Device: D3D11CreateDevice failed");
		}
	}

	ComPtr<ID3D11Device> m_device;                   ///< D3D11デバイス
	ComPtr<ID3D11DeviceContext> m_context;            ///< D3D11即時コンテキスト
	std::unique_ptr<Dx11SwapChain> m_swapChain;      ///< スワップチェーン
};

} // namespace mitiru::gfx

#endif // _WIN32
