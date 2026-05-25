#pragma once

/**
 * @file ResponsiveLayout.hpp
 * @brief MitiruEngine UI 用の responsive breakpoint system。
 *
 * 幅ベースの breakpoint と property rule を定義でき、viewport が指定の
 * breakpoint に一致したとき UI node に適用される。
 * mobile-first / desktop-first の評価 strategy をサポートする。
 */

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mitiru::ui {

// -----------------------------------------------------------------------
// Breakpoint
// -----------------------------------------------------------------------

/// @brief responsive rule 一式を有効化する、名前付きの幅レンジ。
struct Breakpoint {
    std::string name;
    float minWidth = 0.0f;   ///< viewport >= minWidth で有効化。
    float maxWidth = 99999.0f; ///< viewport > maxWidth で無効化。
};

// -----------------------------------------------------------------------
// Responsive rule
// -----------------------------------------------------------------------

/// @brief responsive rule が設定できる property。
enum class ResponsiveProperty : uint8_t {
    Visible,       ///< "true" または "false"
    FontSize,      ///< float を文字列化したもの
    Width,         ///< float を文字列化したもの
    Height,        ///< float を文字列化したもの
    Opacity,       ///< float (0..1) を文字列化したもの
    Text,          ///< 文字列値
    Custom         ///< node properties に格納される
};

/// @brief 1 つの responsive rule: 指定 breakpoint で、node の property を設定する。
struct ResponsiveRule {
    std::string        breakpoint; ///< breakpoint 名。
    std::string        nodeId;     ///< 対象 node の識別子 (名前で指定)。
    ResponsiveProperty property = ResponsiveProperty::Custom;
    std::string        customKey;  ///< property == Custom のときの key。
    std::string        value;      ///< 適用する新しい値。
};

// -----------------------------------------------------------------------
// Evaluation strategy
// -----------------------------------------------------------------------

/// @brief 複数の breakpoint が重なったときの一致のさせ方。
enum class ResponsiveStrategy : uint8_t {
    MobileFirst,  ///< 一致する最小の breakpoint が勝つ (minWidth 昇順)。
    DesktopFirst  ///< 一致する最大の breakpoint が勝つ (minWidth 降順)。
};

// -----------------------------------------------------------------------
// ResponsiveLayout
// -----------------------------------------------------------------------

/**
 * @class ResponsiveLayout
 * @brief viewport 幅に基づいて UI node に responsive rule を適用する。
 *
 * 使い方:
 * @code
 *   ResponsiveLayout responsive;
 *   responsive.addBreakpoint("mobile",  0.0f);
 *   responsive.addBreakpoint("tablet",  768.0f);
 *   responsive.addBreakpoint("desktop", 1280.0f);
 *
 *   responsive.setRule("mobile",  "sidebar", ResponsiveProperty::Visible, "false");
 *   responsive.setRule("tablet",  "sidebar", ResponsiveProperty::Visible, "true");
 *   responsive.setRule("desktop", "sidebar", ResponsiveProperty::Width,   "300");
 *
 *   responsive.apply(currentViewportWidth, rootNode);
 * @endcode
 */
class ResponsiveLayout {
    std::vector<Breakpoint>     m_breakpoints;
    std::vector<ResponsiveRule> m_rules;
    ResponsiveStrategy          m_strategy = ResponsiveStrategy::MobileFirst;

public:
    // -------------------------------------------------------------------
    // Breakpoint 管理
    // -------------------------------------------------------------------

    /// @brief 名前付きの breakpoint を追加する。
    void addBreakpoint(const std::string& name, float minWidth) {
        // 既存なら更新する。
        for (auto& bp : m_breakpoints) {
            if (bp.name == name) {
                bp.minWidth = minWidth;
                sortBreakpoints();
                computeMaxWidths();
                return;
            }
        }
        m_breakpoints.push_back(Breakpoint{name, minWidth, 99999.0f});
        sortBreakpoints();
        computeMaxWidths();
    }

    /// @brief breakpoint と、それに紐づく rule を削除する。
    void removeBreakpoint(const std::string& name) {
        m_breakpoints.erase(
            std::remove_if(m_breakpoints.begin(), m_breakpoints.end(),
                [&](const Breakpoint& bp) { return bp.name == name; }),
            m_breakpoints.end());
        m_rules.erase(
            std::remove_if(m_rules.begin(), m_rules.end(),
                [&](const ResponsiveRule& r) { return r.breakpoint == name; }),
            m_rules.end());
        computeMaxWidths();
    }

    /// @brief 全 breakpoint を取得する (minWidth 昇順でソート済み)。
    [[nodiscard]] const std::vector<Breakpoint>& breakpoints() const noexcept {
        return m_breakpoints;
    }

    // -------------------------------------------------------------------
    // Rule 管理
    // -------------------------------------------------------------------

    /// @brief 標準 property に対する responsive rule を追加する。
    void setRule(const std::string& breakpoint,
                 const std::string& nodeId,
                 ResponsiveProperty property,
                 const std::string& value) {
        m_rules.push_back(ResponsiveRule{breakpoint, nodeId, property, {}, value});
    }

    /// @brief custom な node property に対する responsive rule を追加する。
    void setCustomRule(const std::string& breakpoint,
                       const std::string& nodeId,
                       const std::string& key,
                       const std::string& value) {
        m_rules.push_back(ResponsiveRule{
            breakpoint, nodeId, ResponsiveProperty::Custom, key, value});
    }

    /// @brief 特定 node の rule をすべて削除する。
    void clearRulesForNode(const std::string& nodeId) {
        m_rules.erase(
            std::remove_if(m_rules.begin(), m_rules.end(),
                [&](const ResponsiveRule& r) { return r.nodeId == nodeId; }),
            m_rules.end());
    }

    /// @brief 全 rule を削除する。
    void clearAllRules() noexcept { m_rules.clear(); }

    // -------------------------------------------------------------------
    // Strategy
    // -------------------------------------------------------------------

    /// @brief 評価 strategy を設定する。
    void setStrategy(ResponsiveStrategy strategy) noexcept { m_strategy = strategy; }

    /// @brief 現在の評価 strategy を取得する。
    [[nodiscard]] ResponsiveStrategy strategy() const noexcept { return m_strategy; }

    // -------------------------------------------------------------------
    // Presets
    // -------------------------------------------------------------------

    /// @brief 標準的な mobile-first breakpoint を設定する。
    void mobileFirst() {
        m_strategy = ResponsiveStrategy::MobileFirst;
        m_breakpoints.clear();
        addBreakpoint("mobile",  0.0f);
        addBreakpoint("tablet",  768.0f);
        addBreakpoint("desktop", 1280.0f);
        addBreakpoint("wide",    1920.0f);
    }

    /// @brief 標準的な desktop-first breakpoint を設定する。
    void desktopFirst() {
        m_strategy = ResponsiveStrategy::DesktopFirst;
        m_breakpoints.clear();
        addBreakpoint("wide",    1920.0f);
        addBreakpoint("desktop", 1280.0f);
        addBreakpoint("tablet",  768.0f);
        addBreakpoint("mobile",  0.0f);
    }

    // -------------------------------------------------------------------
    // Apply
    // -------------------------------------------------------------------

    /// @brief 指定された viewport 幅に対して有効な breakpoint 名を見つける。
    [[nodiscard]] std::string activeBreakpoint(float viewportWidth) const {
        if (m_breakpoints.empty()) return {};

        if (m_strategy == ResponsiveStrategy::MobileFirst) {
            // minWidth <= viewportWidth を満たす最後の breakpoint。
            std::string active;
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth) {
                    active = bp.name;
                }
            }
            return active;
        }

        // DesktopFirst: minWidth <= viewportWidth を満たす最初の breakpoint。
        for (auto it = m_breakpoints.rbegin(); it != m_breakpoints.rend(); ++it) {
            if (viewportWidth >= it->minWidth) {
                return it->name;
            }
        }
        return m_breakpoints.front().name;
    }

    /**
     * @brief 一致する responsive rule を UI node tree に適用する。
     *
     * MobileFirst: 一致する最小の breakpoint から有効な breakpoint まで
     * の rule を適用する (cascade)。
     * DesktopFirst: 最大から有効な breakpoint まで降順で適用する。
     *
     * @param viewportWidth 現在の viewport 幅 (pixel)。
     * @param root          UI node tree の root。
     */
    void apply(float viewportWidth, UINode& root) const {
        // 一致する breakpoint を cascade 順で集める。
        auto matchingBps = matchingBreakpoints(viewportWidth);

        // cascade 順に rule を適用する。
        for (const auto& bpName : matchingBps) {
            for (const auto& rule : m_rules) {
                if (rule.breakpoint != bpName) continue;

                auto* node = root.findByName(rule.nodeId);
                if (!node) continue;

                applyRule(*node, rule);
            }
        }
    }

    /**
     * @brief custom な applicator callback を使って rule を適用する。
     *
     * node が UINode tree に無い場合向け (例: 外部 system)。
     *
     * @param viewportWidth 現在の viewport 幅。
     * @param applicator    Callback(nodeId, property, customKey, value)。
     */
    void applyCustom(
        float viewportWidth,
        const std::function<void(const std::string& nodeId,
                                 ResponsiveProperty property,
                                 const std::string& customKey,
                                 const std::string& value)>& applicator) const {
        auto matchingBps = matchingBreakpoints(viewportWidth);
        for (const auto& bpName : matchingBps) {
            for (const auto& rule : m_rules) {
                if (rule.breakpoint != bpName) continue;
                applicator(rule.nodeId, rule.property, rule.customKey, rule.value);
            }
        }
    }

