#pragma once

/// @file Dx12GpuTimer.hpp
/// @brief パスごとの GPU 時間をタイムスタンプで測る
/// @details 結果は同じフレーム番号が次に回ってきた beginFrame で読む。そのとき Dx12Device は
///          そのフレームの完了をフェンスで待ち終えているので、計測のための待ちは足さない。
///          値は 2〜3 フレーム遅れて見える。

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

class Dx12GpuTimer
{
public:
	static constexpr std::uint32_t kMaxPasses = 12;

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
		qd.Count = frameCount * kMaxPasses * 2;
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

	[[nodiscard]] bool ready() const noexcept { return m_heap != nullptr && m_readback; }

	/// @brief スロット frameIndex に残っている結果を読み、スロットを空にする。
	///        そのスロットの GPU 完了を待った後 (beginFrame) に呼ぶ
	void beginFrame(std::uint32_t frameIndex)
	{
		if (!ready() || frameIndex >= m_frameCount) { return; }
		m_frame = frameIndex;
		const std::uint32_t mask = m_written[frameIndex];
		m_written[frameIndex] = 0;
		if (mask == 0) { return; }
		const std::uint32_t base = frameIndex * kMaxPasses * 2;
		D3D12_RANGE range{base * sizeof(std::uint64_t), (base + kMaxPasses * 2) * sizeof(std::uint64_t)};
		void* mapped = nullptr;
		if (FAILED(m_readback->Map(0, &range, &mapped)) || mapped == nullptr) { return; }
		const auto* ticks = static_cast<const std::uint64_t*>(mapped) + base;
		for (std::uint32_t p = 0; p < kMaxPasses; ++p)
		{
			if ((mask & (1u << p)) == 0) { continue; }
			const std::uint64_t t0 = ticks[p * 2];
			const std::uint64_t t1 = ticks[p * 2 + 1];
			m_ms[p] = (t1 >= t0) ? static_cast<double>(t1 - t0) / m_ticksPerMs : 0.0;
		}
		const D3D12_RANGE none{0, 0};
		m_readback->Unmap(0, &none);
	}

	void begin(ID3D12GraphicsCommandList* cl, std::uint32_t pass) const
	{
		if (!ready() || cl == nullptr || pass >= kMaxPasses) { return; }
		cl->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot(pass));
	}

	/// @brief 計測を閉じ、結果を読み戻し用のバッファへ写すコマンドを積む
	void end(ID3D12GraphicsCommandList* cl, std::uint32_t pass)
	{
		if (!ready() || cl == nullptr || pass >= kMaxPasses) { return; }
		const UINT first = slot(pass);
		cl->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + 1);
		cl->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, 2, m_readback.Get(),
		                     static_cast<UINT64>(first) * sizeof(std::uint64_t));
		m_written[m_frame] |= 1u << pass;
	}

	/// @brief 最後に読めたそのパスの GPU 時間 (ms)。一度も測っていなければ 0
	[[nodiscard]] double milliseconds(std::uint32_t pass) const noexcept
	{
		return (pass < kMaxPasses) ? m_ms[pass] : 0.0;
	}

private:
	[[nodiscard]] UINT slot(std::uint32_t pass) const noexcept
	{
		return (m_frame * kMaxPasses + pass) * 2;
	}

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
	gfx::GpuResource m_readback;
	std::vector<std::uint32_t> m_written;
	std::array<double, kMaxPasses> m_ms{};
	double m_ticksPerMs = 1.0;
	std::uint32_t m_frameCount = 0;
	std::uint32_t m_frame = 0;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
