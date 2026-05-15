#pragma once

/// @file LayoutPresets.hpp
/// @brief Pre-defined layout computations for common screen arrangements.
///
/// Each nested struct provides a static `compute(screenW, screenH, ...)` method
/// that returns a filled-in layout descriptor. Consumers just read the rects.
///
/// @code
///   auto hud = HUDLayout::compute(1280, 720);
///   screen.drawRect(hud.topBar, barColor);
///   screen.drawRect(hud.minimap, mapColor);
/// @endcode

#include <sgc/math/Rect.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mitiru::ui::LayoutPresets {

// ─── Menu Layout ──────────────────────────────────────────────────────────

/// @brief Standard menu screen layout (title, list area, footer).
struct MenuLayout
{
	sgc::Rectf title;       ///< Title text area (centered, top).
	sgc::Rectf hint;        ///< Hint text area (below title).
	sgc::Rectf listArea;    ///< Scrollable list region.
	sgc::Rectf pageInfo;    ///< Page counter (top-right).
	sgc::Rectf footer;      ///< Footer bar at bottom.
	sgc::Rectf scrollTrack; ///< Scrollbar track (right of listArea).
	float marginX   = 0.0f; ///< Horizontal margin.
	float headerH   = 0.0f; ///< Header height (title + hint).
	float footerH   = 0.0f; ///< Footer height.
	float listWidth  = 0.0f;
	float listHeight = 0.0f;

	/// @brief Compute layout for the given screen size.
	/// @param screenW Screen width in pixels.
	/// @param screenH Screen height in pixels.
	/// @param margin  Horizontal margin (default 60).
	[[nodiscard]] static MenuLayout compute(
		float screenW, float screenH, float margin = 60.0f) noexcept
	{
		MenuLayout l;
		l.marginX  = margin;
		l.headerH  = 120.0f;
		l.footerH  = 50.0f;
		l.listWidth  = screenW - margin * 2.0f;
		l.listHeight = screenH - l.headerH - l.footerH;

		l.title    = {0.0f, 0.0f, screenW, 72.0f};
		l.hint     = {0.0f, 72.0f, screenW, 40.0f};
		l.listArea = {margin, l.headerH, l.listWidth, l.listHeight};
		l.pageInfo = {screenW - 160.0f, 82.0f, 140.0f, 18.0f};
		l.footer   = {margin, screenH - l.footerH, l.listWidth, l.footerH};

		constexpr float scrollBarW = 4.0f;
		l.scrollTrack = {screenW - margin + 16.0f, l.headerH, scrollBarW, l.listHeight};

		return l;
	}
};

// ─── VN Layout ────────────────────────────────────────────────────────────

/// @brief Visual-novel screen layout (message window, name plate, choices, characters).
struct VNLayout
{
	sgc::Rectf messageWindow;    ///< Main message box.
	sgc::Rectf textArea;         ///< Text region inside message window (with padding).
	sgc::Rectf namePlate;        ///< Speaker name plate (above message window).
	sgc::Rectf autoIndicator;    ///< AUTO mode pill (above message window, right).
	sgc::Rectf waitIndicator;    ///< Click-wait icon position.
	sgc::Rectf choiceArea;       ///< Region for choice buttons.
	sgc::Rectf backlogPanel;     ///< Full-screen backlog overlay.
	float characterLeft   = 0.0f; ///< X ratio for left character.
	float characterCenter = 0.0f; ///< X ratio for center character.
	float characterRight  = 0.0f; ///< X ratio for right character.
	float characterBottomY = 0.0f; ///< Y where characters stand (above message window).

	[[nodiscard]] static VNLayout compute(float screenW, float screenH) noexcept
	{
		VNLayout l;

		// Message window: 140px tall, 10px from bottom, 20px side margins.
		constexpr float winH = 140.0f;
		constexpr float winMarginBottom = 10.0f;
		constexpr float winMarginX = 20.0f;
		const float winY = screenH - winH - winMarginBottom;
		const float winW = screenW - winMarginX * 2.0f;

		l.messageWindow = {winMarginX, winY, winW, winH};
		l.textArea      = {winMarginX + 24.0f, winY + 20.0f, winW - 48.0f, winH - 40.0f};

		// Name plate: above the message window.
		l.namePlate = {winMarginX + 16.0f, winY - 30.0f, winW * 0.4f, 28.0f};

		// AUTO indicator: above message window, right side.
		l.autoIndicator = {winMarginX + winW - 82.0f, winY - 30.0f, 64.0f, 24.0f};

		// Wait indicator: bottom-right of message window.
		l.waitIndicator = {winMarginX + winW - 22.0f, winY + winH - 16.0f, 8.0f, 8.0f};

		// Choices: centered, starting at 25% of screen height.
		l.choiceArea = {(screenW - 400.0f) * 0.5f, screenH * 0.25f, 400.0f, screenH * 0.4f};

		// Backlog: full-screen with 10px margin.
		l.backlogPanel = {10.0f, 10.0f, screenW - 20.0f, screenH - 20.0f};

		// Character positions (X ratios, multiplied by screenW to get center X).
		l.characterLeft   = 0.25f;
		l.characterCenter = 0.50f;
		l.characterRight  = 0.75f;
		l.characterBottomY = winY - 10.0f; // slightly above message window

		return l;
	}
};

// ─── Two-Column Layout ───────────────────────────────────────────────────

/// @brief Two-column showcase layout for widget demos.
struct TwoColumnLayout
{
	sgc::Rectf header;   ///< Full-width header bar.
	sgc::Rectf left;     ///< Left column content area.
	sgc::Rectf right;    ///< Right column content area.
	float contentY = 0.0f; ///< Y where content begins (below header).

