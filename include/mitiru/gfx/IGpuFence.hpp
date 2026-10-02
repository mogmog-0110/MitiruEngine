#pragma once

/// @file IGpuFence.hpp
/// @brief GPU フェンス抽象インターフェース
/// @details CPU-GPU 間の同期を管理するフェンスの基底インターフェース。
///          D3D12 の ID3D12Fence に対応する抽象レイヤー。

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief GPU フェンスの抽象インターフェース
/// @details GPU 側の処理完了を CPU から追跡・待機するためのインターフェース。
///          トリプルバッファリングのフレーム同期に使用する。
///
/// @code
/// auto fence = device->createFence();
/// // GPU にシグナルを発行
/// fence->signal(frameIndex);
/// // CPU 側で完了を待機
/// fence->waitForValue(frameIndex);
/// @endcode
class IGpuFence
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IGpuFence() = default;

	/// @brief 現在のフェンス値を取得する
	/// @return GPU 側が完了した最新のフェンス値
	[[nodiscard]] virtual FenceValue currentValue() const = 0;

	/// @brief フェンスにシグナルを発行する
	/// @param value シグナルするフェンス値
	virtual void signal(FenceValue value) = 0;

	/// @brief 指定したフェンス値の完了を待機する
	/// @param value 待機するフェンス値
	virtual void waitForValue(FenceValue value) = 0;

	/// @brief 指定したフェンス値が完了済みかどうかを判定する
	/// @param value 判定するフェンス値
	/// @return 完了していれば true
	[[nodiscard]] virtual bool isComplete(FenceValue value) const = 0;
};

} // namespace mitiru::gfx
