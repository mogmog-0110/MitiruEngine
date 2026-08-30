#pragma once

#include <vector>
#include <cstddef>

#include "mitiru/time/detail/SmallFunction.hpp"
#include "mitiru/debug/TracyZones.hpp"

/// @file Sequence.hpp
/// @brief 順序付き wait と callback をチェーンできる timeline。
///
/// step は構築時に追加する (そこでの heap allocation は許容)。
/// @c tick() は allocation せずに timeline を進める。cursor を動かし
/// accumulator から減算するだけ。
///
/// callback は @c mitiru::time::detail::SmallFunction で保持する。48 byte
/// SBO の type-erased callable。キャプチャが 48 byte までの lambda は
/// インラインに格納される (action step ごとの heap allocation 無し)。
///
/// 使用例:
/// @code
///   mitiru::time::Sequence seq;
///   seq.wait(1.0f)
///      .action([&]{ playSound("sizzle"); })
///      .wait(2.0f)
///      .action([&]{ showText("Done!"); });
///
///   // in update loop:
///   seq.tick(dt);
///   if (seq.done()) { /* sequence finished */ }
/// @endcode
///
/// スレッド安全性: thread-safe ではない。構築と同じスレッドで tick すること。

namespace mitiru::time {

/// 時間付き wait と即時 callback の順序付きシーケンス。
/// wait と action は自由に交互配置できる。action step は即座に発火し、
/// 同じ @c tick() 呼び出し内で次の step へ進む。
class Sequence {
public:
    Sequence() = default;

    /// @p seconds 秒の wait step を追加する。チェーン用に *this を返す。
    Sequence& wait(float seconds) {
        m_steps.push_back(Step{seconds, detail::SmallFunction{}});
        return *this;
    }

    /// action step (継続時間 0 の callback) を追加する。チェーン用に *this
    /// を返す。callback は cursor がこの step に到達した @c tick() 中に発火し、
    /// その時点でキャプチャが 48 byte までなら allocation は発生しない。
    ///
    /// @tparam F  シグネチャ @c void() の任意の callable。
    template <typename F>
    Sequence& action(F&& fn) {
        m_steps.push_back(Step{0.0f, detail::SmallFunction{std::forward<F>(fn)}});
        return *this;
    }

    /// sequence を @p dt 秒進める。
    /// tick() は allocation 無し: cursor と accumulator のみを変更する。
    void tick(float dt) {
        if (done()) { return; }

        m_acc += dt;

        while (m_cursor < m_steps.size()) {
            Step& step = m_steps[m_cursor];

            if (step.action_) {
                // action step: 即座に発火して進む (時間を消費しない)
                {
                    MITIRU_ZONE_NAMED("Sequence::action");
                    step.action_();
                }
                ++m_cursor;
                continue;
            }

            // wait step: 蓄積した時間を消費する
            if (m_acc >= step.wait_) {
                m_acc -= step.wait_;
                ++m_cursor;
            } else {
                break;
            }
        }
    }

    /// 最後の step が処理された後 true を返す。
    [[nodiscard]] bool done() const noexcept {
        return m_cursor >= m_steps.size();
    }

    /// 最初の step から再開する。蓄積された時間は破棄される。
    void reset() noexcept {
        m_cursor = 0;
        m_acc    = 0.0f;
    }

private:
    struct Step {
        float                    wait_;    // wait step は > 0、action step は 0
        detail::SmallFunction    action_;  // wait step では空
    };

    std::vector<Step> m_steps;
    std::size_t       m_cursor = 0;
    float             m_acc    = 0.0f;
};

} // namespace mitiru::time
