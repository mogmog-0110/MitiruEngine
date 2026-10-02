#pragma once

/// @file HeightfieldLoad.hpp
/// @brief 16 bit PNG と RAW (.r16 / .raw、リトルエンディアン)の高さマップを読む
/// @details Blender や World Machine が書き出す 16 bit グレースケール PNG をそのまま読める。
///          8 bit PNG は段差が 1/256 刻みになる。
///          RAW は正方形としてファイルサイズから辺の長さを決め、正方形でなければ width を渡す。
///          ヒープとファイルを使うため、DLL の読み込み時に 1 度だけ呼ぶ。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <stb_image.h>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/terrain/Heightfield.hpp>

namespace mitiru::terrain
{

struct HeightfieldLoadResult
{
	std::optional<Heightfield> heightfield;
	std::string                error;  ///< 読めなかった理由 (読めたら空)
};

namespace detail
{

[[nodiscard]] inline bool endsWith(std::string_view s, std::string_view tail) noexcept
{
	if (s.size() < tail.size()) { return false; }
	for (std::size_t i = 0; i < tail.size(); ++i)
	{
		char a = s[s.size() - tail.size() + i];
		if (a >= 'A' && a <= 'Z') { a = static_cast<char>(a - 'A' + 'a'); }
		if (a != tail[i]) { return false; }
	}
	return true;
}

[[nodiscard]] inline HeightfieldLoadResult decodePng(const std::vector<std::uint8_t>& bytes, const HeightfieldPlacement& p)
{
	HeightfieldLoadResult r;
	int w = 0;
	int h = 0;
	int channels = 0;
	const auto size = static_cast<int>(bytes.size());
	std::vector<std::uint16_t> samples;
	if (stbi_is_16_bit_from_memory(bytes.data(), size) != 0)
	{
		stbi_us* px = stbi_load_16_from_memory(bytes.data(), size, &w, &h, &channels, 1);
		if (px != nullptr) { samples.assign(px, px + static_cast<std::size_t>(w) * h); }
		stbi_image_free(px);
	}
	else
	{
		stbi_uc* px = stbi_load_from_memory(bytes.data(), size, &w, &h, &channels, 1);
		if (px != nullptr)
		{
			samples.resize(static_cast<std::size_t>(w) * h);
			for (std::size_t i = 0; i < samples.size(); ++i) { samples[i] = static_cast<std::uint16_t>(px[i] * 257u); }
		}
		stbi_image_free(px);
	}
	if (samples.empty())
	{
		r.error = "PNG を復号できない";
		return r;
	}
	r.heightfield = Heightfield::fromSamples(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), std::move(samples), p);
	if (!r.heightfield->valid())
	{
		r.heightfield.reset();
		r.error = "高さマップは 2 x 2 以上にする";
	}
	return r;
}

[[nodiscard]] inline HeightfieldLoadResult decodeRaw(const std::vector<std::uint8_t>& bytes, std::uint32_t width,
                                                     const HeightfieldPlacement& p)
{
	HeightfieldLoadResult r;
	const std::size_t count = bytes.size() / 2;
	if (width == 0) { width = static_cast<std::uint32_t>(std::lround(std::sqrt(static_cast<double>(count)))); }
	if (width < 2 || bytes.size() % 2 != 0 || count % width != 0)
	{
		r.error = "RAW の大きさが標本 16 bit の正方形 (か width の倍数) になっていない";
		return r;
	}
	std::vector<std::uint16_t> samples(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		samples[i] = static_cast<std::uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
	}
	r.heightfield = Heightfield::fromSamples(width, static_cast<std::uint32_t>(count / width), std::move(samples), p);
	if (!r.heightfield->valid())
	{
		r.heightfield.reset();
		r.error = "高さマップは 2 x 2 以上にする";
	}
	return r;
}

} // namespace detail

[[nodiscard]] inline HeightfieldLoadResult decodeHeightfield(const std::vector<std::uint8_t>& bytes, bool raw,
                                                             const HeightfieldPlacement& placement, std::uint32_t rawWidth = 0)
{
	return raw ? detail::decodeRaw(bytes, rawWidth, placement) : detail::decodePng(bytes, placement);
}

/// @brief vfs::readAsset を使うため、pack 内も同じパスで読める。.r16 / .raw は RAW、それ以外は PNG として読む。
[[nodiscard]] inline HeightfieldLoadResult loadHeightfield(std::string_view path, const HeightfieldPlacement& placement,
                                                           std::uint32_t rawWidth = 0)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes)
	{
		HeightfieldLoadResult r;
		r.error = "高さマップを開けない: " + std::string(path);
		return r;
	}
	const bool raw = detail::endsWith(path, ".r16") || detail::endsWith(path, ".raw");
	auto r = decodeHeightfield(*bytes, raw, placement, rawWidth);
	if (!r.error.empty()) { r.error += ": " + std::string(path); }
	return r;
}

} // namespace mitiru::terrain
