#pragma once

/// @file Dx12Device.hpp
/// @brief DirectX 12 デバイス実装
/// @details ID3D12Device と ID3D12CommandQueue を管理し、
///          トリプルバッファリングとフェンスベースの CPU-GPU 同期を提供する IDevice 実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <algorithm>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/dx12/Dx12Buffer.hpp>
#include <mitiru/gfx/dx12/Dx12CommandList.hpp>
#include <mitiru/gfx/dx12/Dx12DescriptorHeap.hpp>
#include <mitiru/gfx/dx12/Dx12DredReport.hpp>
#include <mitiru/gfx/dx12/Dx12Fence.hpp>
#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/gfx/dx12/Dx12FenceWait.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12 デバイス実装
/// @details D3D12 デバイス・コマンドキュー・スワップチェーン・フェンスを統合管理する。
///          トリプルバッファリングによる効率的な GPU 利用を実現する。
///
/// @code
/// auto device = std::make_unique<Dx12Device>(window);
/// device->beginFrame();
/// //... 描画コマンド記録・実行...
/// device->endFrame();
/// @endcode
class Dx12Device final : public IDevice
{
public:
	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief トリプルバッファリングのフレーム数
	static constexpr uint32_t FRAME_COUNT = 3;

	/// @brief コンストラクタ
	/// @param window Win32 ウィンドウ（HWND の取得に使用）
	explicit Dx12Device(mitiru::Win32Window* window)
	{
		if (!window)
		{
			throw std::runtime_error(
				"Dx12Device: Win32Window is null");
		}

		m_width = window->width();
		m_height = window->height();

		createDevice();
		createCommandQueue();
		injectQueueStallIfRequested();

		/// スワップチェーンの生成
		m_swapChain = std::make_unique<Dx12SwapChain>(
			m_device.Get(),
			m_commandQueue.Get(),
			m_factory.Get(),
			window->getHandle(),
			m_width, m_height);

		createFrameResources();

		// DEVICE_HUNG は同一 GPU 上の他デバイスの teardown と絡んで起きうるため、
		// 1 台だけ作り直しても解消しない。全 Dx12Device を横断して検知・
		// 復旧上限判定ができるよう、生存インスタンスをここで登録する。
		s_liveDevices().push_back(this);
	}

	/// @brief windowless (offscreen) コンストラクタ (G2)。スワップチェーンを持たず、
	///        `width`×`height` の RTV 付きテクスチャ 1 枚を描画先にする。トリプル
	///        バッファリングはしない (headless capture 用途で並行性より単純さを優先、
	///        `beginFrame`/`endFrame` は毎フレーム GPU 完了を待つ同期実装)。
	///        `Dx11Device(int,int)` の DX12 版 (`gfx.headless3d` 経路)。present() は
	///        no-op (`m_swapChain` が null であることで呼び出し側が分岐する)。
	explicit Dx12Device(int width, int height)
	{
		if (width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"Dx12Device: windowless width/height must be positive");
		}

		m_width = width;
		m_height = height;

		createDevice();
		createCommandQueue();
		injectQueueStallIfRequested();
		createFrameResources();
		createOffscreenTarget(width, height);

