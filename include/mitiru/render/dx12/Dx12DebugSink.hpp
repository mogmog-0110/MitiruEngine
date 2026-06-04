#pragma once

/// @file Dx12DebugSink.hpp
/// @brief D3D12 デバッグレイヤ有効化 + InfoQueue メッセージ吸い出しヘルパ (NPR #28)
/// @details デバイスロスト級のバグ特定で決定打になるのが「EnableDebugLayer + InfoQueue を
///          毎フレーム drain」。毎回手書きするのは惜しいので validate/ 群の流儀でまとめた。
///
///          使い方:
///            1. デバイス生成「前」に `Dx12DebugSink::enableDebugLayer()`
///            2. デバイス生成「後」に `sink.attach(device)`（debug device のみ成功）
///            3. 各サブミット後/フレーム末に `sink.drain()` で蓄積メッセージを stderr へ流す
///
/// @code
/// Dx12DebugSink::enableDebugLayer();          // CreateDevice より前
/// // ... D3D12CreateDevice(...) ...
/// Dx12DebugSink sink; sink.attach(device);
/// // ... record + ExecuteCommandLists + sync ...
/// sink.drain();                               // WARNING 以上を吐いてクリア
/// @endcode

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

namespace mitiru::render::dx12
{

/// @brief D3D12 デバッグレイヤ + InfoQueue ヘルパ
class Dx12DebugSink
{
public:
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デバッグレイヤを有効化する（D3D12CreateDevice より前に呼ぶ）。
	/// @return 有効化できたら true（SDK/環境に debug layer が無ければ false）。
	static bool enableDebugLayer()
	{
		ComPtr<ID3D12Debug> dbg;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dbg.GetAddressOf()))))
		{
			dbg->EnableDebugLayer();
			return true;
		}
		return false;
	}

	/// @brief デバイスの InfoQueue を取得して以後 drain 可能にする（debug device のみ成功）。
	/// @return InfoQueue を取れたら true。
	bool attach(ID3D12Device* device)
	{
		if (device == nullptr) { return false; }
		return SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(m_infoQueue.GetAddressOf())));
	}

	/// @brief 蓄積メッセージを stderr へ流してクリアする。
	/// @param minSeverity これ以上「深刻」なものだけ出す（既定 WARNING）。
	///        D3D12_MESSAGE_SEVERITY は CORRUPTION(0) < ERROR(1) < WARNING(2) < INFO(3) < MESSAGE(4)
	///        で、数値が小さいほど深刻。よって `Severity <= minSeverity` を出す。
	/// @return 出力した件数。
	int drain(D3D12_MESSAGE_SEVERITY minSeverity = D3D12_MESSAGE_SEVERITY_WARNING)
	{
		if (!m_infoQueue) { return 0; }
		const UINT64 n = m_infoQueue->GetNumStoredMessages();
		int emitted = 0;
		for (UINT64 i = 0; i < n; ++i)
		{
			SIZE_T len = 0;
			m_infoQueue->GetMessage(i, nullptr, &len);
			if (len == 0) { continue; }
			std::vector<char> buf(len);
			auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
			if (FAILED(m_infoQueue->GetMessage(i, msg, &len))) { continue; }
			if (msg->Severity > minSeverity) { continue; }
			std::fprintf(stderr, "[d3d12] %s\n", msg->pDescription ? msg->pDescription : "(no desc)");
			++emitted;
		}
		m_infoQueue->ClearStoredMessages();
		return emitted;
	}

	/// @brief attach 済みか。
	[[nodiscard]] bool attached() const noexcept { return m_infoQueue != nullptr; }

private:
	ComPtr<ID3D12InfoQueue> m_infoQueue;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
