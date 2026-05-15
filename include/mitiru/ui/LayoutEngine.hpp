#pragma once

/**
 * @file LayoutEngine.hpp
 * @brief Flexbox-like declarative layout engine for MitiruEngine UI.
 *
 * Provides anchor-based positioning, directional child layout with flex grow,
 * and recursive tree layout computation. All sizes are in screen-space pixels.
 */

#include <sgc/math/Rect.hpp>
#include <mitiru/ui/UINode.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mitiru::ui {

/**
 * @brief Anchor point within the parent or screen area.
 *
 * Determines the reference corner / edge used when positioning an element
 * inside its available space.
 */
enum class Anchor : uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

/**
 * @brief Primary axis along which children are laid out.
 */
enum class LayoutDirection : uint8_t {
    Horizontal, ///< Left-to-right flow.
    Vertical    ///< Top-to-bottom flow.
};

/**
 * @brief How a dimension (width or height) is determined.
 */
enum class SizeMode : uint8_t {
    Fixed,   ///< Exact pixel size specified by the user.
    Fill,    ///< Expand to fill all remaining space in the parent.
    Wrap,    ///< Shrink to fit the content.
    Percent  ///< Percentage (0-100) of the parent dimension.
};

/**
 * @brief Four-sided spacing value used for margins and padding.
 */
struct Margin {
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
    float left   = 0.0f;

    /** @brief Create uniform margin on all sides. */
    static Margin all(float v) noexcept { return {v, v, v, v}; }

    /** @brief Create symmetric margin (horizontal, vertical). */
    static Margin symmetric(float h, float v) noexcept { return {v, h, v, h}; }
};

/**
 * @brief Full set of layout parameters for a single UI node.
 *
 * Combines sizing mode, min/max bounds, anchor, margin/padding, flex
 * factor, and child-layout direction into one descriptor.
 */
struct LayoutConstraints {
    SizeMode widthMode  = SizeMode::Wrap;
    SizeMode heightMode = SizeMode::Wrap;
    float width         = 0.0f;       ///< Used when widthMode is Fixed or Percent.
    float height        = 0.0f;       ///< Used when heightMode is Fixed or Percent.
    float minWidth      = 0.0f;
    float minHeight     = 0.0f;
    float maxWidth      = 99999.0f;
    float maxHeight     = 99999.0f;
    Anchor anchor       = Anchor::TopLeft;
    Margin margin{};
    Margin padding{};
    float spacing             = 0.0f; ///< Gap between children.
    LayoutDirection direction = LayoutDirection::Vertical;
    float flex                = 0.0f; ///< Flex-grow factor (0 = no flex).
};

/**
 * @brief Computed layout output for a single node.
 */
struct LayoutResult {
    sgc::Rectf bounds;   ///< Computed bounds in screen space.
    std::string nodeId;  ///< Maps back to the originating UINode.
};

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------
namespace detail {

/**
 * @brief Clamp @p value between @p lo and @p hi.
 */
inline float clampf(float value, float lo, float hi) noexcept {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/**
 * @brief Resolve a single dimension from its SizeMode.
 *
 * @param mode       The sizing mode.
 * @param specified  The user-specified value (pixels or percent).
 * @param parentDim  The parent's dimension along the same axis.
 * @param contentDim Measured content size (used for Wrap).
 * @param minDim     Minimum allowed size.
 * @param maxDim     Maximum allowed size.
 * @return Resolved size in pixels, clamped to [minDim, maxDim].
 */
inline float resolveDimension(SizeMode mode,
                              float specified,
                              float parentDim,
                              float contentDim,
                              float minDim,
                              float maxDim) noexcept {
    float raw = 0.0f;
    switch (mode) {
    case SizeMode::Fixed:   raw = specified;                       break;
    case SizeMode::Fill:    raw = parentDim;                       break;
    case SizeMode::Wrap:    raw = contentDim;                      break;
    case SizeMode::Percent: raw = parentDim * specified / 100.0f;  break;
    }
    return clampf(raw, minDim, maxDim);
}

} // namespace detail

// -----------------------------------------------------------------------
// LayoutEngine
// -----------------------------------------------------------------------

/**
 * @class LayoutEngine
 * @brief Computes screen-space positions and sizes for a UI node tree.
 *
 * Usage:
 * @code
 *   LayoutEngine engine(1920.0f, 1080.0f);
 *
 *   // Single anchored element
 *   LayoutConstraints c;
 *   c.anchor = Anchor::Center;
 *   auto rect = engine.computeAnchored(c, 200.0f, 50.0f);
 *
 *   // Full tree layout
 *   std::map<UINodeId, LayoutConstraints> constraints;
 *   engine.layoutTree(root, constraints);
 * @endcode
 */
class LayoutEngine {
    float m_screenW;
    float m_screenH;

public:
    /**
     * @brief Construct with initial screen dimensions.
     * @param screenW  Viewport width in pixels.
     * @param screenH  Viewport height in pixels.
     */
    LayoutEngine(float screenW, float screenH) noexcept
        : m_screenW(screenW), m_screenH(screenH) {}

