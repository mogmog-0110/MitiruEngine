#pragma once

/// @file HostBoundary64.hpp
/// @brief ABI v64 (ADR 0164) の host 側の小さな部品。ドロップされたファイルを InputSnapshot::actionEvents に載せる、外す。
/// @details ドロップのパスは JSON ではなく生の UTF-8 の文字列を payloadJson に入れる (name = kActionFileDropped)。
///          InputSnapshot は大きくしない (録画の frameSize が変わって、既存の .mtrr が全部読めなくなるため)。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module::detail
{

inline constexpr const char* kWarnDropPathTooLong = "abi64.drop_path_too_long";

/// @brief actionEvents の 1 件がドロップの通知か
[[nodiscard]] inline bool isFileDropEvent(const ActionEvent& e) noexcept
{
	return std::strcmp(e.name, kActionFileDropped) == 0;
}

/// @brief パス 1 件を actionEvents の後ろへ足す。空きが無いとき (false)、パスが 255 byte に収まらないとき (捨てて 1 度だけ知らせる) は何も足さない。
///        収まらないパスを切って渡すと、別のファイルを開いてしまうため
inline bool appendFileDropEvent(InputSnapshot& snap, std::string_view path)
{
	constexpr std::size_t kCap = sizeof(snap.actionEvents) / sizeof(snap.actionEvents[0]);
	if (snap.actionEventCount < 0 || static_cast<std::size_t>(snap.actionEventCount) >= kCap) { return false; }
	if (path.empty()) { return false; }
	if (path.size() >= sizeof(snap.actionEvents[0].payloadJson))
	{
		debug::warnOnce(kWarnDropPathTooLong,
			"落としたファイルのパスが 255 byte を超えるので、渡さずに捨てました");
		return false;
	}
	ActionEvent& e = snap.actionEvents[snap.actionEventCount];
	std::memset(&e, 0, sizeof(e));
	std::memcpy(e.name, kActionFileDropped, std::strlen(kActionFileDropped));
	std::memcpy(e.payloadJson, path.data(), path.size());
	++snap.actionEventCount;
	return true;
}

/// @brief ドロップの通知だけを取り除く (UI の操作の並びは保つ)。入力スクリプトの中で、実機のドロップを捨てるために使う
inline void removeFileDropEvents(InputSnapshot& snap) noexcept
{
	std::int32_t kept = 0;
	for (std::int32_t i = 0; i < snap.actionEventCount; ++i)
	{
		if (isFileDropEvent(snap.actionEvents[i])) { continue; }
		if (kept != i) { snap.actionEvents[kept] = snap.actionEvents[i]; }
		++kept;
	}
	snap.actionEventCount = kept;
}

/// @brief 空きの数
[[nodiscard]] inline std::size_t freeActionEventSlots(const InputSnapshot& snap) noexcept
{
	constexpr std::size_t kCap = sizeof(snap.actionEvents) / sizeof(snap.actionEvents[0]);
	const std::size_t used = snap.actionEventCount < 0 ? 0 : static_cast<std::size_t>(snap.actionEventCount);
	return used >= kCap ? 0 : kCap - used;
}

}  // namespace mitiru::module::detail
