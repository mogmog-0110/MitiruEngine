#pragma once

/// @file CefUploadPlanner.hpp
/// @brief CEF 部分アップロード (uploadPartial) の dirty rect 配置を計算する純ロジック。
/// @details DX12 / CEF DOM ヘッダに依存しないため GPU 無しで単体テストできる (#29)。

#include <cstddef>
#include <span>
#include <vector>

namespace mitiru::cef
{

/// @brief 1 つの dirty rect の、アップロードバッファ内での配置。
struct UploadPlacement
{
    int         x, y, width, height;  ///< テクスチャ上の貼り付け先矩形
    std::size_t offset;               ///< バッファ内オフセット (512B 整列)
    std::size_t rowPitch;             ///< 行ピッチ (256B 整列)
};

namespace detail
{
inline std::size_t alignUp(std::size_t v, std::size_t a) noexcept
{
    return (v + a - 1) & ~(a - 1);
}
}

/// @brief 各 dirty rect に重ならない 512B 整列オフセットを割り当てる (#29)。
/// @details 旧 uploadPartial は全 rect を offset=0 に書いていたため、ループ内の memcpy が
///          前 rect の転送元を GPU 実行前に上書きし、全コピーが最後の rect を読んでいた
///          (別位置にゲージ縞が複製)。rect ごとに独立領域へ置けば、同一 ExecuteCommandLists
///          内の複数 CopyTextureRegion が互いの元を壊さない。
///          不正 rect は skip。総量が bufferSize を超えたら overflow=true で空を返す
///          (caller はフル転送へ退避する)。
/// @tparam Rect  `.x .y .width .height` (int) を持つ任意の矩形型 (CefRect 等)。
template <class Rect>
[[nodiscard]] inline std::vector<UploadPlacement> planUploadPlacements(
    std::span<const Rect> rects, int texW, int texH,
    std::size_t bufferSize, bool& overflow) noexcept
{
    overflow = false;
    std::vector<UploadPlacement> out;
    out.reserve(rects.size());
    std::size_t cursor = 0;
    for (const auto& r : rects)
    {
        if (r.x < 0 || r.y < 0 || r.x + r.width > texW || r.y + r.height > texH ||
            r.width <= 0 || r.height <= 0)
        {
            continue;  // 不正な矩形はスキップ
        }
        const std::size_t rowPitch = detail::alignUp(static_cast<std::size_t>(r.width) * 4, 256);
        const std::size_t offset   = detail::alignUp(cursor, 512);
        const std::size_t bytes    = rowPitch * static_cast<std::size_t>(r.height);
        if (offset + bytes > bufferSize)
        {
            overflow = true;
            out.clear();
            return out;
        }
        out.push_back({r.x, r.y, r.width, r.height, offset, rowPitch});
        cursor = offset + bytes;
    }
    return out;
}

}  // namespace mitiru::cef
