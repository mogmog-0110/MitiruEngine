#pragma once

/// @file Dx12Device.hpp
/// @brief DirectX 12デバイス実装
/// @details ID3D12DeviceとID3D12CommandQueueを管理し、
///          トリプルバッファリングとフェンスベースのCPU-GPU同期を提供するIDevice実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
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

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/dx12/Dx12Buffer.hpp>
#include <mitiru/gfx/dx12/Dx12CommandList.hpp>
#include <mitiru/gfx/dx12/Dx12DescriptorHeap.hpp>
#include <mitiru/gfx/dx12/Dx12Fence.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12デバイス実装
/// @details D3D12デバイス・コマンドキュー・スワップチェーン・フェンスを統合管理する。
///          トリプルバッファリングによる効率的なGPU利用を実現する。
///
/// @code
/// auto device = std::make_unique<Dx12Device>(window);
/// device->beginFrame();
/// // ... 描画コマンド記録・実行 ...
/// device->endFrame();
/// @endcode
class Dx12Device final : public IDevice
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief トリプルバッファリングのフレーム数
	static constexpr uint32_t FRAME_COUNT = 3;

	/// @brief コンストラクタ
	/// @param window Win32ウィンドウ（HWNDの取得に使用）
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

		/// スワップチェーンの生成
		m_swapChain = std::make_unique<Dx12SwapChain>(
			m_device.Get(),
			m_commandQueue.Get(),
			m_factory.Get(),
			window->getHandle(),
			m_width, m_height);

		createFrameResources();
	}

	/// @brief デストラクタ
	~Dx12Device() override
	{
		/// GPU処理の完了を待機してからリソースを解放する
		waitForGpu();

		/// 残った遅延解放リソースを全て破棄する
		m_deferredReleases.clear();

		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}
	}

	/// コピー禁止
	Dx12Device(const Dx12Device&) = delete;
	Dx12Device& operator=(const Dx12Device&) = delete;

	/// ムーブ禁止（内部リソースがthisを参照する可能性があるため）
	Dx12Device(Dx12Device&&) = delete;
	Dx12Device& operator=(Dx12Device&&) = delete;

	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		if (!m_swapChain || width <= 0 || height <= 0)
		{
			return {};
		}

		/// リードバック用バッファを生成する
		const auto rowPitch = static_cast<UINT>(
			(width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
			~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
		const auto bufferSize =
			static_cast<UINT64>(rowPitch) * static_cast<UINT64>(height);

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_READBACK;

		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = bufferSize;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> readbackBuffer;
		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(readbackBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			return {};
		}

		/// コマンドリストでバックバッファからリードバックバッファへコピーする
		const auto frameIndex = m_swapChain->currentBackBufferIndex();
		auto* backBuffer = m_swapChain->getBackBufferResource(frameIndex);
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
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
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

		tempCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		/// バリア: CopySrc → Present
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		tempCmdList->ResourceBarrier(1, &barrier);

		tempCmdList->Close();

		ID3D12CommandList* cmdLists[] = {tempCmdList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);

		/// GPU処理完了を待機する
		ComPtr<ID3D12Fence> tempFence;
		m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(tempFence.GetAddressOf()));
		HANDLE tempEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		m_commandQueue->Signal(tempFence.Get(), 1);
		if (tempFence->GetCompletedValue() < 1)
		{
			tempFence->SetEventOnCompletion(1, tempEvent);
			WaitForSingleObject(tempEvent, INFINITE);
		}
		CloseHandle(tempEvent);

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
		const auto frameIndex = m_swapChain->currentBackBufferIndex();
		waitForFrame(frameIndex);

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

	/// @brief 指定フレームインデックスのGPU完了を待機する
	/// @param frameIndex 待機するフレームインデックス
	void waitForFrame(uint32_t frameIndex)
	{
		if (m_fence->GetCompletedValue() < m_fenceValues[frameIndex])
		{
			m_fence->SetEventOnCompletion(
				m_fenceValues[frameIndex], m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	/// @brief フレーム終了・プレゼント処理
	/// @details バックバッファを RENDER_TARGET → PRESENT 状態に遷移させ、
	///          スワップチェーンのプレゼントとフェンスシグナルを行う。
	void endFrame() override
	{
		if (!m_swapChain)
		{
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
	}

	/// @brief GPUバッファを生成する
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

	/// @brief GPUフェンスを作成する
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

	/// @brief GPU処理の完了を待機する
	void waitForGpu() override
	{
		if (!m_fence || !m_commandQueue)
		{
			return;
		}

		++m_currentFenceValue;
		m_commandQueue->Signal(m_fence.Get(), m_currentFenceValue);

		if (m_fence->GetCompletedValue() < m_currentFenceValue)
		{
			m_fence->SetEventOnCompletion(
				m_currentFenceValue, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		/// 全フレームのフェンス値を更新する
		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			m_fenceValues[i] = m_currentFenceValue;
		}

		/// GPU が完全にアイドルになったので全ての遅延解放を破棄する
		m_deferredReleases.clear();
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
	/// @return ID3D12Fenceへのポインタ
	[[nodiscard]] ID3D12Fence* fence() const noexcept
	{
		return m_fence.Get();
	}

	/// @brief 内部のD3D12デバイスを取得する
	/// @return ID3D12Deviceへのポインタ
	[[nodiscard]] ID3D12Device* nativeDevice() const noexcept
	{
		return m_device.Get();
	}

	/// @brief コマンドキューを取得する
	/// @return ID3D12CommandQueueへのポインタ
	[[nodiscard]] ID3D12CommandQueue* commandQueue() const noexcept
	{
		return m_commandQueue.Get();
	}

	/// @brief スワップチェーンを取得する
	/// @return Dx12SwapChainへのポインタ
	[[nodiscard]] Dx12SwapChain* getSwapChain() const noexcept
	{
		return m_swapChain.get();
	}

	/// @brief リソースの遅延解放を予約する
	/// @details 現在のフレームで発行されるコマンドが GPU で完了するまで
	///          リソースの生存を保証する。CEF 等のサブシステムから利用可能。
	/// @param resource 解放を予約する COM オブジェクト
	void deferRelease(ComPtr<IUnknown> resource)
	{
		if (!resource)
		{
			return;
		}

		/// 次回シグナル予定のフェンス値でタグ付けする
		/// (現在記録中のコマンドがその値で完了することを保証する)
		const uint64_t targetFence = m_currentFenceValue + 1;
		m_deferredReleases.emplace_back(targetFence, std::move(resource));
	}

	/// @brief 遅延解放キューに残るリソース数 (テスト/デバッグ用)
	[[nodiscard]] std::size_t deferredReleaseCount() const noexcept
	{
		return m_deferredReleases.size();
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
	}

private:
	/// @brief 完了済みフェンス値の遅延解放リソースをキューから破棄する
	/// @details endFrame() の最後に毎フレーム呼ばれる。GPU フェンスが
	///          タグ付けフェンス値に達したリソースのみ解放する。
	void processDeferredReleases()
	{
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

	/// @brief D3D12デバイスとDXGIファクトリを生成する
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

		/// DXGIファクトリの生成
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

			/// D3D12デバイスが生成可能か確認する
			hr = D3D12CreateDevice(
				adapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(m_device.GetAddressOf()));
			if (SUCCEEDED(hr))
			{
				break;
			}
		}

		/// ハードウェアが見つからない場合はWARPにフォールバック
		if (!m_device)
		{
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
	}

	ComPtr<ID3D12Device> m_device;                                ///< D3D12デバイス
	ComPtr<IDXGIFactory4> m_factory;                              ///< DXGIファクトリ
	ComPtr<ID3D12CommandQueue> m_commandQueue;                    ///< コマンドキュー
	ComPtr<ID3D12Fence> m_fence;                                  ///< フレーム同期フェンス
	HANDLE m_fenceEvent = nullptr;                                ///< フェンスイベント
	uint64_t m_fenceValues[FRAME_COUNT] = {};                     ///< フレーム毎のフェンス値
	uint64_t m_currentFenceValue = 0;                             ///< 現在のフェンス値
	std::unique_ptr<Dx12SwapChain> m_swapChain;                   ///< スワップチェーン
	int m_width = 0;                                               ///< ウィンドウ幅
	int m_height = 0;                                              ///< ウィンドウ高さ
	ComPtr<ID3D12CommandAllocator> m_barrierAllocators[FRAME_COUNT]; ///< バリア用アロケータ
	ComPtr<ID3D12GraphicsCommandList> m_beginBarrierCmdList;         ///< PRESENT→RT バリア用
	ComPtr<ID3D12GraphicsCommandList> m_endBarrierCmdList;           ///< RT→PRESENT バリア用

	/// @brief 遅延解放キュー
	/// @details (タグ付けフェンス値, リソース) のペア。
	///          フェンス値が GetCompletedValue() 以下に達したものを破棄する。
	///          GPU が使用中のリソースを誤って解放することを防ぐ。
	std::deque<std::pair<uint64_t, ComPtr<IUnknown>>> m_deferredReleases;
};

} // namespace mitiru::gfx

#endif // _WIN32
