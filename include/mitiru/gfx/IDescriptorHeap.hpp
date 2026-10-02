#pragma once

/// @file IDescriptorHeap.hpp
/// @brief デスクリプタヒープ抽象インターフェース
/// @details D3D12 のデスクリプタヒープを抽象化し、
///          GPU/CPU デスクリプタの割り当てと管理を提供する。

#include <cstdint>

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief デスクリプタヒープの抽象インターフェース
/// @details D3D12 の ID3D12DescriptorHeap に対応する抽象レイヤー。
///          CBV/SRV/UAV、RTV、DSV、Sampler の各種ヒープを統一的に扱う。
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

	/// @brief デスクリプタを 1 つ割り当てる
	/// @return 割り当てられた CPU デスクリプタハンドル
	[[nodiscard]] virtual CpuDescriptorHandle allocate() = 0;

	/// @brief デスクリプタを解放する
	/// @param handle 解放する CPU デスクリプタハンドル
	virtual void free(CpuDescriptorHandle handle) = 0;

	/// @brief CPU ハンドルに対応する GPU ハンドルを取得する
	/// @param cpuHandle 変換元の CPU デスクリプタハンドル
	/// @return 対応する GPU デスクリプタハンドル
	[[nodiscard]] virtual GpuDescriptorHandle gpuHandle(CpuDescriptorHandle cpuHandle) const = 0;
};

} // namespace mitiru::gfx
