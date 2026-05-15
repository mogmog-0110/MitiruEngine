#pragma once

/**
 * @file GridLayout.hpp
 * @brief CSS Grid-like layout engine for MitiruEngine UI.
 *
 * Supports fixed, fractional (fr), and auto track sizing, gap control,
 * named areas, auto-placement, and per-item alignment.
 */

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <string>
#include <variant>
#include <vector>

namespace mitiru::ui {

// -----------------------------------------------------------------------
// Track sizing
// -----------------------------------------------------------------------

/// @brief Fixed pixel size for a grid track.
struct TrackFixed {
    float pixels = 0.0f;
};

/// @brief Fractional unit (like CSS `fr`) for a grid track.
struct TrackFraction {
    float value = 1.0f;
};

/// @brief Auto-sized track (uses the largest child in that track).
struct TrackAuto {};

/// @brief A single grid track size definition.
using GridTrackSize = std::variant<TrackFixed, TrackFraction, TrackAuto>;

// -----------------------------------------------------------------------
// Alignment
// -----------------------------------------------------------------------

/// @brief Alignment for grid items along an axis.
enum class GridAlign : uint8_t {
    Start,
    Center,
    End,
    Stretch
};

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------

/// @brief Placement of a single child within the grid.
struct GridPlacement {
    int column     = -1; ///< 0-based column index (-1 = auto).
    int row        = -1; ///< 0-based row index (-1 = auto).
    int columnSpan = 1;  ///< Number of columns to span.
    int rowSpan    = 1;  ///< Number of rows to span.
};

/// @brief Configuration for the entire grid container.
struct GridLayoutConfig {
    std::vector<GridTrackSize> columns;          ///< Column track definitions.
    std::vector<GridTrackSize> rows;             ///< Row track definitions.
    float columnGap    = 0.0f;                   ///< Horizontal gap between columns.
    float rowGap       = 0.0f;                   ///< Vertical gap between rows.
    GridAlign justifyItems = GridAlign::Stretch;  ///< Default horizontal alignment.
    GridAlign alignItems   = GridAlign::Stretch;  ///< Default vertical alignment.
};

/// @brief A named rectangular area within the grid.
struct GridArea {
    std::string name;
    int column     = 0;
    int row        = 0;
    int columnSpan = 1;
    int rowSpan    = 1;
};

// -----------------------------------------------------------------------
// GridLayout
// -----------------------------------------------------------------------

/**
 * @class GridLayout
 * @brief Computes child bounds using a CSS Grid-like algorithm.
 *
 * Usage:
 * @code
 *   GridLayoutConfig config;
 *   config.columns = { TrackFixed{200}, TrackFraction{1}, TrackFraction{2} };
 *   config.rows    = { TrackFixed{50}, TrackAuto{} };
 *   config.columnGap = 8.0f;
 *   config.rowGap    = 8.0f;
 *
 *   GridLayout grid;
 *   std::vector<GridPlacement> placements(childCount);
 *   auto bounds = grid.compute(parentRect, config, placements, childHints);
 * @endcode
 */
class GridLayout {
    std::map<std::string, GridArea> m_areas;

public:
    // -------------------------------------------------------------------
    // Named areas
    // -------------------------------------------------------------------

    /// @brief Define a named area spanning the given grid region.
    void defineArea(const std::string& name,
                    int col, int row, int colSpan, int rowSpan) {
        m_areas.insert_or_assign(name, GridArea{name, col, row, colSpan, rowSpan});
    }

    /// @brief Resolve a GridPlacement from a named area.
    /// @return Placement for the area, or default auto-placement if not found.
    [[nodiscard]] GridPlacement placeInArea(const std::string& areaName) const {
        const auto it = m_areas.find(areaName);
        if (it != m_areas.end()) {
            const auto& a = it->second;
            return GridPlacement{a.column, a.row, a.columnSpan, a.rowSpan};
        }
        return {};
    }

    /// @brief Clear all named areas.
    void clearAreas() noexcept { m_areas.clear(); }

    // -------------------------------------------------------------------
    // compute
    // -------------------------------------------------------------------

    /// @brief Size hint for auto-track sizing.
    struct ChildHint {
        float preferredWidth  = 0.0f;
        float preferredHeight = 0.0f;
    };

