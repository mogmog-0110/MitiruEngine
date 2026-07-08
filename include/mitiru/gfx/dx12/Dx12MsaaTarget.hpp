#pragma once
/// @file Dx12MsaaTarget.hpp
/// @brief 2D 描画用の MSAA 中間カラー RT + resolve パス (DX12 のみ)。
/// @details 実バックバッファと同じフォーマット・解像度で SampleDesc.Count=kSampleCount
///          のカラーテクスチャを持つ。フレーム描画前に swapchain の backBuffer を
///          一時的にこの MSAA RT へ override し、全 2D 図形 (回転塗り・三角形・
///          多角形・線・円) をマルチサンプルでラスタライズする。提示前に
///          ResolveSubresource で MSAA → 実バックバッファへ解決する。
///
///          斜辺・回転図形の輪郭が階段状 (ジャギー) になる根本原因は、2D が
///          シングルサンプルのバックバッファに直描きしていたこと。この中間 RT で
///          エッジがサンプル単位のカバレッジで平滑化される。サンプル数の選択理由は
///          kSampleCount の注記を参照 (4x は特定の傾きで片側だけ段が粗くなる)。
///
///          失敗モード回避: 非対応環境では ensure() が false を返し、engine は
///          override せず従来どおり 1x へ黙ってフォールバックする (落とさない)。
///          lo-fi / 3D / postprocess 使用時は engine 側でバイパスされる。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>

