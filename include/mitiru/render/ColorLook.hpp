#pragma once

/// @file ColorLook.hpp
/// @brief 色の仕上げの宣言 (assets/color_looks.json) を読む。SceneLook::colorLut の番号は、この `looks` の並びの 1 始まり
/// @details renderer の初期化で全部読み、ファイルが無ければ何も確保しない。LUT は一辺 N の立方体を、幅 N*N・高さ N の
///          PNG に並べたもの (青が段、段の中は右へ赤・下へ緑)。PNG の値は sRGB 符号化のまま読む。
///          `.cube` からの変換は tools/cube_to_lut_png.py。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/ColorFinishMath.hpp>
#include <mitiru/render/ImageLoader.hpp>

namespace mitiru::render
{

inline constexpr int kMaxColorLooks = 255;   // SceneLook::colorLut は 1 byte
inline constexpr const char* kDefaultColorLooksPath = "assets/color_looks.json";

struct ColorLookSpec
{
	std::string name;
	std::string lutPath;             ///< vfs の論理パス。空 = LUT なし (ビネットとグレインだけ)
	float lutStrength = 1.0f;
	float vignetteStrength = 0.0f;
	float vignetteRadius = 0.5f;
	float grainStrength = 0.0f;
	int grainCell = 1;
};

/// 幅 N*N・高さ N の RGBA8 に並べた LUT
struct ColorLutImage
{
	int size = 0;
	std::vector<std::uint8_t> rgba;

	[[nodiscard]] bool valid() const noexcept { return size >= kMinLutSize && size <= kMaxLutSize && !rgba.empty(); }
};

struct ColorLookSet
{
	std::vector<ColorLookSpec> looks;
	std::vector<ColorLutImage> luts;   ///< looks と同じ添字。LUT のない組は size 0

	[[nodiscard]] bool empty() const noexcept { return looks.empty(); }

	/// SceneLook::colorLut の番号 (1 始まり) の組。範囲外は nullptr
	[[nodiscard]] const ColorLookSpec* find(int number) const noexcept
	{
		return (number >= 1 && number <= static_cast<int>(looks.size())) ? &looks[static_cast<std::size_t>(number - 1)] : nullptr;
	}