		s_liveDevices().push_back(this);
	}

	/// @brief デストラクタ
	~Dx12Device() override
	{
		if (m_injectedStall) { m_injectedStall->Signal(1); }  // 止めたキューを流してから破棄する

		/// GPU 処理の完了を待機してからリソースを解放する
		waitForGpu();

		/// 残った遅延解放リソースを全て破棄する
		m_deferredReleases.clear();
		if (m_gpuMemory) { m_gpuMemory->detachQueue(this); }

		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}

		auto& live = s_liveDevices();
		live.erase(std::remove(live.begin(), live.end(), this), live.end());
	}

	/// コピー禁止
	Dx12Device(const Dx12Device&) = delete;
	Dx12Device& operator=(const Dx12Device&) = delete;

	/// ムーブ禁止（内部リソースが this を参照する可能性があるため）
	Dx12Device(Dx12Device&&) = delete;
	Dx12Device& operator=(Dx12Device&&) = delete;

	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8 形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		if ((!m_swapChain && !m_offscreenTarget) || width <= 0 || height <= 0)
		{
			return {};
		}
		// windowless (G2): 常在状態は PRESENT ではなく RENDER_TARGET。
		const D3D12_RESOURCE_STATES restState = m_swapChain
			? D3D12_RESOURCE_STATE_PRESENT
			: D3D12_RESOURCE_STATE_RENDER_TARGET;

		/// リードバック用バッファを生成する
		const auto rowPitch = static_cast<UINT>(
			(width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
			~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
		const auto bufferSize =
			static_cast<UINT64>(rowPitch) * static_cast<UINT64>(height);

		GpuResource readbackBuffer;
		HRESULT hr = createGpuBuffer(m_device.Get(), D3D12_HEAP_TYPE_READBACK,
			bufferSize, D3D12_RESOURCE_STATE_COPY_DEST, readbackBuffer);
		if (FAILED(hr))
		{
			return {};
		}

		/// コマンドリストでバックバッファ (または windowless の offscreen RT) から
		/// リードバックバッファへコピーする
		auto* backBuffer = m_swapChain
			? m_swapChain->getBackBufferResource(m_swapChain->currentBackBufferIndex())
			: m_offscreenTarget.Get();
		if (!backBuffer)
		{
			return {};
		}

		ComPtr<ID3D12CommandAllocator> tempAllocator;
		hr = m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(tempAllocator.GetAddressOf()));
		if (FAILED(hr))
		{
			return {};
		}

		ComPtr<ID3D12GraphicsCommandList> tempCmdList;
		hr = m_device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			tempAllocator.Get(), nullptr,
			IID_PPV_ARGS(tempCmdList.GetAddressOf()));
		if (FAILED(hr))
		{
			return {};
		}

		/// バリア: RenderTarget → CopySrc
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backBuffer;
		barrier.Transition.StateBefore = restState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		tempCmdList->ResourceBarrier(1, &barrier);

		/// コピー
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = backBuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLoc.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = readbackBuffer.Get();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dstLoc.PlacedFootprint.Offset = 0;
		dstLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		dstLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
		dstLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
		dstLoc.PlacedFootprint.Footprint.Depth = 1;
		dstLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

		// 左上の要求領域だけを写す。バッファは窓より大きいことがある (リサイズ中の
		// 作り直しを避けるため) ので、全面コピーだと footprint と食い違う。
		D3D12_BOX srcBox = {};
		srcBox.right = static_cast<UINT>(width);
		srcBox.bottom = static_cast<UINT>(height);
		srcBox.back = 1;
		tempCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

		/// バリア: CopySrc → 常在状態へ戻す
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barrier.Transition.StateAfter = restState;
		tempCmdList->ResourceBarrier(1, &barrier);

		tempCmdList->Close();

		ID3D12CommandList* cmdLists[] = {tempCmdList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);

		/// GPU 処理完了を待機する
		ComPtr<ID3D12Fence> tempFence;
		m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(tempFence.GetAddressOf()));
		HANDLE tempEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!tempFence || !tempEvent)
		{
			if (tempEvent) { CloseHandle(tempEvent); }
			return {};
		}
		m_commandQueue->Signal(tempFence.Get(), 1);
		// const なのでここでは lost 扱いにしない。GPU が止まっていれば次の beginFrame の待ちで lost 扱いにする
		const FenceWaitResult waited = waitForFenceBounded(
			tempFence.Get(), 1, tempEvent, m_device.Get(), m_fenceWaitBounds);
		CloseHandle(tempEvent);
		if (waited != FenceWaitResult::Completed)
		{
			console::verbosef("画面を読み出すコピーの完了を待てませんでした (%s)。", fenceWaitResultName(waited));
			return {};
		}

		/// リードバックバッファからデータを読み取る
		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, static_cast<SIZE_T>(bufferSize)};
		hr = readbackBuffer->Map(0, &readRange, &mapped);
		if (FAILED(hr))
		{
			return {};
		}

		const auto rowBytes = static_cast<std::size_t>(width) * 4;
		std::vector<std::uint8_t> pixels(
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height) * 4);

		const auto* src = static_cast<const std::uint8_t*>(mapped);
		for (int y = 0; y < height; ++y)
		{
			std::memcpy(
				pixels.data() + static_cast<std::size_t>(y) * rowBytes,
				src + static_cast<std::size_t>(y) * rowPitch,
				rowBytes);
		}

		D3D12_RANGE writeRange = {0, 0};
		readbackBuffer->Unmap(0, &writeRange);

		return pixels;
	}

	/// @brief アクティブなバックエンドを取得する
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::Dx12;
	}

	/// @brief フレーム開始処理
	/// @details 現在フレームのフェンス完了を待機し、バックバッファを
	///          PRESENT → RENDER_TARGET 状態に遷移させる。
	void beginFrame() override
	{
		if (m_haltedPermanently) { return; }
		if (checkDeviceLost()) { return; }

		if (!m_swapChain)
		{
			// windowless (G2): スワップチェーンが無いので単一の offscreen RT を
			// 使い回す。トリプルバッファ相当の並行性は無く、フレームごとに
			// GPU 完了 (スロット 0 のフェンス) を待ってからクリアする
			// (headless capture 用途で並行性より単純さを優先)。
			if (!m_offscreenTarget) { return; }
			if (!waitForFrame(0)) { return; }
			m_barrierAllocators[0]->Reset();
			m_beginBarrierCmdList->Reset(m_barrierAllocators[0].Get(), nullptr);
			m_beginBarrierCmdList->ClearRenderTargetView(
				m_offscreenRtvHandle, m_clearColor, 0, nullptr);
			m_beginBarrierCmdList->Close();
			ID3D12CommandList* offscreenLists[] = {m_beginBarrierCmdList.Get()};
			m_commandQueue->ExecuteCommandLists(1, offscreenLists);
			return;
		}

		const auto frameIndex = m_swapChain->currentBackBufferIndex();
		// 待ちきれなかったスロットのアロケータは GPU がまだ読んでいるので Reset してはいけない
		if (!waitForFrame(frameIndex)) { return; }

		/// フェンス完了後にアロケータをリセット (前フレームの記録を破棄)
		m_barrierAllocators[frameIndex]->Reset();

		/// PRESENT → RENDER_TARGET バリアを発行する
		auto* backBuffer = m_swapChain->getBackBufferResource(frameIndex);
		if (backBuffer)
		{
			m_beginBarrierCmdList->Reset(
				m_barrierAllocators[frameIndex].Get(), nullptr);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = backBuffer;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_beginBarrierCmdList->ResourceBarrier(1, &barrier);

			/// バックバッファをクリア色でクリアする
			/// (DX11 Dx11Device::beginFrame との対称性)
			/// この呼び出しが無いと 2D-only 経路で前フレームの内容が
			/// 累積する (3D 経路は Renderer3D_DX12::beginFrame が独自に
			/// ClearRenderTargetView を発行するため別系統)
			if (auto* rt = dynamic_cast<Dx12RenderTarget*>(
				m_swapChain->backBuffer()))
			{
				m_beginBarrierCmdList->ClearRenderTargetView(
					rt->rtvHandle(), m_clearColor, 0, nullptr);
			}

			m_beginBarrierCmdList->Close();

			ID3D12CommandList* lists[] = {m_beginBarrierCmdList.Get()};
			m_commandQueue->ExecuteCommandLists(1, lists);
		}
	}

	/// @brief 指定フレームインデックスの GPU 完了を待機する
	/// @param frameIndex 待機するフレームインデックス
	/// @return false なら GPU が上限内に終わらず、このデバイスは lost 扱いになった
	bool waitForFrame(uint32_t frameIndex)
	{
		return waitFenceOrMarkLost(
			m_fence.Get(), m_fenceValues[frameIndex], m_fenceEvent, "beginFrame");
	}

	/// @brief フレーム終了・プレゼント処理
	/// @details バックバッファを RENDER_TARGET → PRESENT 状態に遷移させ、
	///          スワップチェーンのプレゼントとフェンスシグナルを行う。
	void endFrame() override
	{
		if (m_haltedPermanently || m_deviceLost)
		{
			return;
		}
		if (!m_swapChain)
		{
			// windowless (G2): present は無い。スロット 0 のフェンスだけ進めて
			// 次回 beginFrame の待機対象にする。
			if (m_offscreenTarget)
			{
				signalFence(0);
				processDeferredReleases();
				checkDeviceLost();
			}
			return;
		}

		const auto frameIndex = m_swapChain->currentBackBufferIndex();

		/// RENDER_TARGET → PRESENT バリアを発行する (present() の前に必須)
		auto* backBuffer = m_swapChain->getBackBufferResource(frameIndex);
		if (backBuffer)
		{
			m_endBarrierCmdList->Reset(
				m_barrierAllocators[frameIndex].Get(), nullptr);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = backBuffer;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_endBarrierCmdList->ResourceBarrier(1, &barrier);
			m_endBarrierCmdList->Close();

			ID3D12CommandList* lists[] = {m_endBarrierCmdList.Get()};
			m_commandQueue->ExecuteCommandLists(1, lists);
		}

		m_swapChain->present();

		/// フェンスシグナルを発行する
		++m_currentFenceValue;
		m_fenceValues[frameIndex] = m_currentFenceValue;
		m_commandQueue->Signal(m_fence.Get(), m_currentFenceValue);

		/// 完了済みフェンス値の遅延解放リソースを破棄する
		processDeferredReleases();

		/// present 後にも確認する。ExecuteCommandLists/Present は GPU 側で
		/// タイムアウトしうるため、フレームの節目ごとに検知しておく
		checkDeviceLost();
	}

	/// @brief GPU バッファを生成する
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ
	/// @return 生成されたバッファ
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override
	{
		return std::make_unique<Dx12Buffer>(
			m_device.Get(), bufferType, sizeBytes,
			dynamic, initialData);
	}

	/// @brief コマンドリストを生成する
	/// @return 生成されたコマンドリスト
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<Dx12CommandList>(m_device.Get());
	}

	/// @brief 現在のフレームインデックスを取得する
	[[nodiscard]] uint32_t currentFrameIndex() const override
	{
		if (m_swapChain)
		{
			return m_swapChain->currentBackBufferIndex();
		}
		return 0;
	}

	/// @brief フレームインフライト数を取得する
	[[nodiscard]] uint32_t frameInFlightCount() const override
	{
		return FRAME_COUNT;
	}

	/// @brief GPU フェンスを作成する
	/// @return 生成されたフェンス
	[[nodiscard]] std::unique_ptr<IGpuFence> createFence() override
	{
		return std::make_unique<Dx12Fence>(
			m_device.Get(), m_commandQueue.Get());
	}

	/// @brief デスクリプタヒープを作成する
	/// @param heapType ヒープ種別
	/// @param capacity 最大デスクリプタ数
	/// @return 生成されたヒープ
	[[nodiscard]] std::unique_ptr<IDescriptorHeap> createDescriptorHeap(
		DescriptorHeapType heapType, uint32_t capacity) override
	{
		return std::make_unique<Dx12DescriptorHeap>(
			m_device.Get(), heapType, capacity);
	}

	/// @brief コマンドリストを実行する
	/// @param lists コマンドリストの配列
	/// @param count コマンドリスト数
	void executeCommandLists(
		ICommandList* const* lists, uint32_t count) override
	{
		if (!lists || count == 0)
		{
			return;
		}

		/// ネイティブコマンドリストの配列を構築する
		std::vector<ID3D12CommandList*> d3dLists;
		d3dLists.reserve(count);

		for (uint32_t i = 0; i < count; ++i)
		{
			auto* dx12CmdList = dynamic_cast<Dx12CommandList*>(lists[i]);
			if (dx12CmdList)
			{
				d3dLists.push_back(dx12CmdList->nativeCommandList());
			}
		}

		if (!d3dLists.empty())
		{
			m_commandQueue->ExecuteCommandLists(
				static_cast<UINT>(d3dLists.size()),
				d3dLists.data());
		}
	}

	/// @brief GPU 処理の完了を待機する
	void waitForGpu() override
	{
		if (!m_fence || !m_commandQueue)
		{
			return;
		}

		if (m_deviceLost)
		{
			// lost 後の GPU はもう実行を進めないので、待たずに遅延解放だけ捨てる
			// (待つたびに上限まで止まると、終了処理や resize が上限 × 呼び出し回数だけ固まる)
			m_deferredReleases.clear();
			return;
		}

		++m_currentFenceValue;
		m_commandQueue->Signal(m_fence.Get(), m_currentFenceValue);

		if (!waitFenceOrMarkLost(m_fence.Get(), m_currentFenceValue, m_fenceEvent, "waitForGpu"))
		{
			m_deferredReleases.clear();
			return;
		}

		/// 全フレームのフェンス値を更新する
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			m_fenceValues[i] = m_currentFenceValue;
		}

		/// GPU が完全にアイドルになったので全ての遅延解放を破棄する
		m_deferredReleases.clear();
		m_gpuMemory->releaseCompleted();
	}

	/// @brief フェンス値を進めてシグナルを発行する
	/// @param frameIndex フェンス値を関連付けるフレームインデックス
	/// @return 新しいフェンス値
	uint64_t signalFence(uint32_t frameIndex)
	{
		++m_currentFenceValue;
		m_fenceValues[frameIndex] = m_currentFenceValue;
		m_commandQueue->Signal(m_fence.Get(), m_currentFenceValue);
		return m_currentFenceValue;
	}

	/// @brief フェンスオブジェクトを取得する
	/// @return ID3D12Fence へのポインタ
	[[nodiscard]] ID3D12Fence* fence() const noexcept
	{
		return m_fence.Get();
	}

	/// @brief 内部の D3D12 デバイスを取得する
	/// @return ID3D12Device へのポインタ
	[[nodiscard]] ID3D12Device* nativeDevice() const noexcept
	{
		return m_device.Get();
	}

	// ── J7: デバイス喪失 (DEVICE_HUNG/REMOVED) 検知と復旧上限 ─────────

	/// @brief 上限に達するまでの許容復旧試行回数 (プロセス全体で共有)
	static constexpr int kMaxDeviceLossRecoveryAttempts = 3;

	/// @brief このインスタンスの GPU デバイスが removed/hung かを調べて内部状態を更新する
	/// @details `GetDeviceRemovedReason()` は removed でなければ S_OK。一度でも removed に
	///          なったデバイスは戻らないため、以後は毎回 true を返す (再チェック不要)。
	bool checkDeviceLost() noexcept
	{
		if (m_deviceLost) { return true; }
		if (!m_device) { return false; }

		const HRESULT reason = m_device->GetDeviceRemovedReason();
		if (reason == S_OK) { return false; }

		m_deviceLost = true;
		char key[32];
		std::snprintf(key, sizeof(key), "dx12.device.lost.%08lX", static_cast<unsigned long>(reason));
		debug::verboseOnce(key, "D3D12 デバイスが失われました。");
		dred::report(m_device.Get(), reason);
		return true;
	}

	/// @brief このインスタンスが device-lost と確定しているか
	[[nodiscard]] bool isDeviceLost() const noexcept { return m_deviceLost; }

	/// @brief lost の原因が removed ではなく、フェンス待ちの上限切れ (#75) だったか
	/// @details ドライバの hang はデバイスを removed にしないまま全プロセスのフェンスを止めることがある。
	[[nodiscard]] bool isGpuUnresponsive() const noexcept { return m_gpuUnresponsive; }

	[[nodiscard]] const FenceWaitBounds& fenceWaitBounds() const noexcept { return m_fenceWaitBounds; }
	void setFenceWaitBounds(const FenceWaitBounds& bounds) noexcept { m_fenceWaitBounds = bounds; }

	/// @brief 生存している Dx12Device のうち 1 台でも device-lost かを横断確認する
	/// @details 呼び出し側 (ホスト側の窓/デバイス管理) がここで true を得たら、
	///          生き残っている個体だけを作り直すのではなく全台を作り直すこと
	///          (1 台のみの作り直しは同じ hung 状態を再度踏んで無限ループする)。
	[[nodiscard]] static bool anyDeviceLost() noexcept
	{
		for (const auto* dev : s_liveDevices())
		{
			if (dev->isDeviceLost()) { return true; }
		}
		return false;
	}

	/// @brief 全台作り直しの試行を 1 回記録し、新しい試行回数を返す
	/// @details ホスト側が `anyDeviceLost()` を見て全台の再構築に入るたび 1 回呼ぶこと。
	///          戻り値が `kMaxDeviceLossRecoveryAttempts` を超えたら再構築を諦めて
	///          `haltAfterRecoveryExhausted()` で停止させる (無限ループの機械的な歯止め)。
	static int recordGlobalRecoveryAttempt() noexcept
	{
		return ++s_globalRecoveryAttempts();
	}

	/// @brief 復旧試行が上限に達したかを返す (再構築ループに入る前に確認する)
	[[nodiscard]] static bool recoveryAttemptsExhausted() noexcept
	{
		return s_globalRecoveryAttempts() >= kMaxDeviceLossRecoveryAttempts;
	}

	/// @brief 復旧を諦めて恒久的に停止する。以後 `beginFrame`/`endFrame` は何もしない
	void haltAfterRecoveryExhausted() noexcept
	{
		m_deviceLost = true;
		m_haltedPermanently = true;
		debug::verboseOnce("dx12.device.recovery_exhausted",
			"デバイスを作り直す試みが上限に達したので、このデバイスでの描画をやめます。");
	}

	/// @brief 恒久停止状態か (`haltAfterRecoveryExhausted` 済み)
	[[nodiscard]] bool isHaltedPermanently() const noexcept { return m_haltedPermanently; }

	/// @brief コマンドキューを取得する
	/// @return ID3D12CommandQueue へのポインタ
	[[nodiscard]] ID3D12CommandQueue* commandQueue() const noexcept
	{
		return m_commandQueue.Get();
	}

	/// @brief スワップチェーンを取得する
	/// @return Dx12SwapChain へのポインタ
	[[nodiscard]] Dx12SwapChain* getSwapChain() const noexcept
	{
		return m_swapChain.get();
	}

	/// @brief 現在の描画先バックバッファを取得する (G2)。
	/// @details スワップチェーンがあればそのバックバッファ、windowless (`m_swapChain`
	///          が null) なら offscreen RT を返す。`Renderer3D_DX12` 系はここを経由
	///          することで `getSwapChain()->backBuffer()` の直呼びを避け、windowless
	///          でも同じコードパスで動く。
	[[nodiscard]] Dx12RenderTarget* currentBackBuffer() noexcept
	{
		if (m_swapChain)
		{
			return static_cast<Dx12RenderTarget*>(m_swapChain->backBuffer());
		}
		return m_offscreenTarget ? &m_offscreenRenderTarget : nullptr;
	}

	/// @brief リソースの遅延解放を予約する
	/// @details 現在のフレームで発行されるコマンドが GPU で完了するまで
	///          リソースの生存を保証する。UI 等のサブシステムから利用可能。
	/// @param resource 解放を予約する COM オブジェクト
	void deferRelease(ComPtr<IUnknown> resource)
	{
		if (!resource)
		{
			return;
		}

		/// 次回シグナル予定のフェンス値でタグ付けする
		/// (現在記録中のコマンドがその値で完了することを保証する)
		m_deferredReleases.emplace_back(m_currentFenceValue + 1, std::move(resource));
	}

	/// @brief 遅延解放キューに残るリソース数 (テスト/デバッグ用)
	[[nodiscard]] std::size_t deferredReleaseCount() const noexcept
	{
		return m_deferredReleases.size();
	}

	/// @brief 垂直同期を設定する
	/// @details 既定は ON。枠を掴んでいる間など「vblank を待つより今の位置に合った絵を出す方が
	///          正しい」場面のために切り替えられるようにしてある。
	void setVSync(bool enabled) noexcept
	{
		if (m_swapChain)
		{
			m_swapChain->setVSync(enabled);
		}
	}

	/// @brief 現在の垂直同期状態
	[[nodiscard]] bool isVSyncEnabled() const noexcept
	{
		return m_swapChain ? m_swapChain->isVSyncEnabled() : true;
	}

	/// @brief ウィンドウリサイズに対応する
	/// @param w 新しいクライアント領域幅
	/// @param h 新しいクライアント領域高さ
	void onResize(int w, int h) override
	{
		if (w <= 0 || h <= 0) return;
		waitForGpu();
		if (m_swapChain)
		{
			m_swapChain->resize(w, h);
		}
		// 生成時にしか入っていないと、名前が「ウィンドウ幅」なのに窓を追わない値になる。
		m_width = w;
		m_height = h;
	}

