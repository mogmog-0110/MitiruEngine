#pragma once

/**
 * @file ResponsiveLayout.hpp
 * @brief Responsive breakpoint system for MitiruEngine UI.
 *
 * Allows defining width-based breakpoints and property rules that are
 * applied to UI nodes when the viewport matches a given breakpoint.
 * Supports mobile-first and desktop-first evaluation strategies.
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

/// @brief A named width range that activates a set of responsive rules.
struct Breakpoint {
    std::string name;
    float minWidth = 0.0f;   ///< Activates when viewport >= minWidth.
    float maxWidth = 99999.0f; ///< Deactivates when viewport > maxWidth.
};

// -----------------------------------------------------------------------
// Responsive rule
// -----------------------------------------------------------------------

/// @brief Property that can be set by a responsive rule.
enum class ResponsiveProperty : uint8_t {
    Visible,       ///< "true" or "false"
    FontSize,      ///< float as string
    Width,         ///< float as string
    Height,        ///< float as string
    Opacity,       ///< float (0..1) as string
    Text,          ///< string value
    Custom         ///< stored in node properties
};

/// @brief A single responsive rule: at a given breakpoint, set a property on a node.
struct ResponsiveRule {
    std::string        breakpoint; ///< Breakpoint name.
    std::string        nodeId;     ///< Target node identifier (by name).
    ResponsiveProperty property = ResponsiveProperty::Custom;
    std::string        customKey;  ///< Key when property == Custom.
    std::string        value;      ///< New value to apply.
};

// -----------------------------------------------------------------------
// Evaluation strategy
// -----------------------------------------------------------------------

/// @brief How breakpoints are matched when multiple overlap.
enum class ResponsiveStrategy : uint8_t {
    MobileFirst,  ///< Smallest matching breakpoint wins (ascending minWidth).
    DesktopFirst  ///< Largest matching breakpoint wins (descending minWidth).
};

// -----------------------------------------------------------------------
// ResponsiveLayout
// -----------------------------------------------------------------------

/**
 * @class ResponsiveLayout
 * @brief Applies responsive rules to UI nodes based on viewport width.
 *
 * Usage:
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
    // Breakpoint management
    // -------------------------------------------------------------------

    /// @brief Add a named breakpoint.
    void addBreakpoint(const std::string& name, float minWidth) {
        // Check if already exists; update if so.
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

    /// @brief Remove a breakpoint and its associated rules.
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

    /// @brief Get all breakpoints (sorted by minWidth ascending).
    [[nodiscard]] const std::vector<Breakpoint>& breakpoints() const noexcept {
        return m_breakpoints;
    }

    // -------------------------------------------------------------------
    // Rule management
    // -------------------------------------------------------------------

    /// @brief Add a responsive rule for a standard property.
    void setRule(const std::string& breakpoint,
                 const std::string& nodeId,
                 ResponsiveProperty property,
                 const std::string& value) {
        m_rules.push_back(ResponsiveRule{breakpoint, nodeId, property, {}, value});
    }

    /// @brief Add a responsive rule for a custom node property.
    void setCustomRule(const std::string& breakpoint,
                       const std::string& nodeId,
                       const std::string& key,
                       const std::string& value) {
        m_rules.push_back(ResponsiveRule{
            breakpoint, nodeId, ResponsiveProperty::Custom, key, value});
    }

    /// @brief Remove all rules for a specific node.
    void clearRulesForNode(const std::string& nodeId) {
        m_rules.erase(
            std::remove_if(m_rules.begin(), m_rules.end(),
                [&](const ResponsiveRule& r) { return r.nodeId == nodeId; }),
            m_rules.end());
    }

    /// @brief Remove all rules.
    void clearAllRules() noexcept { m_rules.clear(); }

    // -------------------------------------------------------------------
    // Strategy
    // -------------------------------------------------------------------

    /// @brief Set the evaluation strategy.
    void setStrategy(ResponsiveStrategy strategy) noexcept { m_strategy = strategy; }

    /// @brief Get the current evaluation strategy.
    [[nodiscard]] ResponsiveStrategy strategy() const noexcept { return m_strategy; }

    // -------------------------------------------------------------------
    // Presets
    // -------------------------------------------------------------------

    /// @brief Configure standard mobile-first breakpoints.
    void mobileFirst() {
        m_strategy = ResponsiveStrategy::MobileFirst;
        m_breakpoints.clear();
        addBreakpoint("mobile",  0.0f);
        addBreakpoint("tablet",  768.0f);
        addBreakpoint("desktop", 1280.0f);
        addBreakpoint("wide",    1920.0f);
    }

    /// @brief Configure standard desktop-first breakpoints.
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

    /// @brief Find the active breakpoint name for the given viewport width.
    [[nodiscard]] std::string activeBreakpoint(float viewportWidth) const {
        if (m_breakpoints.empty()) return {};

        if (m_strategy == ResponsiveStrategy::MobileFirst) {
            // Last breakpoint whose minWidth <= viewportWidth.
            std::string active;
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth) {
                    active = bp.name;
                }
            }
            return active;
        }

        // DesktopFirst: first breakpoint whose minWidth <= viewportWidth.
        for (auto it = m_breakpoints.rbegin(); it != m_breakpoints.rend(); ++it) {
            if (viewportWidth >= it->minWidth) {
                return it->name;
            }
        }
        return m_breakpoints.front().name;
    }

    /**
     * @brief Apply matching responsive rules to a UI node tree.
     *
     * For MobileFirst: applies rules from smallest matching breakpoint
     * up to the active one (cascading).
     * For DesktopFirst: applies rules from largest down to active.
     *
     * @param viewportWidth Current viewport width in pixels.
     * @param root          Root of the UI node tree.
     */
    void apply(float viewportWidth, UINode& root) const {
        // Collect matching breakpoints in cascade order.
        auto matchingBps = matchingBreakpoints(viewportWidth);

        // Apply rules in cascade order.
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
     * @brief Apply rules using a custom applicator callback.
     *
     * For cases where nodes are not in a UINode tree (e.g., external systems).
     *
     * @param viewportWidth Current viewport width.
     * @param applicator    Callback(nodeId, property, customKey, value).
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
    // Internal helpers
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

    /// @brief Get breakpoint names that should cascade for the viewport width.
    [[nodiscard]] std::vector<std::string> matchingBreakpoints(float viewportWidth) const {
        std::vector<std::string> result;

        if (m_strategy == ResponsiveStrategy::MobileFirst) {
            // Apply from smallest up to (and including) active.
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth) {
                    result.push_back(bp.name);
                }
            }
        } else {
            // DesktopFirst: apply from largest down to active.
            for (auto it = m_breakpoints.rbegin(); it != m_breakpoints.rend(); ++it) {
                if (viewportWidth < it->minWidth) {
                    result.push_back(it->name);
                }
            }
            // Always include the active breakpoint.
            for (const auto& bp : m_breakpoints) {
                if (viewportWidth >= bp.minWidth && viewportWidth <= bp.maxWidth) {
                    result.push_back(bp.name);
                    break;
                }
            }
        }

        return result;
    }

    /// @brief Apply a single rule to a node.
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
