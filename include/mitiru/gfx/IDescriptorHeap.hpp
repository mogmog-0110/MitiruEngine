#pragma once

/// @file IDescriptorHeap.hpp
/// @brief デスクリプタヒープ抽象インターフェース
/// @details D3D12のデスクリプタヒープを抽象化し、
///          GPU/CPUデスクリプタの割り当てと管理を提供する。

#include <cstdint>

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief デスクリプタヒープの抽象インターフェース
/// @details D3D12のID3D12DescriptorHeapに対応する抽象レイヤー。
///          CBV/SRV/UAV、RTV、DSV、Samplerの各種ヒープを統一的に扱う。
///
/// @code
/// auto heap = device->createDescriptorHeap(DescriptorHeapType::CbvSrvUav, 256);
/// auto cpuHandle = heap->allocate();
/// auto gpuHandle = heap->gpuHandle(cpuHandle);
/// @endcode
class IDescriptorHeap
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IDescriptorHeap() = default;

	/// @brief ヒープ種別を取得する
	/// @return デスクリプタヒープの種別
	[[nodiscard]] virtual DescriptorHeapType type() const = 0;

	/// @brief ヒープの最大容量を取得する
	/// @return デスクリプタの最大数
	[[nodiscard]] virtual uint32_t capacity() const = 0;

	/// @brief 割り当て済みデスクリプタ数を取得する
	/// @return 現在使用中のデスクリプタ数
	[[nodiscard]] virtual uint32_t allocatedCount() const = 0;

	/// @brief デスクリプタを1つ割り当てる
	/// @return 割り当てられたCPUデスクリプタハンドル
	[[nodiscard]] virtual CpuDescriptorHandle allocate() = 0;

	/// @brief デスクリプタを解放する
	/// @param handle 解放するCPUデスクリプタハンドル
	virtual void free(CpuDescriptorHandle handle) = 0;

	/// @brief CPUハンドルに対応するGPUハンドルを取得する
	/// @param cpuHandle 変換元のCPUデスクリプタハンドル
	/// @return 対応するGPUデスクリプタハンドル
	[[nodiscard]] virtual GpuDescriptorHandle gpuHandle(CpuDescriptorHandle cpuHandle) const = 0;
};

} // namespace mitiru::gfx
