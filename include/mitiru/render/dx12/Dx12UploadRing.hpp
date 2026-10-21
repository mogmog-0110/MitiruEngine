#pragma once

/// @file Dx12UploadRing.hpp
/// @brief DX12 用 per-frame UPLOAD heap リング (transient CB / VB / IB / staging)
/// @details 旧来の Renderer3D_DX12 は drawMesh 毎に `CreateCommittedResource` を
///          呼び出して上りバッファを確保していた（cbTransform / cbLighting /
///          cbLightArray / vertex upload / index upload / etc）。これは
///          描画コール × フレーム数だけ ID3D12Resource を生成するため、ドライバ側で
///          メモリ確保/解放が暴れ、Skybox variant 切替や複数 mesh 描画で
///          顕著な「もっさり」を生んでいた。
///
///          Dx12UploadRing は **frame in flight 数 (= FRAME_COUNT) ぶんの永続的な
///          UPLOAD ヒープ** を持ち、各フレーム内では offset 加算のみで
///          サブアロケーションを切り出す。フェンス完了後の `beginFrame()` で
///          offset をリセットするだけ。GPU は前フレーム範囲の読み込みを終えている
///          ことが保証されているため、上書き OK。
///
///          1 フレームぶんは 1 枚以上のページ (永続マップ済みの UPLOAD バッファ) の列で、
///          最初のページは initialize の大きさ。ページが尽きたら同じ大きさ (要求が大きければ
///          その大きさ) のページを足し、足したページは次のフレームも使い回す。
///          使い捨ての確保は 1 フレームの描画の数に比例して走るので、描画が増えるほど
///          確保の数も増えて急に遅くなる (1 フレーム 8,000 描画あたりから) のを避けるため。
///          使われなくなったページは kTrimAfterFrames フレーム後に手放す。
///
///          API:
///            ```
///            Dx12UploadRing ring;
///            ring.initialize(d3d12Device, FRAME_COUNT, perFrameBytes);
///            // 毎フレーム頭:
///            ring.beginFrame(currentFrameIndex);
///            // 毎描画:
///            auto alloc = ring.allocate(sizeBytes, 256 /*CBV align*/);
///            std::memcpy(alloc.cpuPtr, &cb, sizeof(cb));
///            cmdList->SetGraphicsRootConstantBufferView(slot, alloc.gpuAddr);
///            ```
///
///          Wicked / Diligent / Microsoft DirectXTK12 の `LinearAllocator` /
///          `GraphicsMemory` 等と同じ思想。コンテキストの単純化のため
///          1 リング = 1 用途 (どんな用途でも OK) として実装する。

#ifdef _WIN32

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>

namespace mitiru::render::dx12
{

/// @brief UPLOAD ヒープから切り出した 1 区画
struct UploadAllocation
{
	ID3D12Resource* resource = nullptr;  ///< 親リソース（非所有）
	UINT64 offset = 0;                   ///< parent 内のバイトオフセット
	UINT64 size = 0;                     ///< 切り出したサイズ
	void* cpuPtr = nullptr;              ///< 永続マップ済み CPU ポインタ
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = 0; ///< GPU 仮想アドレス（root CBV 等にバインド）

	/// @brief 有効なアロケーションか
	[[nodiscard]] bool valid() const noexcept
	{
		return resource != nullptr && cpuPtr != nullptr;
	}
};

/// @brief Per-frame UPLOAD ヒープリング
/// @details ヘッダーオンリー。確保は D3D12MA (Dx12GpuMemory.hpp)。
class Dx12UploadRing
{
public:
	/// @brief 足したページが 1 度も使われないまま、同じスロットが beginFrame をこの回数迎えたら手放す
	/// @details 1 フレームだけ描画が増えた後も、ずっと大きいままにしないため。数えるのはスロットごとなので、
	///          実時間は並列のフレーム数倍 (3 面並列で 60fps なら約 30 秒)。数秒の山を挟んで足したり捨てたりを繰り返さないための長さ。
	static constexpr UINT kTrimAfterFrames = 600;

	Dx12UploadRing() = default;
	~Dx12UploadRing() { destroy(); }

