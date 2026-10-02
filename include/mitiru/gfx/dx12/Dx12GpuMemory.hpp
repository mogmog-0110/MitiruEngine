#pragma once

/// @file Dx12GpuMemory.hpp
/// @brief DX12 の buffer / texture を D3D12 Memory Allocator (D3D12MA) で確保する。
/// @details アロケータはデバイス 1 台につき 1 個で、GpuResource が参照を持つ。

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
#include <utility>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <D3D12MemAlloc.h>

#include <mitiru/gfx/dx12/detail/Dx12GpuAllocatorOwner.hpp>

namespace mitiru::gfx
{

namespace detail
{

[[nodiscard]] inline Microsoft::WRL::ComPtr<IDXGIAdapter> adapterOf(ID3D12Device* device)
{
	Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
	{
		factory->EnumAdapterByLuid(device->GetAdapterLuid(),
			IID_PPV_ARGS(adapter.GetAddressOf()));
	}
	return adapter;
}

[[nodiscard]] inline std::shared_ptr<GpuAllocatorOwner> createGpuAllocator(ID3D12Device* device)
{
	const auto adapter = adapterOf(device);
	if (!adapter) { return nullptr; }

	D3D12MA::ALLOCATOR_DESC desc = {};
	// MSAA は 4MB 境界を要求するので、置くと既定プール全体の境界が 4MB に引き上がる。
	desc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED;
	desc.pDevice = device;
	desc.pAdapter = adapter.Get();

	Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator;
	if (FAILED(D3D12MA::CreateAllocator(&desc, allocator.GetAddressOf())))
	{
		return nullptr;
	}
	return std::make_shared<GpuAllocatorOwner>(device, std::move(allocator));
}

/// 同じ ID3D12Device には同じアロケータを返す (D3D12 はアダプタ 1 枚につきデバイスを
/// 1 個しか作らないので、テストが直に作ったデバイスも Dx12Device と同じ物になる)。
[[nodiscard]] inline std::shared_ptr<GpuAllocatorOwner> acquireGpuAllocator(ID3D12Device* device)
{
	return GpuAllocatorOwner::findOrPublish(device, &createGpuAllocator);
}

/// 統計を読むだけの側がアロケータを生成しないよう、探すだけで作らない。
[[nodiscard]] inline std::shared_ptr<GpuAllocatorOwner> findGpuAllocator(ID3D12Device* device)
{
	return GpuAllocatorOwner::findFor(device);
}

} // namespace detail

/// @brief D3D12MA が確保したリソース。ComPtr<ID3D12Resource> と同じ書き味で使える。
/// @details 手放したメモリは、Dx12Device のキューがその時点で投入済みのフレームを
///          終えるまでアロケータへ戻らない。リサイズ等で作り直すときに完了待ちは要らない。
class GpuResource
{
public:
	GpuResource() = default;
	~GpuResource() { Reset(); }

	GpuResource(const GpuResource&) = default;
	GpuResource(GpuResource&&) noexcept = default;

	GpuResource& operator=(const GpuResource& other)
	{
		if (this != &other)
		{
			GpuResource copy(other);
			*this = std::move(copy);
		}
		return *this;
	}

	GpuResource& operator=(GpuResource&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_owner = std::move(other.m_owner);
			m_allocation = std::move(other.m_allocation);
		}
		return *this;
	}

	GpuResource(std::shared_ptr<detail::GpuAllocatorOwner> owner,
	            Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation) noexcept
		: m_owner(std::move(owner))
		, m_allocation(std::move(allocation))
	{
	}

	[[nodiscard]] ID3D12Resource* Get() const noexcept
	{
		return m_allocation ? m_allocation->GetResource() : nullptr;
	}

	[[nodiscard]] ID3D12Resource* operator->() const noexcept { return Get(); }

	explicit operator bool() const noexcept { return Get() != nullptr; }

	void Reset() noexcept
	{
		if (m_owner && m_allocation) { m_owner->retire(std::move(m_allocation)); }
		m_allocation.Reset();
		m_owner.reset();
	}

private:
	std::shared_ptr<detail::GpuAllocatorOwner> m_owner;
	Microsoft::WRL::ComPtr<D3D12MA::Allocation> m_allocation;
};

/// @brief CreateCommittedResource の置き換え。
/// @details RT / DS / UAV は committed に固定する。RT / DS は配置リソースだと最初の操作を
///          Clear / Discard / Copy にする義務が生じ、UAV は GPU が書く前に読むパス
///          (カウンタ・間接引数) が committed のゼロ初期化を当てにしているため。
///          それ以外 (upload / readback / 転送で埋めるテクスチャ・頂点) は D3D12MA に任せる。
[[nodiscard]] inline HRESULT createGpuResource(
	ID3D12Device* device,
	D3D12_HEAP_TYPE heapType,
	const D3D12_RESOURCE_DESC& desc,
	D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE* clearValue,
	GpuResource& out)
{
	out.Reset();
	auto owner = detail::acquireGpuAllocator(device);
	if (!owner) { return E_FAIL; }

	constexpr D3D12_RESOURCE_FLAGS kCommittedOnly =
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12MA::ALLOCATION_DESC allocDesc = {};
	allocDesc.HeapType = heapType;
	if ((desc.Flags & kCommittedOnly) != 0)
	{
		allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
	}

	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
	const HRESULT hr = owner->createResource(allocDesc, desc, initialState, clearValue,
		allocation.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		out = GpuResource(std::move(owner), std::move(allocation));
	}
	return hr;
}

/// @brief 1 次元バッファ用の createGpuResource。
[[nodiscard]] inline HRESULT createGpuBuffer(
	ID3D12Device* device,
	D3D12_HEAP_TYPE heapType,
	std::uint64_t sizeBytes,
	D3D12_RESOURCE_STATES initialState,
	GpuResource& out,
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;
	return createGpuResource(device, heapType, desc, initialState, nullptr, out);
}

/// @brief デバイスのアロケータが今抱えている確保の集計。
struct GpuMemoryStats
{
	std::uint32_t allocationCount = 0;
	std::uint64_t allocationBytes = 0;
	std::uint32_t blockCount = 0;
	std::uint64_t blockBytes = 0;
	std::size_t retiredCount = 0;  ///< 手放されたが GPU の完了待ちでまだ返していない確保
};

/// @brief アロケータが無い (確保が 1 つも無く Dx12Device も無い) ときは全て 0。
[[nodiscard]] inline GpuMemoryStats gpuMemoryStats(ID3D12Device* device)
{
	const auto owner = detail::findGpuAllocator(device);
	if (!owner) { return {}; }
	owner->releaseCompleted();
	D3D12MA::TotalStatistics total = {};
	owner->allocator()->CalculateStatistics(&total);
	const auto& s = total.Total.Stats;
	return {s.AllocationCount, s.AllocationBytes, s.BlockCount, s.BlockBytes, owner->retiredCount()};
}

} // namespace mitiru::gfx

#endif // _WIN32
