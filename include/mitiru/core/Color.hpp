#pragma once

/// @file Color.hpp
/// @brief 初心者向けの色の書き方。mitiru::Color と rgb / rgba / hex + 名前付き色。
/// @details
/// 描画 API の色は内部的には 0..1 の float (sgc::Colorf) だが、それを直接
/// 書かせると `sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f}` のような magic number に
/// なる。この header は sgc を知らなくても 0-255 や 16 進で色を書けるようにする。
///
/// @code
///   using namespace mitiru;
///   screen.drawRect(10, 10, 64, 64, rgb(200, 0, 44));   // 0-255
///   screen.drawRect(10, 10, 64, 64, hex(0xC8002C));     // 16 進
///   screen.drawRect(10, 10, 64, 64, color::Red);        // 名前付き
/// @endcode

#include <cstdint>

#include <sgc/types/Color.hpp>

namespace mitiru
{

/// 色。中身は 0..1 float だが、下の rgb / hex で作れば中身を意識しなくてよい。
using Color = sgc::Colorf;

/// 0-255 の RGB から色を作る (例: rgb(200, 0, 44))。不透明。
inline constexpr Color rgb(int r, int g, int b) noexcept
{
	return Color::fromRGBA8(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
	                        static_cast<std::uint8_t>(b), 255);
}

/// 0-255 の RGBA から色を作る (a=0 で透明、255 で不透明)。
inline constexpr Color rgba(int r, int g, int b, int a) noexcept
{
	return Color::fromRGBA8(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
	                        static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a));
}

/// 6 桁 16 進 0xRRGGBB から色を作る (例: hex(0xC8002C))。不透明。
inline constexpr Color hex(std::uint32_t rrggbb) noexcept
{
	return Color::fromHex((rrggbb << 8) | 0xFFu);
}

/// 8 桁 16 進 0xRRGGBBAA から色を作る (alpha 込み)。
inline constexpr Color hexa(std::uint32_t rrggbbaa) noexcept
{
	return Color::fromHex(rrggbbaa);
}

/// よく使う名前付き色。
namespace color
{
inline constexpr Color White  = rgb(255, 255, 255);
inline constexpr Color Black  = rgb(16, 16, 16);
inline constexpr Color Gray   = rgb(200, 200, 200);
inline constexpr Color Red    = rgb(200, 0, 44);
inline constexpr Color Green  = rgb(95, 184, 95);
inline constexpr Color Blue   = rgb(106, 123, 214);
inline constexpr Color Yellow = rgb(242, 193, 78);
inline constexpr Color Orange = rgb(239, 138, 59);
inline constexpr Color Cyan   = rgb(67, 176, 201);
inline constexpr Color Clear  = rgba(0, 0, 0, 0);   ///< 完全透明
}  // namespace color

}  // namespace mitiru
