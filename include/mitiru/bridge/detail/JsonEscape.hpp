#pragma once

/// @file JsonEscape.hpp
/// @brief JSON-escape helpers shared across bridges.
/// @details bridge コードで頻出する「string を `"..."` JSON literal にする」処理を
///          一箇所に集約する。RFC 8259 §7 に従い、`"` / `\\` / 制御文字
///          (0x00–0x1F: `\\n` `\\r` `\\t` `\\b` `\\f` および `\\u00XX`) を
///          適切にエスケープする。VN 台詞に改行が含まれるケースで
///          view 側に invalid JSON が届く問題を防ぐ。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::bridge::detail
{

/// @brief 4-bit ニブルを 16 進大文字に変換する
inline constexpr char toHexDigit(std::uint8_t nibble) noexcept
{
    return static_cast<char>(nibble < 10 ? ('0' + nibble) : ('A' + (nibble - 10)));
}

/// @brief 制御文字 (`c < 0x20`) を `\\uXXXX` 形式で追記する
inline void appendUnicodeEscape(std::string& out, std::uint8_t c)
{
    out += '\\';
    out += 'u';
    out += '0';
    out += '0';
    out += toHexDigit(static_cast<std::uint8_t>((c >> 4) & 0x0F));
    out += toHexDigit(static_cast<std::uint8_t>(c & 0x0F));
}

/// @brief JSON文字列値を生成する（RFC 8259 §7 準拠の最小エスケープ）
/// @details `"` / `\\` / `\\n` / `\\r` / `\\t` / `\\b` / `\\f` を short-form で、
///          残りの `< 0x20` 制御文字は `\\u00XX` 形式で出力する。
inline std::string quotedJson(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char raw : s)
    {
        const auto uc = static_cast<std::uint8_t>(raw);
        switch (uc)
        {
            case '"':  out += '\\'; out += '"';  break;
            case '\\': out += '\\'; out += '\\'; break;
            case '\n': out += '\\'; out += 'n';  break;
            case '\r': out += '\\'; out += 'r';  break;
            case '\t': out += '\\'; out += 't';  break;
            case '\b': out += '\\'; out += 'b';  break;
            case '\f': out += '\\'; out += 'f';  break;
            default:
                if (uc < 0x20) { appendUnicodeEscape(out, uc); }
                else           { out += raw; }
                break;
        }
    }
    out += '"';
    return out;
}

/// @brief 文字列の JSON 配列を生成する（最小エスケープ）
inline std::string jsonStringArray(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0) { out += ','; }
        out += quotedJson(items[i]);
    }
    out += "]";
    return out;
}

} // namespace mitiru::bridge::detail
