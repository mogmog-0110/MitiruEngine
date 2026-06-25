#pragma once

/// @file Merge.hpp
/// @brief 決定論タイムラインの 3-way マージ (「つむぎ」の核)。共通祖先 A から分岐した
///        2 つの GameMemory blob X / Y を、reflection の FieldDescriptor を使って
///        フィールド単位で 1 つに織り合わせる。
/// @details VCS の 3-way merge をゲーム状態に適用する:
///            - どちらも変えてない         → A のまま
///            - 片方だけ変えた             → その側を採用
///            - 両方が「同じ値」に変えた    → 採用 (衝突でない)
///            - 両方が「別の値」に変えた    → ★衝突 → policy で解決 + report に記録
///          flat-POD + reflection だからフィールド/要素単位の意味マージが厳密にできる。
///          FixedVec は index 単位 + count を 3-way (v1: 挿入/削除のズレ吸収は未対応=既知の限界)。
///          nested struct / FixedVec<struct> は v1 では「要素まるごと」の粗マージ (whole-region)。
///
/// 依存は FieldDescriptor (module/Reflection.hpp) のみ。pure 関数なのでヘッドレスで単体テスト可。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mitiru/module/Reflection.hpp>

namespace mitiru::module
{

/// 衝突の解決方針 (v1)。両側が別の値に変えたフィールドをどちらで埋めるか。
enum class MergePolicy : std::uint8_t { TakeX, TakeY };

/// 1 件の衝突 (どのフィールドの・どの要素が割れたか)。
struct MergeConflict
{
    std::string field;       ///< フィールド名 (FixedVec なら "bag[2]" のように要素付き)
    int         elemIndex;   ///< scalar/str/struct は -1、FixedVec 要素は 0..N-1
};

/// マージ結果レポート。
struct MergeReport
{
    std::vector<MergeConflict> conflicts;
    [[nodiscard]] bool clean() const noexcept { return conflicts.empty(); }
};

namespace detail
{

/// 1 領域 (size バイト) を 3-way マージして out へ書く。衝突なら true。
inline bool mergeRegion(std::uint8_t* out, const std::uint8_t* a, const std::uint8_t* x,
                        const std::uint8_t* y, std::size_t size, MergePolicy policy)
{
    const bool cX = std::memcmp(a, x, size) != 0;
    const bool cY = std::memcmp(a, y, size) != 0;
    if (!cX && !cY) { std::memcpy(out, a, size); return false; }   // 変更なし
    if (cX && !cY)  { std::memcpy(out, x, size); return false; }   // X だけ
    if (!cX && cY)  { std::memcpy(out, y, size); return false; }   // Y だけ
    if (std::memcmp(x, y, size) == 0) { std::memcpy(out, x, size); return false; }  // 同じ変更
    std::memcpy(out, (policy == MergePolicy::TakeX) ? x : y, size);  // 衝突
    return true;
}

}  // namespace detail

/// @brief 3-way マージ。out は blobSize バイト。fields は GameMemory のフィールド記述子。
/// @param A 共通祖先 / @param X,Y 分岐後の 2 状態 / @param out 結果 (A から開始し織り込む)
inline void merge3(const FieldDescriptor* fields, std::size_t fieldCount,
                   const std::uint8_t* A, const std::uint8_t* X, const std::uint8_t* Y,
                   std::uint8_t* out, std::size_t blobSize,
                   MergeReport& report, MergePolicy policy = MergePolicy::TakeX)
{
    report.conflicts.clear();
    std::memcpy(out, A, blobSize);   // 既定は祖先。各フィールドを上書きしていく。

    for (std::size_t fi = 0; fi < fieldCount; ++fi)
    {
        const FieldDescriptor& f = fields[fi];
        const std::uint32_t off = f.offset;

        if (f.hasCount != 0)   // FixedVec: count を 3-way + 要素を index 単位
        {
            // count (uint32) を 3-way
            const std::uint32_t co = off + f.countOffset;
            if (detail::mergeRegion(out + co, A + co, X + co, Y + co, 4u, policy))
            {
                report.conflicts.push_back({std::string(f.name) + ".count", -1});
            }
            // 全スロットを要素単位でマージ (決定論・単純優先。live count 外も織るが害なし)
            for (std::uint32_t i = 0; i < f.elemCount; ++i)
            {
                const std::uint32_t eo = off + i * f.elemSize;
                if (detail::mergeRegion(out + eo, A + eo, X + eo, Y + eo, f.elemSize, policy))
                {
                    report.conflicts.push_back(
                        {std::string(f.name) + "[" + std::to_string(i) + "]", static_cast<int>(i)});
                }
            }
        }
        else   // scalar / str / nested struct: 1 領域 (elemSize * elemCount)
        {
            const std::size_t size = static_cast<std::size_t>(f.elemSize)
                                   * static_cast<std::size_t>(f.elemCount);
            if (detail::mergeRegion(out + off, A + off, X + off, Y + off, size, policy))
            {
                report.conflicts.push_back({std::string(f.name), -1});
            }
        }
    }
}

/// @brief std::vector 版の薄いラッパ。
inline MergeReport merge3(const std::vector<FieldDescriptor>& fields,
                          const std::uint8_t* A, const std::uint8_t* X, const std::uint8_t* Y,
                          std::uint8_t* out, std::size_t blobSize,
                          MergePolicy policy = MergePolicy::TakeX)
{
    MergeReport r;
    merge3(fields.data(), fields.size(), A, X, Y, out, blobSize, r, policy);
    return r;
}

}  // namespace mitiru::module
