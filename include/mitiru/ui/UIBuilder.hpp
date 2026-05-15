#pragma once

/// @file UIBuilder.hpp
/// @brief Declarative layout builder for MitiruEngine UI.
/// @details Provides one-line layout helpers that return positioned Rectf values,
///          making declarative layouts easier than hardcoding coordinates.

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mitiru::ui {

/// @brief Convenience layout builder that turns layout intent into positioned rects.
///
/// @code
///   UIBuilder ui(1280, 720);
///   ui.padding(16);
///   auto [btn1, btn2, btn3] = ui.row(3, 40, 8).cells;
///   auto center = ui.centered(400, 300);
///   auto cell   = ui.grid(3, 4).cells[1][2];
/// @endcode
class UIBuilder {
public:
	// ── Result types ──────────────────────────────────

	/// @brief Result of a horizontal row layout.
	struct RowResult {
		std::vector<sgc::Rectf> cells;
		float totalWidth  = 0.0f;
		float totalHeight = 0.0f;
	};

	/// @brief Result of a vertical column layout.
	struct ColResult {
		std::vector<sgc::Rectf> cells;
		float totalWidth  = 0.0f;
		float totalHeight = 0.0f;
	};

	/// @brief Result of a grid layout.
	struct GridResult {
		std::vector<std::vector<sgc::Rectf>> cells;
	};

	/// @brief Result of a horizontal split.
	struct SplitResult {
		sgc::Rectf left;
		sgc::Rectf right;
	};

	/// @brief Result of a vertical split.
	struct VSplitResult {
		sgc::Rectf top;
		sgc::Rectf bottom;
	};

	// ── Construction ──────────────────────────────────

	/// @brief Set the working area to the full screen.
	/// @param screenW Screen width in pixels.
	/// @param screenH Screen height in pixels.
	UIBuilder(float screenW, float screenH) noexcept
		: m_screenW(screenW)
		, m_screenH(screenH)
		, m_areaX(0.0f)
		, m_areaY(0.0f)
		, m_areaW(screenW)
		, m_areaH(screenH)
	{
	}

	// ── Spacing ───────────────────────────────────────

	/// @brief Apply uniform padding to all sides.
	UIBuilder& padding(float p) noexcept {
		m_areaX += p;
		m_areaY += p;
		m_areaW -= p * 2.0f;
		m_areaH -= p * 2.0f;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief Apply horizontal and vertical padding.
	UIBuilder& padding(float h, float v) noexcept {
		m_areaX += h;
		m_areaY += v;
		m_areaW -= h * 2.0f;
		m_areaH -= v * 2.0f;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief Apply per-side padding (top, right, bottom, left).
	UIBuilder& padding(float top, float right, float bottom, float left) noexcept {
		m_areaX += left;
		m_areaY += top;
		m_areaW -= left + right;
		m_areaH -= top + bottom;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief Apply uniform margin (equivalent to padding for builder purposes).
	UIBuilder& margin(float m) noexcept {
		return padding(m);
	}

	/// @brief Override the working area explicitly.
	UIBuilder& area(const sgc::Rectf& rect) noexcept {
		m_areaX = rect.x();
		m_areaY = rect.y();
		m_areaW = rect.width();
		m_areaH = rect.height();
		return *this;
	}

	/// @brief Get the current working area as a Rectf.
	[[nodiscard]] sgc::Rectf workingArea() const noexcept {
		return sgc::Rectf{m_areaX, m_areaY, m_areaW, m_areaH};
	}

	// ── Row layout (horizontal) ───────────────────────

	/// @brief Create N equal-width cells in a horizontal row.
	/// @param count Number of cells.
	/// @param height Cell height in pixels.
	/// @param gap Space between cells.
	[[nodiscard]] RowResult row(int count, float height = 40.0f, float gap = 8.0f) const {
		if (count <= 0) { return {}; }
		const float totalGap = gap * static_cast<float>(count - 1);
		const float cellW    = std::max(0.0f, (m_areaW - totalGap) / static_cast<float>(count));
		std::vector<sgc::Rectf> cells;
		cells.reserve(static_cast<size_t>(count));
		float x = m_areaX;
		for (int i = 0; i < count; ++i) {
			cells.push_back(sgc::Rectf{x, m_areaY, cellW, height});
			x += cellW + gap;
		}
		return RowResult{std::move(cells), m_areaW, height};
	}

	/// @brief Create cells with explicit widths in a horizontal row.
	/// @param widths Per-cell widths.
	/// @param height Cell height in pixels.
	/// @param gap Space between cells.
	[[nodiscard]] RowResult row(const std::vector<float>& widths,
	                            float height = 40.0f, float gap = 8.0f) const {
		std::vector<sgc::Rectf> cells;
		cells.reserve(widths.size());
		float x = m_areaX;
		float totalW = 0.0f;
		for (size_t i = 0; i < widths.size(); ++i) {
			cells.push_back(sgc::Rectf{x, m_areaY, widths[i], height});
			x += widths[i] + gap;
			totalW += widths[i];
			if (i < widths.size() - 1) { totalW += gap; }
		}
		return RowResult{std::move(cells), totalW, height};
	}

	// ── Column layout (vertical) ──────────────────────

	/// @brief Create N equal-height cells in a vertical column.
	/// @param count Number of cells.
	/// @param itemHeight Cell height in pixels.
	/// @param gap Space between cells.
	[[nodiscard]] ColResult column(int count, float itemHeight = 40.0f, float gap = 8.0f) const {
		if (count <= 0) { return {}; }
		std::vector<sgc::Rectf> cells;
		cells.reserve(static_cast<size_t>(count));
		float y = m_areaY;
		for (int i = 0; i < count; ++i) {
			cells.push_back(sgc::Rectf{m_areaX, y, m_areaW, itemHeight});
			y += itemHeight + gap;
		}
		const float totalH = itemHeight * static_cast<float>(count) +
		                      gap * static_cast<float>(count - 1);
		return ColResult{std::move(cells), m_areaW, totalH};
	}

	// ── Grid layout ───────────────────────────────────

	/// @brief Create a rows x cols grid of equal-sized cells.
	/// @param rows Number of rows.
	/// @param cols Number of columns.
	/// @param gap Space between cells.
	[[nodiscard]] GridResult grid(int rows, int cols, float gap = 8.0f) const {
		GridResult result;
		if (rows <= 0 || cols <= 0) { return result; }
		const float hGap  = gap * static_cast<float>(cols - 1);
		const float vGap  = gap * static_cast<float>(rows - 1);
		const float cellW = std::max(0.0f, (m_areaW - hGap) / static_cast<float>(cols));
		const float cellH = std::max(0.0f, (m_areaH - vGap) / static_cast<float>(rows));
		result.cells.resize(static_cast<size_t>(rows));
		float y = m_areaY;
		for (int r = 0; r < rows; ++r) {
			result.cells[static_cast<size_t>(r)].reserve(static_cast<size_t>(cols));
			float x = m_areaX;
			for (int c = 0; c < cols; ++c) {
				result.cells[static_cast<size_t>(r)].push_back(
					sgc::Rectf{x, y, cellW, cellH});
				x += cellW + gap;
			}
			y += cellH + gap;
		}
		return result;
	}

	// ── Centering ─────────────────────────────────────

	/// @brief Center a rect of given size within the working area.
	[[nodiscard]] sgc::Rectf centered(float w, float h) const noexcept {
		return sgc::Rectf{
			m_areaX + (m_areaW - w) * 0.5f,
			m_areaY + (m_areaH - h) * 0.5f,
			w, h};
	}

	/// @brief Center horizontally at a given Y position.
	[[nodiscard]] sgc::Rectf centeredHorizontally(float w, float h, float y) const noexcept {
		return sgc::Rectf{m_areaX + (m_areaW - w) * 0.5f, y, w, h};
	}

	/// @brief Center vertically at a given X position.
	[[nodiscard]] sgc::Rectf centeredVertically(float w, float h, float x) const noexcept {
		return sgc::Rectf{x, m_areaY + (m_areaH - h) * 0.5f, w, h};
	}

	// ── Anchoring ─────────────────────────────────────

	[[nodiscard]] sgc::Rectf topLeft(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX, m_areaY, w, h};
	}

	[[nodiscard]] sgc::Rectf topCenter(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + (m_areaW - w) * 0.5f, m_areaY, w, h};
	}

	[[nodiscard]] sgc::Rectf topRight(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + m_areaW - w, m_areaY, w, h};
	}

	[[nodiscard]] sgc::Rectf bottomLeft(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX, m_areaY + m_areaH - h, w, h};
	}

	[[nodiscard]] sgc::Rectf bottomCenter(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + (m_areaW - w) * 0.5f, m_areaY + m_areaH - h, w, h};
	}

	[[nodiscard]] sgc::Rectf bottomRight(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + m_areaW - w, m_areaY + m_areaH - h, w, h};
	}

	/// @brief Vertically centered, left aligned.
	[[nodiscard]] sgc::Rectf left(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX, m_areaY + (m_areaH - h) * 0.5f, w, h};
	}

	/// @brief Vertically centered, right aligned.
	[[nodiscard]] sgc::Rectf right(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + m_areaW - w, m_areaY + (m_areaH - h) * 0.5f, w, h};
	}

	// ── Split layout ──────────────────────────────────

	/// @brief Split working area horizontally into left and right panels.
	/// @param ratio Left panel proportion (0.0 .. 1.0).
	/// @param gap Space between panels.
	[[nodiscard]] SplitResult splitHorizontal(float ratio = 0.5f, float gap = 8.0f) const noexcept {
		const float leftW  = std::max(0.0f, (m_areaW - gap) * ratio);
		const float rightW = std::max(0.0f, m_areaW - gap - leftW);
		return SplitResult{
			sgc::Rectf{m_areaX, m_areaY, leftW, m_areaH},
			sgc::Rectf{m_areaX + leftW + gap, m_areaY, rightW, m_areaH}};
	}

	/// @brief Split working area vertically into top and bottom panels.
	/// @param ratio Top panel proportion (0.0 .. 1.0).
	/// @param gap Space between panels.
	[[nodiscard]] VSplitResult splitVertical(float ratio = 0.5f, float gap = 8.0f) const noexcept {
		const float topH    = std::max(0.0f, (m_areaH - gap) * ratio);
		const float bottomH = std::max(0.0f, m_areaH - gap - topH);
		return VSplitResult{
			sgc::Rectf{m_areaX, m_areaY, m_areaW, topH},
			sgc::Rectf{m_areaX, m_areaY + topH + gap, m_areaW, bottomH}};
	}

	// ── Stack (accumulates Y) ─────────────────────────

	/// @brief Begin a vertical stack at the given position and width.
	UIBuilder& beginStack(float x, float y, float width) noexcept {
		m_stackX = x;
		m_stackY = y;
		m_stackW = width;
		m_stackActive = true;
		return *this;
	}

	/// @brief Get the next rect in the stack and advance Y.
	/// @param height Item height.
	/// @param gap Space after this item.
	[[nodiscard]] sgc::Rectf stackItem(float height, float gap = 4.0f) noexcept {
		const sgc::Rectf rect{m_stackX, m_stackY, m_stackW, height};
		m_stackY += height + gap;
		return rect;
	}

	/// @brief Current Y position in the stack.
	[[nodiscard]] float stackY() const noexcept { return m_stackY; }

	// ── Responsive helpers ────────────────────────────

	/// @brief True if screen width suggests a mobile layout (< 768px).
	[[nodiscard]] bool isMobile() const noexcept { return m_screenW < 768.0f; }

	/// @brief True if screen width suggests a tablet layout (768 .. 1279px).
	[[nodiscard]] bool isTablet() const noexcept {
		return m_screenW >= 768.0f && m_screenW < 1280.0f;
	}

	/// @brief True if screen width suggests a desktop layout (>= 1280px).
	[[nodiscard]] bool isDesktop() const noexcept { return m_screenW >= 1280.0f; }

	/// @brief Responsive scale factor relative to 1280px baseline.
	[[nodiscard]] float scale() const noexcept {
		return std::max(0.5f, m_screenW / 1280.0f);
	}

	/// @brief Suggested column count based on screen width.
	[[nodiscard]] int columns() const noexcept {
		if (m_screenW < 768.0f)  { return 2; }
		if (m_screenW < 1280.0f) { return 3; }
		return 4;
	}

private:
	float m_screenW = 0.0f;
	float m_screenH = 0.0f;
	float m_areaX   = 0.0f;
	float m_areaY   = 0.0f;
	float m_areaW   = 0.0f;
	float m_areaH   = 0.0f;

	// Stack state.
	float m_stackX = 0.0f;
	float m_stackY = 0.0f;
	float m_stackW = 0.0f;
	bool  m_stackActive = false;
};

} // namespace mitiru::ui
