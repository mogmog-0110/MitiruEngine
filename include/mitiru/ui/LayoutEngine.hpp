#pragma once

/**
 * @file LayoutEngine.hpp
 * @brief MitiruEngine UI 用の Flexbox 風 宣言的 layout engine。
 *
 * anchor ベースの位置決め、flex grow 付きの方向性子要素 layout、
 * 再帰的な tree layout 計算を提供する。サイズは全て screen-space の px。
 */

#include <sgc/math/Rect.hpp>
#include <mitiru/ui/UINode.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mitiru::ui {

/**
 * @brief 親または screen 領域内の anchor 点。
 *
 * 要素を利用可能空間内に配置する際の基準となる角 / 辺を決める。
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
 * @brief 子要素を並べる主軸 (primary axis)。
 */
enum class LayoutDirection : uint8_t {
    Horizontal, ///< 左から右への flow。
    Vertical    ///< 上から下への flow。
};

/**
 * @brief 寸法 (幅または高さ) の決定方法。
 */
enum class SizeMode : uint8_t {
    Fixed,   ///< user 指定の正確な px サイズ。
    Fill,    ///< 親の残り空間を全て埋めるよう拡張。
    Wrap,    ///< content に合わせて縮小。
    Percent  ///< 親寸法に対する割合 (0-100)。
};

/**
 * @brief margin / padding に使う四辺の spacing 値。
 */
struct Margin {
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
    float left   = 0.0f;

    /** @brief 全辺均一の margin を生成する。 */
    static Margin all(float v) noexcept { return {v, v, v, v}; }

    /** @brief 対称な margin を生成する (horizontal, vertical)。 */
    static Margin symmetric(float h, float v) noexcept { return {v, h, v, h}; }
};

/**
 * @brief 単一 UI node の layout パラメータ一式。
 *
 * sizing mode、min/max bounds、anchor、margin/padding、flex factor、
 * 子要素 layout direction を 1 つの descriptor にまとめたもの。
 */
struct LayoutConstraints {
    SizeMode widthMode  = SizeMode::Wrap;
    SizeMode heightMode = SizeMode::Wrap;
    float width         = 0.0f;       ///< widthMode が Fixed または Percent の時に使用。
    float height        = 0.0f;       ///< heightMode が Fixed または Percent の時に使用。
    float minWidth      = 0.0f;
    float minHeight     = 0.0f;
    float maxWidth      = 99999.0f;
    float maxHeight     = 99999.0f;
    Anchor anchor       = Anchor::TopLeft;
    Margin margin{};
    Margin padding{};
    float spacing             = 0.0f; ///< 子要素間の gap。
    LayoutDirection direction = LayoutDirection::Vertical;
    float flex                = 0.0f; ///< flex-grow factor (0 = flex 無し)。
};

/**
 * @brief 単一 node の計算済み layout 出力。
 */
