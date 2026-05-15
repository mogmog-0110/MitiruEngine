#pragma once
/// @file PostEffects.hpp
/// @brief ポストエフェクト（ブルーム・ビネット・カラーグレーディング）

#include <mitiru/render/RenderTexture.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::render {

class PostEffects {
public:
    // ── ブルーム（明るい部分のにじみ）──

    /// @brief ブルームエフェクトを適用する
    /// @param rt 対象RenderTexture
    /// @param threshold 明るさの閾値 (0.0-1.0)
    /// @param intensity にじみの強さ
    /// @param radius ぼかし半径（ピクセル）
    static void bloom(RenderTexture& rt, float threshold = 0.7f,
                      float intensity = 0.5f, int radius = 3)
    {
        const int w = rt.width(), h = rt.height();
        if (w <= 0 || h <= 0) return;

        // Extract bright pixels
        std::vector<float> bright(static_cast<size_t>(w * h * 3), 0.0f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                auto c = rt.pixelAt(x, y);
                float lum = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
                if (lum > threshold) {
                    size_t i = static_cast<size_t>((y * w + x) * 3);
                    bright[i]   = (c.r - threshold) * intensity;
                    bright[i+1] = (c.g - threshold) * intensity;
                    bright[i+2] = (c.b - threshold) * intensity;
                }
            }
        }

        // Simple box blur
        std::vector<float> blurred(bright.size(), 0.0f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float r = 0, g = 0, b = 0;
                int count = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int sx = x + dx, sy = y + dy;
                        if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                            size_t si = static_cast<size_t>((sy * w + sx) * 3);
                            r += bright[si]; g += bright[si+1]; b += bright[si+2];
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    size_t i = static_cast<size_t>((y * w + x) * 3);
                    blurred[i]   = r / static_cast<float>(count);
                    blurred[i+1] = g / static_cast<float>(count);
                    blurred[i+2] = b / static_cast<float>(count);
                }
            }
        }

        // Additive blend
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                auto c = rt.pixelAt(x, y);
                size_t i = static_cast<size_t>((y * w + x) * 3);
                rt.setPixel(x, y, {
                    std::min(1.0f, c.r + blurred[i]),
                    std::min(1.0f, c.g + blurred[i+1]),
                    std::min(1.0f, c.b + blurred[i+2]),
                    c.a
                });
            }
        }
    }

    // ── ビネット（周辺減光）──

    static void vignette(RenderTexture& rt, float strength = 0.5f, float radius = 0.8f)
    {
        const int w = rt.width(), h = rt.height();
        const float cx = static_cast<float>(w) * 0.5f;
        const float cy = static_cast<float>(h) * 0.5f;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dx = (static_cast<float>(x) - cx) / cx;
                float dy = (static_cast<float>(y) - cy) / cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float factor = 1.0f - std::clamp((dist - radius) / (1.0f - radius), 0.0f, 1.0f) * strength;

                auto c = rt.pixelAt(x, y);
                rt.setPixel(x, y, {c.r * factor, c.g * factor, c.b * factor, c.a});
            }
        }
    }

    // ── カラーグレーディング（色調補正）──

    /// @brief 彩度を調整する
    static void saturation(RenderTexture& rt, float amount = 1.2f)
    {
        const int w = rt.width(), h = rt.height();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                auto c = rt.pixelAt(x, y);
                float grey = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
                rt.setPixel(x, y, {
                    std::clamp(grey + (c.r - grey) * amount, 0.0f, 1.0f),
                    std::clamp(grey + (c.g - grey) * amount, 0.0f, 1.0f),
                    std::clamp(grey + (c.b - grey) * amount, 0.0f, 1.0f),
                    c.a
                });
            }
        }
    }

    /// @brief コントラストを調整する
    static void contrast(RenderTexture& rt, float amount = 1.1f)
    {
        const int w = rt.width(), h = rt.height();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                auto c = rt.pixelAt(x, y);
                rt.setPixel(x, y, {
                    std::clamp((c.r - 0.5f) * amount + 0.5f, 0.0f, 1.0f),
                    std::clamp((c.g - 0.5f) * amount + 0.5f, 0.0f, 1.0f),
                    std::clamp((c.b - 0.5f) * amount + 0.5f, 0.0f, 1.0f),
                    c.a
                });
            }
        }
    }
};

} // namespace mitiru::render
