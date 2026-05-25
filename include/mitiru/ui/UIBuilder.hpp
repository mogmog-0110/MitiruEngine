#pragma once

/// @file UIBuilder.hpp
/// @brief MitiruEngine UI 用の宣言的 layout builder。
/// @details 配置済みの Rectf 値を返す 1 行 layout helper を提供し、座標を
///          ハードコードするより簡単に宣言的 layout を組めるようにする。

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mitiru::ui {

/// @brief layout の意図を配置済み rect に変換する簡易 layout builder。
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

	/// @brief 水平 row layout の結果。
	struct RowResult {
		std::vector<sgc::Rectf> cells;
		float totalWidth  = 0.0f;
		float totalHeight = 0.0f;
	};

	/// @brief 垂直 column layout の結果。
	struct ColResult {
		std::vector<sgc::Rectf> cells;
		float totalWidth  = 0.0f;
		float totalHeight = 0.0f;
	};

	/// @brief grid layout の結果。
	struct GridResult {
		std::vector<std::vector<sgc::Rectf>> cells;
	};

	/// @brief 水平 split の結果。
	struct SplitResult {
		sgc::Rectf left;
		sgc::Rectf right;
	};

	/// @brief 垂直 split の結果。
	struct VSplitResult {
		sgc::Rectf top;
		sgc::Rectf bottom;
	};

	// ── Construction ──────────────────────────────────

	/// @brief 作業領域を画面全体に設定する。
	/// @param screenW 画面幅 (px)。
	/// @param screenH 画面高さ (px)。
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

	/// @brief 全辺に均一な padding を適用する。
	UIBuilder& padding(float p) noexcept {
		m_areaX += p;
		m_areaY += p;
		m_areaW -= p * 2.0f;
		m_areaH -= p * 2.0f;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief 水平・垂直の padding を適用する。
	UIBuilder& padding(float h, float v) noexcept {
		m_areaX += h;
		m_areaY += v;
		m_areaW -= h * 2.0f;
		m_areaH -= v * 2.0f;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief 辺ごとの padding を適用する (top, right, bottom, left)。
	UIBuilder& padding(float top, float right, float bottom, float left) noexcept {
		m_areaX += left;
		m_areaY += top;
		m_areaW -= left + right;
		m_areaH -= top + bottom;
		m_areaW = std::max(0.0f, m_areaW);
		m_areaH = std::max(0.0f, m_areaH);
		return *this;
	}

	/// @brief 均一な margin を適用する (builder 上は padding と等価)。
	UIBuilder& margin(float m) noexcept {
		return padding(m);
	}

	/// @brief 作業領域を明示的に上書きする。
	UIBuilder& area(const sgc::Rectf& rect) noexcept {
		m_areaX = rect.x();
		m_areaY = rect.y();
		m_areaW = rect.width();
		m_areaH = rect.height();
		return *this;
	}

	/// @brief 現在の作業領域を Rectf として取得する。
	[[nodiscard]] sgc::Rectf workingArea() const noexcept {
		return sgc::Rectf{m_areaX, m_areaY, m_areaW, m_areaH};
	}

	// ── Row layout (horizontal) ───────────────────────

	/// @brief 水平 row に N 個の等幅 cell を生成する。
	/// @param count cell 数。
	/// @param height cell の高さ (px)。
	/// @param gap cell 間の間隔。
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

	/// @brief 水平 row に幅を明示指定した cell を生成する。
	/// @param widths cell ごとの幅。
	/// @param height cell の高さ (px)。
	/// @param gap cell 間の間隔。
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

	/// @brief 垂直 column に N 個の等高 cell を生成する。
	/// @param count cell 数。
	/// @param itemHeight cell の高さ (px)。
	/// @param gap cell 間の間隔。
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

	/// @brief 等サイズ cell の rows x cols grid を生成する。
	/// @param rows 行数。
	/// @param cols 列数。
	/// @param gap cell 間の間隔。
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

	/// @brief 指定サイズの rect を作業領域内で中央寄せする。
	[[nodiscard]] sgc::Rectf centered(float w, float h) const noexcept {
		return sgc::Rectf{
			m_areaX + (m_areaW - w) * 0.5f,
			m_areaY + (m_areaH - h) * 0.5f,
			w, h};
	}

	/// @brief 指定 Y 位置で水平方向に中央寄せする。
	[[nodiscard]] sgc::Rectf centeredHorizontally(float w, float h, float y) const noexcept {
		return sgc::Rectf{m_areaX + (m_areaW - w) * 0.5f, y, w, h};
	}

	/// @brief 指定 X 位置で垂直方向に中央寄せする。
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

	/// @brief 垂直中央寄せ・左揃え。
	[[nodiscard]] sgc::Rectf left(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX, m_areaY + (m_areaH - h) * 0.5f, w, h};
	}

	/// @brief 垂直中央寄せ・右揃え。
	[[nodiscard]] sgc::Rectf right(float w, float h) const noexcept {
		return sgc::Rectf{m_areaX + m_areaW - w, m_areaY + (m_areaH - h) * 0.5f, w, h};
	}

	// ── Split layout ──────────────────────────────────

	/// @brief 作業領域を水平に左右 panel へ分割する。
	/// @param ratio 左 panel の割合 (0.0 .. 1.0)。
	/// @param gap panel 間の間隔。
	[[nodiscard]] SplitResult splitHorizontal(float ratio = 0.5f, float gap = 8.0f) const noexcept {
		const float leftW  = std::max(0.0f, (m_areaW - gap) * ratio);
		const float rightW = std::max(0.0f, m_areaW - gap - leftW);
		return SplitResult{
			sgc::Rectf{m_areaX, m_areaY, leftW, m_areaH},
			sgc::Rectf{m_areaX + leftW + gap, m_areaY, rightW, m_areaH}};
	}

	/// @brief 作業領域を垂直に上下 panel へ分割する。
	/// @param ratio 上 panel の割合 (0.0 .. 1.0)。
	/// @param gap panel 間の間隔。
	[[nodiscard]] VSplitResult splitVertical(float ratio = 0.5f, float gap = 8.0f) const noexcept {
		const float topH    = std::max(0.0f, (m_areaH - gap) * ratio);
		const float bottomH = std::max(0.0f, m_areaH - gap - topH);
		return VSplitResult{
			sgc::Rectf{m_areaX, m_areaY, m_areaW, topH},
			sgc::Rectf{m_areaX, m_areaY + topH + gap, m_areaW, bottomH}};
	}

	// ── Stack (accumulates Y) ─────────────────────────

	/// @brief 指定位置・幅で垂直 stack を開始する。
	UIBuilder& beginStack(float x, float y, float width) noexcept {
		m_stackX = x;
		m_stackY = y;
		m_stackW = width;
		m_stackActive = true;
		return *this;
	}

	/// @brief stack 内の次の rect を取得し Y を進める。
	/// @param height item の高さ。
	/// @param gap この item の後ろの間隔。
	[[nodiscard]] sgc::Rectf stackItem(float height, float gap = 4.0f) noexcept {
		const sgc::Rectf rect{m_stackX, m_stackY, m_stackW, height};
		m_stackY += height + gap;
		return rect;
	}

	/// @brief stack 内の現在の Y 位置。
	[[nodiscard]] float stackY() const noexcept { return m_stackY; }

	// ── Responsive helpers ────────────────────────────

	/// @brief 画面幅が mobile layout 相当か (< 768px) を返す。
	[[nodiscard]] bool isMobile() const noexcept { return m_screenW < 768.0f; }

	/// @brief 画面幅が tablet layout 相当か (768 .. 1279px) を返す。
	[[nodiscard]] bool isTablet() const noexcept {
		return m_screenW >= 768.0f && m_screenW < 1280.0f;
	}

	/// @brief 画面幅が desktop layout 相当か (>= 1280px) を返す。
	[[nodiscard]] bool isDesktop() const noexcept { return m_screenW >= 1280.0f; }

	/// @brief 1280px を基準とした responsive scale factor。
	[[nodiscard]] float scale() const noexcept {
		return std::max(0.5f, m_screenW / 1280.0f);
	}

	/// @brief 画面幅に基づく推奨列数。
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

	// stack の状態。
	float m_stackX = 0.0f;
	float m_stackY = 0.0f;
	float m_stackW = 0.0f;
	bool  m_stackActive = false;
};

} // namespace mitiru::ui