struct LayoutResult {
    sgc::Rectf bounds;   ///< screen space 上の計算済み bounds。
    std::string nodeId;  ///< 元の UINode への対応付け。
};

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------
namespace detail {

/**
 * @brief @p value を @p lo と @p hi の間に clamp する。
 */
inline float clampf(float value, float lo, float hi) noexcept {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/**
 * @brief SizeMode から単一の寸法を解決する。
 *
 * @param mode       sizing mode。
 * @param specified  user 指定値 (px または percent)。
 * @param parentDim  同軸方向の親の寸法。
 * @param contentDim 計測した content サイズ (Wrap 用)。
 * @param minDim     許容最小サイズ。
 * @param maxDim     許容最大サイズ。
 * @return [minDim, maxDim] に clamp した解決済みサイズ (px)。
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
 * @brief UI node tree の screen-space 位置とサイズを計算する。
 *
 * 使用例:
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
     * @brief 初期 screen 寸法を与えて構築する。
     * @param screenW  viewport 幅 (px)。
     * @param screenH  viewport 高さ (px)。
     */
    LayoutEngine(float screenW, float screenH) noexcept
        : m_screenW(screenW), m_screenH(screenH) {}

    /**
     * @brief viewport サイズを更新する (window resize 時など)。
     */
    void setScreenSize(float w, float h) noexcept {
        m_screenW = w;
        m_screenH = h;
    }

    /** @brief 現在の viewport 幅。 */
    float screenWidth()  const noexcept { return m_screenW; }
    /** @brief 現在の viewport 高さ。 */
    float screenHeight() const noexcept { return m_screenH; }

    // -------------------------------------------------------------------
    // computeAnchored
    // -------------------------------------------------------------------

    /**
     * @brief 単一要素を anchor に従って screen 内に配置する。
     *
     * 要素の最終的な幅/高さは @p contentW / @p contentH で与えられる
     * (解決済み)。本関数は anchor 点と margin に基づき (x, y) 原点のみを
     * 計算する。
     *
     * @param constraints layout 制約 (anchor と margin を使用)。
     * @param contentW    要素の解決済み幅。
     * @param contentH    要素の解決済み高さ。
     * @return screen-space の矩形。
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
     * @brief 親矩形内で子要素を方向に沿って配置する。
     *
     * 子要素は @p direction に沿って順番に配置される。Fixed サイズと
     * wrap の子要素を先に配置し、残り空間を flex factor が 0 より大きい
     * 子要素に分配する。margin と spacing は尊重される。
     *
     * @param parentBounds      子要素が使える領域。
     * @param childConstraints  子要素ごとの LayoutConstraints (順序通り)。
     * @param direction         主軸 (primary layout axis)。
     * @param spacing           連続する子要素間の追加 gap。
     * @return 子要素ごとの Rectf (@p childConstraints と同順)。
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

        // --- 第 1 パス: 非 flex サイズを解決し、flex 合計を集計 ---
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

            // 交差軸 (cross-axis) のサイズ
            const SizeMode crossMode = horizontal ? c.heightMode : c.widthMode;
            const float crossSpec    = horizontal ? c.height     : c.width;
            const float crossMin     = horizontal ? c.minHeight  : c.minWidth;
            const float crossMax     = horizontal ? c.maxHeight  : c.maxWidth;
            const float availCross   = parentCross - info.marginCrossBefore - info.marginCrossAfter;
            info.crossSize = detail::resolveDimension(crossMode, crossSpec, availCross, 0.0f, crossMin, crossMax);

            // 主軸 (main-axis) のサイズ
            const SizeMode mainMode = horizontal ? c.widthMode : c.heightMode;
            const float mainSpec    = horizontal ? c.width     : c.height;
            const float mainMin     = horizontal ? c.minWidth  : c.minHeight;
            const float mainMax     = horizontal ? c.maxWidth  : c.maxHeight;

            info.flex = c.flex;
            if (c.flex > 0.0f) {
                // 第 2 パスで解決する。集計用には min を使う。
                info.mainSize = mainMin;
                totalFlex += c.flex;
            } else {
                info.mainSize = detail::resolveDimension(mainMode, mainSpec, parentMain, 0.0f, mainMin, mainMax);
            }
            totalFixed += info.mainSize + info.marginMainBefore + info.marginMainAfter;
        }

        // 子要素間の spacing
        const float totalSpacing = (count > 1) ? spacing * static_cast<float>(count - 1) : 0.0f;
        const float remaining    = parentMain - totalFixed - totalSpacing;

        // --- 第 2 パス: 残り空間を flex の子要素へ分配 ---
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

        // --- 第 3 パス: 各子要素を配置 ---
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
     * @brief tree 内の全 node の bounds を再帰的に計算する。
     *
     * 各 node は @p constraints から自身の LayoutConstraints を引く。
     * エントリが無い node には default の LayoutConstraints (Wrap, TopLeft)
     * を使う。計算済み bounds は @c UINode::setPosition / @c UINode::setSize
     * 経由で各 node のデータへ直接書き込まれる (UINode API によっては外部
     * 保存)。
     *
     * @param root        UI tree の root。
     * @param constraints UINodeId をキーとした node ごとの layout パラメータ。
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
     * @brief 任意の親矩形内に要素を配置する。
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
     * @brief 制約と親 bounds から node のサイズを解決する。
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
     * @brief 深さ優先の再帰 layout パス。
     */
    void layoutNodeRecursive(
        UINode& node,
        const sgc::Rectf& parentBounds,
        const std::map<UINodeId, LayoutConstraints>& constraintMap) const {

        // 制約を引く (無ければ default)。
        LayoutConstraints lc;
        {
            auto it = constraintMap.find(node.id());
            if (it != constraintMap.end()) {
                lc = it->second;
            }
        }

        // この node の bounds を解決する。
        const sgc::Rectf nodeBounds = resolveNodeBounds(parentBounds, lc);
        node.setBounds(nodeBounds);

        // padding を引いた後の content 領域。
        const sgc::Rectf contentArea{
            nodeBounds.x() + lc.padding.left,
            nodeBounds.y() + lc.padding.top,
            nodeBounds.width()  - lc.padding.left - lc.padding.right,
            nodeBounds.height() - lc.padding.top  - lc.padding.bottom
        };

        // 子要素の制約を収集する。
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

        // content 領域内に子要素を配置する。
        const auto rects = layoutChildren(contentArea, childLCs, lc.direction, lc.spacing);

        // 各子要素へ再帰する。
        for (size_t i = 0; i < children.size(); ++i) {
            layoutNodeRecursive(*children[i], rects[i], constraintMap);
        }
    }
};

} // namespace mitiru::ui
