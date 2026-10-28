#pragma once

/// @file HostBoundary62.hpp
/// @brief ABI v62 (ADR 0099) の host 側の小さな関数。Engine を持ち込まずにテストする。使うのは core/detail/Engine_Module_*.hpp

#include <cstdint>
#include <cstring>
#include <string>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module::detail
{

/// Screen::takePendingV62 の bit。入口だけの機能が使われたことを、DLL の側から host へ伝える
inline constexpr std::uint32_t kPendingV62Video        = 1u << 1;
inline constexpr std::uint32_t kPendingV62VerticalText = 1u << 2;

/// 名前で頼まれた先 (モニタ・音の機器)。present = このフレームに頼まれた、name が空 = 既定へ戻す
struct NamedRequest
{
	bool present = false;
	std::string name;
};

[[nodiscard]] inline NamedRequest decodeNamedRequest(std::uint8_t set, const char* field, std::size_t cap) noexcept
{
	NamedRequest r;
	if (set == 0) { return r; }
	r.present = true;
	std::size_t len = 0;
	while (len < cap && field[len] != '\0') { ++len; }
	r.name.assign(field, len);
	return r;
}

[[nodiscard]] inline NamedRequest decodeMonitorRequest(const FrameIntents& intents)
{
	return decodeNamedRequest(intents.windowMonitorSet, intents.windowMonitorName, sizeof(intents.windowMonitorName));
}

[[nodiscard]] inline NamedRequest decodeAudioDeviceRequest(const FrameIntents& intents)
{
	return decodeNamedRequest(intents.audioDeviceSet, intents.audioDeviceName, sizeof(intents.audioDeviceName));
}

/// 入口だけの機能が使われたときの warnOnce の key (試験が発火を確かめる)
inline constexpr const char* kWarnVideo        = "abi62.video";
inline constexpr const char* kWarnVerticalText = "abi62.vertical_text";
/// モニタと音の機器の依頼を受ける host の部品が無いとき
inline constexpr const char* kWarnMonitor      = "abi62.window_monitor";
inline constexpr const char* kWarnAudioDevice  = "abi62.audio_device";

/// Screen::takePendingV62 の印を、使われた機能ごとに 1 度だけ知らせる。文は何が起きるかを「。」なしの 1 文で言う
inline void reportPendingV62(std::uint32_t bits)
{
	if ((bits & kPendingV62Video) != 0)
	{
		debug::warnOnce(kWarnVideo, "動画 (drawVideo) を描けません (名前が 96 byte を超えるか、この host に動画の口がありません)");
	}
	if ((bits & kPendingV62VerticalText) != 0)
	{
		debug::warnOnce(kWarnVerticalText, "縦書き (drawTextVertical) は、この描画では書体を使えないので横書きで描きます");
	}
}

/// モニタと音の機器の依頼を受ける部品 (EngineConfig の onMonitorRequest / onAudioDeviceRequest) が無い host では、
/// 依頼を捨てて 1 度だけ知らせる
inline void reportUnhandledEnvironmentRequestV62(const NamedRequest& monitor, const NamedRequest& audio, bool monitorHandled, bool audioHandled)
{
	if (monitor.present && !monitorHandled)
	{
		debug::warnOnce(kWarnMonitor, "この host はモニタの指定 (windowMonitor) を受けないので、ウィンドウは動きません");
	}
	if (audio.present && !audioHandled)
	{
		debug::warnOnce(kWarnAudioDevice, "この host は音の出力機器の指定 (audioDevice) を受けないので、機器は変わりません");
	}
}

/// 設定のミリ秒を InputSnapshot の u16 に収める。0 以下は 0 (ゲームの既定)。65535 ms で頭打ち
[[nodiscard]] inline std::uint16_t secondsToMs16(float seconds) noexcept
{
	if (!(seconds > 0.0f)) { return 0; }
	const float ms = seconds * 1000.0f + 0.5f;
	return ms >= 65535.0f ? std::uint16_t{65535} : static_cast<std::uint16_t>(ms);
}

}  // namespace mitiru::module::detail