	[[nodiscard]] const ColorLutImage* lut(int number) const noexcept
	{
		return (number >= 1 && number <= static_cast<int>(luts.size())) ? &luts[static_cast<std::size_t>(number - 1)] : nullptr;
	}
};

/// @brief 幅 N*N・高さ N の画像を LUT として受ける。形が違えば size 0 の LUT と理由を返す
[[nodiscard]] inline ColorLutImage makeLutFromStrip(int width, int height, std::vector<std::uint8_t> rgba, std::string& why)
{
	ColorLutImage lut;
	if (height < kMinLutSize || height > kMaxLutSize || width != height * height)
	{
		why = "LUT の画像は幅 N*N・高さ N (N は 2 から 64) にしてください (今は " + std::to_string(width) + "x" + std::to_string(height) + ")";
		return lut;
	}
	if (rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
	{
		why = "LUT の画素の数が画像の大きさと合いません";
		return lut;
	}
	lut.size = height;
	lut.rgba = std::move(rgba);
	return lut;
}

namespace detail
{

[[nodiscard]] inline float numberOr(const nlohmann::json& j, const char* key, float fallback, float lo, float hi)
{
	if (!j.is_object() || !j.contains(key) || !j[key].is_number()) { return fallback; }
	return std::clamp(j[key].get<float>(), lo, hi);
}

[[nodiscard]] inline std::string joinBase(std::string_view baseDir, const std::string& rel)
{
	if (rel.empty()) { return {}; }
	return baseDir.empty() ? rel : (std::string(baseDir) + "/" + rel);
}

}  // namespace detail

/// @brief 宣言の JSON を読む。壊れていれば空を返して 1 度だけ知らせる。baseDir は lut のパスの基準
[[nodiscard]] inline std::vector<ColorLookSpec> parseColorLooks(std::string_view text, std::string_view baseDir, std::string_view source)
{
	std::vector<ColorLookSpec> out;
	const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object() || !root.contains("looks") || !root["looks"].is_array())
	{
		debug::warnOnce("color_looks.parse:" + std::string(source), std::string(source) + " を読めないので、色の仕上げは使いません ({\"looks\": [...]} の形にしてください)");
		return out;
	}
	for (const nlohmann::json& j : root["looks"])
	{
		if (static_cast<int>(out.size()) >= kMaxColorLooks)
		{
			debug::warnOnce("color_looks.count:" + std::string(source), std::string(source) + " の looks は 255 組までなので、残りは使いません");
			break;
		}
		ColorLookSpec s;
		if (j.is_object())
		{
			if (j.contains("name") && j["name"].is_string()) { s.name = j["name"].get<std::string>(); }
			if (j.contains("lut") && j["lut"].is_string()) { s.lutPath = detail::joinBase(baseDir, j["lut"].get<std::string>()); }
			s.lutStrength = detail::numberOr(j, "strength", 1.0f, 0.0f, 1.0f);
			if (j.contains("vignette"))
			{
				s.vignetteStrength = detail::numberOr(j["vignette"], "strength", 0.0f, 0.0f, 1.0f);
				s.vignetteRadius = detail::numberOr(j["vignette"], "radius", 0.5f, 0.0f, 0.99f);
			}
			if (j.contains("grain"))
			{
				s.grainStrength = detail::numberOr(j["grain"], "strength", 0.0f, 0.0f, 0.25f);
				s.grainCell = static_cast<int>(detail::numberOr(j["grain"], "cell", 1.0f, 1.0f, 16.0f));
			}
		}
		out.push_back(std::move(s));
	}
	return out;
}

/// @brief 宣言と、そこから指す LUT の PNG を全部読む。manifestPath が無ければ空 (何も確保しない)
[[nodiscard]] inline ColorLookSet loadColorLooks(const std::string& manifestPath)
{
	ColorLookSet set;
	const auto bytes = vfs::readGlobal(manifestPath, {});
	if (!bytes) { return set; }
	const std::string text(bytes->begin(), bytes->end());
	const std::string baseDir = std::filesystem::path(manifestPath).parent_path().generic_string();
	set.looks = parseColorLooks(text, baseDir, manifestPath);
	set.luts.resize(set.looks.size());
	for (std::size_t i = 0; i < set.looks.size(); ++i)
	{
		const ColorLookSpec& s = set.looks[i];
		if (s.lutPath.empty()) { continue; }
		// ImageLoader::fromFile は diskPath に論理パスを渡すので、MITIRU_ASSET_ROOT を通らない。宣言と同じ解き方で読む
		const auto bytes = vfs::readGlobal(s.lutPath, {});
		if (!bytes)
		{
			debug::warnOnce("color_looks.lut.missing:" + s.lutPath, s.lutPath + " を読めないので、この組の LUT は使いません (assets からの相対パスを確かめてください)");
			continue;
		}
		const Texture tex = ImageLoader::fromMemory(bytes->data(), static_cast<int>(bytes->size()));
		if (!tex.valid())
		{
			debug::warnOnce("color_looks.lut.decode:" + s.lutPath, s.lutPath + " を画像として読めないので、この組の LUT は使いません");
			continue;
		}
		std::string why;
		std::vector<std::uint8_t> px(tex.pixels().begin(), tex.pixels().end());
		set.luts[i] = makeLutFromStrip(tex.width(), tex.height(), std::move(px), why);
		if (!set.luts[i].valid())
		{
			debug::warnOnce("color_looks.lut:" + s.lutPath, s.lutPath + ": " + why + "、この組の LUT は使いません");
		}
	}
	return set;
}

/// @brief 1 組の宣言と画面の大きさから、GPU へ渡す定数を作る。DX12 と Dawn が同じものを使う。lut は転送済みのときだけ渡す
[[nodiscard]] inline ColorFinishCb makeColorFinishCb(const ColorLookSpec& spec, const ColorLutImage* lut, int width, int height,
                                                     std::uint32_t seed) noexcept
{
	ColorFinishCb cb;
	cb.lutSize = (lut != nullptr && lut->valid()) ? static_cast<float>(lut->size) : 0.0f;
	cb.lutStrength = spec.lutStrength;
	cb.vignetteStrength = spec.vignetteStrength;
	cb.vignetteRadius = spec.vignetteRadius;
	cb.grainStrength = spec.grainStrength;
	cb.grainCell = static_cast<std::uint32_t>(std::max(spec.grainCell, 1));
	cb.seed = seed;
	cb.outWidth = static_cast<float>(std::max(width, 1));
	cb.outHeight = static_cast<float>(std::max(height, 1));
	return cb;
}

}  // namespace mitiru::render
