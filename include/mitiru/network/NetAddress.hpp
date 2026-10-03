#pragma once

/// @file NetAddress.hpp
/// @brief IPv4 の住所と、それを人が読み上げやすい参加コードにする変換
///
/// 参加コードは住所 (ip 4 byte + port 2 byte) を 10 文字で表すだけで、相手探しのサーバーは無い。
/// 読み違えやすい I / L / O / U を使わない文字 (Crockford の base32) で書き、5 文字ずつ - でつなぐ。

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace mitiru::network
{

/// @brief IPv4 の住所 (どちらもホストのバイト順)
struct NetAddress
{
	std::uint32_t ip = 0;
	std::uint16_t port = 0;

	[[nodiscard]] bool valid() const noexcept { return port != 0; }
	[[nodiscard]] bool isLoopback() const noexcept { return (ip >> 24) == 127u; }
	friend bool operator==(const NetAddress&, const NetAddress&) = default;
};

[[nodiscard]] inline std::string toString(const NetAddress& a)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (a.ip >> 24) & 255u, (a.ip >> 16) & 255u, (a.ip >> 8) & 255u,
		a.ip & 255u, static_cast<unsigned>(a.port));
	return text;
}

namespace detail
{
[[nodiscard]] inline std::optional<std::uint16_t> parsePort(std::string_view text) noexcept
{
	unsigned long port = 0;
	for (const char c : text)
	{
		if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
		port = port * 10 + static_cast<unsigned long>(c - '0');
		if (port > 65535) return std::nullopt;
	}
	if (text.empty() || port == 0) return std::nullopt;
	return static_cast<std::uint16_t>(port);
}

[[nodiscard]] inline std::optional<std::uint32_t> parseIpV4(std::string_view host) noexcept
{
	std::uint32_t ip = 0;
	unsigned part = 0;
	int parts = 0;
	int digits = 0;
	for (std::size_t i = 0; i <= host.size(); ++i)
	{
		if (i < host.size() && std::isdigit(static_cast<unsigned char>(host[i])))
		{
			part = part * 10 + static_cast<unsigned>(host[i] - '0');
			if (++digits > 3 || part > 255) return std::nullopt;
			continue;
		}
		if ((i < host.size() && host[i] != '.') || digits == 0 || ++parts > 4) return std::nullopt;
		ip = (ip << 8) | part;
		part = 0;
		digits = 0;
	}
	if (parts != 4) return std::nullopt;
	return ip;
}
} // namespace detail

/// @brief "a.b.c.d:port" を読む。port だけ ("47100") なら ip は defaultIp
[[nodiscard]] inline std::optional<NetAddress> parseAddress(std::string_view text, std::uint32_t defaultIp = 0)
{
	const auto colon = text.rfind(':');
	const auto port = detail::parsePort(colon == std::string_view::npos ? text : text.substr(colon + 1));
	if (!port) return std::nullopt;
	if (colon == std::string_view::npos) return NetAddress{defaultIp, *port};
	const auto ip = detail::parseIpV4(text.substr(0, colon));
	if (!ip) return std::nullopt;
	return NetAddress{*ip, *port};
}

namespace detail
{
inline constexpr std::string_view kCodeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

[[nodiscard]] inline int codeDigit(char c) noexcept
{
	c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	if (c == 'O') c = '0';
	if (c == 'I' || c == 'L') c = '1';
	const auto at = kCodeAlphabet.find(c);
	return at == std::string_view::npos ? -1 : static_cast<int>(at);
}
} // namespace detail

/// @brief 住所を参加コード "XXXXX-XXXXX" にする
[[nodiscard]] inline std::string encodeJoinCode(const NetAddress& a)
{
	const std::uint64_t bits = (static_cast<std::uint64_t>(a.ip) << 16) | a.port;
	std::string out;
	for (int i = 9; i >= 0; --i)
	{
		out.push_back(detail::kCodeAlphabet[static_cast<std::size_t>((bits >> (i * 5)) & 31u)]);
		if (i == 5) out.push_back('-');
	}
	return out;
}

/// @brief 参加コードを住所に戻す。- と空白は無視し、大文字小文字と O/0・I/L/1 の取り違えは許す
[[nodiscard]] inline std::optional<NetAddress> decodeJoinCode(std::string_view code)
{
	std::uint64_t bits = 0;
	int digits = 0;
	for (const char c : code)
	{
		if (c == '-' || c == ' ') continue;
		const int d = detail::codeDigit(c);
		if (d < 0 || ++digits > 10) return std::nullopt;
		bits = (bits << 5) | static_cast<std::uint64_t>(d);
	}
	if (digits != 10 || (bits >> 48) != 0) return std::nullopt;
	const NetAddress a{static_cast<std::uint32_t>(bits >> 16), static_cast<std::uint16_t>(bits & 0xFFFFu)};
	if (!a.valid()) return std::nullopt;
	return a;
}

/// @brief 住所か参加コードのどちらでも読む
[[nodiscard]] inline std::optional<NetAddress> parseAddressOrCode(std::string_view text)
{
	if (auto a = parseAddress(text); a && a->ip != 0) return a;
	return decodeJoinCode(text);
}

} // namespace mitiru::network
