#pragma once

/**
 * @file ConstraintLayout.hpp
 * @brief Constraint-based layout engine for MitiruEngine UI.
 *
 * Provides a simplified Auto Layout-style system where nodes are positioned
 * by declaring relationships (constraints) between edges, centers, and
 * dimensions. Uses an iterative solver (max 10 iterations) rather than
 * a full Cassowary implementation.
 */

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mitiru::ui {

// -----------------------------------------------------------------------
// Constraint types
// -----------------------------------------------------------------------

/// @brief The kind of spatial relationship a constraint describes.
enum class ConstraintType : uint8_t {
    LeftToLeft,      ///< node.left   = target.left   + offset
    LeftToRight,     ///< node.left   = target.right  + offset
    RightToRight,    ///< node.right  = target.right  + offset
    RightToLeft,     ///< node.right  = target.left   + offset
    TopToTop,        ///< node.top    = target.top    + offset
    TopToBottom,     ///< node.top    = target.bottom + offset
    BottomToBottom,  ///< node.bottom = target.bottom + offset
    BottomToTop,     ///< node.bottom = target.top    + offset
    CenterX,         ///< node.centerX = target.centerX + offset
    CenterY,         ///< node.centerY = target.centerY + offset
    Width,           ///< node.width  = target.width  * multiplier + offset
    Height,          ///< node.height = target.height * multiplier + offset
    AspectRatio      ///< node.width  = node.height * multiplier
};

/// @brief A single constraint linking a node to a target.
struct UIConstraint {
    ConstraintType type     = ConstraintType::LeftToLeft;
    std::string    target;      ///< Target node ID string, or "parent".
    float          offset     = 0.0f;
    float          multiplier = 1.0f;
};

// -----------------------------------------------------------------------
// Resolved node bounds (internal)
// -----------------------------------------------------------------------

namespace detail {

/// @brief Mutable bounds used during iterative solving.
struct SolvedBounds {
    float left   = 0.0f;
    float top    = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    [[nodiscard]] float right()   const noexcept { return left + width; }
    [[nodiscard]] float bottom()  const noexcept { return top + height; }
    [[nodiscard]] float centerX() const noexcept { return left + width * 0.5f; }
    [[nodiscard]] float centerY() const noexcept { return top + height * 0.5f; }

    void setRight(float v)   noexcept { width  = std::max(0.0f, v - left); }
    void setBottom(float v)  noexcept { height = std::max(0.0f, v - top); }
    void setCenterX(float v) noexcept { left   = v - width * 0.5f; }
    void setCenterY(float v) noexcept { top    = v - height * 0.5f; }
};

} // namespace detail

// -----------------------------------------------------------------------
// ConstraintLayout
// -----------------------------------------------------------------------

/**
 * @class ConstraintLayout
 * @brief Positions nodes by solving a set of declared constraints.
 *
 * Usage:
 * @code
 *   ConstraintLayout layout;
 *
 *   // Center "title" horizontally in parent, 20px from top.
 *   layout.addConstraint("title", {ConstraintType::CenterX, "parent", 0.0f});
 *   layout.addConstraint("title", {ConstraintType::TopToTop, "parent", 20.0f});
 *   layout.addConstraint("title", {ConstraintType::Width, "parent", -40.0f, 1.0f});
 *   layout.addConstraint("title", {ConstraintType::Height, "parent", 0.0f, 0.0f}); // explicit 50
 *
 *   // Chain: "subtitle" sits below "title" with 8px gap.
 *   layout.addConstraint("subtitle", {ConstraintType::TopToBottom, "title", 8.0f});
 *   layout.addConstraint("subtitle", {ConstraintType::LeftToLeft, "title", 0.0f});
 *   layout.addConstraint("subtitle", {ConstraintType::RightToRight, "title", 0.0f});
 *
 *   sgc::Rectf parent{0, 0, 800, 600};
 *   auto results = layout.solve(parent);
 * @endcode
 */
class ConstraintLayout {
    /// @brief All constraints grouped by node ID.
    std::map<std::string, std::vector<UIConstraint>> m_constraints;

    /// @brief Default sizes for nodes before constraint solving.
    std::map<std::string, std::pair<float, float>> m_defaultSizes;

    /// @brief Maximum solver iterations.
    static constexpr int kMaxIterations = 10;

public:
    /// @brief Add a constraint for the given node.
    void addConstraint(const std::string& nodeId, UIConstraint constraint) {
        m_constraints[nodeId].push_back(std::move(constraint));
    }

    /// @brief Remove all constraints for a node.
    void clearConstraints(const std::string& nodeId) {
        m_constraints.erase(nodeId);
    }

    /// @brief Remove all constraints.
    void clearAll() noexcept {
        m_constraints.clear();
        m_defaultSizes.clear();
    }

    /// @brief Set a default (intrinsic) size for a node.
    void setDefaultSize(const std::string& nodeId, float w, float h) {
        m_defaultSizes.insert_or_assign(nodeId, std::make_pair(w, h));
    }

    /**
     * @brief Solve all constraints and return bounds per node.
     *
     * @param parentBounds The parent rectangle ("parent" target resolves to this).
     * @return Map from node ID to resolved screen-space rectangle.
     */
    [[nodiscard]] std::map<std::string, sgc::Rectf> solve(
        const sgc::Rectf& parentBounds) const {

        // Initialize solved bounds for all constrained nodes.
        detail::SolvedBounds parentSolved{
            parentBounds.x(), parentBounds.y(),
            parentBounds.width(), parentBounds.height()
        };

        std::map<std::string, detail::SolvedBounds> solved;
        for (const auto& [nodeId, _] : m_constraints) {
            detail::SolvedBounds sb;
            sb.left = parentBounds.x();
            sb.top  = parentBounds.y();

            auto defIt = m_defaultSizes.find(nodeId);
            if (defIt != m_defaultSizes.end()) {
                sb.width  = defIt->second.first;
                sb.height = defIt->second.second;
            }
            solved.insert_or_assign(nodeId, sb);
        }

        // Iterative solver.
        for (int iter = 0; iter < kMaxIterations; ++iter) {
            bool changed = false;

            for (const auto& [nodeId, constraints] : m_constraints) {
                auto& node = solved[nodeId];

                for (const auto& c : constraints) {
                    const auto& target = resolveTarget(c.target, solved, parentSolved);
                    changed |= applyConstraint(node, target, c);
                }
            }

            if (!changed) break;
        }

        // Convert to output format.
        std::map<std::string, sgc::Rectf> results;
        for (const auto& [nodeId, sb] : solved) {
            results.insert_or_assign(nodeId,
                sgc::Rectf{sb.left, sb.top, sb.width, sb.height});
        }

        return results;
    }

private:
    // -------------------------------------------------------------------
    // Target resolution
    // -------------------------------------------------------------------

    [[nodiscard]] static const detail::SolvedBounds& resolveTarget(
        const std::string& targetId,
        const std::map<std::string, detail::SolvedBounds>& solved,
        const detail::SolvedBounds& parentSolved) {

        if (targetId == "parent") {
            return parentSolved;
        }
        auto it = solved.find(targetId);
        if (it != solved.end()) {
            return it->second;
        }
        return parentSolved; // Fallback to parent.
    }

    // -------------------------------------------------------------------
    // Constraint application
    // -------------------------------------------------------------------

    /// @return true if any value changed significantly.
    [[nodiscard]] static bool applyConstraint(
        detail::SolvedBounds& node,
        const detail::SolvedBounds& target,
        const UIConstraint& c) {

        constexpr float kEpsilon = 0.01f;
        const float prev[] = {node.left, node.top, node.width, node.height};

        switch (c.type) {
        case ConstraintType::LeftToLeft:
            node.left = target.left + c.offset;
            break;
        case ConstraintType::LeftToRight:
            node.left = target.right() + c.offset;
            break;
        case ConstraintType::RightToRight: {
            const float newRight = target.right() + c.offset;
            node.setRight(newRight);
            break;
        }
        case ConstraintType::RightToLeft: {
            const float newRight = target.left + c.offset;
            node.setRight(newRight);
            break;
        }
        case ConstraintType::TopToTop:
            node.top = target.top + c.offset;
            break;
        case ConstraintType::TopToBottom:
            node.top = target.bottom() + c.offset;
            break;
        case ConstraintType::BottomToBottom: {
            const float newBottom = target.bottom() + c.offset;
            node.setBottom(newBottom);
            break;
        }
        case ConstraintType::BottomToTop: {
            const float newBottom = target.top + c.offset;
            node.setBottom(newBottom);
            break;
        }
        case ConstraintType::CenterX:
            node.setCenterX(target.centerX() + c.offset);
            break;
        case ConstraintType::CenterY:
            node.setCenterY(target.centerY() + c.offset);
            break;
        case ConstraintType::Width:
            node.width = std::max(0.0f, target.width * c.multiplier + c.offset);
            break;
        case ConstraintType::Height:
            node.height = std::max(0.0f, target.height * c.multiplier + c.offset);
            break;
        case ConstraintType::AspectRatio:
            node.width = node.height * c.multiplier;
            break;
        }

        return (std::abs(node.left   - prev[0]) > kEpsilon ||
                std::abs(node.top    - prev[1]) > kEpsilon ||
                std::abs(node.width  - prev[2]) > kEpsilon ||
                std::abs(node.height - prev[3]) > kEpsilon);
    }
};

} // namespace mitiru::ui
