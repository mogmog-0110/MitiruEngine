#pragma once

/// @file Dx12DescriptorPool.hpp
/// @brief offline free-list サブアロケーション付き DX12 descriptor heap pool
/// @details 単一の ID3D12DescriptorHeap をラップし、隣接する空きブロックを
///          結合する DescriptorFreeList を通じて descriptor slot を管理する。
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
///          Thread-safety: スレッドセーフではない。複数スレッドから
///          allocate / free する場合は呼び出し側で同期すること。

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
// DescriptorFreeList。純粋な計算のみ、D3D12 依存なし (単体でテスト可能)
// ─────────────────────────────────────────────────────────────────────────────

/// @brief 連続した空き descriptor slot のひとかたまり
struct FreeBlock
{
    UINT offset = 0; ///< 最初の空き slot index
    UINT count  = 0; ///< 連続する空き slot の数
};

/// @brief descriptor index 用の offset ベース free-list allocator。
/// @details 空き領域を offset 順にソートした FreeBlock として保持し、free()
///          時に隣接ブロックを結合する。D3D12 型を使わないので device 無しで
///          unit test を実行できる。
///
///          使い方:
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

    /// @brief 連続した `count` 個の descriptor slot を確保する。
    /// @return 先頭 slot index。heap を使い切った場合は kInvalid。
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
        return kInvalid; // 使い切り
    }

    /// @brief `offset` から始まる `count` 個の slot を free list へ返却する。
    /// @details 断片化を防ぐため隣接ブロックを結合する。
    void free(UINT offset, UINT count)
    {
        assert(count > 0 && offset + count <= m_capacity);

        // 挿入位置を探す (ブロックは offset 順にソート維持)
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

    /// @brief この list を構築したときの総容量。
    [[nodiscard]] UINT capacity()    const noexcept { return m_capacity; }

    /// @brief free-block エントリの数 (診断 / test 用)。
    [[nodiscard]] std::size_t blockCount() const noexcept { return m_blocks.size(); }

    /// @brief 全 slot が空きのとき true (単一の連続ブロック == capacity)。
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

/// @brief サブアロケートされた D3D12 descriptor heap slot
struct DescriptorSlot
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle  = {0}; ///< 先頭 descriptor の CPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle  = {0}; ///< GPU handle (shaderVisible のときのみ有効)
    UINT                        offset     = 0;   ///< pool 内の slot index
    UINT                        count      = 0;   ///< 確保した descriptor の数

    /// @brief この slot が確保に成功していれば true
    [[nodiscard]] bool valid() const noexcept { return count > 0; }
};

/// @brief サブアロケーション方式の DX12 descriptor heap pool。
/// @details 単一の ID3D12DescriptorHeap を所有し、DescriptorFreeList を使って
///          連続した descriptor 範囲を払い出す。free した slot は即座に返却・
///          結合される。
///
///          heap は initialize() で一度だけ作られ resize されない。
///          余裕を持った capacity を前もって渡すこと (例: CBV/SRV/UAV なら 4096)。
class Dx12DescriptorPool
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
    Dx12DescriptorPool() = default;
    ~Dx12DescriptorPool() { destroy(); }

    Dx12DescriptorPool(const Dx12DescriptorPool&) = delete;
    Dx12DescriptorPool& operator=(const Dx12DescriptorPool&) = delete;

    /// @brief 裏付けとなる descriptor heap を生成する。
    /// @param device        D3D12 device
    /// @param type          heap の種別 (CBV_SRV_UAV, SAMPLER, RTV, DSV)
    /// @param capacity      descriptor の総数
    /// @param shaderVisible pipeline に bind する CBV/SRV/UAV・SAMPLER heap なら true
    /// @return 成功時 true
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

    /// @brief heap を解放し free-list の状態をリセットする。
    void destroy()
    {
        m_heap.Reset();
        m_freeList      = DescriptorFreeList();
        m_incrementSize = 0;
        m_shaderVisible = false;
    }

    /// @brief 連続した `count` 個の descriptor を確保する。
    /// @return 成功時は valid()==true の DescriptorSlot。
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

    /// @brief 確保済みの slot を pool へ返却する。
    void free(const DescriptorSlot& slot)
    {
        if (!slot.valid()) return;
        m_freeList.free(slot.offset, slot.count);
    }

    /// @brief command list へ bind するため裏付けの heap を公開する。
    [[nodiscard]] ID3D12DescriptorHeap* heap() const noexcept { return m_heap.Get(); }

    /// @brief descriptor の increment size (byte 単位)。
    [[nodiscard]] UINT incrementSize() const noexcept { return m_incrementSize; }

    /// @brief pool の初期化に成功していれば true。
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