private:
	/// @brief 上限付きで待ち、来なければこのデバイスを lost 扱いにして false を返す (#75)
	bool waitFenceOrMarkLost(ID3D12Fence* fence, uint64_t value, HANDLE event, const char* site) noexcept
	{
		if (m_deviceLost) { return false; }
		const FenceWaitResult waited =
			waitForFenceBounded(fence, value, event, m_device.Get(), m_fenceWaitBounds);
		if (waited == FenceWaitResult::Completed)
		{
			// キューは 1 本なので、ここが済めば部品のフェンスも全部進んでいる。一度きりの時間切れで
			// 立った latch を残すと、生きている GPU で部品が待たずに使用中の資源を触り続ける (#78)
			fenceTimeoutLatch().store(false, std::memory_order_relaxed);
			return true;
		}
		if (checkDeviceLost()) { return false; }  // removed なら理由と DRED は checkDeviceLost が出す

		m_deviceLost = true;
		m_gpuUnresponsive = true;
		if (waited == FenceWaitResult::TimedOut) { fenceTimeoutLatch().store(true, std::memory_order_relaxed); }
		console::verbosef("GPU が応答しません。%s でフェンス値 %llu を %lu ms 待っても完了しませんでした "
			"(完了値 %llu、%s)。このデバイスは失われたものとして扱います。",
			site, static_cast<unsigned long long>(value),
			static_cast<unsigned long>(m_fenceWaitBounds.totalMs),
			static_cast<unsigned long long>(fence ? fence->GetCompletedValue() : 0),
			fenceWaitResultName(waited));
		dred::report(m_device.Get(), m_device ? m_device->GetDeviceRemovedReason() : S_OK);
		return false;
	}

	/// @brief 完了済みフェンス値の遅延解放リソースをキューから破棄する
	/// @details endFrame() の最後に毎フレーム呼ばれる。GPU フェンスが
	///          タグ付けフェンス値に達したリソースのみ解放する。
	void processDeferredReleases()
	{
		m_gpuMemory->releaseCompleted();
		if (m_deferredReleases.empty() || !m_fence)
		{
			return;
		}

		const uint64_t completed = m_fence->GetCompletedValue();
		while (!m_deferredReleases.empty() &&
		       m_deferredReleases.front().first <= completed)
		{
			m_deferredReleases.pop_front();
		}
	}

	/// @brief D3D12 デバイスと DXGI ファクトリを生成する
	void createDevice()
	{
		// D3D12 デバッグレイヤー(検証用)。環境によっては D3D12SDKLayers.dll が 0xc0000005 で
		// クラッシュし、Debug ビルドの consumer ゲームを巻き込んで落とす。既定 OFF にし、
		// グラフィクス検証時のみ MITIRU_D3D12_DEBUG=1 で opt-in する。
		bool d3d12Debug = false;
#ifdef _DEBUG
		if (const char* e = std::getenv("MITIRU_D3D12_DEBUG"); e != nullptr && e[0] != '\0' && e[0] != '0')
		{
			d3d12Debug = true;
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(
				IID_PPV_ARGS(debugController.GetAddressOf()))))
			{
				debugController->EnableDebugLayer();

				// MITIRU_D3D12_GBV=1 で GPU-Based Validation を追加有効化する
				// (device removed の原因操作を特定する診断用 opt-in)
				if (const char* g = std::getenv("MITIRU_D3D12_GBV");
				    g != nullptr && g[0] != '\0' && g[0] != '0')
				{
					ComPtr<ID3D12Debug1> debug1;
					if (SUCCEEDED(debugController.As(&debug1)))
					{
						debug1->SetEnableGPUBasedValidation(TRUE);
					}
				}
			}
		}
