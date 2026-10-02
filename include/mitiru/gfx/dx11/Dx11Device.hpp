#pragma once

/// @file Dx11Device.hpp
/// @brief DirectX 11 デバイス実装
/// @details ID3D11Device と ID3D11DeviceContext を管理し、
///          フレーム制御・ピクセル読み戻しを提供する IDevice 実装。

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

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11CommandList.hpp>
#include <mitiru/gfx/dx11/Dx11RenderTarget.hpp>
#include <mitiru/gfx/dx11/Dx11SwapChain.hpp>
#include <mitiru/gfx/dx11/Dx11Texture.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#include <mitiru/render/PostProcessIntegration.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11 デバイス実装
/// @details D3D11 デバイス・コンテキスト・スワップチェーンを統合管理する。
///          beginFrame() でクリア、endFrame() でプレゼントを行う。
class Dx11Device final : public IDevice
{
public:
	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param window Win32 ウィンドウ（HWND の取得に使用）
	/// @param bufferWidth バックバッファ幅（0=ウィンドウサイズに合わせる）
	/// @param bufferHeight バックバッファ高さ（0=ウィンドウサイズに合わせる）
	explicit Dx11Device(mitiru::Win32Window* window,
	                     int bufferWidth = 0, int bufferHeight = 0)
	{
		if (!window)
		{
			throw std::runtime_error(
				"Dx11Device: Win32Window is null");
		}

		createDevice();

		// バッファサイズ: 指定があればそれを使用、なければウィンドウサイズ
		const int bw = (bufferWidth > 0) ? bufferWidth : window->width();
		const int bh = (bufferHeight > 0) ? bufferHeight : window->height();

		m_swapChain = std::make_unique<Dx11SwapChain>(
			m_device.Get(),
			window->getHandle(),
			bw, bh);
		initGpuTimer();
	}

	/// @brief windowless (offscreen) コンストラクタ (G2)。ウィンドウ / スワップチェーンを
	///        持たず、`width`×`height` のテクスチャ 1 枚を RTV+SRV 付きで作って描画先にする。
	///        `--headless` で 3D を描いて `readPixels()` で読み戻したい (`--capture-dir`) が、
	///        実ウィンドウは要らない用途向け。present() は no-op (呼び出し側の endFrame が
	///        m_swapChain の有無で分岐済み)。
	explicit Dx11Device(int width, int height)
	{
		if (width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"Dx11Device: windowless width/height must be positive");
		}

		createDevice();

