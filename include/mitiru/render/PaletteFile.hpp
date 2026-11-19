#pragma once

// 1 行 1 色の RRGGBB のファイル。形式は docs/PALETTE.md。Palette は 0xAABBGGRR で、読んだ色のアルファは常に 255。

#include "mitiru/render/Palette.hpp"
#include "mitiru/render/lofi/LoFiQuantize.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace mitiru::render
{

struct PaletteFileResult
{
	Palette palette;
	std::string error;
	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] inline PaletteFileResult parsePaletteFile(std::string_view text)
{
	PaletteFileResult result;
	if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") { text.remove_prefix(3); }
	std::size_t count = 0;
	int lineNumber = 0;
	while (!text.empty())
	{
		const std::size_t end = text.find('\n');
		std::string_view line = text.substr(0, end);
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
		++lineNumber;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) { line.remove_suffix(1); }
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) { line.remove_prefix(1); }
		if (line.empty()) { continue; }
		if (line.front() == '#') { line.remove_prefix(1); }
		std::uint32_t rgb = 0;
		bool valid = line.size() == 6;
		for (std::size_t i = 0; valid && i < 6; ++i)
		{
			const char c = line[i];
			const int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
			if (v < 0) { valid = false; } else { rgb = (rgb << 4) | static_cast<std::uint32_t>(v); }
		}
		if (!valid) { result.error = std::to_string(lineNumber) + " line: not a 6-digit RRGGBB hex color"; return result; }
		if (count >= 256) { result.error = "More than 256 colors"; return result; }
		result.palette.set(count++, Palette::rgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb)));
	}
	if (count == 0) { result.error = "No colors"; }
	return result;
}

[[nodiscard]] inline std::string formatPaletteFile(const Palette& palette)
{
	static constexpr char kHex[] = "0123456789abcdef";
	std::string out;
	for (std::size_t i = 0; i < palette.count; ++i)
	{
		const std::uint32_t c = palette.colors[i];
		for (const std::uint32_t byte : { c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu })
		{
			out += kHex[byte >> 4];
			out += kHex[byte & 15];
		}
		out += '\n';
	}
	return out;
}

[[nodiscard]] inline PaletteFileResult loadPaletteFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		PaletteFileResult result;
		result.error = "Can't read: " + path;
		return result;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return parsePaletteFile(text);
}

[[nodiscard]] inline bool savePaletteFile(const std::string& path, const Palette& palette)
{
	// 読み込めない 0 色のファイルを書かない
	if (palette.count == 0) { return false; }
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	const std::string text = formatPaletteFile(palette);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(out);
}

[[nodiscard]] inline std::size_t nearestPaletteIndex(const Palette& palette, int r, int g, int b) noexcept
{
	std::size_t best = 0;
	int bestDistance = 0;
	for (std::size_t i = 0; i < palette.count; ++i)
	{
		const std::uint32_t c = palette.colors[i];
		const int dr = static_cast<int>(c & 0xFFu) - r;
		const int dg = static_cast<int>((c >> 8) & 0xFFu) - g;
		const int db = static_cast<int>((c >> 16) & 0xFFu) - b;
		const int d = dr * dr + dg * dg + db * db;
		if (i == 0 || d < bestDistance) { best = i; bestDistance = d; }
	}
	return best;
}

// tools/pal_convert.py と同じ画素が同じ番号になるよう、ディザの量は bayerNorm * spread を浮動小数にせず、床関数で整数にする
[[nodiscard]] inline std::size_t quantizeToPalette(const Palette& palette, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                                   int x, int y, int spread = 0) noexcept
{
	const int bayer = lofi::kBayer4x4[static_cast<std::size_t>(y & 3)][static_cast<std::size_t>(x & 3)];
	const int offset = ((2 * bayer - 15) * spread) >> 5;
	const auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
	return nearestPaletteIndex(palette, clamp(r + offset), clamp(g + offset), clamp(b + offset));
}

} // namespace mitiru::render
