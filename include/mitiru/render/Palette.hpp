#pragma once

/// @file Palette.hpp
/// @brief X68000 流のインデックスカラー + パレットアニメ (色回し) ユーティリティ。
/// @details モダンなエンジンは RGBA 直描きだが、X68000/アーケードの質感は
///          「16/256 色パレット + 色回し」が肝。本ヘッダは純 CPU の薄い道具:
///            1. Palette       … 最大 256 色 (0xAABBGGRR)
///            2. IndexedImage  … 1 byte = パレット index の画像
///            3. cycle()       … パレットの一部を回す = 色回し (水面/炎/グラデの動き)
///            4. bake()        … index 画像 + パレット → RGBA(uint32) バッファ
///          焼いた RGBA を Screen::drawPixelGrid(..., PixelArtFilter::Point) に渡せば
///          毎フレーム内容ハッシュで差分アップロード + ドット等倍で出る。色回しは
///          毎フレーム cycle()→bake()→drawPixelGrid するだけ (絵は描き直さない)。
///
/// @par 使い方:
/// @code
///   mitiru::render::Palette      pal;   // pal.set(i, Palette::rgba(r,g,b))
///   mitiru::render::IndexedImage img(W, H);  // img.at(x,y) = index
///   std::vector<std::uint32_t>   frame;      // 焼き先 (使い回す)
///   // 毎フレーム:
///   pal.cycle(1, 7, 1);                 // slot 1..7 を 1 つ回す = 色回し
///   img.bake(pal, frame);
///   screen.drawPixelGrid(dst, frame.data(), img.w, img.h,
///                        mitiru::render::PixelArtFilter::Point);
/// @endcode

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mitiru::render
{

/// @brief 最大 256 色のパレット。色は 0xAABBGGRR (drawPixelGrid と同じ並び)。
struct Palette
{
    std::array<std::uint32_t, 256> colors{};
    std::size_t                    count = 0;   ///< 使用色数 (情報用。回しは範囲指定)

    /// R,G,B,A → 0xAABBGGRR。
    static constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                        std::uint8_t a = 255) noexcept
    {
        return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16)
             | (static_cast<std::uint32_t>(g) << 8)  |  static_cast<std::uint32_t>(r);
    }

    void set(std::size_t i, std::uint32_t color) noexcept
    {
        if (i >= 256) { return; }
        colors[i] = color;
        if (i + 1 > count) { count = i + 1; }
    }

    [[nodiscard]] std::uint32_t at(std::size_t i) const noexcept
    {
        return (i < 256) ? colors[i] : 0u;
    }

    /// @brief slot [first, first+n) を step だけ回す = X68000 の色回し。
    /// @param step 正で前進 (色が index 増方向へ流れる)、負で後退。n でラップ。
    /// @details 範囲外・n<=1 は no-op。絵 (index) は不変、パレットだけ動く。
    void cycle(std::size_t first, std::size_t n, int step = 1) noexcept
    {
        if (n <= 1 || first >= 256 || first + n > 256) { return; }
        const int ni = static_cast<int>(n);
        step = ((step % ni) + ni) % ni;          // 正の剰余へ正規化
        if (step == 0) { return; }
        std::array<std::uint32_t, 256> tmp{};
        for (std::size_t i = 0; i < n; ++i) { tmp[i] = colors[first + i]; }
        for (std::size_t i = 0; i < n; ++i)
        {
            colors[first + ((i + static_cast<std::size_t>(step)) % n)] = tmp[i];
        }
    }
};

/// @brief 1 byte = パレット index の画像。bake() で RGBA(uint32) 化。
struct IndexedImage
{
    int                       w = 0;
    int                       h = 0;
    std::vector<std::uint8_t> indices;   ///< 長さ w*h

    IndexedImage() = default;
    IndexedImage(int width, int height)
        : w(width), h(height),
          indices(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u)
    {
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return w > 0 && h > 0 &&
               indices.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    }

    [[nodiscard]] std::uint8_t&       at(int x, int y) noexcept       { return indices[idx(x, y)]; }
    [[nodiscard]] const std::uint8_t& at(int x, int y) const noexcept { return indices[idx(x, y)]; }

    /// @brief パレット適用 → RGBA(0xAABBGGRR) を out へ (バッファ使い回しで毎フレーム焼ける)。
    void bake(const Palette& pal, std::vector<std::uint32_t>& out) const
    {
        out.resize(indices.size());
        for (std::size_t i = 0; i < indices.size(); ++i) { out[i] = pal.at(indices[i]); }
    }

    [[nodiscard]] std::vector<std::uint32_t> bake(const Palette& pal) const
    {
        std::vector<std::uint32_t> out;
        bake(pal, out);
        return out;
    }

private:
    [[nodiscard]] std::size_t idx(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    }
};

/// @brief RGBA(0xAABBGGRR) 画像から、出現色を集めて Palette + IndexedImage を作る。
/// @details 既存のドット絵 PNG (RGBA) を読み込んで index 化したい時に使う。出現色が 256 を
///          超えたら false (パレットに収まらない = X68000 流に色を減らす必要あり)。
/// @return 256 色以内なら true。outPal/outImg に結果。
inline bool quantizeToPalette(const std::uint32_t* rgba, int w, int h,
                              Palette& outPal, IndexedImage& outImg)
{
    if (rgba == nullptr || w <= 0 || h <= 0) { return false; }
    outImg = IndexedImage(w, h);
    outPal = Palette{};
    std::unordered_map<std::uint32_t, std::uint8_t> map;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint32_t c = rgba[i];
        auto it = map.find(c);
        std::uint8_t index;
        if (it == map.end())
        {
            if (map.size() >= 256) { return false; }   // 257 色目 = パレットに収まらない
            index = static_cast<std::uint8_t>(map.size());
            map.emplace(c, index);
            outPal.set(index, c);
        }
        else { index = it->second; }
        outImg.indices[i] = index;
    }
    return true;
}

}  // namespace mitiru::render
