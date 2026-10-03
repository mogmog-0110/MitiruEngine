#pragma once

/// @file Dx12PassTimeline.hpp
/// @brief 3D のメインのリストに置いたパスのしるしごとにタイムスタンプを打ち、パスごとの GPU 時間を出す
/// @details しるし n の時間は、そのフレームで次に打ったしるし (無ければ終わりの印) までの差。
///          有効にした時だけ打つので、計測しない普段のフレームは分岐 1 つで済む。
///          結果は Dx12GpuTimer と同じく、そのスロットの GPU 完了を待った後の beginFrame で読む。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>

namespace mitiru::render::dx12
{

class Dx12PassTimeline
{
public:
	static constexpr std::uint32_t kMaxMarks = 32;

	[[nodiscard]] bool init(ID3D12Device* device, ID3D12CommandQueue* queue, std::uint32_t frameCount)
	{
		if (device == nullptr || queue == nullptr || frameCount == 0) { return false; }
		UINT64 freq = 0;
		if (FAILED(queue->GetTimestampFrequency(&freq)) || freq == 0) { return false; }
		m_ticksPerMs = static_cast<double>(freq) / 1000.0;
		m_frameCount = frameCount;
		m_written.assign(frameCount, 0u);
		D3D12_QUERY_HEAP_DESC qd = {};
		qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		qd.Count = frameCount * kSlots;
		if (FAILED(device->CreateQueryHeap(&qd, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())))) { return false; }
		const std::uint64_t bytes = static_cast<std::uint64_t>(qd.Count) * sizeof(std::uint64_t);
		if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST,
		                                m_readback)))
		{
			m_heap.Reset();
			return false;
		}
		return true;
	}

	void setEnabled(bool on) noexcept { m_enabled = on; }
	[[nodiscard]] bool enabled() const noexcept { return m_enabled && m_heap != nullptr && m_readback; }

	/// @brief スロット frameIndex に残っている結果を読む。そのスロットの GPU 完了を待った後に呼ぶ
	void beginFrame(std::uint32_t frameIndex)
	{
		if (m_heap == nullptr || !m_readback || frameIndex >= m_frameCount) { return; }
		m_frame = frameIndex;
		const std::uint32_t mask = m_written[frameIndex];
		m_written[frameIndex] = 0;
		if (mask == 0) { return; }
		const std::uint32_t base = frameIndex * kSlots;
		D3D12_RANGE range{base * sizeof(std::uint64_t), (base + kSlots) * sizeof(std::uint64_t)};
		void* mapped = nullptr;
		if (FAILED(m_readback->Map(0, &range, &mapped)) || mapped == nullptr) { return; }
		readDeltas(static_cast<const std::uint64_t*>(mapped) + base, mask);
		const D3D12_RANGE none{0, 0};
		m_readback->Unmap(0, &none);
	}

	/// @brief しるし mark のタイムスタンプを打つ
	void mark(ID3D12GraphicsCommandList* cl, std::uint32_t mark)
	{
		if (!enabled() || cl == nullptr || mark >= kMaxMarks) { return; }
		cl->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_frame * kSlots + mark);
		m_written[m_frame] |= 1u << mark;
	}

	/// @brief 終わりの印を打ち、このフレームのタイムスタンプを読み戻し用のバッファへ写す
	void endFrame(ID3D12GraphicsCommandList* cl)
	{
		if (!enabled() || cl == nullptr || m_written[m_frame] == 0) { return; }
		const UINT base = m_frame * kSlots;
		cl->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + kMaxMarks);
		cl->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base, kSlots, m_readback.Get(),
		                     static_cast<UINT64>(base) * sizeof(std::uint64_t));
	}

	/// @brief 最後に読めたしるし mark の区間の GPU 時間 (ms)。打っていなければ 0
	[[nodiscard]] double milliseconds(std::uint32_t mark) const noexcept
	{
		return (mark < kMaxMarks) ? m_ms[mark] : 0.0;
	}

private:
	static constexpr std::uint32_t kSlots = kMaxMarks + 1;   // 最後の 1 つは終わりの印

	// 打たなかったしるしは飛ばし、次に打ったしるしまでを区間にする
	void readDeltas(const std::uint64_t* ticks, std::uint32_t mask)
	{
		m_ms.fill(0.0);
		for (std::uint32_t m = 0; m < kMaxMarks; ++m)
		{
			if ((mask & (1u << m)) == 0) { continue; }
			std::uint32_t next = m + 1;
			while (next < kMaxMarks && (mask & (1u << next)) == 0) { ++next; }
			const std::uint64_t t0 = ticks[m];
			const std::uint64_t t1 = ticks[next];
			m_ms[m] = (t1 >= t0) ? static_cast<double>(t1 - t0) / m_ticksPerMs : 0.0;
		}
	}

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
	gfx::GpuResource m_readback;
	std::vector<std::uint32_t> m_written;
	std::array<double, kMaxMarks> m_ms{};
	double m_ticksPerMs = 1.0;
	std::uint32_t m_frameCount = 0;
	std::uint32_t m_frame = 0;
	bool m_enabled = false;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
