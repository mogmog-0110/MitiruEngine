#pragma once

/// @file Dx12DescriptorPool.hpp
/// @brief DX12 descriptor heap pool with offline free-list suballocation
/// @details Wraps a single ID3D12DescriptorHeap and manages descriptor slots
///          through a DescriptorFreeList that coalesces adjacent free blocks.
///
///          API:
///            ```cpp
///            Dx12DescriptorPool pool;
///            pool.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
///                            1024, /*shaderVisible=*/true);
///
///            auto slot = pool.allocate(3);   // 3 contiguous descriptors
///            // ... write descriptors into slot.cpuHandle + n * incrementSize
///            pool.free(slot);
///            ```
///
///          Thread-safety: NOT thread-safe. Caller must synchronize when
///          allocating/freeing from multiple threads.

#ifdef _WIN32

#include <cassert>
#include <cstdint>
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

// ─────────────────────────────────────────────────────────────────────────────
// DescriptorFreeList — pure math, no D3D12 dependency (testable standalone)
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Contiguous run of free descriptor slots
struct FreeBlock
{
    UINT offset = 0; ///< First free slot index
    UINT count  = 0; ///< Number of consecutive free slots
};

/// @brief Offset-based free-list allocator for descriptor indices.
/// @details Stores free runs as sorted FreeBlock entries and coalesces
///          adjacent blocks on free(). No D3D12 types used here so unit
///          tests can run without a device.
///
///          Usage:
///            ```cpp
///            DescriptorFreeList fl(1024);
///            UINT off = fl.allocate(4);   // returns 0
///            fl.free(off, 4);             // coalesces back to [0,1024)
///            ```
class DescriptorFreeList
{
public:
    static constexpr UINT kInvalid = ~0u;

    explicit DescriptorFreeList(UINT capacity = 0)
        : m_capacity(capacity)
    {
        if (capacity > 0)
        {
            m_blocks.push_back({0, capacity});
        }
    }

    /// @brief Allocate `count` contiguous descriptor slots.
    /// @return Starting slot index, or kInvalid when the heap is exhausted.
    [[nodiscard]] UINT allocate(UINT count)
    {
        if (count == 0) return kInvalid;

        for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it)
        {
            if (it->count < count) continue;

            const UINT offset = it->offset;
            it->offset += count;
            it->count  -= count;
            if (it->count == 0)
            {
                m_blocks.erase(it);
            }
            return offset;
        }
        return kInvalid; // exhausted
    }

    /// @brief Return `count` slots starting at `offset` back to the free list.
    /// @details Adjacent blocks are coalesced to avoid fragmentation.
    void free(UINT offset, UINT count)
    {
        assert(count > 0 && offset + count <= m_capacity);

        // Find insertion point (blocks kept sorted by offset)
        auto it = m_blocks.begin();
        while (it != m_blocks.end() && it->offset < offset) ++it;

        const bool hasPrev = (it != m_blocks.begin());
        auto prev = hasPrev ? std::prev(it) : m_blocks.end();

        const bool mergeLeft  = hasPrev && (prev->offset + prev->count == offset);
        const bool mergeRight = (it != m_blocks.end()) && (offset + count == it->offset);

        if (mergeLeft && mergeRight)
        {
            prev->count += count + it->count;
            m_blocks.erase(it);
        }
        else if (mergeLeft)
        {
            prev->count += count;
        }
        else if (mergeRight)
        {
            it->offset  = offset;
            it->count  += count;
        }
        else
        {
            m_blocks.insert(it, {offset, count});
        }
    }

    /// @brief Total capacity this list was constructed with.
    [[nodiscard]] UINT capacity()    const noexcept { return m_capacity; }

    /// @brief Number of free-block entries (diagnostic / test use).
    [[nodiscard]] std::size_t blockCount() const noexcept { return m_blocks.size(); }

    /// @brief True when every slot is free (single contiguous block == capacity).
    [[nodiscard]] bool fullyFree() const noexcept
    {
        return m_blocks.size() == 1
            && m_blocks[0].offset == 0
            && m_blocks[0].count  == m_capacity;
    }

private:
    UINT                  m_capacity = 0;
    std::vector<FreeBlock> m_blocks;
};


// ─────────────────────────────────────────────────────────────────────────────
// Dx12DescriptorPool
// ─────────────────────────────────────────────────────────────────────────────