    /**
     * @brief Update the viewport size (e.g. on window resize).
     */
    void setScreenSize(float w, float h) noexcept {
        m_screenW = w;
        m_screenH = h;
    }

    /** @brief Current viewport width. */
    float screenWidth()  const noexcept { return m_screenW; }
    /** @brief Current viewport height. */
    float screenHeight() const noexcept { return m_screenH; }

    // -------------------------------------------------------------------
    // computeAnchored
    // -------------------------------------------------------------------

    /**
     * @brief Position a single element inside the screen using its anchor.
     *
     * The element's final width/height are given by @p contentW / @p contentH
     * (already resolved). This function only computes the (x, y) origin
     * based on the anchor point and margins.
     *
     * @param constraints Layout constraints (anchor and margin are used).
     * @param contentW    Resolved width of the element.
     * @param contentH    Resolved height of the element.
     * @return Screen-space rectangle.
     */
    sgc::Rectf computeAnchored(const LayoutConstraints& constraints,
                                float contentW,
                                float contentH) const noexcept {
        return computeAnchoredInRect(
            sgc::Rectf{0.0f, 0.0f, m_screenW, m_screenH},
            constraints, contentW, contentH);
    }

    // -------------------------------------------------------------------
    // layoutChildren
    // -------------------------------------------------------------------

    /**
     * @brief Distribute child elements along a direction inside a parent rect.
     *
     * Children are placed sequentially along @p direction. Fixed-size and
     * wrap children are laid out first; remaining space is divided among
     * children whose flex factor is greater than zero. Margins and spacing
     * are respected.
     *
     * @param parentBounds      Available area for children.
     * @param childConstraints  One LayoutConstraints per child, in order.
     * @param direction         Primary layout axis.
     * @param spacing           Extra gap between consecutive children.
     * @return One Rectf per child, in the same order as @p childConstraints.
     */
    std::vector<sgc::Rectf> layoutChildren(
        const sgc::Rectf& parentBounds,
        const std::vector<LayoutConstraints>& childConstraints,
        LayoutDirection direction,
        float spacing = 0.0f) const noexcept {

        const bool horizontal = (direction == LayoutDirection::Horizontal);
        const float parentMain  = horizontal ? parentBounds.width()  : parentBounds.height();
        const float parentCross = horizontal ? parentBounds.height() : parentBounds.width();
        const size_t count = childConstraints.size();

        // --- First pass: resolve non-flex sizes, accumulate flex total ---
        struct ChildInfo {
            float mainSize  = 0.0f;
            float crossSize = 0.0f;
            float flex      = 0.0f;
            float marginMainBefore  = 0.0f;
            float marginMainAfter   = 0.0f;
            float marginCrossBefore = 0.0f;
            float marginCrossAfter  = 0.0f;
        };
        std::vector<ChildInfo> infos(count);

        float totalFixed = 0.0f;
        float totalFlex  = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            const auto& c = childConstraints[i];
            auto& info = infos[i];

            if (horizontal) {
                info.marginMainBefore  = c.margin.left;
                info.marginMainAfter   = c.margin.right;
                info.marginCrossBefore = c.margin.top;
                info.marginCrossAfter  = c.margin.bottom;
            } else {
                info.marginMainBefore  = c.margin.top;
                info.marginMainAfter   = c.margin.bottom;
                info.marginCrossBefore = c.margin.left;
                info.marginCrossAfter  = c.margin.right;
            }

            // Cross-axis size
            const SizeMode crossMode = horizontal ? c.heightMode : c.widthMode;
            const float crossSpec    = horizontal ? c.height     : c.width;
            const float crossMin     = horizontal ? c.minHeight  : c.minWidth;
            const float crossMax     = horizontal ? c.maxHeight  : c.maxWidth;
            const float availCross   = parentCross - info.marginCrossBefore - info.marginCrossAfter;
            info.crossSize = detail::resolveDimension(crossMode, crossSpec, availCross, 0.0f, crossMin, crossMax);

            // Main-axis size
            const SizeMode mainMode = horizontal ? c.widthMode : c.heightMode;
            const float mainSpec    = horizontal ? c.width     : c.height;
            const float mainMin     = horizontal ? c.minWidth  : c.minHeight;
            const float mainMax     = horizontal ? c.maxWidth  : c.maxHeight;

            info.flex = c.flex;
            if (c.flex > 0.0f) {
                // Will be resolved in second pass; use min for accounting.
                info.mainSize = mainMin;
                totalFlex += c.flex;
            } else {
                info.mainSize = detail::resolveDimension(mainMode, mainSpec, parentMain, 0.0f, mainMin, mainMax);
            }
            totalFixed += info.mainSize + info.marginMainBefore + info.marginMainAfter;
        }