private:
    // -------------------------------------------------------------------
    // 内部ヘルパー
    // -------------------------------------------------------------------

    void sortBreakpoints() {
        std::sort(m_breakpoints.begin(), m_breakpoints.end(),
            [](const Breakpoint& a, const Breakpoint& b) {
                return a.minWidth < b.minWidth;
            });
    }

    void computeMaxWidths() {
        for (size_t i = 0; i < m_breakpoints.size(); ++i) {
            if (i + 1 < m_breakpoints.size()) {
                m_breakpoints[i].maxWidth = m_breakpoints[i + 1].minWidth - 0.01f;
            } else {
                m_breakpoints[i].maxWidth = 99999.0f;
            }
        }
    }

    /// @brief 指定 viewport 幅で cascade すべき breakpoint 名を取得する。
    [[nodiscard]] std::vector<std::string> matchingBreakpoints(float viewportWidth) const {
        std::vector<std::string> result;

        if (m_strategy == ResponsiveStrategy::MobileFirst) {
            // 最小から有効な breakpoint まで (含む) を適用する。
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth) {
                    result.push_back(bp.name);
                }
            }
        } else {
            // DesktopFirst: 最大から有効な breakpoint まで降順で適用する。
            for (auto it = m_breakpoints.rbegin(); it != m_breakpoints.rend(); ++it) {
                if (viewportWidth < it->minWidth) {
                    result.push_back(it->name);
                }
            }
            // 有効な breakpoint は常に含める。
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth && viewportWidth <= bp.maxWidth) {
                    result.push_back(bp.name);
                    break;
                }
            }
        }

        return result;
    }

    /// @brief 1 つの rule を node に適用する。
    static void applyRule(UINode& node, const ResponsiveRule& rule) {
        switch (rule.property) {
        case ResponsiveProperty::Visible:
            node.setVisible(rule.value == "true" || rule.value == "1");
            break;
        case ResponsiveProperty::FontSize:
            node.setProperty("fontSize", rule.value);
            break;
        case ResponsiveProperty::Width:
            node.setProperty("width", rule.value);
            break;
        case ResponsiveProperty::Height:
            node.setProperty("height", rule.value);
            break;
        case ResponsiveProperty::Opacity:
            node.setProperty("opacity", rule.value);
            break;
        case ResponsiveProperty::Text:
            node.setText(rule.value);
            break;
        case ResponsiveProperty::Custom:
            node.setProperty(rule.customKey, rule.value);
            break;
        }
    }
};

} // namespace mitiru::ui
