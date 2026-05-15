#pragma once

/// @file FrameBudget.hpp
/// @brief フレームバジェットトラッキングシステム
/// @details カテゴリ別のフレーム時間を計測し、16.67msバジェットに対する
///          使用率を計算する。F11キーでオーバーレイ表示を切り替え可能。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include <sgc/types/Color.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::debug
{

/// @brief フレームバジェットトラッキング
/// @details 各カテゴリの処理時間を計測し、60FPS (16.67ms) バジェットに対する
///          使用率をリアルタイムに算出する。
///
/// @code
/// FrameBudget budget;
/// // メインループ:
/// budget.beginFrame();
/// budget.beginCategory(FrameBudget::Category::Render);
/// // ... 描画処理 ...
/// budget.endCategory();
/// budget.endFrame();
///
/// float usage = budget.budgetUsagePercent();
/// @endcode
class FrameBudget
{
public:
    /// @brief 計測カテゴリ
    enum class Category : std::uint8_t
    {
        Render,
        Physics,
        Audio,
        Script,
        UI,
        Other,
        Count
    };

    /// @brief 60FPSのフレームバジェット（ミリ秒）
    static constexpr float kBudgetMs = 16.667f;

    /// @brief カテゴリ数
    static constexpr std::size_t kCategoryCount =
        static_cast<std::size_t>(Category::Count);

    /// @brief コンストラクタ
    FrameBudget() noexcept
    {
        m_categoryMs.fill(0.0f);
    }

    // ── フレーム計測 ─────────────────────────────────────────

    /// @brief フレーム計測を開始する
    void beginFrame()
    {
        m_frameStart = HiResClock::now();
        m_categoryMs.fill(0.0f);
        m_activeCategory = Category::Count;
    }

    /// @brief カテゴリ計測を開始する
    /// @param cat 計測するカテゴリ
    void beginCategory(Category cat)
    {
        if (m_activeCategory != Category::Count)
        {
            endCategory();
        }
        m_activeCategory = cat;
        m_categoryStart = HiResClock::now();
    }

    /// @brief 現在のカテゴリ計測を終了する
    void endCategory()
    {
        if (m_activeCategory == Category::Count)
        {
            return;
        }
        const auto end = HiResClock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - m_categoryStart).count();
        const float ms = static_cast<float>(us) / 1000.0f;

        const auto idx = static_cast<std::size_t>(m_activeCategory);
        m_categoryMs[idx] += ms;
        m_activeCategory = Category::Count;
    }

    /// @brief フレーム計測を終了する
    void endFrame()
    {
        if (m_activeCategory != Category::Count)
        {
            endCategory();
        }
        const auto end = HiResClock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - m_frameStart).count();
        m_totalFrameMs = static_cast<float>(us) / 1000.0f;

        // Other = total - sum of known categories
        float knownMs = 0.0f;
        for (std::size_t i = 0; i < kCategoryCount; ++i)
        {
            if (static_cast<Category>(i) != Category::Other)
            {
                knownMs += m_categoryMs[i];
            }
        }
        m_categoryMs[static_cast<std::size_t>(Category::Other)] =
            std::max(0.0f, m_totalFrameMs - knownMs);
    }

    // ── 統計取得 ─────────────────────────────────────────────

    /// @brief フレーム全体の処理時間を取得する
    /// @return ミリ秒
    [[nodiscard]] float totalFrameMs() const noexcept
    {
        return m_totalFrameMs;
    }

    /// @brief カテゴリごとの処理時間を取得する
    /// @param cat カテゴリ
    /// @return ミリ秒
    [[nodiscard]] float categoryMs(Category cat) const noexcept
    {
        const auto idx = static_cast<std::size_t>(cat);
        if (idx >= kCategoryCount)
        {
            return 0.0f;
        }
        return m_categoryMs[idx];
    }

    /// @brief 16.67msバジェットに対する使用率を取得する
    /// @return パーセント（100.0 = バジェット丁度）
    [[nodiscard]] float budgetUsagePercent() const noexcept
    {
        return (m_totalFrameMs / kBudgetMs) * 100.0f;
    }

    // ── オーバーレイ ─────────────────────────────────────────

    /// @brief オーバーレイの表示状態を取得する
    [[nodiscard]] bool isOverlayVisible() const noexcept
    {
        return m_overlayVisible;
    }

    /// @brief オーバーレイの表示/非表示をトグルする
    void toggleOverlay() noexcept
    {
        m_overlayVisible = !m_overlayVisible;
    }

    /// @brief オーバーレイの表示状態を設定する
    /// @param visible true=表示、false=非表示
    void setOverlayVisible(bool visible) noexcept
    {
        m_overlayVisible = visible;
    }

    /// @brief フレームバジェットオーバーレイを描画する
    /// @param screen 描画先スクリーン
    /// @param x 左上X座標
    /// @param y 左上Y座標
    /// @param w 幅
    /// @param h 高さ
    void drawOverlay(Screen& screen, float x, float y, float w, float h) const
    {
        if (!m_overlayVisible)
        {
            return;
        }
        drawBackground(screen, x, y, w, h);
        drawTitle(screen, x, y, w);
        drawCategoryBars(screen, x, y + 24.0f, w, h - 24.0f);
    }

    // ── カテゴリ名 ───────────────────────────────────────────

    /// @brief カテゴリの表示名を取得する
    /// @param cat カテゴリ
    /// @return 表示名
    [[nodiscard]] static constexpr std::string_view categoryName(Category cat) noexcept
    {
        switch (cat)
        {
        case Category::Render:  return "Render";
        case Category::Physics: return "Physics";
        case Category::Audio:   return "Audio";
        case Category::Script:  return "Script";
        case Category::UI:      return "UI";
        case Category::Other:   return "Other";
        default:                return "Unknown";
        }
    }

    /// @brief カテゴリの表示色を取得する
    /// @param cat カテゴリ
    /// @return RGBA色
    [[nodiscard]] static constexpr sgc::Colorf categoryColor(Category cat) noexcept
    {
        switch (cat)
        {
        case Category::Render:
            return sgc::Colorf{0.27f, 0.27f, 1.0f, 1.0f};
        case Category::Physics:
            return sgc::Colorf{0.27f, 1.0f, 0.27f, 1.0f};
        case Category::Audio:
            return sgc::Colorf{1.0f, 1.0f, 0.27f, 1.0f};
        case Category::Script:
            return sgc::Colorf{0.67f, 0.27f, 1.0f, 1.0f};
        case Category::UI:
            return sgc::Colorf{1.0f, 0.53f, 0.27f, 1.0f};
        case Category::Other:
            return sgc::Colorf{0.53f, 0.53f, 0.53f, 1.0f};
        default:
            return sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};
        }
    }

