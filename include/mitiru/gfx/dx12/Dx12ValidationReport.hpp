#pragma once

/// @file Dx12ValidationReport.hpp
/// @brief debug layer と GPU-Based Validation が見つけた誤りを、出たその場で端末へ出す
/// @details `MITIRU_D3D12_DEBUG=1` の実行で ID3D12InfoQueue1 の callback を登録する。ERROR と CORRUPTION は常に、
///          WARNING は `MITIRU_LOG=verbose` の時だけ出す。WARNING には遅くなるだけの知らせ (clear 色が作った時の値と違う等) が
///          毎フレーム出るため。行には `D3D12 <重さ> #<ID>` を入れ、ctest の gbv の試験はこれを見つけたら落とす
///          (tests/CMakeLists.txt の FAIL_REGULAR_EXPRESSION)。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/debug/ConsoleOut.hpp>

namespace mitiru::gfx::validation
{

[[nodiscard]] inline const char* severityName(D3D12_MESSAGE_SEVERITY s) noexcept
{
	switch (s)
	{
	case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
	case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
	default: return "WARNING";
	}
}

inline void CALLBACK onMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                               LPCSTR description, void*)
{
	if (severity > D3D12_MESSAGE_SEVERITY_WARNING) { return; }
	const char* text = description != nullptr ? description : "";
	if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
	{
		console::verbosef("D3D12 の検証が注意を出しました。D3D12 WARNING #%d: %s", static_cast<int>(id), text);
		return;
	}
	console::noticef("D3D12 の検証が誤りを報告しました。D3D12 %s #%d: %s", severityName(severity), static_cast<int>(id), text);
}

/// @brief debug device なら callback を登録する。InfoQueue1 が無い debug layer (Windows 10 の古い版) なら知らせて何もしない
inline void reportToConsole(ID3D12Device* device)
{
	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> queue;
	if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()))))
	{
		console::notice("MITIRU_D3D12_DEBUG=1 ですが、この debug layer は誤りをその場で知らせる口 (ID3D12InfoQueue1) を持ちません。"
		                "Windows 11 か新しい Graphics Tools で走らせてください。");
		return;
	}
	DWORD cookie = 0;
	if (FAILED(queue->RegisterMessageCallback(&onMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie)))
	{
		console::notice("D3D12 の検証の知らせを受け取る callback を登録できませんでした。誤りは端末に出ません。");
	}
}

} // namespace mitiru::gfx::validation

#endif // _WIN32