#endif

		// debug layer より前でも後でもよいが、D3D12CreateDevice より前でないと有効にならない
		dred::enableIfRequested();

		/// DXGI ファクトリの生成
		UINT dxgiFactoryFlags = 0;
		if (d3d12Debug) { dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG; }

		HRESULT hr = CreateDXGIFactory2(
			dxgiFactoryFlags,
			IID_PPV_ARGS(m_factory.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: CreateDXGIFactory2 failed");
		}

		/// ハードウェアアダプタの取得
		/// MITIRU_D3D12_WARP=1 で列挙を飛ばし WARP を強制する (golden 検証用 opt-in)
		bool forceWarp = false;
		if (const char* w = std::getenv("MITIRU_D3D12_WARP");
		    w != nullptr && w[0] != '\0' && w[0] != '0')
		{
			forceWarp = true;
		}

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT i = 0;
		     !forceWarp &&
		     m_factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) !=
		     DXGI_ERROR_NOT_FOUND;
		     ++i)
		{
			DXGI_ADAPTER_DESC1 adapterDesc = {};
			adapter->GetDesc1(&adapterDesc);

			/// ソフトウェアアダプタをスキップする
			if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			/// D3D12 デバイスが生成可能か確認する
			hr = D3D12CreateDevice(
				adapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(m_device.GetAddressOf()));
			if (SUCCEEDED(hr))
			{
				// 複数 GPU 機では device-lost の切り分けにどの GPU かが要る
				// (イベントログの nvlddmkm 等と突き合わせる)
				console::verbosef("D3D12 のアダプタは %s です。", dred::Utf8Name(adapterDesc.Description).text);
				break;
			}
		}

		/// ハードウェアが見つからない場合は WARP にフォールバック
		if (!m_device)
		{
			m_fenceWaitBounds = kWarpFenceWait;
			ComPtr<IDXGIAdapter> warpAdapter;
			hr = m_factory->EnumWarpAdapter(
				IID_PPV_ARGS(warpAdapter.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"Dx12Device: EnumWarpAdapter failed");
			}

			hr = D3D12CreateDevice(
				warpAdapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(m_device.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"Dx12Device: D3D12CreateDevice (WARP) failed");
			}
		}

		// リソースが 0 個になる瞬間 (リサイズで全部作り直す等) にアロケータごと
		// 作り直さないよう、デバイスが生きている間は参照を持ち続ける。
		m_gpuMemory = detail::acquireGpuAllocator(m_device.Get());
		if (!m_gpuMemory)
		{
			throw std::runtime_error("Dx12Device: D3D12MA::CreateAllocator failed");
		}
	}

	/// @brief コマンドキューを生成する
	void createCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		HRESULT hr = m_device->CreateCommandQueue(
			&queueDesc,
			IID_PPV_ARGS(m_commandQueue.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: CreateCommandQueue failed");
		}
		m_commandQueue->SetName(L"Dx12Device direct queue");
	}

	/// `MITIRU_D3D12_INJECT_QUEUE_STALL=1` のとき、誰も Signal しないフェンスをキューに Wait させる。
	/// ドライバ hang と同じ「removed にならないままフェンスが進まない」状態を、GPU を壊さずに作る (#75 の検証用)。
	void injectQueueStallIfRequested()
	{
		if (!dred::envEnabled("MITIRU_D3D12_INJECT_QUEUE_STALL")) { return; }
		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		                                 IID_PPV_ARGS(m_injectedStall.GetAddressOf()))))
		{
			return;
		}
		m_commandQueue->Wait(m_injectedStall.Get(), 1);
		console::verbose("MITIRU_D3D12_INJECT_QUEUE_STALL でコマンドキューを止めました。");
	}

	/// @brief フレームリソース（フェンス）を生成する
	void createFrameResources()
	{
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			m_fenceValues[i] = 0;
		}

		/// フェンスを生成する
		HRESULT hr = m_device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(m_fence.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: CreateFence failed");
		}
		m_gpuMemory->attachQueue(this, m_fence.Get(), &m_currentFenceValue);

		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent)
		{
			throw std::runtime_error(
				"Dx12Device: CreateEvent failed");
		}

		/// バリア発行用コマンドアロケータ + コマンドリストを生成する
		/// beginFrame (PRESENT→RT) / endFrame (RT→PRESENT) でフレームごとに共用する
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			hr = m_device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(m_barrierAllocators[i].GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"Dx12Device: CreateCommandAllocator (barrier) failed");
			}
		}

		hr = m_device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_barrierAllocators[0].Get(), nullptr,
			IID_PPV_ARGS(m_beginBarrierCmdList.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: CreateCommandList (beginBarrier) failed");
		}
		m_beginBarrierCmdList->Close(); ///< 初期状態はクローズ
		m_beginBarrierCmdList->SetName(L"Dx12Device beginFrame barrier");

		hr = m_device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_barrierAllocators[0].Get(), nullptr,
			IID_PPV_ARGS(m_endBarrierCmdList.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: CreateCommandList (endBarrier) failed");
		}
		m_endBarrierCmdList->Close(); ///< 初期状態はクローズ
		m_endBarrierCmdList->SetName(L"Dx12Device endFrame barrier");
	}

	/// @brief windowless (G2) 用の offscreen RT を生成する。常在状態は
	///        `D3D12_RESOURCE_STATE_RENDER_TARGET` (readPixels 中だけ COPY_SOURCE へ
	///        一時遷移して戻す、`readPixels` の `restState` 分岐を参照)。
	void createOffscreenTarget(int width, int height)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = static_cast<UINT64>(width);
		desc.Height = static_cast<UINT>(height);
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = desc.Format;
		clearValue.Color[0] = m_clearColor[0];
		clearValue.Color[1] = m_clearColor[1];
		clearValue.Color[2] = m_clearColor[2];
		clearValue.Color[3] = m_clearColor[3];

		HRESULT hr = createGpuResource(m_device.Get(), D3D12_HEAP_TYPE_DEFAULT, desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, m_offscreenTarget);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: windowless offscreen target allocation failed");
		}

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = 1;
		hr = m_device->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(m_offscreenRtvHeap.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Device: windowless offscreen CreateDescriptorHeap failed");
		}

		m_offscreenRtvHandle = m_offscreenRtvHeap->GetCPUDescriptorHandleForHeapStart();
		m_offscreenRenderTarget = Dx12RenderTarget::createFromBackBuffer(
			m_device.Get(), m_offscreenTarget.Get(), m_offscreenRtvHandle, width, height);
	}

	/// @brief 生存中の Dx12Device 全台のレジストリ (J7、`anyDeviceLost` が横断参照する)
	[[nodiscard]] static std::vector<Dx12Device*>& s_liveDevices() noexcept
	{
		static std::vector<Dx12Device*> devices;
		return devices;
	}

	/// @brief プロセス全体で共有する全台再構築の試行回数 (J7)
	[[nodiscard]] static int& s_globalRecoveryAttempts() noexcept
	{
		static int attempts = 0;
		return attempts;
	}

	bool m_deviceLost = false;                                    ///< デバイス喪失確定フラグ
	bool m_haltedPermanently = false;                             ///< 復旧上限到達後の恒久停止
	bool m_gpuUnresponsive = false;
	FenceWaitBounds m_fenceWaitBounds = kHardwareFenceWait;
	ComPtr<ID3D12Fence> m_injectedStall;

	ComPtr<ID3D12Device> m_device;                                ///< D3D12デバイス
	std::shared_ptr<detail::GpuAllocatorOwner> m_gpuMemory;       ///< このデバイスの D3D12MA アロケータ
	ComPtr<IDXGIFactory4> m_factory;                             ///< DXGIファクトリ
	ComPtr<ID3D12CommandQueue> m_commandQueue;                    ///< コマンドキュー
	ComPtr<ID3D12Fence> m_fence;                                  ///< フレーム同期フェンス
	HANDLE m_fenceEvent = nullptr;                                ///< フェンスイベント
	uint64_t m_fenceValues[FRAME_COUNT] = {};                     ///< フレーム毎のフェンス値
	/// 現在のフェンス値。GpuAllocatorOwner が別スレッドの GpuResource 破棄からも読むので atomic
	std::atomic<uint64_t> m_currentFenceValue{0};
	std::unique_ptr<Dx12SwapChain> m_swapChain;                   ///< スワップチェーン (windowless 時は null)
	GpuResource m_offscreenTarget;                                ///< windowless (G2) の描画先 (m_swapChain が null の時だけ使う)
	ComPtr<ID3D12DescriptorHeap> m_offscreenRtvHeap;              ///< 上記の RTV 用ヒープ (1 デスクリプタ)
	D3D12_CPU_DESCRIPTOR_HANDLE m_offscreenRtvHandle = {};        ///< 上記の RTV ハンドル
	Dx12RenderTarget m_offscreenRenderTarget;                     ///< 上記を IRenderTarget として包んだもの (currentBackBuffer() が返す)
	int m_width = 0;                                               ///< ウィンドウ幅
	int m_height = 0;                                              ///< ウィンドウ高さ
	ComPtr<ID3D12CommandAllocator> m_barrierAllocators[FRAME_COUNT]; ///< バリア用アロケータ
	ComPtr<ID3D12GraphicsCommandList> m_beginBarrierCmdList;         ///< PRESENT→RT バリア用
	ComPtr<ID3D12GraphicsCommandList> m_endBarrierCmdList;           ///< RT→PRESENT バリア用

	/// @brief 遅延解放キュー
	/// @details (タグ付けフェンス値, リソース) のペア。
	///          フェンス値が GetCompletedValue() 以下に達したものを破棄する。
	///          GPU が使用中のリソースを誤って解放することを防ぐ。
	///          GpuResource は自前で完了を待つのでここには来ない (detail::GpuAllocatorOwner)。
	std::deque<std::pair<uint64_t, ComPtr<IUnknown>>> m_deferredReleases;
};

} // namespace mitiru::gfx

#endif // _WIN32
