#pragma once

/// @file ScreenLayout.hpp
/// @brief screen 空間の layout primitive と、layout を意識した描画 layer。
/// Part 1: responsive な rect 計算用の値型 / free function。
/// Part 2: layout + Screen 描画 を組み合わせた ScreenLayout class。

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/ui/UIBuilder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::ui {

/// @brief 四辺の inset (padding / margin)。
struct Insets
{
	float top    = 0.0f;
	float right  = 0.0f;
	float bottom = 0.0f;
	float left   = 0.0f;

	/// 全辺一様な inset。
	static constexpr Insets uniform(float v) noexcept { return {v, v, v, v}; }

	/// 対称 (horizontal, vertical)。
	static constexpr Insets symmetric(float h, float v) noexcept { return {v, h, v, h}; }
};

/// @brief 等サイズ cell の縦 column。
struct ColumnLayout
{
	std::vector<sgc::Rectf> cells;
};

/// @brief 左右に並んだ rect の組 (left, right)。
struct SplitH
{
	sgc::Rectf left;
	sgc::Rectf right;
};

/// @brief 上下に積んだ rect の組 (top, bottom)。
struct SplitV
{
	sgc::Rectf top;
	sgc::Rectf bottom;
};

/// @brief inset を適用して矩形を縮める。
[[nodiscard]] inline constexpr sgc::Rectf applyInsets(
	const sgc::Rectf& r, const Insets& ins) noexcept
{
	return {
		r.x() + ins.left,
		r.y() + ins.top,
		std::max(0.0f, r.width()  - ins.left - ins.right),
		std::max(0.0f, r.height() - ins.top  - ins.bottom)
	};
}

/// @brief rect を比 (0..1) で水平分割する。両半の間に gap pixel。
[[nodiscard]] inline SplitH splitHorizontal(
	const sgc::Rectf& r, float ratio, float gap = 0.0f) noexcept
{
	const float halfGap = gap * 0.5f;
	const float lw = r.width() * ratio - halfGap;
	const float rw = r.width() * (1.0f - ratio) - halfGap;
	return {
		{r.x(), r.y(), std::max(0.0f, lw), r.height()},
		{r.x() + r.width() * ratio + halfGap, r.y(), std::max(0.0f, rw), r.height()}
	};
}

/// @brief rect を比 (0..1) で垂直分割する。両半の間に gap pixel。
[[nodiscard]] inline SplitV splitVertical(
	const sgc::Rectf& r, float ratio, float gap = 0.0f) noexcept
{
	const float halfGap = gap * 0.5f;
	const float th = r.height() * ratio - halfGap;
	const float bh = r.height() * (1.0f - ratio) - halfGap;
	return {
		{r.x(), r.y(), r.width(), std::max(0.0f, th)},
		{r.x(), r.y() + r.height() * ratio + halfGap, r.width(), std::max(0.0f, bh)}
	};
}

/// @brief rect を `gap` 空きで `count` 個の等 row に分割する。
[[nodiscard]] inline ColumnLayout columnLayout(
	const sgc::Rectf& area, int count, float cellH, float gap) noexcept
{
	ColumnLayout col;
	col.cells.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const float y = area.y() + static_cast<float>(i) * (cellH + gap);
		col.cells.push_back({area.x(), y, area.width(), cellH});
	}
	return col;
}

/// @brief sidebar 配置のための screen の左右。
enum class Side : uint8_t {
	Left,
	Right
};

/// @brief ScreenLayout widget 用の最小限の panel style。
struct LayoutPanelStyle {
	sgc::Colorf background{0.15f, 0.15f, 0.15f, 0.9f};
	sgc::Colorf border{0.3f, 0.3f, 0.3f, 1.0f};
	float borderWidth = 1.0f;
};

/// @brief ScreenLayout widget 用の最小限の button style。
struct LayoutButtonStyle {
	sgc::Colorf background{0.25f, 0.25f, 0.3f, 1.0f};
	sgc::Colorf backgroundSelected{0.35f, 0.45f, 0.7f, 1.0f};
	sgc::Colorf border{0.4f, 0.4f, 0.5f, 1.0f};
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	float fontSize    = 16.0f;
	float borderWidth = 1.0f;
};

/// @brief tab bar 用の tab 定義。
struct TabDef {
	std::string label;
};

