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
///          Dx12UploadRing は **frame in flight 数 (= FRAME_COUNT) の永続的な
///          UPLOAD ヒープ** を持ち、各フレーム内では offset 加算のみで
///          サブアロケーションを切り出す。フェンス完了後の `beginFrame()` で
///          offset をリセットするだけ。GPU は前フレーム範囲の読み込みを終えている
///          ことが保証されているため、上書き OK。
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
///          フレーム内で per-frame ヒープが枯渇した場合は、自動的に
///          オーバーフロー用の使い捨て ID3D12Resource を作る（"safety overflow"）。
///          オーバーフロー分は `releaseOverflow(frameIndex)` で次フレームの
///          beginFrame タイミングに開放する。
///
///          Wicked / Diligent / Microsoft DirectXTK12 の `LinearAllocator` /
///          `GraphicsMemory` 等と同じ思想。コンテキストの単純化のため
///          1 リング = 1 用途 (どんな用途でも OK) として実装する。

#ifdef _WIN32

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
/// @details ヘッダーオンリー、依存は <d3d12.h> のみ。
class Dx12UploadRing
{
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	Dx12UploadRing() = default;
	~Dx12UploadRing() { destroy(); }

	Dx12UploadRing(const Dx12UploadRing&) = delete;
	Dx12UploadRing& operator=(const Dx12UploadRing&) = delete;

	/// @brief 初期化
	/// @param device  D3D12 デバイス
	/// @param frameCount フレーム並列度（典型 2 または 3）
	/// @param perFrameBytes 各フレームの UPLOAD ヒープ容量（例 8 MiB）
	/// @return true で成功、false で失敗（リソース確保失敗等）
	bool initialize(ID3D12Device* device,
	                UINT frameCount,
	                UINT64 perFrameBytes)
	{
		if (!device || frameCount == 0 || perFrameBytes == 0)
		{
			return false;
		}

		m_device = device;
		m_frames.resize(frameCount);
		m_currentFrame = 0;

		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width            = perFrameBytes;
		rd.Height           = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels        = 1;
		rd.Format           = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		for (UINT i = 0; i < frameCount; ++i)
		{
			auto& f = m_frames[i];
			if (FAILED(device->CreateCommittedResource(
					&hp, D3D12_HEAP_FLAG_NONE, &rd,
					D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
					IID_PPV_ARGS(f.buffer.GetAddressOf()))))
			{
				m_frames.clear();
				return false;
			}

			void* mapped = nullptr;
			D3D12_RANGE noRead{0, 0};
			if (FAILED(f.buffer->Map(0, &noRead, &mapped)))
			{
				m_frames.clear();
				return false;
			}
			f.cpuBase   = static_cast<std::byte*>(mapped);
			f.gpuBase   = f.buffer->GetGPUVirtualAddress();
			f.capacity  = perFrameBytes;
			f.offset    = 0;
		}
		return true;
	}

	/// @brief 解放
	void destroy()
	{
		for (auto& f : m_frames)
		{
			f.overflow.clear();
			if (f.buffer)
			{
				const D3D12_RANGE noWrite{0, 0};
				f.buffer->Unmap(0, &noWrite);
			}
			f.buffer.Reset();
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
		f.offset = 0;
		// 前ターンのオーバーフローを解放（前回 beginFrame で残された分）
		f.overflow.clear();
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

		// 現在 offset を align に切り上げ
		const UINT64 mask = alignment - 1u;
		const UINT64 alignedOffset = (f.offset + mask) & ~mask;

		if (alignedOffset + sizeBytes <= f.capacity)
		{
			UploadAllocation a;
			a.resource = f.buffer.Get();
			a.offset   = alignedOffset;
			a.size     = sizeBytes;
			a.cpuPtr   = f.cpuBase + alignedOffset;
			a.gpuAddr  = f.gpuBase + alignedOffset;
			f.offset   = alignedOffset + sizeBytes;
			return a;
		}

		// オーバーフロー: 1 回限りの大型確保（次の beginFrame で解放される）
		return allocateOverflow(f, sizeBytes, alignment);
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

	/// @brief 現在フレームの残量（デバッグ用）
	[[nodiscard]] UINT64 bytesRemaining() const noexcept
	{
		if (m_frames.empty()) return 0;
		const auto& f = m_frames[m_currentFrame];
		return f.capacity - f.offset;
	}

	/// @brief 現在フレームの容量（デバッグ用）
	[[nodiscard]] UINT64 capacityPerFrame() const noexcept
	{
		if (m_frames.empty()) return 0;
		return m_frames[m_currentFrame].capacity;
	}

	/// @brief 現在オーバーフロー（使い捨て確保）が走っているか
	[[nodiscard]] bool hadOverflowThisFrame() const noexcept
	{
		if (m_frames.empty()) return false;
		return !m_frames[m_currentFrame].overflow.empty();
	}

private:
	struct PerFrame
	{
		ComPtr<ID3D12Resource>             buffer;
		UINT64                             capacity = 0;
		UINT64                             offset   = 0;
		std::byte*                         cpuBase  = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS          gpuBase  = 0;
		std::vector<ComPtr<ID3D12Resource>> overflow; ///< 当該フレームで作った使い捨て
	};

	/// @brief リング枯渇時の使い捨て確保
	UploadAllocation allocateOverflow(PerFrame& f, UINT64 sizeBytes, UINT64 alignment)
	{
		if (!m_device) return {};

		const UINT64 mask = alignment - 1u;
		const UINT64 alignedSize = (sizeBytes + mask) & ~mask;

		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width            = alignedSize;
		rd.Height           = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels        = 1;
		rd.Format           = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> over;
		if (FAILED(m_device->CreateCommittedResource(
				&hp, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(over.GetAddressOf()))))
		{
			return {};
		}
		void* mapped = nullptr;
		D3D12_RANGE noRead{0, 0};
		if (FAILED(over->Map(0, &noRead, &mapped)))
		{
			return {};
		}

		UploadAllocation a;
		a.resource = over.Get();
		a.offset   = 0;
		a.size     = sizeBytes;
		a.cpuPtr   = mapped;
		a.gpuAddr  = over->GetGPUVirtualAddress();

		f.overflow.push_back(std::move(over));
		return a;
	}

	ID3D12Device* m_device = nullptr;
	std::vector<PerFrame> m_frames;
	UINT m_currentFrame = 0;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