private:
    using HiResClock = std::chrono::high_resolution_clock;

    // ── 描画ヘルパー ─────────────────────────────────────────

    /// @brief 背景を描画する
    void drawBackground(Screen& screen, float x, float y, float w, float h) const
    {
        const sgc::Colorf bgColor{0.0f, 0.0f, 0.0f, 0.75f};
        screen.drawRect(sgc::Rectf{x, y, w, h}, bgColor);
    }

    /// @brief タイトル行を描画する
    void drawTitle(Screen& screen, float x, float y, float w) const
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Frame Budget: %.1f%% (%.2fms / %.2fms)",
                      budgetUsagePercent(), m_totalFrameMs, kBudgetMs);

        const sgc::Colorf titleColor = budgetUsagePercent() > 100.0f
            ? sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f}
            : sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};

        const sgc::Rectf titleRect{x + 4.0f, y + 2.0f, w - 8.0f, 20.0f};
        screen.drawTextInRect(titleRect, buf, titleColor, 12.0f);
    }

    /// @brief カテゴリバーを描画する
    void drawCategoryBars(Screen& screen, float x, float y,
                          float w, float h) const
    {
        const float barHeight = 16.0f;
        const float rowHeight = 20.0f;
        const float labelWidth = 60.0f;
        const float barAreaWidth = w - labelWidth - 80.0f;
        const float valueX = x + labelWidth + barAreaWidth + 4.0f;

        for (std::size_t i = 0; i < kCategoryCount; ++i)
        {
            const auto cat = static_cast<Category>(i);
            const float rowY = y + static_cast<float>(i) * rowHeight;

            if (rowY + barHeight > y + h)
            {
                break;
            }

            // Label
            const sgc::Rectf labelRect{x + 4.0f, rowY, labelWidth - 4.0f, barHeight};
            screen.drawTextInRect(labelRect,
                                  categoryName(cat),
                                  sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f},
                                  11.0f);

            // Bar background
            const sgc::Rectf barBgRect{x + labelWidth, rowY, barAreaWidth, barHeight};
            screen.drawRect(barBgRect, sgc::Colorf{0.2f, 0.2f, 0.2f, 1.0f});

            // Bar fill
            const float ms = m_categoryMs[i];
            const float ratio = std::min(ms / kBudgetMs, 1.0f);
            if (ratio > 0.0f)
            {
                const float barW = barAreaWidth * ratio;
                const sgc::Rectf barRect{x + labelWidth, rowY, barW, barHeight};
                screen.drawRect(barRect, categoryColor(cat));
            }

            // Value text
            char valBuf[32];
            std::snprintf(valBuf, sizeof(valBuf), "%.2fms", ms);
            const sgc::Rectf valueRect{valueX, rowY, 72.0f, barHeight};
            screen.drawTextInRect(valueRect, valBuf,
                                  sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f},
                                  11.0f);
        }

        // Budget line marker (red line at 100%)
        const float budgetLineX = x + labelWidth + barAreaWidth;
        const float budgetLineY = y;
        const float budgetLineH = std::min(
            static_cast<float>(kCategoryCount) * rowHeight, h);
        const sgc::Rectf budgetLine{budgetLineX, budgetLineY, 1.0f, budgetLineH};
        screen.drawRect(budgetLine, sgc::Colorf{1.0f, 0.0f, 0.0f, 0.8f});
    }

    // ── メンバー変数 ─────────────────────────────────────────

    HiResClock::time_point m_frameStart{};          ///< フレーム開始時点
    HiResClock::time_point m_categoryStart{};       ///< カテゴリ開始時点
    Category m_activeCategory = Category::Count;    ///< 現在計測中のカテゴリ
    float m_totalFrameMs = 0.0f;                    ///< フレーム全体（ミリ秒）
    std::array<float, kCategoryCount> m_categoryMs{}; ///< カテゴリ別時間（ミリ秒）
    bool m_overlayVisible = false;                  ///< オーバーレイ表示フラグ
};