/// @brief auto-layout form 用の form field (label-value の組)。
struct FormField {
	std::string label;
	std::string value;
};

/// @brief 配置と描画を組み合わせた、layout を意識した Screen wrapper。
/// @code
///   ScreenLayout layout(screen);
///   layout.drawHeader("My Game", 50);
///   layout.drawButtonRow({"New Game", "Load", "Quit"}, 300, 40, style, &sel);
/// @endcode
class ScreenLayout {
public:
	/// @brief Screen 参照から構築する。
	explicit ScreenLayout(Screen& screen) noexcept
		: m_screen(screen)
		, m_screenW(static_cast<float>(screen.width()))
		, m_screenH(static_cast<float>(screen.height()))
	{
	}

	/// @brief 内部の Screen にアクセスする。
	[[nodiscard]] Screen& screen() noexcept { return m_screen; }
	[[nodiscard]] const Screen& screen() const noexcept { return m_screen; }

	/// @brief 背景と border 付きの中央 panel を描画する。
	void drawCenteredPanel(float w, float h, const LayoutPanelStyle& style) {
		const UIBuilder ui(m_screenW, m_screenH);
		const auto rect = ui.centered(w, h);
		m_screen.drawRect(rect, style.background);
		if (style.borderWidth > 0.0f) {
			m_screen.drawRectFrame(rect, style.border, style.borderWidth);
		}
	}

