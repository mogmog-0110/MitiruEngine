#pragma once
/// @file TrueTypeRenderer.hpp
/// @brief TrueType フォントレンダラー（stb_truetype ベース）
/// @details TTF/OTFフォントを読み込み、任意のテキストをピクセルバッファにレンダリングする。
///          日本語テキスト対応。

#include <mitiru/render/Texture.hpp>

#include <stb_truetype.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::render {

/// @brief TrueType フォントレンダラー
class TrueTypeRenderer {
public:
    /// @brief TTF/OTFファイルからフォントを読み込む
    bool loadFont(const std::string& fontPath, float pixelHeight = 32.0f) {
        std::ifstream file(fontPath, std::ios::binary);
        if (!file.is_open()) return false;

        m_fontData = std::vector<uint8_t>(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        if (!stbtt_InitFont(&m_fontInfo, m_fontData.data(), 0))
            return false;

        m_scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelHeight);
        m_pixelHeight = pixelHeight;
        m_loaded = true;
        return true;
    }

    /// @brief テキストをRGBA8テクスチャにレンダリングする
    /// @param text UTF-8テキスト
    /// @param r テキスト色 赤 (0-255)
    /// @param g テキスト色 緑 (0-255)
    /// @param b テキスト色 青 (0-255)
    /// @param a テキスト色 アルファ (0-255)
    /// @return テクスチャ（失敗時は空テクスチャ）
    [[nodiscard]] Texture renderText(const std::string& text,
                                      uint8_t r = 255, uint8_t g = 255,
                                      uint8_t b = 255, uint8_t a = 255) const {
        if (!m_loaded || text.empty()) return {};

        // Measure text width
        int totalWidth = 0;
        int maxHeight = static_cast<int>(m_pixelHeight * 1.5f);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&m_fontInfo, &ascent, &descent, &lineGap);
        float baselineY = static_cast<float>(ascent) * m_scale;

        // First pass: measure
        const char* p = text.c_str();
        while (*p) {
            int codepoint = decodeUTF8(p);
            if (codepoint <= 0) break;

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&m_fontInfo, codepoint, &advance, &lsb);
            totalWidth += static_cast<int>(static_cast<float>(advance) * m_scale);
        }

        if (totalWidth <= 0) return {};

        // Create RGBA pixel buffer
        std::vector<uint8_t> pixels(static_cast<size_t>(totalWidth * maxHeight * 4), 0);

        // Second pass: render
        float x = 0;
        p = text.c_str();
        while (*p) {
            int codepoint = decodeUTF8(p);
            if (codepoint <= 0) break;

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&m_fontInfo, codepoint, &advance, &lsb);

            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&m_fontInfo, codepoint, m_scale, m_scale, &x0, &y0, &x1, &y1);

            int glyphW = x1 - x0, glyphH = y1 - y0;
            if (glyphW > 0 && glyphH > 0) {
                std::vector<uint8_t> bitmap(static_cast<size_t>(glyphW * glyphH), 0);
                stbtt_MakeCodepointBitmap(&m_fontInfo, bitmap.data(), glyphW, glyphH, glyphW,
                    m_scale, m_scale, codepoint);

                int gx = static_cast<int>(x + static_cast<float>(lsb) * m_scale);
                int gy = static_cast<int>(baselineY) + y0;

                for (int dy = 0; dy < glyphH; ++dy) {
                    for (int dx = 0; dx < glyphW; ++dx) {
                        int px = gx + dx, py = gy + dy;
                        if (px >= 0 && px < totalWidth && py >= 0 && py < maxHeight) {
                            uint8_t alpha = bitmap[static_cast<size_t>(dy * glyphW + dx)];
                            if (alpha > 0) {
                                auto idx = static_cast<size_t>((py * totalWidth + px) * 4);
                                pixels[idx] = r;
                                pixels[idx+1] = g;
                                pixels[idx+2] = b;
                                pixels[idx+3] = static_cast<uint8_t>(
                                    static_cast<int>(alpha) * a / 255);
                            }
                        }
                    }
                }
            }

            x += static_cast<float>(advance) * m_scale;
        }

        return Texture(totalWidth, maxHeight, pixels);
    }

    [[nodiscard]] bool isLoaded() const noexcept { return m_loaded; }
    [[nodiscard]] float pixelHeight() const noexcept { return m_pixelHeight; }

private:
    std::vector<uint8_t> m_fontData;
    stbtt_fontinfo m_fontInfo{};
    float m_scale = 0;
    float m_pixelHeight = 32;
    bool m_loaded = false;

    /// @brief UTF-8デコード（1文字分）
    static int decodeUTF8(const char*& p) {
        unsigned char c = static_cast<unsigned char>(*p);
        int codepoint;
        int bytes;

        if (c < 0x80) { codepoint = c; bytes = 1; }
        else if (c < 0xC0) { ++p; return -1; } // invalid
        else if (c < 0xE0) { codepoint = c & 0x1F; bytes = 2; }
        else if (c < 0xF0) { codepoint = c & 0x0F; bytes = 3; }
        else { codepoint = c & 0x07; bytes = 4; }

        ++p;
        for (int i = 1; i < bytes; ++i) {
            if ((*p & 0xC0) != 0x80) return -1;
            codepoint = (codepoint << 6) | (*p & 0x3F);
            ++p;
        }

        return codepoint;
    }
};

} // namespace mitiru::render
