#pragma once

/// @file TimeTravelRecorder.hpp
/// @brief 時間移動 (time-travel) 用の固定容量リングバッファ
/// @details
/// engine の差別化軸 2 (タイムトラベル inspector) の foundation primitive。
/// 任意のユーザー定義 `Snapshot` 型を毎フレーム push、後で `at(offset)` で
/// "n フレーム前の状態" を読み戻す。容量を超えたら自動で oldest を捨てる。
///
/// 設計判断:
/// - ユーザーの Snapshot 型に対して engine 側は知識を持たない (template T)
/// - "0 offset = newest" にすることで HTML 側の scrubber UI と直結 (右端 = 現在,
///   左端 = 最古) する自然な原点になる
/// - copy 可能な値型を想定 (移動可能なら move-aware に拡張可能)
/// - 例外を投げない: out-of-range は nullptr を返す
/// - header-only / 単一 deque ラッパ。pool / arena 化は profiler が必要と
///   叫ぶまで遅延

#include <cstddef>
#include <deque>
#include <utility>

namespace mitiru::observe
{

/// @brief 固定容量のリングバッファ — engine 差別化軸 2 用 foundation
/// @tparam Snapshot ユーザー定義のフレーム状態型 (copyable / movable)
template <typename Snapshot>
class TimeTravelRecorder
{
public:
    /// @brief 指定容量で構築
    /// @param capacity 保持する最大フレーム数 (典型値 300 = 60fps × 5sec)
    explicit TimeTravelRecorder(std::size_t capacity = 300) noexcept
        : m_capacity(capacity == 0 ? 1 : capacity)
    {
    }

    /// @brief 1 フレーム分の snapshot を最新として記録
    /// @details 容量超過時は最古を破棄する
    void push(Snapshot snap)
    {
        m_buf.push_back(std::move(snap));
        while (m_buf.size() > m_capacity)
        {
            m_buf.pop_front();
        }
    }

    /// @brief 現フレームを基準に N フレーム前の snapshot を取得
    /// @param offsetFromNewest 0 = newest, 1 = 1 frame ago, ...
    /// @return 範囲内なら snapshot へのポインタ、範囲外なら nullptr
    /// @note 返り値は次の push / clear まで valid
    [[nodiscard]] const Snapshot* at(std::size_t offsetFromNewest) const noexcept
    {
        if (m_buf.empty() || offsetFromNewest >= m_buf.size())
        {
            return nullptr;
        }
        return &m_buf[m_buf.size() - 1 - offsetFromNewest];
    }

    /// @brief 現在保持してる snapshot 数
    [[nodiscard]] std::size_t size() const noexcept { return m_buf.size(); }

    /// @brief 最大容量
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    /// @brief 保持なし判定
    [[nodiscard]] bool empty() const noexcept { return m_buf.empty(); }

    /// @brief 全フレーム破棄 (タイムトラベル復帰後など、未来 history が無効化される時に呼ぶ)
    void clear() noexcept { m_buf.clear(); }

private:
    std::deque<Snapshot> m_buf;
    std::size_t          m_capacity;
};

}  // namespace mitiru::observe