/// @brief RAIIカテゴリ計測スコープ
/// @details コンストラクタでbeginCategory、デストラクタでendCategoryを呼ぶ。
///
/// @code
/// {
///     FrameBudgetScope scope(budget, FrameBudget::Category::Render);
///     // ... 描画処理 ...
/// } // 自動的にendCategory
/// @endcode
class FrameBudgetScope
{
public:
    /// @brief コンストラクタ（カテゴリ計測開始）
    /// @param budget FrameBudgetインスタンス（非所有）
    /// @param cat 計測カテゴリ
    FrameBudgetScope(FrameBudget& budget, FrameBudget::Category cat) noexcept
        : m_budget(budget)
    {
        m_budget.beginCategory(cat);
    }

    /// @brief デストラクタ（カテゴリ計測終了）
    ~FrameBudgetScope()
    {
        m_budget.endCategory();
    }

    /// コピー禁止
    FrameBudgetScope(const FrameBudgetScope&) = delete;
    FrameBudgetScope& operator=(const FrameBudgetScope&) = delete;

    /// ムーブ禁止
    FrameBudgetScope(FrameBudgetScope&&) = delete;
    FrameBudgetScope& operator=(FrameBudgetScope&&) = delete;

private:
    FrameBudget& m_budget; ///< FrameBudgetインスタンス
};

} // namespace mitiru::debug