namespace mitiru::gfx
{

/// @brief DX12 MSAA 中間 RT (2D アンチエイリアス用、サンプル数は kSampleCount)
class Dx12MsaaTarget
{
public:
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 2D アンチエイリアスの MSAA サンプル数。
	/// @details 8x を使う。理由: 標準 4x のサンプル配置は、辺の傾きが 0.5 (底辺幅=高さ
	///          の二等辺三角形の斜辺など) のとき、"/" 向きの辺で被覆が {0,2,4} の 3 段に
	///          潰れ (サンプルの水平射影が 2 値に退化する)、"\" 向きの {0,1,2,3,4} より
	///          明らかに粗い段になる。左右対称形なのに片側だけ階段状に見えるのはこれが原因。
	///          8x 標準配置では同じ傾きでも射影が 8 値に分かれ ({0..8} の 9 段)、左右とも
	///          滑らかで対称になる。8x on R8G8B8A8 は DX12 デスクトップ GPU で広く対応。
	///          非対応環境では ensure() が false を返し 1x へ黙って落ちる (従来どおり)。
	static constexpr UINT kSampleCount = 8;
	static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	/// @brief 内部リソースを（必要なら再）生成する。失敗時 false。
	/// @details 初回に command allocator/list/fence を作り、(w,h) 変化のたびに
	///          MSAA テクスチャを再確保する (バックバッファのリサイズ追随)。
	///          kSampleCount 非対応の場合は false を返す (engine が 1x へフォールバック)。
	[[nodiscard]] bool ensure(ID3D12Device* device, ID3D12CommandQueue* queue, int w, int h)
	{
		if (!device || !queue || w <= 0 || h <= 0) { return false; }
		m_device = device;
		m_queue = queue;

		if (!m_supportChecked)
		{
			m_supportChecked = true;
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms = {};
			ms.Format = kFormat;
			ms.SampleCount = kSampleCount;
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms, sizeof(ms))))
			{
				m_supported = (ms.NumQualityLevels > 0);
			}
		}
		if (!m_supported) { return false; }

		if (!m_infra && !buildInfra()) { return false; }
		if (w != m_width || h != m_height) { if (!buildTarget(w, h)) { return false; } }
		return ready();
	}

	/// @brief フレーム描画前: MSAA RT をクリアして backBuffer override に差す。
	/// @details 以後の全 2D 描画はこの MSAA RT へ向かう。実バックバッファは
	///          engine の beginFrame が別途クリア済みだが resolve で全面上書きされる。
	void beginFrame(Dx12SwapChain* swap, const float clear[4])
	{
		if (!ready() || !swap) { return; }
		waitGpu();
		m_alloc->Reset();
		m_list->Reset(m_alloc.Get(), nullptr);
		transitionTex(m_texState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_texState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		m_list->ClearRenderTargetView(m_rtv.rtvHandle(), clear, 0, nullptr);
		execute();
		swap->setBackBufferOverride(&m_rtv);
	}

	/// @brief 描画後: override を外し、MSAA → 実バックバッファへ resolve する。
	/// @details ResolveSubresource で全サンプルを平均し 1x バックバッファへ書く。
	///          実バックバッファは engine の beginFrame で RENDER_TARGET 状態にあり、
	///          resolve 後も RENDER_TARGET に戻すので後段の CEF composite / present と
	///          整合する (順序: 2D→resolve→CEF→present)。
	void resolve(Dx12SwapChain* swap)
	{
		if (!ready() || !swap) { return; }
		swap->clearBackBufferOverride();
		ID3D12Resource* back = swap->getBackBufferResource(swap->currentBackBufferIndex());
		if (!back) { return; }

		waitGpu();
		m_alloc->Reset();
		m_list->Reset(m_alloc.Get(), nullptr);

		// MSAA: RENDER_TARGET → RESOLVE_SOURCE、backbuffer: RENDER_TARGET → RESOLVE_DEST
		transitionTex(m_texState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		m_texState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		transitionRes(back, D3D12_RESOURCE_STATE_RENDER_TARGET,
		              D3D12_RESOURCE_STATE_RESOLVE_DEST);

		m_list->ResolveSubresource(back, 0, m_tex.Get(), 0, kFormat);

		// backbuffer: RESOLVE_DEST → RENDER_TARGET (CEF composite / endFrame 前提)
		transitionRes(back, D3D12_RESOURCE_STATE_RESOLVE_DEST,
		              D3D12_RESOURCE_STATE_RENDER_TARGET);
		// MSAA: RESOLVE_SOURCE → RENDER_TARGET (次フレーム clear に備える)
		transitionTex(m_texState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_texState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		execute();
	}

	[[nodiscard]] bool ready() const noexcept
	{
		return m_tex && m_alloc && m_list && m_fence;
	}

	/// @brief kSampleCount の MSAA がこの環境で使えるか (ensure 呼び出し後に有効)
	[[nodiscard]] bool supported() const noexcept { return m_supported; }

	~Dx12MsaaTarget()
	{
		waitGpu();
		if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
	}

private:
	bool buildInfra()
	{
		if (FAILED(m_device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc)))) { return false; }
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_list)))) { return false; }
		m_list->Close();
		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
				IID_PPV_ARGS(&m_fence)))) { return false; }
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		D3D12_DESCRIPTOR_HEAP_DESC rh = {};
		rh.NumDescriptors = 1;
		rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (FAILED(m_device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&m_rtvHeap)))) { return false; }

		m_infra = true;
		return true;
	}

	bool buildTarget(int w, int h)
	{
		waitGpu();
		m_tex.Reset();

		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC td = {};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = static_cast<UINT64>(w);
		td.Height = static_cast<UINT>(h);
		td.DepthOrArraySize = 1;
		td.MipLevels = 1;
		td.Format = kFormat;
		td.SampleDesc.Count = kSampleCount;
		td.SampleDesc.Quality = 0;
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE cv = {};
		cv.Format = kFormat;

		if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
				D3D12_RESOURCE_STATE_COMMON, &cv, IID_PPV_ARGS(&m_tex)))) { return false; }
		m_texState = D3D12_RESOURCE_STATE_COMMON;

		m_device->CreateRenderTargetView(m_tex.Get(), nullptr,
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		m_rtv = Dx12RenderTarget::createFromBackBuffer(m_device, m_tex.Get(),
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), w, h,
			static_cast<int>(kSampleCount));

		m_width = w;
		m_height = h;
		return true;
	}

	void transitionTex(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		transitionRes(m_tex.Get(), before, after);
	}

	void transitionRes(ID3D12Resource* res, D3D12_RESOURCE_STATES before,
	                   D3D12_RESOURCE_STATES after)
	{
		if (!res || before == after) { return; }
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = res;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_list->ResourceBarrier(1, &b);
	}

	void execute()
	{
		m_list->Close();
		ID3D12CommandList* lists[] = { m_list.Get() };
		m_queue->ExecuteCommandLists(1, lists);
		m_queue->Signal(m_fence.Get(), ++m_fenceVal);
	}

	void waitGpu()
	{
		if (!m_fence) { return; }
		if (m_fence->GetCompletedValue() < m_fenceVal)
		{
			m_fence->SetEventOnCompletion(m_fenceVal, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	ID3D12Device* m_device = nullptr;
	ID3D12CommandQueue* m_queue = nullptr;
	ComPtr<ID3D12Resource> m_tex;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Dx12RenderTarget m_rtv;
	ComPtr<ID3D12CommandAllocator> m_alloc;
	ComPtr<ID3D12GraphicsCommandList> m_list;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent = nullptr;
	UINT64 m_fenceVal = 0;
	D3D12_RESOURCE_STATES m_texState = D3D12_RESOURCE_STATE_COMMON;
	int m_width = 0, m_height = 0;
	bool m_infra = false;
	bool m_supportChecked = false;
	bool m_supported = false;
};

} // namespace mitiru::gfx

#endif // _WIN32
