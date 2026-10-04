#pragma once

/// @file Dx12FenceWait.hpp
/// @brief GPU フェンス待ちに上限を付ける (GAME_REQUESTS #75)
/// @details ドライバが GPU hang から戻らないと、フェンスは完了せずデバイスも removed にならない。
///          `WaitForSingleObject(INFINITE)` はそのまま永遠に戻らず、窓は「応答なし」になる
///          (2026-09-22、nvlddmkm 153 の後に機械上の全 D3D12 プロセスが起動直後の waitForGpu で止まった)。
///          待ちを刻み、刻みごとに完了値と `GetDeviceRemovedReason` を見直して、上限で諦めて呼び出し側へ返す。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/debug/ConsoleOut.hpp>

namespace mitiru::gfx
{

enum class FenceWaitResult : std::uint8_t
{
	Completed,
	DeviceRemoved,
	TimedOut,
	Failed,  ///< SetEventOnCompletion か WaitForSingleObject が失敗した
};

struct FenceWaitBounds
{
	DWORD sliceMs = 2000;
	DWORD totalMs = 8000;
	/// 刻みの合間に PeekMessage を 1 回呼ぶ。Windows は 5 秒 PeekMessage しないスレッドの窓を
	/// 「応答なし」にするので、上限 8 秒の待ちでもこれで窓は応答しているように見える。PM_NOREMOVE なので
	/// 入力は奪わず、配られるのは他スレッドからの SendMessage だけ。
	bool keepWindowResponsive = true;
};

/// ハードウェア GPU 用。TDR は 1 つの GPU 処理が 2 秒を越えると発動するので、正常に進んでいるキューで
/// 1 回の待ちが 8 秒を越えるのは 2 秒近い処理を 4 つ以上積んだときだけになる。
inline constexpr FenceWaitBounds kHardwareFenceWait{2000, 8000, true};
/// WARP は CPU で描くので重い golden シーンは 1 回の待ちが秒単位になる。ハードウェアと同じ上限だと正常な待ちを喪失扱いする。
inline constexpr FenceWaitBounds kWarpFenceWait{2000, 60000, true};

/// 型引数は ID3D12Fence / ID3D12Device を想定し、テストは同じ形の偽物を渡す。event は auto-reset を
/// 前提とする。時間切れで諦めた待ちの登録は後から event を立てるので、event が立っても完了値を見直し、
/// 足りなければ待ち続ける。
template <typename Fence, typename Device>
[[nodiscard]] FenceWaitResult waitForFenceBounded(
	Fence* fence, std::uint64_t value, HANDLE event, Device* device,
	const FenceWaitBounds& bounds = kHardwareFenceWait) noexcept
{
	if (fence == nullptr || event == nullptr) { return FenceWaitResult::Failed; }
	if (fence->GetCompletedValue() >= value) { return FenceWaitResult::Completed; }
	if (FAILED(fence->SetEventOnCompletion(value, event))) { return FenceWaitResult::Failed; }

	const ULONGLONG start = GetTickCount64();
	for (;;)
	{
		const ULONGLONG elapsed = GetTickCount64() - start;
		if (elapsed >= bounds.totalMs) { break; }
		const auto slice = static_cast<DWORD>(
			(std::min)(static_cast<ULONGLONG>(bounds.sliceMs), bounds.totalMs - elapsed));
		if (WaitForSingleObject(event, slice) == WAIT_FAILED) { return FenceWaitResult::Failed; }
		if (fence->GetCompletedValue() >= value) { return FenceWaitResult::Completed; }
		if (device != nullptr && FAILED(device->GetDeviceRemovedReason()))
		{
			return FenceWaitResult::DeviceRemoved;
		}
		if (bounds.keepWindowResponsive)
		{
			MSG msg;
			(void)PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
		}
	}
	return (fence->GetCompletedValue() >= value) ? FenceWaitResult::Completed : FenceWaitResult::TimedOut;
}

[[nodiscard]] inline const char* fenceWaitResultName(FenceWaitResult r) noexcept
{
	switch (r)
	{
	case FenceWaitResult::Completed:     return "完了";
	case FenceWaitResult::DeviceRemoved: return "デバイス喪失";
	case FenceWaitResult::TimedOut:      return "時間切れ";
	case FenceWaitResult::Failed:        return "待機 API の失敗";
	}
	return "?";
}

/// 一度どこかの待ちが時間切れになったら立つ。hang はプロセス単位ではなく機械単位で起きるので
/// (#75 の実例では全 D3D12 プロセスが止まった)、以後の待ちは 1 回ずつ上限まで待たずに諦める。
/// 立てたままにしないと、バッチごとに待つ 2D 描画などが 1 フレームで上限 × 回数だけ固まる。
/// 下ろすのは Dx12Device のフェンス待ちが完了したとき (GPU が進んでいる証拠)。
[[nodiscard]] inline std::atomic<bool>& fenceTimeoutLatch() noexcept
{
	static std::atomic<bool> latched{false};
	return latched;
}

/// 自前のフェンスを持ち、lost の状態を持たない部品 (2D パイプライン、MSAA/LoFi 中間 RT、
/// パーティクル、UI 合成) 用。失敗は端末に知らせて false を返す。
[[nodiscard]] inline bool waitForFenceOrReport(
	ID3D12Fence* fence, std::uint64_t value, HANDLE event, const char* site,
	const FenceWaitBounds& bounds = kHardwareFenceWait) noexcept
{
	if (fence == nullptr) { return false; }
	if (fence->GetCompletedValue() >= value) { return true; }
	if (fenceTimeoutLatch().load(std::memory_order_relaxed)) { return false; }

	Microsoft::WRL::ComPtr<ID3D12Device> device;
	(void)fence->GetDevice(IID_PPV_ARGS(device.GetAddressOf()));
	const FenceWaitResult waited = waitForFenceBounded(fence, value, event, device.Get(), bounds);
	if (waited == FenceWaitResult::Completed) { return true; }
	if (waited == FenceWaitResult::TimedOut) { fenceTimeoutLatch().store(true, std::memory_order_relaxed); }
	console::noticef("GPU の処理が終わるのを待てませんでした (%s)。ゲームを起動し直し、続くようなら GPU ドライバーを更新してください。",
		fenceWaitResultName(waited));
	console::verbosef("待てなかった場所は %s、フェンス値は %llu です。", site, static_cast<unsigned long long>(value));
	return false;
}

} // namespace mitiru::gfx

#endif // _WIN32