        // Spacing between children
        const float totalSpacing = (count > 1) ? spacing * static_cast<float>(count - 1) : 0.0f;
        const float remaining    = parentMain - totalFixed - totalSpacing;

        // --- Second pass: distribute remaining space to flex children ---
        if (totalFlex > 0.0f && remaining > 0.0f) {
            for (size_t i = 0; i < count; ++i) {
                if (infos[i].flex > 0.0f) {
                    const auto& c = childConstraints[i];
                    const float mainMin = horizontal ? c.minWidth : c.minHeight;
                    const float mainMax = horizontal ? c.maxWidth : c.maxHeight;
                    const float share   = remaining * (infos[i].flex / totalFlex);
                    infos[i].mainSize = detail::clampf(infos[i].mainSize + share, mainMin, mainMax);
                }
            }
        }

        // --- Third pass: position each child ---
        std::vector<sgc::Rectf> results(count);
        float cursor = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            const auto& info = infos[i];
            cursor += info.marginMainBefore;

            const float mainPos  = cursor;
            const float crossPos = info.marginCrossBefore;

            if (horizontal) {
                results[i] = sgc::Rectf{
                    parentBounds.x() + mainPos,
                    parentBounds.y() + crossPos,
                    info.mainSize,
                    info.crossSize
                };
            } else {
                results[i] = sgc::Rectf{
                    parentBounds.x() + crossPos,
                    parentBounds.y() + mainPos,
                    info.crossSize,
                    info.mainSize
                };
            }

            cursor += info.mainSize + info.marginMainAfter + spacing;
        }

        return results;
    }

    // -------------------------------------------------------------------
    // layoutTree
    // -------------------------------------------------------------------

    /**
     * @brief Recursively compute bounds for every node in the tree.
     *
     * Each node looks up its LayoutConstraints in @p constraints. If a node
     * has no entry, a default LayoutConstraints (Wrap, TopLeft) is used.
     * Computed bounds are written directly into each node's data via
     * @c UINode::setPosition / @c UINode::setSize (or stored externally
     * depending on UINode API).
     *
     * @param root        Root of the UI tree.
     * @param constraints Per-node layout parameters keyed by UINodeId.
     */
    void layoutTree(UINode& root,
                    const std::map<UINodeId, LayoutConstraints>& constraints) const {
        const sgc::Rectf screenRect{0.0f, 0.0f, m_screenW, m_screenH};
        layoutNodeRecursive(root, screenRect, constraints);
    }