    /**
     * @brief Resolve child bounds within the parent rectangle.
     *
     * @param parentBounds  Available space for the grid.
     * @param config        Grid configuration.
     * @param placements    Per-child placement (same order as children).
     * @param hints         Per-child size hints (used for Auto tracks).
     * @return Resolved bounds per child, in input order.
     */
    [[nodiscard]] std::vector<sgc::Rectf> compute(
        const sgc::Rectf& parentBounds,
        const GridLayoutConfig& config,
        std::vector<GridPlacement> placements,
        const std::vector<ChildHint>& hints) const {

        const size_t childCount = placements.size();
        const size_t numCols = config.columns.empty() ? 1 : config.columns.size();
        const size_t numRows = config.rows.empty() ? 1 : config.rows.size();

        // Auto-place children that have no explicit position.
        autoPlace(placements, static_cast<int>(numCols), static_cast<int>(numRows));

        // Resolve track sizes.
        auto colSizes = resolveTracks(config.columns, parentBounds.width(),
                                      config.columnGap, numCols,
                                      hints, placements, true);
        auto rowSizes = resolveTracks(config.rows, parentBounds.height(),
                                      config.rowGap, numRows,
                                      hints, placements, false);

        // Compute track start positions.
        auto colStarts = trackStarts(colSizes, config.columnGap, parentBounds.x());
        auto rowStarts = trackStarts(rowSizes, config.rowGap, parentBounds.y());

        // Build child bounds.
        std::vector<sgc::Rectf> results(childCount);
        for (size_t i = 0; i < childCount; ++i) {
            const auto& p = placements[i];
            const int c0 = clampIndex(p.column, static_cast<int>(numCols));
            const int r0 = clampIndex(p.row, static_cast<int>(numRows));
            const int c1 = std::min(c0 + p.columnSpan, static_cast<int>(numCols));
            const int r1 = std::min(r0 + p.rowSpan, static_cast<int>(numRows));

            const float cellX = colStarts[static_cast<size_t>(c0)];
            const float cellY = rowStarts[static_cast<size_t>(r0)];
            float cellW = 0.0f;
            float cellH = 0.0f;
            for (int c = c0; c < c1; ++c) {
                cellW += colSizes[static_cast<size_t>(c)];
                if (c > c0) cellW += config.columnGap;
            }
            for (int r = r0; r < r1; ++r) {
                cellH += rowSizes[static_cast<size_t>(r)];
                if (r > r0) cellH += config.rowGap;
            }

            const float hintW = (i < hints.size()) ? hints[i].preferredWidth  : 0.0f;
            const float hintH = (i < hints.size()) ? hints[i].preferredHeight : 0.0f;

            results[i] = alignInCell(cellX, cellY, cellW, cellH,
                                     hintW, hintH,
                                     config.justifyItems, config.alignItems);
        }

        return results;
    }

private:
    // -------------------------------------------------------------------
    // Auto-placement
    // -------------------------------------------------------------------

    static void autoPlace(std::vector<GridPlacement>& placements,
                          int numCols, int numRows) {
        // Build occupancy grid.
        const size_t gridSize = static_cast<size_t>(numCols) * static_cast<size_t>(numRows);
        std::vector<bool> occupied(gridSize, false);

        // Mark explicitly placed children.
        for (const auto& p : placements) {
            if (p.column >= 0 && p.row >= 0) {
                markOccupied(occupied, numCols, numRows, p);
            }
        }

        // Fill unplaced children left-to-right, top-to-bottom.
        int cursorCol = 0;
        int cursorRow = 0;
        for (auto& p : placements) {
            if (p.column >= 0 && p.row >= 0) {
                continue;
            }
            while (cursorRow < numRows) {
                if (canFit(occupied, numCols, numRows,
                           cursorCol, cursorRow, p.columnSpan, p.rowSpan)) {
                    p.column = cursorCol;
                    p.row    = cursorRow;
                    markOccupied(occupied, numCols, numRows, p);
                    break;
                }
                ++cursorCol;
                if (cursorCol + p.columnSpan > numCols) {
                    cursorCol = 0;
                    ++cursorRow;
                }
            }
            // Fallback: place at (0,0) if grid is full.
            if (p.column < 0) { p.column = 0; }
            if (p.row    < 0) { p.row    = 0; }
        }
    }

    static bool canFit(const std::vector<bool>& occupied,
                       int numCols, int numRows,
                       int col, int row, int colSpan, int rowSpan) {
        if (col + colSpan > numCols || row + rowSpan > numRows) return false;
        for (int r = row; r < row + rowSpan; ++r) {
            for (int c = col; c < col + colSpan; ++c) {
                if (occupied[static_cast<size_t>(r * numCols + c)]) return false;
            }
        }
        return true;
    }

