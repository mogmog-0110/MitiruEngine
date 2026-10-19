#pragma once

/// @file HostBoundary60.hpp
/// @brief ABI v60（ADR 0086）の host 側宣言。Engine に依存せずテストでき、core/detail/Engine_Module_*.hpp から使う

#include <string>
#include <utility>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module::detail
{

/// @brief ウィンドウの題名に使える文字列。要求がないか、空になったときは present が false
struct WindowTitleRequest
{
	bool present;
	std::string title;
};

/// @brief 切れた UTF-8 の末尾と制御文字を除く。切れた多バイト文字は OS の関数で文字化けするため、ゲーム側の文字列をそのまま渡さない
[[nodiscard]] inline WindowTitleRequest decodeWindowTitle(const FrameIntents& intents)
{
	if (intents.windowTitleSet == 0) { return WindowTitleRequest{false, {}}; }
	std::size_t n = 0;
	while (n < sizeof(intents.windowTitle) && intents.windowTitle[n] != '\0') { ++n; }
	std::string out;
	out.reserve(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto c = static_cast<unsigned char>(intents.windowTitle[i]);
		if (c < 0x20 || c == 0x7F) { continue; }
		out.push_back(static_cast<char>(c));
	}
	// 末尾で切れた多バイト文字は、その文字ごと除く
	std::size_t tail = out.size();
	while (tail > 0 && (static_cast<unsigned char>(out[tail - 1]) & 0xC0) == 0x80) { --tail; }
	if (tail > 0 && static_cast<unsigned char>(out[tail - 1]) >= 0xC0)
	{
		const auto lead = static_cast<unsigned char>(out[tail - 1]);
		const std::size_t want = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : 2);
		if (out.size() - (tail - 1) < want) { out.resize(tail - 1); }
	}
	if (out.empty()) { return WindowTitleRequest{false, {}}; }
	return WindowTitleRequest{true, std::move(out)};
}

}  // namespace mitiru::module::detail
