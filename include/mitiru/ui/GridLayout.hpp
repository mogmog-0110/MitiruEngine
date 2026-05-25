#pragma once

/**
 * @file GridLayout.hpp
 * @brief MitiruEngine UI 向けの CSS Grid 風 layout engine。
 *
 * fixed / fractional (fr) / auto の track sizing、gap 制御、
 * named area、auto-placement、item ごとの alignment に対応。
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

/// @brief grid track の固定 pixel サイズ。
struct TrackFixed {
    float pixels = 0.0f;
};

/// @brief grid track の分数単位 (CSS の `fr` に相当)。
struct TrackFraction {
    float value = 1.0f;
};

/// @brief auto サイズの track (その track 内で最大の child に合わせる)。
struct TrackAuto {};

/// @brief grid track 1 本のサイズ定義。
using GridTrackSize = std::variant<TrackFixed, TrackFraction, TrackAuto>;

// -----------------------------------------------------------------------
// Alignment
// -----------------------------------------------------------------------

/// @brief 軸方向の grid item の alignment。
enum class GridAlign : uint8_t {
    Start,
    Center,
    End,
    Stretch
};

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------

/// @brief grid 内での child 1 個の配置。
struct GridPlacement {
    int column     = -1; ///< 0 始まりの column index (-1 = auto)。
    int row        = -1; ///< 0 始まりの row index (-1 = auto)。
    int columnSpan = 1;  ///< またぐ column 数。
    int rowSpan    = 1;  ///< またぐ row 数。
};

/// @brief grid container 全体の設定。
struct GridLayoutConfig {
    std::vector<GridTrackSize> columns;          ///< column track の定義。
    std::vector<GridTrackSize> rows;             ///< row track の定義。
    float columnGap    = 0.0f;                   ///< column 間の水平 gap。
    float rowGap       = 0.0f;                   ///< row 間の垂直 gap。
    GridAlign justifyItems = GridAlign::Stretch;  ///< デフォルトの水平 alignment。
    GridAlign alignItems   = GridAlign::Stretch;  ///< デフォルトの垂直 alignment。
};

/// @brief grid 内の名前付き矩形領域。
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
 * @brief CSS Grid 風の algorithm で child の bounds を計算する。
 *
 * 使い方:
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

    /// @brief 指定した grid 領域にまたがる named area を定義する。
    void defineArea(const std::string& name,
                    int col, int row, int colSpan, int rowSpan) {
        m_areas.insert_or_assign(name, GridArea{name, col, row, colSpan, rowSpan});
    }

    /// @brief named area から GridPlacement を解決する。
    /// @return その area の placement。見つからなければデフォルトの auto-placement。
    [[nodiscard]] GridPlacement placeInArea(const std::string& areaName) const {
        const auto it = m_areas.find(areaName);
        if (it != m_areas.end()) {
            const auto& a = it->second;
            return GridPlacement{a.column, a.row, a.columnSpan, a.rowSpan};
        }
        return {};
    }

    /// @brief すべての named area を消去する。
    void clearAreas() noexcept { m_areas.clear(); }

    // -------------------------------------------------------------------
    // compute
    // -------------------------------------------------------------------

    /// @brief auto track sizing 用のサイズ hint。
    struct ChildHint {
        float preferredWidth  = 0.0f;
        float preferredHeight = 0.0f;
    };

    /**
     * @brief parent 矩形内で各 child の bounds を解決する。
     *
     * @param parentBounds  grid が使える領域。
     * @param config        grid の設定。
     * @param placements    child ごとの placement (children と同じ順)。
     * @param hints         child ごとのサイズ hint (Auto track で使用)。
     * @return 入力順で並んだ child ごとの解決済み bounds。
     */
    [[nodiscard]] std::vector<sgc::Rectf> compute(
        const sgc::Rectf& parentBounds,
        const GridLayoutConfig& config,
        std::vector<GridPlacement> placements,
        const std::vector<ChildHint>& hints) const {

        const size_t childCount = placements.size();
        const size_t numCols = config.columns.empty() ? 1 : config.columns.size();
        const size_t numRows = config.rows.empty() ? 1 : config.rows.size();

        // 明示位置を持たない child を auto-place する。
        autoPlace(placements, static_cast<int>(numCols), static_cast<int>(numRows));

        // track サイズを解決する。
        auto colSizes = resolveTracks(config.columns, parentBounds.width(),
                                      config.columnGap, numCols,
                                      hints, placements, true);
        auto rowSizes = resolveTracks(config.rows, parentBounds.height(),
                                      config.rowGap, numRows,
                                      hints, placements, false);

        // track の開始位置を計算する。
        auto colStarts = trackStarts(colSizes, config.columnGap, parentBounds.x());
        auto rowStarts = trackStarts(rowSizes, config.rowGap, parentBounds.y());

        // child の bounds を組み立てる。
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
        // 占有 grid を構築する。
        const size_t gridSize = static_cast<size_t>(numCols) * static_cast<size_t>(numRows);
        std::vector<bool> occupied(gridSize, false);

        // 明示配置された child を mark する。
        for (const auto& p : placements) {
            if (p.column >= 0 && p.row >= 0) {
                markOccupied(occupied, numCols, numRows, p);
            }
        }

        // 未配置の child を 左→右、上→下 の順で埋める。
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
            // fallback: grid が満杯なら (0,0) に置く。
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

        // 第 1 pass: fixed と auto。
        for (size_t i = 0; i < count; ++i) {
            const auto& track = (i < tracks.size()) ? tracks[i] : GridTrackSize{TrackAuto{}};

            if (const auto* fixed = std::get_if<TrackFixed>(&track)) {
                sizes[i] = fixed->pixels;
                usedSpace += sizes[i];
            } else if (std::holds_alternative<TrackAuto>(track)) {
                // この track 内の child の preferred size の最大を測る。
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

        // 第 2 pass: 残り領域を fractional track に分配する。
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