		m_offscreenTarget = std::make_unique<Dx11RenderTarget>(
			Dx11RenderTarget::createTexture(
				m_device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM));
		initGpuTimer();
	}

	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8 形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		ComPtr<ID3D11Texture2D> source;

		if (m_swapChain)
		{
			/// バックバッファの取得
			ComPtr<ID3D11Texture2D> backBuffer;
			HRESULT hr = m_swapChain->getSwapChain()->GetBuffer(
				0, __uuidof(ID3D11Texture2D),
				reinterpret_cast<void**>(backBuffer.GetAddressOf()));
			if (FAILED(hr))
			{
				return {};
			}

			/// MSAA バックバッファの場合はまず非 MSAA に解決する
			D3D11_TEXTURE2D_DESC bbDesc = {};
			backBuffer->GetDesc(&bbDesc);
			source = backBuffer;
			if (bbDesc.SampleDesc.Count > 1)
			{
				D3D11_TEXTURE2D_DESC resolvedDesc = bbDesc;
				resolvedDesc.SampleDesc.Count = 1;
				resolvedDesc.SampleDesc.Quality = 0;
				resolvedDesc.Usage = D3D11_USAGE_DEFAULT;
				resolvedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				resolvedDesc.CPUAccessFlags = 0;
				resolvedDesc.MiscFlags = 0;
				ComPtr<ID3D11Texture2D> resolved;
				if (FAILED(m_device->CreateTexture2D(
						&resolvedDesc, nullptr, resolved.GetAddressOf())))
				{
					return {};
				}
				m_context->ResolveSubresource(
					resolved.Get(), 0, backBuffer.Get(), 0, bbDesc.Format);
				source = resolved;
			}
		}
		else if (m_offscreenTarget)
		{
			/// windowless (G2): RTV から実体の Resource を辿ってテクスチャを得る
			/// (Dx11RenderTarget は生テクスチャポインタを外に出さないため)。
			ID3D11RenderTargetView* rtv = m_offscreenTarget->getRTV();
			if (!rtv)
			{
				return {};
			}
			ComPtr<ID3D11Resource> resource;
			rtv->GetResource(resource.GetAddressOf());
			if (!resource || FAILED(resource.As(&source)))
			{
				return {};
			}
		}
		else
		{
			return {};
		}

		/// ステージングテクスチャの生成
		auto staging = Dx11Texture::createStaging(
			m_device.Get(), width, height);

		/// バックバッファからステージングテクスチャへコピー
		m_context->CopyResource(
			staging.getTexture(), source.Get());

		/// ステージングテクスチャをマップして CPU から読み取る
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT hr = m_context->Map(
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
		// windowless (G2) では m_swapChain の代わりにオフスクリーンテクスチャの RTV へ描く。
		ID3D11RenderTargetView* rtv = m_swapChain
			? m_swapChain->getRenderTargetView()
			: (m_offscreenTarget ? m_offscreenTarget->getRTV() : nullptr);
		if (!rtv)
		{
			return;
		}

		// GPU タイムスタンプ: フレーム開始
		if (m_gpuTimerReady && m_gpuTimestampBegin)
		{
			m_context->End(m_gpuTimestampBegin.Get());
		}

		m_context->ClearRenderTargetView(rtv, m_clearColor);
		m_context->OMSetRenderTargets(1, &rtv, nullptr);
	}

	/// @brief フレーム終了・プレゼント処理
	void endFrame() override
	{
		// GPU タイムスタンプ: フレーム終了 + 周波数取得
		if (m_gpuTimerReady && m_gpuTimestampEnd && m_gpuTimestampDisjoint)
		{
			m_context->End(m_gpuTimestampEnd.Get());
			m_context->End(m_gpuTimestampDisjoint.Get());

			// 前フレームの結果を取得（1 フレーム遅延）
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
			UINT64 tsBegin = 0, tsEnd = 0;

			if (m_context->GetData(m_gpuTimestampDisjoint.Get(), &disjointData,
				sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
				&& !disjointData.Disjoint
				&& m_context->GetData(m_gpuTimestampBegin.Get(), &tsBegin,
					sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
				&& m_context->GetData(m_gpuTimestampEnd.Get(), &tsEnd,
					sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
			{
				const double freq = static_cast<double>(disjointData.Frequency);
				if (freq > 0.0)
				{
					m_gpuFrameMs = static_cast<float>(
						static_cast<double>(tsEnd - tsBegin) / freq * 1000.0);
				}
			}

			// 次フレーム用に Disjoint クエリを再開
			m_context->Begin(m_gpuTimestampDisjoint.Get());
		}

		if (m_swapChain)
		{
			m_swapChain->present();
		}
	}

	/// @brief GPU フレーム時間（ミリ秒）を取得する
	[[nodiscard]] float gpuFrameMs() const noexcept { return m_gpuFrameMs; }

	/// @brief 内部の D3D11 デバイスを取得する
	/// @return ID3D11Device へのポインタ
	[[nodiscard]] ID3D11Device* getD3DDevice() const noexcept
	{
		return m_device.Get();
	}

	/// @brief 内部の D3D11 デバイスコンテキストを取得する
	/// @return ID3D11DeviceContext へのポインタ
	[[nodiscard]] ID3D11DeviceContext* getD3DContext() const noexcept
	{
		return m_context.Get();
	}

	/// @brief スワップチェーンを取得する
	/// @return Dx11SwapChain へのポインタ
	[[nodiscard]] Dx11SwapChain* getSwapChain() const noexcept
	{
		return m_swapChain.get();
	}

	/// @brief 現在の描画先 RTV を取得する (`m_swapChain` があればそれ、windowless (G2) なら
	///        `m_offscreenTarget`)。`Renderer3D` (3D パイプライン) が `getSwapChain()==nullptr`
	///        で早期 return し windowless では何も描かれなかった問題を解消する統一アクセサ。
	[[nodiscard]] ID3D11RenderTargetView* currentRenderTargetView() const noexcept
	{
		if (m_swapChain) { return m_swapChain->getRenderTargetView(); }
		return m_offscreenTarget ? m_offscreenTarget->getRTV() : nullptr;
	}

	/// @brief GPU バッファを生成する
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptr で初期化なし）
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

	/// @brief 独立したレンダーターゲットを生成する（RenderTargetPool 用）
	/// @details Depth24Stencil8 は RTV ではなく DSV+SRV の depth-only テクスチャとして作る
	///          （`IRenderTarget::nativeDsv()` 経由でバインドする、`nativeRtv()` は常に nullptr）。
	[[nodiscard]] std::unique_ptr<IRenderTarget> createRenderTarget(
		const RenderTargetDesc& desc) override
	{
		if (desc.format == PixelFormat::Depth24Stencil8)
		{
			return std::make_unique<Dx11RenderTarget>(
				Dx11RenderTarget::createDepthTexture(
					m_device.Get(), desc.width, desc.height));
		}

		const DXGI_FORMAT fmt = toDxgiFormat(desc.format);
		if (fmt == DXGI_FORMAT_UNKNOWN)
		{
			debug::warnOnceFix("gfx.dx11.create_render_target.unsupported_format",
				"Dx11Device::createRenderTarget: 未対応の PixelFormat が指定された",
				"toDxgiFormat に対応表がない PixelFormat を渡した",
				"対応済みの PixelFormat を使うか、toDxgiFormat の対応表に追加する");
			return nullptr;
		}

		return std::make_unique<Dx11RenderTarget>(
			Dx11RenderTarget::createTexture(
				m_device.Get(), desc.width, desc.height, fmt));
	}

	/// @brief ウィンドウリサイズに対応する
	/// @param w 新しいクライアント領域幅
	/// @param h 新しいクライアント領域高さ
	void onResize(int w, int h) override
	{
		if (w <= 0 || h <= 0) return;
		/// RTV の参照を解除してからリサイズする
		if (m_context)
		{
			ID3D11RenderTargetView* nullRTV[] = {nullptr};
			m_context->OMSetRenderTargets(1, nullRTV, nullptr);
		}
		if (m_swapChain)
		{
			m_swapChain->resize(w, h);
		}
	}

	/// @brief 3D 描画後に 2D 描画用にレンダーターゲットをリセットする
	void resetRenderTargetFor2D() override
	{
		if (!m_context) return;
		// windowless (G2) では m_swapChain の代わりにオフスクリーンテクスチャの RTV へ戻す。
		auto* rtv = m_swapChain
			? m_swapChain->getRenderTargetView()
			: (m_offscreenTarget ? m_offscreenTarget->getRTV() : nullptr);
		if (rtv)
		{
			// 深度バッファなしでレンダーターゲットを再設定
			m_context->OMSetRenderTargets(1, &rtv, nullptr);
			// 深度ステンシルステートを無効化
			m_context->OMSetDepthStencilState(nullptr, 0);
		}
	}

	/// @brief ポストプロセスマネージャーを設定する
	void setPostProcessManager(void* pp) override
	{
		m_postProcessManager = static_cast<render::PostProcessManager*>(pp);
	}

	/// @brief ポストプロセスのオフスクリーン RT への描画を開始する
	void beginPostProcess() override
	{
		if (m_postProcessManager && m_postProcessManager->isEnabled()
			&& m_postProcessManager->isInitialized() && m_context)
		{
			m_postProcessManager->beginScene(m_context.Get());
		}
	}

	/// @brief ポストプロセスチェーンを実行しバックバッファに出力する
	void endPostProcess() override
	{
		if (!m_postProcessManager || !m_postProcessManager->isEnabled()
			|| !m_postProcessManager->isInitialized() || !m_context)
		{
			return;
		}
		// windowless (G2) では m_swapChain の代わりにオフスクリーンテクスチャの RTV へ出力する。
		auto* rtv = m_swapChain
			? m_swapChain->getRenderTargetView()
			: (m_offscreenTarget ? m_offscreenTarget->getRTV() : nullptr);
		if (rtv)
		{
			m_postProcessManager->endScene(m_context.Get(), rtv);
		}
	}

private:
	/// @brief D3D11 デバイスとコンテキストを生成する
	void createDevice()
	{
		/// フィーチャーレベル（DX11.0 を要求）
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

		/// ハードウェアが失敗した場合は WARP にフォールバック
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
	std::unique_ptr<Dx11SwapChain> m_swapChain;      ///< スワップチェーン (windowless 時は null)
	std::unique_ptr<Dx11RenderTarget> m_offscreenTarget; ///< windowless (G2) の描画先 (m_swapChain が null の時だけ使う)
	render::PostProcessManager* m_postProcessManager = nullptr; ///< ポストプロセス（非所有）

	// GPU タイムスタンプクエリ
	ComPtr<ID3D11Query> m_gpuTimestampBegin;
	ComPtr<ID3D11Query> m_gpuTimestampEnd;
	ComPtr<ID3D11Query> m_gpuTimestampDisjoint;
	bool m_gpuTimerReady = false;
	float m_gpuFrameMs = 0.0f;

	/// @brief GPU タイムスタンプクエリを初期化する
	void initGpuTimer()
	{
		D3D11_QUERY_DESC desc{};
		desc.Query = D3D11_QUERY_TIMESTAMP;
		if (SUCCEEDED(m_device->CreateQuery(&desc, m_gpuTimestampBegin.GetAddressOf()))
			&& SUCCEEDED(m_device->CreateQuery(&desc, m_gpuTimestampEnd.GetAddressOf())))
		{
			desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			if (SUCCEEDED(m_device->CreateQuery(&desc, m_gpuTimestampDisjoint.GetAddressOf())))
			{
				m_gpuTimerReady = true;
				m_context->Begin(m_gpuTimestampDisjoint.Get());
			}
		}
	}
};

} // namespace mitiru::gfx

#endif // _WIN32