    static void markOccupied(std::vector<bool>& occupied,
                             int numCols, int numRows,
                             const GridPlacement& p) {
        const int c1 = std::min(p.column + p.columnSpan, numCols);
        const int r1 = std::min(p.row + p.rowSpan, numRows);
        for (int r = p.row; r < r1; ++r) {
            for (int c = p.column; c < c1; ++c) {
                occupied[static_cast<size_t>(r * numCols + c)] = true;
            }
        }
    }

    // -------------------------------------------------------------------
    // Track resolution
    // -------------------------------------------------------------------

    [[nodiscard]] static std::vector<float> resolveTracks(
        const std::vector<GridTrackSize>& tracks,
        float totalSpace,
        float gap,
        size_t count,
        const std::vector<ChildHint>& hints,
        const std::vector<GridPlacement>& placements,
        bool isColumnAxis) {

        std::vector<float> sizes(count, 0.0f);
        const float totalGap = (count > 1) ? gap * static_cast<float>(count - 1) : 0.0f;
        float usedSpace = totalGap;
        float totalFr   = 0.0f;

        // First pass: fixed and auto.
        for (size_t i = 0; i < count; ++i) {
            const auto& track = (i < tracks.size()) ? tracks[i] : GridTrackSize{TrackAuto{}};

            if (const auto* fixed = std::get_if<TrackFixed>(&track)) {
                sizes[i] = fixed->pixels;
                usedSpace += sizes[i];
            } else if (std::holds_alternative<TrackAuto>(track)) {
                // Measure max preferred size of children in this track.
                float maxPref = 0.0f;
                for (size_t ci = 0; ci < placements.size(); ++ci) {
                    const int idx = isColumnAxis ? placements[ci].column : placements[ci].row;
                    if (idx == static_cast<int>(i) && ci < hints.size()) {
                        const float pref = isColumnAxis
                            ? hints[ci].preferredWidth
                            : hints[ci].preferredHeight;
                        maxPref = std::max(maxPref, pref);
                    }
                }
                sizes[i] = maxPref;
                usedSpace += sizes[i];
            } else if (const auto* fr = std::get_if<TrackFraction>(&track)) {
                totalFr += fr->value;
            }
        }

        // Second pass: distribute remaining space to fractional tracks.
        const float remaining = std::max(0.0f, totalSpace - usedSpace);
        if (totalFr > 0.0f) {
            for (size_t i = 0; i < count; ++i) {
                const auto& track = (i < tracks.size()) ? tracks[i] : GridTrackSize{TrackAuto{}};
                if (const auto* fr = std::get_if<TrackFraction>(&track)) {
                    sizes[i] = remaining * (fr->value / totalFr);
                }
            }
        }

        return sizes;
    }

    // -------------------------------------------------------------------
    // Positioning helpers
    // -------------------------------------------------------------------

    [[nodiscard]] static std::vector<float> trackStarts(
        const std::vector<float>& sizes, float gap, float origin) {
        std::vector<float> starts(sizes.size());
        float pos = origin;
        for (size_t i = 0; i < sizes.size(); ++i) {
            starts[i] = pos;
            pos += sizes[i] + gap;
        }
        return starts;
    }

    [[nodiscard]] static sgc::Rectf alignInCell(
        float cellX, float cellY, float cellW, float cellH,
        float childW, float childH,
        GridAlign justifyItems, GridAlign alignItems) {

        float x = cellX;
        float y = cellY;
        float w = (justifyItems == GridAlign::Stretch || childW <= 0.0f) ? cellW : childW;
        float h = (alignItems   == GridAlign::Stretch || childH <= 0.0f) ? cellH : childH;

        switch (justifyItems) {
        case GridAlign::Start:   x = cellX; break;
        case GridAlign::Center:  x = cellX + (cellW - w) * 0.5f; break;
        case GridAlign::End:     x = cellX + cellW - w; break;
        case GridAlign::Stretch: x = cellX; break;
        }

        switch (alignItems) {
        case GridAlign::Start:   y = cellY; break;
        case GridAlign::Center:  y = cellY + (cellH - h) * 0.5f; break;
        case GridAlign::End:     y = cellY + cellH - h; break;
        case GridAlign::Stretch: y = cellY; break;
        }

        return sgc::Rectf{x, y, w, h};
    }

    [[nodiscard]] static int clampIndex(int index, int count) noexcept {
        if (index < 0) return 0;
        if (index >= count) return count - 1;
        return index;
    }
};

} // namespace mitiru::ui