	Dx12UploadRing(const Dx12UploadRing&) = delete;
	Dx12UploadRing& operator=(const Dx12UploadRing&) = delete;

	/// @brief 初期化
	/// @param device  D3D12 デバイス
	/// @param frameCount フレーム並列度（典型 2 または 3）
	/// @param perFrameBytes 各フレームの最初のページの容量（例 8 MiB）。足すページもこの大きさ
	/// @return true で成功、false で失敗（リソース確保失敗等）
	bool initialize(ID3D12Device* device,
	                UINT frameCount,
	                UINT64 perFrameBytes)
	{
		if (!device || frameCount == 0 || perFrameBytes == 0)
		{
			return false;
		}

		destroy();
		m_device = device;
		m_frames.resize(frameCount);
		m_currentFrame = 0;

		for (auto& f : m_frames)
		{
			f.pageBytes = perFrameBytes;
			if (!addPage(f, perFrameBytes))
			{
				destroy();
				return false;
			}
			f.added = 0;
		}
		return true;
	}

	/// @brief 解放
	void destroy()
	{
		for (auto& f : m_frames)
		{
			releasePages(f, 0);
		}
		m_frames.clear();
		m_device = nullptr;
	}

	/// @brief フレーム開始
	/// @details フェンス完了後（GPU が前ターンの ring を読み終えた後）に呼ぶこと。
	void beginFrame(UINT frameIndex) noexcept
	{
		if (m_frames.empty()) return;
		m_currentFrame = frameIndex % static_cast<UINT>(m_frames.size());
		auto& f = m_frames[m_currentFrame];

		trimUnused(f);
		for (auto& page : f.pages)
		{
			page.offset = 0;
		}
		f.cursor = 0;
	}

	/// @brief 1 区画を切り出す
	/// @param sizeBytes 必要バイト数
	/// @param alignment アラインメント（CBV なら 256、VB/IB なら 4 など）
	/// @return UploadAllocation（valid() で成否を確認）
	[[nodiscard]] UploadAllocation allocate(UINT64 sizeBytes,
	                                        UINT64 alignment = 256)
	{
		if (sizeBytes == 0 || m_frames.empty())
		{
			return {};
		}

		auto& f = m_frames[m_currentFrame];
		const UINT64 mask = alignment - 1u;

		for (;;)
		{
			if (f.cursor == f.pages.size())
			{
				// 尽きた。次のフレームからも使えるページを足す (使い捨てにしない)
				if (!addPage(f, (std::max)(f.pageBytes, (sizeBytes + mask) & ~mask)))
				{
					return {};
				}
			}

			Page& page = f.pages[f.cursor];
			const UINT64 alignedOffset = (page.offset + mask) & ~mask;
			if (alignedOffset + sizeBytes <= page.capacity)
			{
				UploadAllocation a;
				a.resource = page.buffer.Get();
				a.offset   = alignedOffset;
				a.size     = sizeBytes;
				a.cpuPtr   = page.cpuBase + alignedOffset;
				a.gpuAddr  = page.gpuBase + alignedOffset;
				page.offset = alignedOffset + sizeBytes;
				return a;
			}
			++f.cursor;
		}
	}

	/// @brief 配列を 1 区画にコピーして allocate して返す（VB/IB 用ヘルパー）
	[[nodiscard]] UploadAllocation upload(const void* src,
	                                      UINT64 sizeBytes,
	                                      UINT64 alignment = 256)
	{
		auto a = allocate(sizeBytes, alignment);
		if (a.valid() && src && sizeBytes > 0)
		{
			std::memcpy(a.cpuPtr, src, sizeBytes);
		}
		return a;
	}

	/// @brief 今のページの残量（デバッグ用）
	[[nodiscard]] UINT64 bytesRemaining() const noexcept
	{
		if (m_frames.empty()) return 0;
		const auto& f = m_frames[m_currentFrame];
		if (f.cursor >= f.pages.size()) return 0;
		return f.pages[f.cursor].capacity - f.pages[f.cursor].offset;
	}

