#pragma once

/// @file Dx12SwapChain.hpp
/// @brief DirectX 12 スワップチェーン実装
/// @details IDXGISwapChain3 をラップし、トリプルバッファリングとフレーム同期を管理する
///          ISwapChain 実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

#include <d3d12.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/gfx/dx12/Dx12GpuProbe.hpp>
#include <mitiru/gfx/dx12/Dx12HdrDisplay.hpp>
#include <mitiru/gfx/dx12/Dx12HdrPresentPass.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12 スワップチェーン実装
/// @details IDXGISwapChain3 によるトリプルバッファリングを管理する。
///          present() でフェンスのシグナルとフレームインデックスの更新を行う。
class Dx12SwapChain final : public ISwapChain
{
public:
	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief トリプルバッファリングのフレーム数
	static constexpr uint32_t FRAME_COUNT = 3;

	/// @brief コンストラクタ
	/// @param device D3D12 デバイス
	/// @param commandQueue コマンドキュー
	/// @param factory DXGI ファクトリ
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

		/// ティアリング対応を確認する (IDXGIFactory5 が必要)
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))))
		{
			BOOL allowTearing = FALSE;
			if (SUCCEEDED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing))))
			{
				m_tearingSupported = (allowTearing == TRUE) && !compat::fakeMissing(compat::GpuCap::Tearing);
			}
		}

		m_hwnd = hwnd;
		createSwapChain(factory, hwnd, width, height);
		createRenderTargets();
	}

	/// @brief バックバッファを画面に表示する
	/// @details m_syncInterval=1 で vsync ON、0 で OFF (ティアリング許可)
	void present() override
	{
		if (m_hdrActive) { recordHdrPresent(); }
		const UINT flags = (m_syncInterval == 0 && m_tearingSupported)
			? DXGI_PRESENT_ALLOW_TEARING : 0;
		HRESULT hr = m_swapChain->Present(m_syncInterval, flags);
		if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING)
		{
			/// デバイスロスト時はログのみ（クラッシュさせない）
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	/// @brief 垂直同期を設定する
	/// @param enabled true=vsync ON (Present interval=1), false=tearing 許可
	void setVSync(bool enabled) noexcept
	{
		m_syncInterval = enabled ? 1u : 0u;
	}

	/// @brief 現在の vsync 状態
	[[nodiscard]] bool isVSyncEnabled() const noexcept
	{
		return m_syncInterval != 0;
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
		releaseBuffers();

		/// バッファのリサイズ (スワップチェーン作成時と同じフラグが必要)
		HRESULT hr = m_swapChain->ResizeBuffers(
			FRAME_COUNT,
			static_cast<UINT>(width),
			static_cast<UINT>(height),
			m_hdrActive ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM,
			m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
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
	/// @return レンダーターゲットへのポインタ（override 設定時はそちら）
	/// @details ローファイ・ポスト FX 等が、描画を一時的に低解像オフスクリーン RT へ
	///          リダイレクトするために backBuffer を差し替える。override が無ければ実バックバッファ。
	[[nodiscard]] IRenderTarget* backBuffer() noexcept override
	{
		if (m_overrideRT) { return m_overrideRT; }
		return m_hdrActive ? &m_proxyTargets[m_frameIndex] : &m_renderTargets[m_frameIndex];
	}

	/// @brief backBuffer() の戻り値を一時的に差し替える（ポスト FX のオフスクリーン RT 用）。
	void setBackBufferOverride(IRenderTarget* rt) noexcept { m_overrideRT = rt; }

	/// @brief backBuffer override を解除し、実バックバッファに戻す。
	void clearBackBufferOverride() noexcept { m_overrideRT = nullptr; }

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
	/// @return ID3D12Resource へのポインタ
	[[nodiscard]] ID3D12Resource* getBackBufferResource(uint32_t index) const noexcept
	{
		if (index < FRAME_COUNT)
		{
			// HDR のときも、描く側 (UI、色覚のフィルタ、2D の MSAA、キャプチャ) には今までどおりの RGBA8 の絵を見せる
			return m_hdrActive ? m_proxies[index].Get() : m_backBuffers[index].Get();
		}
		return nullptr;
	}

	// ── HDR (scRGB) の Present ──────────────────────────────────────────────
	// 3D と UI は今までどおり RGBA8 のレンダーターゲット (proxy) に描き、Present の直前に 1 パスで FP16 の実バッファへ scRGB を書く。
	// キャプチャとリプレイの動画は proxy を読むので SDR のまま。HDR になるのは画面への Present だけ。

	/// @brief 利用者が HDR を望むか (graphics.hdr = auto / --hdr)。望んでも、モニターが HDR でなければ SDR のまま
	void setHdrRequested(bool requested) noexcept { m_hdrRequested = requested; }
	[[nodiscard]] bool hdrRequested() const noexcept { return m_hdrRequested; }
	[[nodiscard]] HWND windowHandle() const noexcept { return m_hwnd; }
	/// @brief 実バッファが scRGB (FP16) になっているか
	[[nodiscard]] bool hdrActive() const noexcept { return m_hdrActive; }
	[[nodiscard]] const render::HdrParams& hdrParams() const noexcept { return m_hdrParams; }

	/// @brief このフレームの highlight excess (RGBA16F、PIXEL_SHADER_RESOURCE) を渡す。Present で使い切り、渡さなければ足さない
	void setHdrExtra(ID3D12Resource* extra) noexcept { m_hdrExtra = extra; }

	/// @brief ウィンドウがあるモニターの HDR の状態を見て、変わったときだけ info を返す (約 2 秒ごと)。呼び出し側は GPU を待ってから applyHdrDisplay する
	[[nodiscard]] std::optional<HdrDisplayInfo> pollHdrDisplay()
	{
		if (!m_hdrPolling || (!m_hdrRequested && !m_hdrActive)) { return std::nullopt; }
		if (m_hdrPollCountdown > 0) { --m_hdrPollCountdown; return std::nullopt; }
		m_hdrPollCountdown = kHdrPollFrames;
		const HdrDisplayInfo info = queryHdrDisplay(m_hwnd);
		const bool flip = (m_hdrRequested && info.hdrActive) != m_hdrActive;
		const bool changed = info.paperWhiteNits != m_hdrParams.paperWhiteNits || info.peakNits != m_hdrParams.peakNits;
		if (!flip && !changed) { return std::nullopt; }
		return info;
	}

	/// @brief モニターの状態に合わせて SDR と scRGB を切り替え、基準の白とピークを更新する。切り替えのときは GPU が止まっていること。
	///        切り替えられなければ今のまま false
	bool applyHdrDisplay(const HdrDisplayInfo& info)
	{
		m_hdrParams.paperWhiteNits = info.paperWhiteNits;
		m_hdrParams.peakNits = info.peakNits;
		const bool want = m_hdrRequested && info.hdrActive;
		if (want == m_hdrActive) { return true; }
		return want ? switchToHdr() : switchToSdr();
	}

	/// @brief 試験用。モニターの見直しを止める。止めないと、SDR のモニターでは次のフレームの頭で SDR へ戻される
	void setHdrPolling(bool enabled) noexcept { m_hdrPolling = enabled; }

	/// @brief 試験用。モニターが HDR でなくても scRGB の Present を強制する (SDR のモニターでは DWM が 0..1 に切り詰めて表示する)
	bool forceHdrForTest(const render::HdrParams& params)
	{
		m_hdrRequested = true;
		m_hdrPolling = false;
		m_hdrParams = params;
		return m_hdrActive || switchToHdr();
	}

	/// @brief 内部の IDXGISwapChain3 を取得する
	/// @return スワップチェーンへのポインタ
	[[nodiscard]] IDXGISwapChain3* getSwapChain() const noexcept
	{
		return m_swapChain.Get();
	}

private:
	/// @brief スワップチェーンを生成する
	/// @param factory DXGI ファクトリ
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
		/// 窓よりバッファが大きい時は左上を 1:1 で見せる (既定の STRETCH は引き伸ばす)。
		/// バッファを窓より大きめに確保しておけば、リサイズ中に ResizeBuffers を呼ばずに済む。
		desc.Scaling = DXGI_SCALING_NONE;
		desc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

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
			compat::failSetup("CreateSwapChainForHwnd", hr);
		}

		/// ALT+Enter のフルスクリーン切り替えを無効化する
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

		/// IDXGISwapChain3 にキャストする
		hr = swapChain1.As(&m_swapChain);
		if (FAILED(hr))
		{
			compat::failSetup("IDXGISwapChain3", hr);
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	/// @brief バックバッファからレンダーターゲットを生成する
	void createRenderTargets()
	{
		/// RTV デスクリプタヒープの生成
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FRAME_COUNT;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT hr = m_device->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(m_rtvHeap.GetAddressOf()));
		if (FAILED(hr))
		{
			compat::failSetup("CreateDescriptorHeap (RTV)", hr);
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
				compat::failSetup("GetBuffer", hr);
			}

			m_renderTargets[i] = Dx12RenderTarget::createFromBackBuffer(
				m_device,
				m_backBuffers[i].Get(),
				rtvHandle,
				m_width, m_height);

			rtvHandle.ptr += rtvDescriptorSize;
		}
		if (m_hdrActive) { createProxies(); }
	}

	static constexpr int kHdrPollFrames = 120;

	void releaseBuffers()
	{
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			m_renderTargets[i].release();
			m_proxyTargets[i].release();
			m_backBuffers[i].Reset();
			m_proxies[i].Reset();
		}
	}

	/// 実バッファの形式を替える。proxy は作り直し、色空間を合わせる。どちらかが失敗したら false (バッファは作り直しが要る状態)
	bool reformat(DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace)
	{
		releaseBuffers();
		const UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		const HRESULT resized = m_swapChain->ResizeBuffers(FRAME_COUNT, static_cast<UINT>(m_width), static_cast<UINT>(m_height), format, flags);
		if (FAILED(resized))
		{
			console::verbosef("HDR: スワップチェーンの形式を替えられません (ResizeBuffers 0x%08lX)", static_cast<unsigned long>(resized));
			return false;
		}
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		// 形式だけ FP16 で色空間が sRGB のままだと、明るさと色が崩れて表示される。失敗は切り替えの失敗として返す
		// 対応の有無は形式ごとに決まる (RGBA8 に scRGB を問うと常に 0 になる)。形式を替えた後で問う
		UINT support = 0;
		const HRESULT checked = m_swapChain->CheckColorSpaceSupport(colorSpace, &support);
		if (FAILED(checked) || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
		{
			console::verbosef("HDR: この色空間を Present に使えません (0x%08lX、対応 0x%X)", static_cast<unsigned long>(checked), support);
			return false;
		}
		const HRESULT spaced = m_swapChain->SetColorSpace1(colorSpace);
		if (FAILED(spaced)) { console::verbosef("HDR: 色空間を設定できません (SetColorSpace1 0x%08lX)", static_cast<unsigned long>(spaced)); }
		return SUCCEEDED(spaced);
	}

	bool switchToHdr()
	{
		try
		{
			if (!m_hdrPass)
			{
				auto pass = std::make_unique<Dx12HdrPresentPass>();
				pass->init(m_device);
				m_hdrPass = std::move(pass);
			}
			if (!m_hdrList) { createHdrCommandObjects(); }
		}
		catch (const std::exception& e)
		{
			console::verbosef("HDR: Present のパスを作れません (%s)", e.what());
			return false;
		}
		const bool ok = reformat(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
		m_hdrActive = ok;
		if (!ok) { (void)reformat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709); }
		createRenderTargets();
		return ok;
	}

	bool switchToSdr()
	{
		const bool ok = reformat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		// 戻せなかったときは、描く側が見ている形 (proxy) を残すために scRGB の形式へ戻す
		if (ok) { m_hdrActive = false; }
		else { m_hdrActive = reformat(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709); }
		createRenderTargets();
		return ok;
	}

	void createHdrCommandObjects()
	{
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_hdrAlloc[i].GetAddressOf()))))
			{
				throw std::runtime_error("Dx12SwapChain: HDR command allocator");
			}
		}
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_hdrAlloc[0].Get(), nullptr, IID_PPV_ARGS(m_hdrList.GetAddressOf()))))
		{
			throw std::runtime_error("Dx12SwapChain: HDR command list");
		}
		m_hdrList->Close();
	}

	/// 描く側が見る RGBA8 のレンダーターゲットを 3 枚作る。状態は PRESENT (COMMON) から始め、Dx12Device の barrier が実バッファと同じ流れで回す
	void createProxies()
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.NumDescriptors = FRAME_COUNT;
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_proxyRtvHeap.ReleaseAndGetAddressOf())))) { compat::failSetup("CreateDescriptorHeap (proxy RTV)", E_FAIL); }
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = static_cast<UINT64>(m_width);
		rd.Height = static_cast<UINT>(m_height);
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		const UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		auto handle = m_proxyRtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			const HRESULT hr = m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PRESENT, nullptr,
			                                                     IID_PPV_ARGS(m_proxies[i].ReleaseAndGetAddressOf()));
			if (FAILED(hr)) { compat::failSetup("CreateCommittedResource (HDR proxy)", hr); }
			m_proxyTargets[i] = Dx12RenderTarget::createFromBackBuffer(m_device, m_proxies[i].Get(), handle, m_width, m_height);
			handle.ptr += inc;
		}
	}

	/// Dx12Device の endFrame が proxy を PRESENT にした後、Present の直前に呼ぶ。proxy (と highlight excess) を読んで実バッファへ scRGB を書く
	void recordHdrPresent()
	{
		const uint32_t i = m_frameIndex;
		ID3D12Resource* real = m_backBuffers[i].Get();
		ID3D12Resource* proxy = m_proxies[i].Get();
		if (!m_hdrPass || !m_hdrList || real == nullptr || proxy == nullptr) { return; }
		if (FAILED(m_hdrAlloc[i]->Reset()) || FAILED(m_hdrList->Reset(m_hdrAlloc[i].Get(), nullptr))) { return; }
		const auto transition = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = r;
			b.Transition.StateBefore = from;
			b.Transition.StateAfter = to;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_hdrList->ResourceBarrier(1, &b);
		};
		transition(proxy, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		transition(real, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_hdrPass->record(m_hdrList.Get(), i, proxy, m_hdrExtra, m_renderTargets[i].rtvHandle(), static_cast<UINT>(m_width),
		                  static_cast<UINT>(m_height), m_hdrParams.paperWhiteNits);
		transition(real, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		transition(proxy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PRESENT);
		m_hdrList->Close();
		ID3D12CommandList* lists[] = {m_hdrList.Get()};
		m_commandQueue->ExecuteCommandLists(1, lists);
		m_hdrExtra = nullptr;
	}

	ID3D12Device* m_device = nullptr;                    ///< D3D12デバイス（非所有）
	ID3D12CommandQueue* m_commandQueue = nullptr;         ///< コマンドキュー（非所有）
	ComPtr<IDXGISwapChain3> m_swapChain;                  ///< DXGIスワップチェーン
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;              ///< RTVデスクリプタヒープ
	ComPtr<ID3D12Resource> m_backBuffers[FRAME_COUNT];   ///< バックバッファリソース
	Dx12RenderTarget m_renderTargets[FRAME_COUNT];       ///< レンダーターゲット
	IRenderTarget* m_overrideRT = nullptr;               ///< backBuffer override（ポストFX 用・非所有）
	uint32_t m_frameIndex = 0;                           ///< 現在のフレームインデックス
	int m_width = 0;                                      ///< バッファ幅
	int m_height = 0;                                     ///< バッファ高さ
	UINT m_syncInterval = 1;                              ///< Present interval (1=vsync, 0=off)
	bool m_tearingSupported = false;                      ///< DXGI_FEATURE_PRESENT_ALLOW_TEARING 対応フラグ

	HWND m_hwnd = nullptr;
	bool m_hdrRequested = false;
	bool m_hdrActive = false;
	bool m_hdrPolling = true;                             ///< false の間は pollHdrDisplay が何も返さない (試験用)
	int m_hdrPollCountdown = 0;
	render::HdrParams m_hdrParams;
	ID3D12Resource* m_hdrExtra = nullptr;                 ///< このフレームの highlight excess (非所有)
	std::unique_ptr<Dx12HdrPresentPass> m_hdrPass;
	ComPtr<ID3D12CommandAllocator> m_hdrAlloc[FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList> m_hdrList;
	ComPtr<ID3D12Resource> m_proxies[FRAME_COUNT];        ///< HDR のとき描く側に見せる RGBA8 のレンダーターゲット
	ComPtr<ID3D12DescriptorHeap> m_proxyRtvHeap;
	Dx12RenderTarget m_proxyTargets[FRAME_COUNT];
};

} // namespace mitiru::gfx

#endif // _WIN32
