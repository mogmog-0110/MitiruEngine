#pragma once

/// @file VideoTypes.hpp
/// @brief 動画の型、秒から絵の番号を決める式、host の resolver に渡す動画 id の書式 (ADR 0104)
/// @details 動画の位置は GameMemory の秒だけで決める。壁時計と音の時計は使わないため、巻き戻し、撮影、リプレイでも動画の位置が一致する。
/// pl_mpeg は PTS を見ないため、絵の番号は fps から求める。

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <sgc/types/Color.hpp>

namespace mitiru::video
{

struct VideoInfo
{
	int    width = 0;
	int    height = 0;
	double fps = 0.0;
};

/// @brief 秒から絵の番号を求める。floor を使うため、境目の秒は次の絵に入る。負の値と NaN は 0
[[nodiscard]] inline int frameIndexAt(double seconds, double fps) noexcept
{
	if (!(seconds > 0.0) || !(fps > 0.0)) { return 0; }
	const double f = std::floor(seconds * fps);
	return f >= static_cast<double>(INT_MAX) ? INT_MAX : static_cast<int>(f);
}

/// Screen::drawVideo が host の SpriteResolveFunc に渡す id の先頭。通常の sprite id は ASCII の印字文字で始まるため重ならない
inline constexpr char kVideoIdLead = '\x01';

/// 動画名の最大長 (終端を除く)。Canvas の textPool に入る名前と同じ上限
inline constexpr std::size_t kVideoNameMax = 96;

/// id の長さの上限。先頭 1 + "video:" 6 + 名前 + '@' + 8 + '#' + 8 + 終端
inline constexpr std::size_t kVideoIdMax = 1 + 6 + kVideoNameMax + 1 + 8 + 1 + 8 + 1;

/// 動画の名前として使えるか。assets/video の下だけを指すよう、NUL・バックスラッシュ・ドライブ名・先頭の `/`・`..` の区間を断る
[[nodiscard]] inline bool isSafeVideoName(std::string_view name) noexcept
{
	if (name.empty() || name.size() > kVideoNameMax || name.front() == '/') { return false; }
	for (const char c : name)
	{
		if (c == '\0' || c == '\\' || c == ':') { return false; }
	}
	std::size_t start = 0;
	while (start <= name.size())
	{
		std::size_t end = name.find('/', start);
		if (end == std::string_view::npos) { end = name.size(); }
		if (name.substr(start, end - start) == "..") { return false; }
		start = end + 1;
	}
	return true;
}

[[nodiscard]] inline std::uint32_t floatBits(float v) noexcept
{
	std::uint32_t u = 0;
	std::memcpy(&u, &v, sizeof(u));
	return u;
}

[[nodiscard]] inline float bitsToFloat(std::uint32_t u) noexcept
{
	float v = 0.0f;
	std::memcpy(&v, &u, sizeof(v));
	return v;
}

/// 色 (0..1) を 8 ビット RGBA に詰める。範囲外は丸める
[[nodiscard]] inline std::uint32_t packTint(const sgc::Colorf& c) noexcept
{
	const auto q = [](float v) -> std::uint32_t {
		if (!(v > 0.0f)) { return 0u; }
		if (v >= 1.0f) { return 255u; }
		return static_cast<std::uint32_t>(v * 255.0f + 0.5f);
	};
	return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
}

inline constexpr std::uint32_t kTintWhite = 0xFFFFFFFFu;

/// @brief id を書く。秒は float のビット列をそのまま 16 進数にする。10 進数にすると端数でリプレイの絵が変わる。
/// 名前が長すぎるときは false
[[nodiscard]] inline bool formatVideoId(char* out, std::size_t cap, std::string_view name, float seconds, std::uint32_t tint) noexcept
{
	if (out == nullptr || cap < kVideoIdMax || !isSafeVideoName(name)) { return false; }
	const int n = std::snprintf(out, cap, "%cvideo:%.*s@%08x#%08x", kVideoIdLead, static_cast<int>(name.size()), name.data(),
	                            static_cast<unsigned>(floatBits(seconds)), static_cast<unsigned>(tint));
	return n > 0 && static_cast<std::size_t>(n) < cap;
}

struct ParsedVideoId
{
	std::string   name;
	float         seconds = 0.0f;
	std::uint32_t tint = kTintWhite;
};

[[nodiscard]] inline bool isVideoId(const char* id) noexcept
{
	return id != nullptr && id[0] == kVideoIdLead;
}

[[nodiscard]] inline bool parseVideoId(const char* id, ParsedVideoId& out)
{
	if (!isVideoId(id)) { return false; }
	const std::string_view s{id + 1};
	constexpr std::string_view kTag = "video:";
	if (s.substr(0, kTag.size()) != kTag) { return false; }
	const std::size_t at = s.rfind('@');
	// 末尾は '@' + 8 桁 + '#' + 8 桁で固定
	if (at == std::string_view::npos || at <= kTag.size() || s.size() != at + 1 + 8 + 1 + 8 || s[at + 9] != '#') { return false; }
	const auto hex = [](std::string_view h, std::uint32_t& v) {
		v = 0;
		for (const char c : h)
		{
			std::uint32_t d = 0;
			if (c >= '0' && c <= '9') { d = static_cast<std::uint32_t>(c - '0'); }
			else if (c >= 'a' && c <= 'f') { d = static_cast<std::uint32_t>(c - 'a' + 10); }
			else { return false; }
			v = (v << 4) | d;
		}
		return true;
	};
	std::uint32_t bits = 0;
	std::uint32_t tint = 0;
	if (!hex(s.substr(at + 1, 8), bits) || !hex(s.substr(at + 10, 8), tint)) { return false; }
	if (!isSafeVideoName(s.substr(kTag.size(), at - kTag.size()))) { return false; }
	out.name.assign(s.substr(kTag.size(), at - kTag.size()));
	out.seconds = bitsToFloat(bits);
	out.tint = tint;
	return true;
}

}  // namespace mitiru::video