	/// @brief screen 上部に header bar を描画する。
	void drawHeader(std::string_view title, float height = 60.0f,
	                const sgc::Colorf& bg = sgc::Colorf{0.1f, 0.1f, 0.15f, 1.0f},
	                const sgc::Colorf& textColor = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}) {
		const sgc::Rectf rect{0.0f, 0.0f, m_screenW, height};
		m_screen.drawRect(rect, bg);
		const float fontSize = std::min(height * 0.5f, 24.0f);
		const auto ts = m_screen.measureText(title, fontSize);
		m_screen.drawText(
			sgc::Vec2f{(m_screenW - ts.x) * 0.5f, (height - ts.y) * 0.5f},
			title, textColor, fontSize);
	}

	/// @brief screen 下部に footer bar を描画する。
	void drawFooter(std::string_view text, float height = 40.0f,
	                const sgc::Colorf& bg = sgc::Colorf{0.08f, 0.08f, 0.1f, 1.0f},
	                const sgc::Colorf& textColor = sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f}) {
		const sgc::Rectf rect{0.0f, m_screenH - height, m_screenW, height};
		m_screen.drawRect(rect, bg);
		const float fontSize = std::min(height * 0.5f, 14.0f);
		const auto ts = m_screen.measureText(text, fontSize);
		m_screen.drawText(
			sgc::Vec2f{(m_screenW - ts.x) * 0.5f,
			            m_screenH - height + (height - ts.y) * 0.5f},
			text, textColor, fontSize);
	}

	/// @brief sidebar panel を描画し、追加描画用にその rect を返す。
	[[nodiscard]] sgc::Rectf drawSidebar(
		float width, Side side = Side::Left,
		const sgc::Colorf& bg = sgc::Colorf{0.12f, 0.12f, 0.15f, 1.0f}) {
		const float x = (side == Side::Left) ? 0.0f : m_screenW - width;
		const sgc::Rectf rect{x, 0.0f, width, m_screenH};
		m_screen.drawRect(rect, bg);
		return rect;
	}

	/// @brief button の水平 row を描画する。
	void drawButtonRow(const std::vector<std::string>& labels, float y, float height,
	                   const LayoutButtonStyle& style,
	                   const int* selectedIndex = nullptr) {
		if (labels.empty()) { return; }
		constexpr float pad = 16.0f;
		constexpr float gap = 8.0f;
		UIBuilder ui(m_screenW, m_screenH);
		ui.area(sgc::Rectf{pad, y, m_screenW - pad * 2.0f, height});
		const auto result = ui.row(static_cast<int>(labels.size()), height, gap);
		for (size_t i = 0; i < labels.size(); ++i) {
			const bool sel = selectedIndex && *selectedIndex == static_cast<int>(i);
			m_screen.drawRect(result.cells[i], sel ? style.backgroundSelected : style.background);
			if (style.borderWidth > 0.0f) {
				m_screen.drawRectFrame(result.cells[i], style.border, style.borderWidth);
			}
			const auto ts = m_screen.measureText(labels[i], style.fontSize);
			m_screen.drawText(
				sgc::Vec2f{result.cells[i].x() + (result.cells[i].width() - ts.x) * 0.5f,
				            result.cells[i].y() + (result.cells[i].height() - ts.y) * 0.5f},
				labels[i], style.textColor, style.fontSize);
		}
	}

	// ── panel grid ────────────────────────────────────

	/// @brief cell ごとにカスタム描画する panel の grid を描画する。
	void drawPanelGrid(int rows, int cols, float gap,
	                   std::function<void(Screen&, sgc::Rectf, int, int)> drawCell) {
		UIBuilder ui(m_screenW, m_screenH);
		ui.padding(16.0f);
		const auto result = ui.grid(rows, cols, gap);
		for (int r = 0; r < rows; ++r) {
			for (int c = 0; c < cols; ++c) {
				drawCell(m_screen,
				         result.cells[static_cast<size_t>(r)][static_cast<size_t>(c)],
				         r, c);
			}
		}
	}

	// ── scroll list ───────────────────────────────────

	/// @brief text item の縦 scroll list を描画する。
	void drawScrollList(const std::vector<std::string>& items,
	                    float x, float y, float w, float h,
	                    int* scrollPos, int* selectedIndex) {
		constexpr float itemH    = 28.0f;
		constexpr float fontSize = 14.0f;
		const int visible  = std::max(1, static_cast<int>(h / itemH));
		const int maxScr   = std::max(0, static_cast<int>(items.size()) - visible);
		if (scrollPos) { *scrollPos = std::clamp(*scrollPos, 0, maxScr); }
		const int offset = scrollPos ? *scrollPos : 0;

		m_screen.drawRect(sgc::Rectf{x, y, w, h},
		                  sgc::Colorf{0.1f, 0.1f, 0.12f, 0.9f});
		for (int i = 0; i < visible && (offset + i) < static_cast<int>(items.size()); ++i) {
			const int idx     = offset + i;
			const float iy    = y + static_cast<float>(i) * itemH;
			const bool sel    = selectedIndex && *selectedIndex == idx;
			if (sel) {
				m_screen.drawRect(sgc::Rectf{x, iy, w, itemH},
				                  sgc::Colorf{0.25f, 0.35f, 0.6f, 0.8f});
			}
			m_screen.drawText(sgc::Vec2f{x + 8.0f, iy + 6.0f},
			                  items[static_cast<size_t>(idx)],
			                  sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f}, fontSize);
		}
		if (static_cast<int>(items.size()) > visible) {
			const float ratio  = static_cast<float>(offset) / static_cast<float>(maxScr);
			const float thumbH = std::max(20.0f, h * static_cast<float>(visible) /
			                                      static_cast<float>(items.size()));
			m_screen.drawRect(sgc::Rectf{x + w - 6.0f, y + ratio * (h - thumbH), 4.0f, thumbH},
			                  sgc::Colorf{0.4f, 0.4f, 0.5f, 0.7f});
		}
	}

	// ── label 付き field ─────────────────────────────────

	/// @brief label 付き field を描画する: "Label: [value]"。
	void drawLabeledField(float x, float y, float labelW, float fieldW, float height,
	                      std::string_view label, std::string_view value) {
		const float fontSize = std::min(height * 0.6f, 14.0f);
		const float textY    = y + (height - m_screen.measureText(label, fontSize).y) * 0.5f;
		m_screen.drawText(sgc::Vec2f{x, textY}, label,
		                  sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f}, fontSize);
		const sgc::Rectf field{x + labelW, y, fieldW, height};
		m_screen.drawRect(field, sgc::Colorf{0.18f, 0.18f, 0.22f, 1.0f});
		m_screen.drawRectFrame(field, sgc::Colorf{0.3f, 0.3f, 0.35f, 1.0f});
		m_screen.drawText(sgc::Vec2f{field.x() + 6.0f, textY}, value,
		                  sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}, fontSize);
	}

	// ── auto-layout form ──────────────────────────────

	/// @brief label-value row を並べた縦 form を描画する。
	void drawForm(const sgc::Rectf& area, const std::vector<FormField>& fields,
	              float rowHeight = 30.0f) {
		constexpr float gap = 4.0f;
		const float labelW  = area.width() * 0.35f;
		const float fieldW  = area.width() * 0.6f;
		float y = area.y();
		for (const auto& f : fields) {
			if (y + rowHeight > area.y() + area.height()) { break; }
			drawLabeledField(area.x(), y, labelW, fieldW, rowHeight, f.label, f.value);
			y += rowHeight + gap;
		}
	}

	// ── status bar ────────────────────────────────────

	/// @brief 下部に key-value item を並べた status bar を描画する。
	void drawStatusBar(const std::map<std::string, std::string>& items,
	                   float height = 24.0f) {
		const sgc::Rectf bar{0.0f, m_screenH - height, m_screenW, height};
		m_screen.drawRect(bar, sgc::Colorf{0.1f, 0.1f, 0.12f, 1.0f});
		const float fontSize = std::min(height * 0.6f, 12.0f);
		float x = 8.0f;
		for (const auto& [key, val] : items) {
			const std::string text = key + ": " + val;
			m_screen.drawText(sgc::Vec2f{x, bar.y() + 4.0f}, text,
			                  sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f}, fontSize);
			x += m_screen.measureText(text, fontSize).x + 24.0f;
		}
	}

	// ── progress bar ──────────────────────────────────

	/// @brief 指定 rect 内に progress bar を描画する。
	void drawProgressBar(const sgc::Rectf& rect, float progress,
	                     std::string_view label = "") {
		const float p = std::clamp(progress, 0.0f, 1.0f);
		m_screen.drawRect(rect, sgc::Colorf{0.15f, 0.15f, 0.18f, 1.0f});
		m_screen.drawRect(sgc::Rectf{rect.x(), rect.y(), rect.width() * p, rect.height()},
		                  sgc::Colorf{0.2f, 0.6f, 1.0f, 1.0f});
		m_screen.drawRectFrame(rect, sgc::Colorf{0.3f, 0.3f, 0.35f, 1.0f});
		if (!label.empty()) {
			const float fs = std::min(rect.height() * 0.7f, 14.0f);
			const auto ts = m_screen.measureText(label, fs);
			m_screen.drawText(
				sgc::Vec2f{rect.x() + (rect.width() - ts.x) * 0.5f,
				            rect.y() + (rect.height() - ts.y) * 0.5f},
				label, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}, fs);
		}
	}

	// ── tab bar ───────────────────────────────────────

	/// @brief tab bar を描画し、その下の content 領域を返す。
	[[nodiscard]] sgc::Rectf drawTabBar(const std::vector<TabDef>& tabs, int* activeTab,
	                                     float y, float height = 32.0f) {
		if (tabs.empty()) {
			return sgc::Rectf{0.0f, y + height, m_screenW, m_screenH - y - height};
		}
		const float tabW   = m_screenW / static_cast<float>(tabs.size());
		const float fs     = std::min(height * 0.5f, 14.0f);
		const int active   = activeTab ? *activeTab : 0;
		for (size_t i = 0; i < tabs.size(); ++i) {
			const float tx = tabW * static_cast<float>(i);
			const bool act = static_cast<int>(i) == active;
			m_screen.drawRect(sgc::Rectf{tx, y, tabW, height},
				act ? sgc::Colorf{0.2f, 0.25f, 0.35f, 1.0f}
				    : sgc::Colorf{0.12f, 0.12f, 0.15f, 1.0f});
			if (act) {
				m_screen.drawRect(sgc::Rectf{tx, y + height - 2.0f, tabW, 2.0f},
				                  sgc::Colorf{0.3f, 0.6f, 1.0f, 1.0f});
			}
			const auto ts = m_screen.measureText(tabs[i].label, fs);
			m_screen.drawText(
				sgc::Vec2f{tx + (tabW - ts.x) * 0.5f, y + (height - ts.y) * 0.5f},
				tabs[i].label, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}, fs);
		}
		return sgc::Rectf{0.0f, y + height, m_screenW, m_screenH - y - height};
	}

private:
	Screen& m_screen;
	float   m_screenW = 0.0f;
	float   m_screenH = 0.0f;
};

} // namespace mitiru::ui