private:
    // -------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------

    /**
     * @brief Position an element inside an arbitrary parent rectangle.
     */
    sgc::Rectf computeAnchoredInRect(const sgc::Rectf& parent,
                                      const LayoutConstraints& constraints,
                                      float contentW,
                                      float contentH) const noexcept {
        float x = parent.x();
        float y = parent.y();

        switch (constraints.anchor) {
        case Anchor::TopLeft:
            x = parent.x() + constraints.margin.left;
            y = parent.y() + constraints.margin.top;
            break;
        case Anchor::TopCenter:
            x = parent.x() + (parent.width() - contentW) * 0.5f;
            y = parent.y() + constraints.margin.top;
            break;
        case Anchor::TopRight:
            x = parent.x() + parent.width() - contentW - constraints.margin.right;
            y = parent.y() + constraints.margin.top;
            break;
        case Anchor::CenterLeft:
            x = parent.x() + constraints.margin.left;
            y = parent.y() + (parent.height() - contentH) * 0.5f;
            break;
        case Anchor::Center:
            x = parent.x() + (parent.width() - contentW) * 0.5f;
            y = parent.y() + (parent.height() - contentH) * 0.5f;
            break;
        case Anchor::CenterRight:
            x = parent.x() + parent.width() - contentW - constraints.margin.right;
            y = parent.y() + (parent.height() - contentH) * 0.5f;
            break;
        case Anchor::BottomLeft:
            x = parent.x() + constraints.margin.left;
            y = parent.y() + parent.height() - contentH - constraints.margin.bottom;
            break;
        case Anchor::BottomCenter:
            x = parent.x() + (parent.width() - contentW) * 0.5f;
            y = parent.y() + parent.height() - contentH - constraints.margin.bottom;
            break;
        case Anchor::BottomRight:
            x = parent.x() + parent.width() - contentW - constraints.margin.right;
            y = parent.y() + parent.height() - contentH - constraints.margin.bottom;
            break;
        }

        return sgc::Rectf{x, y, contentW, contentH};
    }

    /**
     * @brief Resolve the size of a node given its constraints and parent bounds.
     */
    sgc::Rectf resolveNodeBounds(const sgc::Rectf& parentBounds,
                                  const LayoutConstraints& lc) const noexcept {
        const float availW = parentBounds.width()  - lc.margin.left - lc.margin.right;
        const float availH = parentBounds.height() - lc.margin.top  - lc.margin.bottom;

        const float w = detail::resolveDimension(lc.widthMode,  lc.width,  availW, 0.0f, lc.minWidth,  lc.maxWidth);
        const float h = detail::resolveDimension(lc.heightMode, lc.height, availH, 0.0f, lc.minHeight, lc.maxHeight);

        return computeAnchoredInRect(parentBounds, lc, w, h);
    }

    /**
     * @brief Depth-first recursive layout pass.
     */
    void layoutNodeRecursive(
        UINode& node,
        const sgc::Rectf& parentBounds,
        const std::map<UINodeId, LayoutConstraints>& constraintMap) const {

        // Look up constraints (default if absent).
        LayoutConstraints lc;
        {
            auto it = constraintMap.find(node.id());
            if (it != constraintMap.end()) {
                lc = it->second;
            }
        }

        // Resolve this node's bounds.
        const sgc::Rectf nodeBounds = resolveNodeBounds(parentBounds, lc);
        node.setBounds(nodeBounds);

        // Content area after padding.
        const sgc::Rectf contentArea{
            nodeBounds.x() + lc.padding.left,
            nodeBounds.y() + lc.padding.top,
            nodeBounds.width()  - lc.padding.left - lc.padding.right,
            nodeBounds.height() - lc.padding.top  - lc.padding.bottom
        };

        // Collect children constraints.
        auto& children = node.children();
        if (children.empty()) {
            return;
        }

        std::vector<LayoutConstraints> childLCs;
        childLCs.reserve(children.size());
        for (auto& child : children) {
            auto it = constraintMap.find(child->id());
            childLCs.push_back(it != constraintMap.end() ? it->second : LayoutConstraints{});
        }

        // Layout children within content area.
        const auto rects = layoutChildren(contentArea, childLCs, lc.direction, lc.spacing);

        // Recurse into each child.
        for (size_t i = 0; i < children.size(); ++i) {
            layoutNodeRecursive(*children[i], rects[i], constraintMap);
        }
    }
};

} // namespace mitiru::ui
