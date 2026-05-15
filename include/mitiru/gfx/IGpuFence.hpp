#pragma once

/// @file IGpuFence.hpp
/// @brief GPUフェンス抽象インターフェース
/// @details CPU-GPU間の同期を管理するフェンスの基底インターフェース。
///          D3D12のID3D12Fenceに対応する抽象レイヤー。

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief GPUフェンスの抽象インターフェース
/// @details GPU側の処理完了をCPUから追跡・待機するためのインターフェース。
///          トリプルバッファリングのフレーム同期に使用する。
///
/// @code
/// auto fence = device->createFence();
/// // GPUにシグナルを発行
/// fence->signal(frameIndex);
/// // CPU側で完了を待機
/// fence->waitForValue(frameIndex);
/// @endcode
class IGpuFence
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IGpuFence() = default;

	/// @brief 現在のフェンス値を取得する
	/// @return GPU側が完了した最新のフェンス値
	[[nodiscard]] virtual FenceValue currentValue() const = 0;

	/// @brief フェンスにシグナルを発行する
	/// @param value シグナルするフェンス値
	virtual void signal(FenceValue value) = 0;

	/// @brief 指定したフェンス値の完了を待機する
	/// @param value 待機するフェンス値
	virtual void waitForValue(FenceValue value) = 0;

	/// @brief 指定したフェンス値が完了済みかどうかを判定する
	/// @param value 判定するフェンス値
	/// @return 完了していればtrue
	[[nodiscard]] virtual bool isComplete(FenceValue value) const = 0;
};

} // namespace mitiru::gfx
