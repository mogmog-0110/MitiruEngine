#pragma once

/// @file SdfTextMeasure.hpp
/// @brief SDFテキスト計測・ワードラップ

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/render/sdf/SdfFontAtlas.hpp>
#include <mitiru/render/sdf/Utf8Utils.hpp>

namespace mitiru::render
{

// ── テキスト計測結果 ───────────────────────────────────────────

/// @brief テキスト計測結果
struct SdfTextSize
{
	float width = 0.0f;  ///< テキスト幅（ピクセル）
	float height = 0.0f; ///< テキスト高さ（ピクセル）
};

/// @brief ワードラップ結果の1行
struct SdfWrappedLine
{
	std::string_view text; ///< この行のテキスト（元の文字列のビュー）
	float width = 0.0f;   ///< この行の幅（ピクセル）
};

} // namespace mitiru::render
