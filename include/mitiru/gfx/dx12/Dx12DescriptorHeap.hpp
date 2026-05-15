#pragma once

/// @file Dx12DescriptorHeap.hpp
/// @brief DirectX 12 デスクリプタヒープ実装
/// @details ID3D12DescriptorHeapをラップし、リニアアロケータによる
///          デスクリプタの割り当てと管理を提供するIDescriptorHeap実装。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/IDescriptorHeap.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12デスクリプタヒープ実装
/// @details リニアアロケータで効率的にデスクリプタを割り当てる。
///          フリーリストによる再利用もサポートする。
///
/// @code
/// auto heap = device->createDescriptorHeap(DescriptorHeapType::CbvSrvUav, 256);
/// auto cpuHandle = heap->allocate();
/// auto gpuHandle = heap->gpuHandle(cpuHandle);
/// @endcode
class Dx12DescriptorHeap final : public IDescriptorHeap
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param heapType ヒープ種別
	/// @param capacity 最大デスクリプタ数
	Dx12DescriptorHeap(ID3D12Device* device,
	                   DescriptorHeapType heapType,
	                   uint32_t capacity)
		: m_heapType(heapType)
		, m_capacity(capacity)
	{
		if (!device || capacity == 0)
		{
			throw std::runtime_error(
				"Dx12DescriptorHeap: invalid device or capacity");
		}

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = capacity;
		desc.Type = toD3D12HeapType(heapType);
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		/// CBV/SRV/UAVとSamplerヒープはシェーダーから参照可能にする
		if (heapType == DescriptorHeapType::CbvSrvUav ||
		    heapType == DescriptorHeapType::Sampler)
		{
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		}

		HRESULT hr = device->CreateDescriptorHeap(
			&desc, IID_PPV_ARGS(m_heap.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12DescriptorHeap: CreateDescriptorHeap failed");
		}

		m_descriptorSize = device->GetDescriptorHandleIncrementSize(desc.Type);
		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();

		/// シェーダー可視ヒープの場合はGPUハンドルも取得
		if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
			m_shaderVisible = true;
		}
	}

	/// @brief ヒープ種別を取得する
	[[nodiscard]] DescriptorHeapType type() const override
	{
		return m_heapType;
	}

	/// @brief ヒープの最大容量を取得する
	[[nodiscard]] uint32_t capacity() const override
	{
		return m_capacity;
	}

	/// @brief 割り当て済みデスクリプタ数を取得する
	[[nodiscard]] uint32_t allocatedCount() const override
	{
		return m_allocatedCount;
	}

	/// @brief デスクリプタを1つ割り当てる
	/// @return 割り当てられたCPUデスクリプタハンドル
	[[nodiscard]] CpuDescriptorHandle allocate() override
	{
		/// フリーリストに空きがあれば再利用する
		if (!m_freeList.empty())
		{
			const auto handle = m_freeList.back();
			m_freeList.pop_back();
			++m_allocatedCount;
			return handle;
		}

		if (m_nextIndex >= m_capacity)
		{
			throw std::runtime_error(
				"Dx12DescriptorHeap: out of descriptors");
		}

		CpuDescriptorHandle handle;
		handle.ptr = m_cpuStart.ptr +
			static_cast<uint64_t>(m_nextIndex) * m_descriptorSize;
		++m_nextIndex;
		++m_allocatedCount;
		return handle;
	}

	/// @brief デスクリプタを解放する
	/// @param handle 解放するCPUデスクリプタハンドル
	void free(CpuDescriptorHandle handle) override
	{
		if (handle.isValid() && m_allocatedCount > 0)
		{
			m_freeList.push_back(handle);
			--m_allocatedCount;
		}
	}

	/// @brief CPUハンドルに対応するGPUハンドルを取得する
	/// @param cpuHandle 変換元のCPUデスクリプタハンドル
	/// @return 対応するGPUデスクリプタハンドル
	[[nodiscard]] GpuDescriptorHandle gpuHandle(
		CpuDescriptorHandle cpuHandle) const override
	{
		if (!m_shaderVisible)
		{
			return GpuDescriptorHandle{0};
		}

		const uint64_t offset = cpuHandle.ptr - m_cpuStart.ptr;
		GpuDescriptorHandle handle;
		handle.ptr = m_gpuStart.ptr + offset;
		return handle;
	}

	/// @brief 内部のID3D12DescriptorHeapを取得する
	/// @return ヒープへのポインタ
	[[nodiscard]] ID3D12DescriptorHeap* nativeHeap() const noexcept
	{
		return m_heap.Get();
	}

	/// @brief デスクリプタサイズを取得する
	/// @return 1デスクリプタあたりのバイト数
	[[nodiscard]] uint32_t descriptorSize() const noexcept
	{
		return m_descriptorSize;
	}

private:
	/// @brief DescriptorHeapTypeをD3D12_DESCRIPTOR_HEAP_TYPEに変換する
	/// @param heapType 変換元の種別
	/// @return D3D12のヒープ種別
	[[nodiscard]] static D3D12_DESCRIPTOR_HEAP_TYPE toD3D12HeapType(
		DescriptorHeapType heapType) noexcept
	{
		switch (heapType)
		{
		case DescriptorHeapType::CbvSrvUav:
			return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		case DescriptorHeapType::Rtv:
			return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		case DescriptorHeapType::Dsv:
			return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		case DescriptorHeapType::Sampler:
			return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		default:
			return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		}
	}

	ComPtr<ID3D12DescriptorHeap> m_heap;          ///< D3D12デスクリプタヒープ
	DescriptorHeapType m_heapType;                 ///< ヒープ種別
	uint32_t m_capacity = 0;                       ///< 最大容量
	uint32_t m_nextIndex = 0;                      ///< 次の割り当てインデックス
	uint32_t m_allocatedCount = 0;                 ///< 現在の割り当て数
	uint32_t m_descriptorSize = 0;                 ///< デスクリプタサイズ
	D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};   ///< CPU側先頭ハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};   ///< GPU側先頭ハンドル
	bool m_shaderVisible = false;                  ///< シェーダー可視フラグ
	std::vector<CpuDescriptorHandle> m_freeList;   ///< 解放済みデスクリプタリスト
};

} // namespace mitiru::gfx

#endif // _WIN32
