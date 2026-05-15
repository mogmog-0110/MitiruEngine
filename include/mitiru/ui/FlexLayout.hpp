#pragma once

/**
 * @file FlexLayout.hpp
 * @brief Full CSS Flexbox layout engine for MitiruEngine UI.
 */

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace mitiru::ui {

/// @brief Main axis direction.
enum class FlexDirection : uint8_t {
    Row,
    RowReverse,
    Column,
    ColumnReverse
};

enum class FlexWrap : uint8_t {
    NoWrap,
    Wrap,
    WrapReverse
};

enum class FlexJustify : uint8_t {
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class FlexAlignItems : uint8_t {
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
    Baseline
};

enum class FlexAlignContent : uint8_t {
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
    Stretch
};

enum class FlexAlignSelf : uint8_t {
    Auto,
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
    Baseline
};

/// @brief Container-level flex configuration.
struct FlexConfig {
    FlexDirection   direction    = FlexDirection::Row;
    FlexWrap        wrap         = FlexWrap::NoWrap;
    FlexJustify     justifyContent = FlexJustify::FlexStart;
    FlexAlignItems  alignItems   = FlexAlignItems::Stretch;
    FlexAlignContent alignContent = FlexAlignContent::Stretch;
    float           gap          = 0.0f;
};

/// @brief Per-child flex item configuration.
struct FlexItemConfig {
    float          flexGrow   = 0.0f;
    float          flexShrink = 1.0f;
    float          flexBasis  = -1.0f; ///< -1 = use preferred size.
    FlexAlignSelf  alignSelf  = FlexAlignSelf::Auto;
    int            order      = 0;
    float preferredWidth  = 0.0f;
    float preferredHeight = 0.0f;
};

/// @brief Computes child bounds using a full CSS Flexbox algorithm.
class FlexLayout {
public:
    /// @brief Compute child bounds within the parent rectangle.
    [[nodiscard]] std::vector<sgc::Rectf> compute(
        const sgc::Rectf& parentBounds,
        const FlexConfig& config,
        const std::vector<FlexItemConfig>& items) const {

        if (items.empty()) return {};

        const bool isRow    = (config.direction == FlexDirection::Row ||
                               config.direction == FlexDirection::RowReverse);
        const bool reversed = (config.direction == FlexDirection::RowReverse ||
                               config.direction == FlexDirection::ColumnReverse);

        const float mainSize  = isRow ? parentBounds.width()  : parentBounds.height();
        const float crossSize = isRow ? parentBounds.height() : parentBounds.width();

        // Build sorted index list by `order`.
        std::vector<size_t> sorted(items.size());
        std::iota(sorted.begin(), sorted.end(), 0u);
        std::stable_sort(sorted.begin(), sorted.end(),
            [&](size_t a, size_t b) { return items[a].order < items[b].order; });

        std::vector<float> bases(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            bases[i] = (items[i].flexBasis >= 0.0f) ? items[i].flexBasis
                : (isRow ? items[i].preferredWidth : items[i].preferredHeight);
        }

        struct FlexLine {
            std::vector<size_t> indices;
            float totalBasis = 0.0f, maxCross = 0.0f, resolvedCross = 0.0f;
        };

        std::vector<FlexLine> lines;
        {
            FlexLine currentLine;
            float lineMain = 0.0f;

            for (size_t si = 0; si < sorted.size(); ++si) {
                const size_t idx = sorted[si];
                const float basis = bases[idx];
                const float gapBefore = currentLine.indices.empty() ? 0.0f : config.gap;

                const bool canWrap = (config.wrap != FlexWrap::NoWrap);
                if (canWrap && !currentLine.indices.empty() &&
                    lineMain + gapBefore + basis > mainSize) {
                    lines.push_back(std::move(currentLine));
                    currentLine = FlexLine{};
                    lineMain = 0.0f;
                }

                if (!currentLine.indices.empty()) {
                    lineMain += config.gap;
                }
                currentLine.indices.push_back(idx);
                currentLine.totalBasis += basis;
                lineMain += basis;

                const float itemCross = isRow
                    ? items[idx].preferredHeight
                    : items[idx].preferredWidth;
                currentLine.maxCross = std::max(currentLine.maxCross, itemCross);
            }
            if (!currentLine.indices.empty()) {
                lines.push_back(std::move(currentLine));
            }
        }

        // Reverse line order for WrapReverse.
        if (config.wrap == FlexWrap::WrapReverse) {
            std::reverse(lines.begin(), lines.end());
        }

        resolveCrossSizes(lines, crossSize, config.gap, config.alignContent);
        auto lineStarts = distributeLines(lines, crossSize, config.gap, config.alignContent);
        std::vector<sgc::Rectf> results(items.size());

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            const float lineCross = line.resolvedCross;
            const float lineCrossStart = lineStarts[li];

            // Flex grow / shrink.
            const float totalGaps = (line.indices.size() > 1)
                ? config.gap * static_cast<float>(line.indices.size() - 1)
                : 0.0f;
            const float freeSpace = mainSize - line.totalBasis - totalGaps;

            float totalGrow   = 0.0f;
            float totalShrink = 0.0f;
            for (size_t idx : line.indices) {
                totalGrow   += items[idx].flexGrow;
                totalShrink += items[idx].flexShrink * bases[idx];
            }

            std::vector<float> mainSizes(line.indices.size());
            for (size_t i = 0; i < line.indices.size(); ++i) {
                const size_t idx = line.indices[i];
                float size = bases[idx];
                if (freeSpace > 0.0f && totalGrow > 0.0f) {
                    size += freeSpace * (items[idx].flexGrow / totalGrow);
                } else if (freeSpace < 0.0f && totalShrink > 0.0f) {
                    const float shrinkRatio = (items[idx].flexShrink * bases[idx]) / totalShrink;
                    size += freeSpace * shrinkRatio;
                }
                mainSizes[i] = std::max(0.0f, size);
            }

            // Main-axis positioning (justify-content).
            float totalMain = 0.0f;
            for (float s : mainSizes) totalMain += s;
            const float remainingMain = mainSize - totalMain - totalGaps;

            std::vector<float> mainPositions(line.indices.size());
            distributeMainAxis(mainPositions, mainSizes, remainingMain,
                               config.gap, config.justifyContent, reversed);

            // Place each item.
            for (size_t i = 0; i < line.indices.size(); ++i) {
                const size_t idx = line.indices[i];
                const float itemMain = mainPositions[i];
                const float itemMainSize = mainSizes[i];

                // Cross-axis alignment.
                FlexAlignItems align = config.alignItems;
                if (items[idx].alignSelf != FlexAlignSelf::Auto) {
                    align = static_cast<FlexAlignItems>(
                        static_cast<uint8_t>(items[idx].alignSelf) - 1);
                }

                const float itemPrefCross = isRow
                    ? items[idx].preferredHeight
                    : items[idx].preferredWidth;
                float itemCross = lineCrossStart;
                float itemCrossSize = (align == FlexAlignItems::Stretch)
                    ? lineCross
                    : std::max(itemPrefCross, 0.0f);

                switch (align) {
                case FlexAlignItems::FlexStart:
                case FlexAlignItems::Baseline:
                    itemCross = lineCrossStart;
                    break;
                case FlexAlignItems::FlexEnd:
                    itemCross = lineCrossStart + lineCross - itemCrossSize;
                    break;
                case FlexAlignItems::Center:
                    itemCross = lineCrossStart + (lineCross - itemCrossSize) * 0.5f;
                    break;
                case FlexAlignItems::Stretch:
                    itemCross = lineCrossStart;
                    break;
                }

                if (isRow) {
                    results[idx] = sgc::Rectf{
                        parentBounds.x() + itemMain,
                        parentBounds.y() + itemCross,
                        itemMainSize,
                        itemCrossSize
                    };
                } else {
                    results[idx] = sgc::Rectf{
                        parentBounds.x() + itemCross,
                        parentBounds.y() + itemMain,
                        itemCrossSize,
                        itemMainSize
                    };
                }
            }
        }

        return results;
    }

private:
    static void resolveCrossSizes(std::vector<struct FlexLine>& lines,
                                  float totalCross, float gap,
                                  FlexAlignContent alignContent) {
        const float totalGap = (lines.size() > 1)
            ? gap * static_cast<float>(lines.size() - 1) : 0.0f;
        const float available = totalCross - totalGap;

        if (alignContent == FlexAlignContent::Stretch) {
            float totalMaxCross = 0.0f;
            for (const auto& line : lines) totalMaxCross += line.maxCross;

            const float remaining = std::max(0.0f, available - totalMaxCross);
            const float extra = lines.empty() ? 0.0f : remaining / static_cast<float>(lines.size());
            for (auto& line : lines) {
                line.resolvedCross = line.maxCross + extra;
            }
        } else {
            for (auto& line : lines) {
                line.resolvedCross = line.maxCross;
            }
        }
    }

    [[nodiscard]] static std::vector<float> distributeLines(
        const std::vector<struct FlexLine>& lines,
        float totalCross, float gap,
        FlexAlignContent alignContent) {

        const size_t n = lines.size();
        std::vector<float> starts(n);
        if (n == 0) return starts;

        float totalUsed = 0.0f;
        for (const auto& line : lines) totalUsed += line.resolvedCross;
        const float totalGap = (n > 1) ? gap * static_cast<float>(n - 1) : 0.0f;
        const float freeSpace = totalCross - totalUsed - totalGap;

        float cursor = 0.0f;
        float extraGap = 0.0f;

        switch (alignContent) {
        case FlexAlignContent::FlexStart:
        case FlexAlignContent::Stretch:
            cursor = 0.0f;
            break;
        case FlexAlignContent::FlexEnd:
            cursor = freeSpace;
            break;
        case FlexAlignContent::Center:
            cursor = freeSpace * 0.5f;
            break;
        case FlexAlignContent::SpaceBetween:
            cursor = 0.0f;
            extraGap = (n > 1) ? freeSpace / static_cast<float>(n - 1) : 0.0f;
            break;
        case FlexAlignContent::SpaceAround:
            extraGap = freeSpace / static_cast<float>(n);
            cursor = extraGap * 0.5f;
            break;
        case FlexAlignContent::SpaceEvenly:
            extraGap = freeSpace / static_cast<float>(n + 1);
            cursor = extraGap;
            break;
        }

        for (size_t i = 0; i < n; ++i) {
            starts[i] = cursor;
            cursor += lines[i].resolvedCross + gap + extraGap;
        }

        return starts;
    }

    static void distributeMainAxis(std::vector<float>& positions,
                                   const std::vector<float>& sizes,
                                   float remainingSpace,
                                   float gap,
                                   FlexJustify justify,
                                   bool reversed) {
        const size_t n = sizes.size();
        if (n == 0) return;

        float cursor = 0.0f;
        float extraGap = 0.0f;

        if (remainingSpace > 0.0f) {
            switch (justify) {
            case FlexJustify::FlexStart:
                cursor = reversed ? remainingSpace : 0.0f;
                break;
            case FlexJustify::FlexEnd:
                cursor = reversed ? 0.0f : remainingSpace;
                break;
            case FlexJustify::Center:
                cursor = remainingSpace * 0.5f;
                break;
            case FlexJustify::SpaceBetween:
                cursor = 0.0f;
                extraGap = (n > 1) ? remainingSpace / static_cast<float>(n - 1) : 0.0f;
                break;
            case FlexJustify::SpaceAround:
                extraGap = remainingSpace / static_cast<float>(n);
                cursor = extraGap * 0.5f;
                break;
            case FlexJustify::SpaceEvenly:
                extraGap = remainingSpace / static_cast<float>(n + 1);
                cursor = extraGap;
                break;
            }
        }

        if (reversed) {
            // Place items from end to start.
            float pos = cursor;
            for (size_t i = 0; i < n; ++i) {
                const size_t ri = n - 1 - i;
                positions[ri] = pos;
                pos += sizes[ri] + gap + extraGap;
            }
        } else {
            float pos = cursor;
            for (size_t i = 0; i < n; ++i) {
                positions[i] = pos;
                pos += sizes[i] + gap + extraGap;
            }
        }
    }
};

} // namespace mitiru::ui
