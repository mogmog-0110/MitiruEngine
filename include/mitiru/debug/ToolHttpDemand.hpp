#pragma once

/// @file ToolHttpDemand.hpp
/// @brief ゲームの HTTP API に話しかける窓のために、host が API を開く・閉じる判断。
/// @details 通常の起動では API は閉じている。kToolTable で needsHttp を立てた窓 (motion_tune など) を頼まれたときだけ
///          host が開き、その窓が全部閉じたら、host が開いた API だけを閉じる。--http-port や MITIRU_AI=1 で
///          利用者が開いた API には触らない。判断だけを持ち、socket や process は持たない。

#include <string_view>

#include <mitiru/debug/ToolRegistry.hpp>

namespace mitiru::debug
{

/// @brief 要求された (exe, args) が、API を要る窓か
/// @details args は、spawn のとき --window-pos などが後ろに付き、ゲームは "?schema=..." のような絞り込みも付けるので、
///          表の args で始まり、その直後が区切りであればよい
[[nodiscard]] inline bool toolRequestNeedsHttp(std::string_view exe, std::string_view args) noexcept
{
	for (const auto& spec : mitiru::detail::kToolTable)
	{
		if (!spec.needsHttp || exe != spec.exe) { continue; }
		const std::string_view head{spec.args};
		if (args.substr(0, head.size()) != head) { continue; }
		if (args.size() == head.size() || args[head.size()] == ' ' || args[head.size()] == '?') { return true; }
	}
	return false;
}

/// @brief ToolSpawnLedger の key ("exe|args") を分けて toolRequestNeedsHttp に渡す
[[nodiscard]] inline bool toolKeyNeedsHttp(std::string_view key) noexcept
{
	const std::size_t bar = key.find('|');
	if (bar == std::string_view::npos) { return false; }
	return toolRequestNeedsHttp(key.substr(0, bar), key.substr(bar + 1));
}

/// @brief host が自分で開いた API を覚え、要らなくなったら閉じてよいと答える
class ToolHttpDemand
{
public:
	/// @param httpRunning 今 API が動いているか
	/// @return true なら、窓を開く前に API を開く
	[[nodiscard]] bool shouldOpen(bool httpRunning, bool requestNeedsHttp) const noexcept
	{
		return requestNeedsHttp && !httpRunning;
	}

	void markOpened() noexcept { m_owned = true; }

	/// @param windowRunning API を要る窓が、まだ 1 つでも動いているか
	/// @return true なら、host が開いた API を閉じる
	[[nodiscard]] bool shouldClose(bool windowRunning) const noexcept
	{
		return m_owned && !windowRunning;
	}

	void markClosed() noexcept { m_owned = false; }

	[[nodiscard]] bool owned() const noexcept { return m_owned; }

private:
	bool m_owned = false;
};

} // namespace mitiru::debug
