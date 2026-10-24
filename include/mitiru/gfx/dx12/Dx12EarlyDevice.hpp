#pragma once

/// @file Dx12EarlyDevice.hpp
/// @brief D3D12 のデバイス作りを、窓や音や DLL の読み込みと並べて始める
/// @details D3D12CreateDevice は初めての呼び出しで GPU のドライバーを読み込むので 250 ms 前後かかり、起動の半分以上を占める。
///          host が起動の早い時点で `EarlyDevice::start` を呼ぶと、別スレッドが DXGI のファクトリ、アダプタ選び、デバイス、
///          コマンドキューまで作る。窓を作る側 (`Dx12Device` の createDevice) は `take` で結果を受け取り、まだなら待つ。
///          始めていない実行 (試験、headless) は今までどおり `Dx12Device` が自分で作る。
///          結果の条件 (WARP を許すか、WARP でやり直すか) が受け取る側と食い違えば、結果は捨てて自分で作り直す。
///          例外 (GPU が使えない) は受け取る側へ投げ直すので、フォールバックの動きは今までと同じ。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/BootTrace.hpp>
#include <mitiru/gfx/GpuCompat.hpp>
#include <mitiru/gfx/GpuNotice.hpp>
#include <mitiru/gfx/dx12/Dx12DredReport.hpp>
#include <mitiru/gfx/dx12/Dx12GpuProbe.hpp>

namespace mitiru::gfx
{

/// アダプタ選びまでの結果
struct PreparedDevice
{
	Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
	compat::DeviceChoice choice;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;   ///< 早い作成のときだけ入る。作れなければ空で、受け取る側が作って失敗を報告する
	bool allowWarp = true;
	bool retryWithWarp = false;
};

/// debug layer、DRED、DXGI ファクトリ、アダプタ選びとデバイス作成。Dx12Device の createDevice の前半
[[nodiscard]] inline PreparedDevice prepareAdapterDevice(bool allowWarp)
{
	PreparedDevice out;
	out.allowWarp = allowWarp;
	out.retryWithWarp = compat::retryWithWarpSlot();
	bool d3d12Debug = false;
	// D3D12 デバッグレイヤー(検証用)。環境によっては D3D12SDKLayers.dll が 0xc0000005 でクラッシュし、Debug ビルドの
	// consumer ゲームを巻き込んで落とす。既定 OFF にし、グラフィクス検証時のみ MITIRU_D3D12_DEBUG=1 で opt-in する。
	if (env::flag("MITIRU_D3D12_DEBUG"))
	{
		d3d12Debug = true;
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
		{
			debugController->EnableDebugLayer();
			// MITIRU_D3D12_GBV=1 で GPU-Based Validation を追加有効化する (device removed の原因操作を特定する診断用 opt-in)
			if (env::flag("MITIRU_D3D12_GBV"))
			{
				Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
				if (SUCCEEDED(debugController.As(&debug1))) { debug1->SetEnableGPUBasedValidation(TRUE); }
			}
		}
	}
	// debug layer より前でも後でもよいが、D3D12CreateDevice より前でないと有効にならない
	dred::enableIfRequested();
	mitiru::debug::boottrace::mark("dx12_begin");

	UINT dxgiFactoryFlags = 0;
	if (d3d12Debug) { dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG; }
	const HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(out.factory.GetAddressOf()));
	if (FAILED(hr))
	{
		char detail[48];
		std::snprintf(detail, sizeof(detail), "CreateDXGIFactory2 0x%08lX", static_cast<unsigned long>(hr));
		throw compat::GpuCompatError(compat::GpuFailure::DeviceSetup,
			compat::failureMessage(compat::GpuFailure::DeviceSetup, {}, detail));
	}
	mitiru::debug::boottrace::mark("dxgi_factory");
	out.choice = compat::createBestDevice(out.factory.Get(), allowWarp);
	mitiru::debug::boottrace::mark("dx12_adapter_device");
	return out;
}

class EarlyDevice
{
public:
	/// 1 回だけ始める。2 回目以降は何もしない。呼ぶ前に環境変数の書き込みを済ませること (スレッドが環境変数を読む)
	static void start(bool allowWarp)
	{
		State& s = state();
		const std::lock_guard<std::mutex> lock(s.mutex);
		if (s.started) { return; }
		s.started = true;
		s.thread = std::thread([allowWarp] { run(allowWarp); });
	}

	/// 結果を 1 回だけ渡す。始めていなければ nullopt。まだなら待つ。作成が投げた例外は、ここで投げ直す。
	/// 条件が食い違う結果は捨てて nullopt (受け取る側が自分で作り直す)
	[[nodiscard]] static std::optional<PreparedDevice> take(bool allowWarp)
	{
		State& s = state();
		std::unique_lock<std::mutex> lock(s.mutex);
		if (!s.started || s.taken) { return std::nullopt; }
		s.taken = true;
		s.cv.wait(lock, [&s] { return s.done; });
		lock.unlock();
		if (s.thread.joinable()) { s.thread.join(); }
		if (s.error) { std::rethrow_exception(std::exchange(s.error, nullptr)); }
		if (s.result.allowWarp != allowWarp || s.result.retryWithWarp != compat::retryWithWarpSlot()) { s.result = {}; return std::nullopt; }
		return std::exchange(s.result, PreparedDevice{});
	}

	[[nodiscard]] static bool started() noexcept { return state().started; }

private:
	struct State
	{
		std::mutex mutex;
		std::condition_variable cv;
		std::thread thread;
		bool started = false;
		bool taken = false;
		bool done = false;
		std::exception_ptr error;
		PreparedDevice result;

		// 受け取られないまま終わる実行 (引数の誤りなど) でも、スレッドが動いたままプロセスを閉じない
		~State()
		{
			if (thread.joinable()) { thread.join(); }
		}
	};

	static State& state()
	{
		static State s;
		return s;
	}

	static void run(bool allowWarp) noexcept
	{
		State& s = state();
		PreparedDevice prepared;
		std::exception_ptr error;
		try
		{
			prepared = prepareAdapterDevice(allowWarp);
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			if (FAILED(prepared.choice.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(prepared.queue.GetAddressOf()))))
			{
				prepared.queue.Reset();
			}
			mitiru::debug::boottrace::mark("dx12_queue_created");
		}
		catch (...)
		{
			error = std::current_exception();
		}
		{
			const std::lock_guard<std::mutex> lock(s.mutex);
			s.result = std::move(prepared);
			s.error = error;
			s.done = true;
		}
		s.cv.notify_all();
	}
};

}  // namespace mitiru::gfx

#endif  // _WIN32
