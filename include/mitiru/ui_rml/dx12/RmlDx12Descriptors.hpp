#pragma once
// RmlUi 描画層の SRV 置き場 (shader-visible ヒープ 1 枚)。
// 手放した枠は、手放した時点までに投入したフレームを GPU が終えるまで再利用しない。
// 実行中のコマンドリストが読んでいる枠を別のテクスチャで上書きすると、まだ描いているフレームの
// 絵が化ける (コマンドリストは SRV の中身を実行時に読む)。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace mitiru::ui_rml::dx12
{

class RmlDescriptorPool
{
public:
	static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

	bool create(ID3D12Device* device, std::uint32_t capacity)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = capacity;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())))) { return false; }
		m_heap->SetName(L"RmlUi SRV heap");
		m_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_free.clear();
		m_free.reserve(capacity);
		for (std::uint32_t i = capacity; i > 0; --i) { m_free.push_back(i - 1); }
		return true;
	}

	[[nodiscard]] std::uint32_t allocate()
	{
		if (m_free.empty()) { return kInvalid; }
		const std::uint32_t slot = m_free.back();
		m_free.pop_back();
		return slot;
	}

	/// @param fenceValue この値まで GPU が終えたら、枠を使い回してよい
	void release(std::uint32_t slot, std::uint64_t fenceValue)
	{
		if (slot != kInvalid) { m_pending.push_back({ slot, fenceValue }); }
	}

	void reclaim(std::uint64_t completedFence)
	{
		while (!m_pending.empty() && m_pending.front().fence <= completedFence)
		{
			m_free.push_back(m_pending.front().slot);
			m_pending.pop_front();
		}
	}

	[[nodiscard]] ID3D12DescriptorHeap* heap() const noexcept { return m_heap.Get(); }

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpu(std::uint32_t slot) const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(slot) * m_increment;
		return h;
	}

	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpu(std::uint32_t slot) const noexcept
	{
		D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<UINT64>(slot) * m_increment;
		return h;
	}

	[[nodiscard]] std::size_t freeCount() const noexcept { return m_free.size(); }

private:
	struct Pending { std::uint32_t slot; std::uint64_t fence; };
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
	std::uint32_t m_increment = 0;
	std::vector<std::uint32_t> m_free;
	std::deque<Pending> m_pending;
};

/// RTV / DSV 用の CPU 側ヒープ。中身は OMSetRenderTargets の時点で写されるので、遅らせて解放する必要は無い。
class RmlCpuDescriptorHeap
{
public:
	bool create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())))) { return false; }
		m_increment = device->GetDescriptorHandleIncrementSize(type);
		m_capacity = capacity;
		return true;
	}

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE at(std::uint32_t index) const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(index) * m_increment;
		return h;
	}

	[[nodiscard]] std::uint32_t capacity() const noexcept { return m_capacity; }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
	std::uint32_t m_increment = 0;
	std::uint32_t m_capacity = 0;
};

} // namespace mitiru::ui_rml::dx12

#endif // _WIN32