	/// @brief 現在フレームが持つページの容量の合計（デバッグ用）
	[[nodiscard]] UINT64 capacityPerFrame() const noexcept
	{
		if (m_frames.empty()) return 0;
		UINT64 total = 0;
		for (const auto& page : m_frames[m_currentFrame].pages) { total += page.capacity; }
		return total;
	}

	/// @brief 現在フレームが持つページの数
	[[nodiscard]] std::size_t pageCount() const noexcept
	{
		return m_frames.empty() ? 0 : m_frames[m_currentFrame].pages.size();
	}

	/// @brief initialize の後に足したページの数 (全フレームの合計)。定常状態で増え続けなければ確保は 0
	[[nodiscard]] std::uint64_t pagesAdded() const noexcept
	{
		std::uint64_t total = 0;
		for (const auto& f : m_frames) { total += f.added; }
		return total;
	}

private:
	struct Page
	{
		gfx::GpuResource          buffer;
		UINT64                    capacity = 0;
		UINT64                    offset   = 0;
		std::byte*                cpuBase  = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuBase  = 0;
	};

	struct PerFrame
	{
		std::vector<Page> pages;
		std::size_t       cursor = 0;       ///< いま切り出しているページ。pages.size() なら全部使い切った
		std::size_t       peakPages = 0;    ///< 手放しの判定の窓で、1 フレームに触ったページの数の最大
		UINT              calmFrames = 0;   ///< 持っているページを使い切らなかった連続のフレーム数
		UINT64            pageBytes = 0;    ///< 足すページの標準の大きさ
		std::uint64_t     added = 0;        ///< initialize の後に足したページの数
	};

	bool addPage(PerFrame& f, UINT64 capacity)
	{
		Page page;
		if (FAILED(gfx::createGpuBuffer(m_device, D3D12_HEAP_TYPE_UPLOAD, capacity,
				D3D12_RESOURCE_STATE_GENERIC_READ, page.buffer)))
		{
			return false;
		}
		void* mapped = nullptr;
		const D3D12_RANGE noRead{0, 0};
		if (FAILED(page.buffer->Map(0, &noRead, &mapped)))
		{
			return false;
		}
		page.buffer->SetName(L"Dx12UploadRing page");
		page.cpuBase  = static_cast<std::byte*>(mapped);
		page.gpuBase  = page.buffer->GetGPUVirtualAddress();
		page.capacity = capacity;
		f.pages.push_back(std::move(page));
		++f.added;
		return true;
	}

	static void releasePages(PerFrame& f, std::size_t keep)
	{
		for (std::size_t i = keep; i < f.pages.size(); ++i)
		{
			if (f.pages[i].buffer)
			{
				const D3D12_RANGE noWrite{0, 0};
				f.pages[i].buffer->Unmap(0, &noWrite);
			}
		}
		f.pages.erase(f.pages.begin() + static_cast<std::ptrdiff_t>((std::min)(keep, f.pages.size())), f.pages.end());
	}

	/// @brief 足したページが使われない状態が続いたら手放す
	/// @details 手放したバッファは GpuResource が GPU の完了を待ってから返す。
	static void trimUnused(PerFrame& f) noexcept
	{
		const std::size_t touched = (std::min)(f.cursor + 1, f.pages.size());
		f.peakPages = (std::max)(f.peakPages, touched);
		if (f.pages.size() <= 1 || f.peakPages >= f.pages.size())
		{
			f.calmFrames = 0;
			f.peakPages = 0;
			return;
		}
		if (++f.calmFrames >= kTrimAfterFrames)
		{
			releasePages(f, (std::max<std::size_t>)(f.peakPages, 1));
			f.calmFrames = 0;
			f.peakPages = 0;
		}
	}

	// ring の状態は PerFrame に持たせ、メンバーを増やさない。このクラスは Renderer3D_DX12 や RmlRenderInterfaceDx12 の中に
	// 値で入るので、大きさが変わると、別の engine から組んだ host とゲーム DLL の組が動かなくなった (ooze_drive で確認)
	ID3D12Device* m_device = nullptr;
	std::vector<PerFrame> m_frames;
	UINT m_currentFrame = 0;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