	[[nodiscard]] static TwoColumnLayout compute(
		float screenW, float screenH, float headerH = 36.0f,
		float padding = 30.0f, float gap = 20.0f) noexcept
	{
		TwoColumnLayout l;
		l.header   = {0.0f, 0.0f, screenW, headerH};
		l.contentY = headerH + 10.0f;

		const float contentW = screenW - padding * 2.0f;
		const float colW = (contentW - gap) * 0.5f;
		l.left  = {padding, l.contentY, colW, screenH - l.contentY - padding};
		l.right = {padding + colW + gap, l.contentY, colW, screenH - l.contentY - padding};

		return l;
	}
};

// ─── HUD Layout ───────────────────────────────────────────────────────────

/// @brief Game HUD layout (top bar, bottom bar, minimap, dialogue, center).
struct HUDLayout
{
	sgc::Rectf topBar;     ///< HP / MP / score bar at top.
	sgc::Rectf bottomBar;  ///< Actions / items bar at bottom.
	sgc::Rectf minimap;    ///< Minimap area (top-right corner).
	sgc::Rectf dialogue;   ///< Dialogue text area (bottom, above bottomBar).
	sgc::Rectf center;     ///< Remaining game viewport area.

	/// @brief Compute HUD layout for the given screen size.
	/// @param screenW  Screen width.
	/// @param screenH  Screen height.
	/// @param topH     Top bar height.
	/// @param bottomH  Bottom bar height.
	/// @param minimapSize Minimap side length.
	[[nodiscard]] static HUDLayout compute(
		float screenW, float screenH,
		float topH = 40.0f, float bottomH = 120.0f,
		float minimapSize = 150.0f) noexcept
	{
		HUDLayout l;
		constexpr float margin = 8.0f;

		l.topBar    = {0.0f, 0.0f, screenW, topH};
		l.bottomBar = {0.0f, screenH - bottomH, screenW, bottomH};
		l.minimap   = {screenW - minimapSize - margin, topH + margin,
		               minimapSize, minimapSize};

		// Dialogue: centered horizontally, above the bottom bar.
		constexpr float dialogueH = 100.0f;
		const float dialogueW     = screenW * 0.7f;
		l.dialogue = {(screenW - dialogueW) * 0.5f,
		              screenH - bottomH - dialogueH - margin,
		              dialogueW, dialogueH};

		// Center: everything between top and bottom bars, excluding minimap column.
		l.center = {0.0f, topH, screenW - minimapSize - margin * 2.0f,
		            screenH - topH - bottomH};

		return l;
	}
};

// ─── Split Layout (editor-like) ──────────────────────────────────────────

/// @brief Editor-like split panel layout (left sidebar, right panel, top, bottom, center).
struct SplitLayout
{
	sgc::Rectf left;    ///< Left sidebar.
	sgc::Rectf right;   ///< Right panel.
	sgc::Rectf top;     ///< Top toolbar / menu bar.
	sgc::Rectf bottom;  ///< Bottom status bar.
	sgc::Rectf center;  ///< Main content area.

	/// @brief Compute editor-like split layout.
	/// @param screenW  Screen width.
	/// @param screenH  Screen height.
	/// @param leftW    Left sidebar width.
	/// @param rightW   Right panel width.
	/// @param topH     Top bar height.
	/// @param bottomH  Bottom bar height.
	[[nodiscard]] static SplitLayout compute(
		float screenW, float screenH,
		float leftW = 250.0f, float rightW = 300.0f,
		float topH = 40.0f, float bottomH = 30.0f) noexcept
	{
		SplitLayout l;
		l.top    = {0.0f, 0.0f, screenW, topH};
		l.bottom = {0.0f, screenH - bottomH, screenW, bottomH};
		l.left   = {0.0f, topH, leftW, screenH - topH - bottomH};
		l.right  = {screenW - rightW, topH, rightW, screenH - topH - bottomH};
		l.center = {leftW, topH,
		            std::max(0.0f, screenW - leftW - rightW),
		            screenH - topH - bottomH};
		return l;
	}
};

// ─── Dashboard Layout (grid of cards) ────────────────────────────────────

/// @brief Dashboard layout: a responsive grid of equal-height cards.
struct DashboardLayout
{
	std::vector<sgc::Rectf> cards; ///< Card rects, row-major order.

	/// @brief Compute dashboard card positions.
	/// @param screenW    Screen width.
	/// @param screenH    Screen height.
	/// @param cols       Number of columns.
	/// @param cardHeight Height of each card.
	/// @param gap        Gap between cards.
	[[nodiscard]] static DashboardLayout compute(
		float screenW, float screenH,
		int cols, float cardHeight, float gap = 16.0f) noexcept
	{
		DashboardLayout l;
		if (cols <= 0) { return l; }

		constexpr float padding = 16.0f;
		const float totalW = screenW - padding * 2.0f;
		const float totalH = screenH - padding * 2.0f;
		const float colGap = gap * static_cast<float>(cols - 1);
		const float cardW  = std::max(0.0f, (totalW - colGap) / static_cast<float>(cols));
		const int rows     = std::max(1, static_cast<int>((totalH + gap) / (cardHeight + gap)));

		l.cards.reserve(static_cast<std::size_t>(rows * cols));
		for (int r = 0; r < rows; ++r) {
			for (int c = 0; c < cols; ++c) {
				const float x = padding + static_cast<float>(c) * (cardW + gap);
				const float y = padding + static_cast<float>(r) * (cardHeight + gap);
				if (y + cardHeight > screenH - padding) { break; }
				l.cards.push_back(sgc::Rectf{x, y, cardW, cardHeight});
			}
		}
		return l;
	}
};

} // namespace mitiru::ui::LayoutPresets