/// @brief A suballocated D3D12 descriptor heap slot
struct DescriptorSlot
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle  = {0}; ///< CPU handle of first descriptor
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle  = {0}; ///< GPU handle (valid only if shaderVisible)
    UINT                        offset     = 0;   ///< Slot index inside the pool
    UINT                        count      = 0;   ///< Number of descriptors allocated

    /// @brief True if this slot was successfully allocated
    [[nodiscard]] bool valid() const noexcept { return count > 0; }
};

/// @brief Suballocated DX12 descriptor heap pool.
/// @details Owns one ID3D12DescriptorHeap and uses DescriptorFreeList to hand
///          out contiguous descriptor ranges. Freed slots are immediately
///          returned and coalesced.
///
///          The heap is created once at initialize() and never resized —
///          pass a generous capacity up front (e.g., 4096 for CBV/SRV/UAV).
class Dx12DescriptorPool
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
    Dx12DescriptorPool() = default;
    ~Dx12DescriptorPool() { destroy(); }

    Dx12DescriptorPool(const Dx12DescriptorPool&) = delete;
    Dx12DescriptorPool& operator=(const Dx12DescriptorPool&) = delete;

    /// @brief Create the backing descriptor heap.
    /// @param device        D3D12 device
    /// @param type          Heap type (CBV_SRV_UAV, SAMPLER, RTV, DSV)
    /// @param capacity      Total number of descriptors
    /// @param shaderVisible True for CBV/SRV/UAV and SAMPLER heaps bound to the pipeline
    /// @return true on success
    bool initialize(ID3D12Device*              device,
                    D3D12_DESCRIPTOR_HEAP_TYPE type,
                    UINT                       capacity,
                    bool                       shaderVisible)
    {
        if (!device || capacity == 0) return false;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = type;
        desc.NumDescriptors = capacity;
        desc.Flags          = shaderVisible
                                  ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                  : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.GetAddressOf()))))
        {
            return false;
        }

        m_incrementSize   = device->GetDescriptorHandleIncrementSize(type);
        m_cpuBase         = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_gpuBase         = shaderVisible
                                ? m_heap->GetGPUDescriptorHandleForHeapStart()
                                : D3D12_GPU_DESCRIPTOR_HANDLE{0};
        m_shaderVisible   = shaderVisible;
        m_freeList        = DescriptorFreeList(capacity);
        return true;
    }

    /// @brief Release the heap and reset free-list state.
    void destroy()
    {
        m_heap.Reset();
        m_freeList      = DescriptorFreeList();
        m_incrementSize = 0;
        m_shaderVisible = false;
    }

    /// @brief Allocate `count` contiguous descriptors.
    /// @return DescriptorSlot with valid()==true on success.
    [[nodiscard]] DescriptorSlot allocate(UINT count = 1)
    {
        if (!m_heap) return {};

        const UINT offset = m_freeList.allocate(count);
        if (offset == DescriptorFreeList::kInvalid) return {};

        DescriptorSlot slot;
        slot.offset      = offset;
        slot.count       = count;
        slot.cpuHandle.ptr = m_cpuBase.ptr + static_cast<SIZE_T>(offset) * m_incrementSize;
        if (m_shaderVisible)
        {
            slot.gpuHandle.ptr = m_gpuBase.ptr + static_cast<UINT64>(offset) * m_incrementSize;
        }
        return slot;
    }

    /// @brief Return a previously allocated slot back to the pool.
    void free(const DescriptorSlot& slot)
    {
        if (!slot.valid()) return;
        m_freeList.free(slot.offset, slot.count);
    }

    /// @brief Expose the backing heap for binding to the command list.
    [[nodiscard]] ID3D12DescriptorHeap* heap() const noexcept { return m_heap.Get(); }

    /// @brief Descriptor increment size in bytes.
    [[nodiscard]] UINT incrementSize() const noexcept { return m_incrementSize; }

    /// @brief True when the pool has been successfully initialized.
    [[nodiscard]] bool ready() const noexcept { return m_heap != nullptr; }

private:
    ComPtr<ID3D12DescriptorHeap>   m_heap;
    DescriptorFreeList             m_freeList;
    D3D12_CPU_DESCRIPTOR_HANDLE    m_cpuBase         = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE    m_gpuBase         = {0};
    UINT                           m_incrementSize   = 0;
    bool                           m_shaderVisible   = false;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
