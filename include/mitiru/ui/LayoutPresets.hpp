#pragma once

/// @file LayoutPresets.hpp
/// @brief よくある画面構成のための定義済み layout 計算。
///
/// 各 nested struct は static な `compute(screenW, screenH, ...)` を提供し、
/// 値を埋めた layout descriptor を返す。consumer は rect を読むだけでよい。
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

/// @brief 標準的なメニュー画面の layout (title, list area, footer)。
struct MenuLayout
{
	sgc::Rectf title;       ///< タイトル文字領域 (中央寄せ・上部)。
	sgc::Rectf hint;        ///< ヒント文字領域 (title の下)。
	sgc::Rectf listArea;    ///< スクロール可能なリスト領域。
	sgc::Rectf pageInfo;    ///< ページカウンタ (右上)。
	sgc::Rectf footer;      ///< 最下部の footer bar。
	sgc::Rectf scrollTrack; ///< Scrollbar track (listArea の右)。
	float marginX   = 0.0f; ///< 水平マージン。
	float headerH   = 0.0f; ///< ヘッダ高さ (title + hint)。
	float footerH   = 0.0f; ///< footer 高さ。
	float listWidth  = 0.0f;
	float listHeight = 0.0f;

	/// @brief 指定された画面サイズに対する layout を計算する。
	/// @param screenW 画面幅 (pixel)。
	/// @param screenH 画面高さ (pixel)。
	/// @param margin  水平マージン (default 60)。
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

/// @brief Visual-novel 画面の layout (message window, name plate, choices, characters)。
struct VNLayout
{
	sgc::Rectf messageWindow;    ///< メインの message box。
	sgc::Rectf textArea;         ///< message window 内部の文字領域 (padding あり)。
	sgc::Rectf namePlate;        ///< 話者の name plate (message window の上)。
	sgc::Rectf autoIndicator;    ///< AUTO mode の pill (message window の上・右)。
	sgc::Rectf waitIndicator;    ///< クリック待ちアイコンの位置。
	sgc::Rectf choiceArea;       ///< 選択肢ボタンの領域。
	sgc::Rectf backlogPanel;     ///< 全画面の backlog overlay。
	float characterLeft   = 0.0f; ///< 左キャラの X 比率。
	float characterCenter = 0.0f; ///< 中央キャラの X 比率。
	float characterRight  = 0.0f; ///< 右キャラの X 比率。
	float characterBottomY = 0.0f; ///< キャラが立つ Y (message window の上)。

	[[nodiscard]] static VNLayout compute(float screenW, float screenH) noexcept
	{
		VNLayout l;

		// Message window: 高さ 140px、下から 10px、左右 20px マージン。
		constexpr float winH = 140.0f;
		constexpr float winMarginBottom = 10.0f;
		constexpr float winMarginX = 20.0f;
		const float winY = screenH - winH - winMarginBottom;
		const float winW = screenW - winMarginX * 2.0f;

		l.messageWindow = {winMarginX, winY, winW, winH};
		l.textArea      = {winMarginX + 24.0f, winY + 20.0f, winW - 48.0f, winH - 40.0f};

		// Name plate: message window の上。
		l.namePlate = {winMarginX + 16.0f, winY - 30.0f, winW * 0.4f, 28.0f};

		// AUTO indicator: message window の上・右側。
		l.autoIndicator = {winMarginX + winW - 82.0f, winY - 30.0f, 64.0f, 24.0f};

		// Wait indicator: message window の右下。
		l.waitIndicator = {winMarginX + winW - 22.0f, winY + winH - 16.0f, 8.0f, 8.0f};

		// Choices: 中央寄せ、画面高さの 25% から開始。
		l.choiceArea = {(screenW - 400.0f) * 0.5f, screenH * 0.25f, 400.0f, screenH * 0.4f};

		// Backlog: 10px マージンの全画面。
		l.backlogPanel = {10.0f, 10.0f, screenW - 20.0f, screenH - 20.0f};

		// キャラ位置 (X 比率。screenW を掛けて中心 X を得る)。
		l.characterLeft   = 0.25f;
		l.characterCenter = 0.50f;
		l.characterRight  = 0.75f;
		l.characterBottomY = winY - 10.0f; // message window の少し上

		return l;
	}
};

// ─── Two-Column Layout ───────────────────────────────────────────────────

/// @brief widget demo 用の 2 カラムショーケース layout。
struct TwoColumnLayout
{
	sgc::Rectf header;   ///< 全幅の header bar。
	sgc::Rectf left;     ///< 左カラムのコンテンツ領域。
	sgc::Rectf right;    ///< 右カラムのコンテンツ領域。
	float contentY = 0.0f; ///< コンテンツの開始 Y (header の下)。

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

/// @brief ゲームの HUD layout (top bar, bottom bar, minimap, dialogue, center)。
struct HUDLayout
{
	sgc::Rectf topBar;     ///< 上部の HP / MP / score bar。
	sgc::Rectf bottomBar;  ///< 下部の Actions / items bar。
	sgc::Rectf minimap;    ///< minimap 領域 (右上隅)。
	sgc::Rectf dialogue;   ///< dialogue 文字領域 (下部・bottomBar の上)。
	sgc::Rectf center;     ///< 残りのゲーム viewport 領域。

	/// @brief 指定された画面サイズに対する HUD layout を計算する。
	/// @param screenW  画面幅。
	/// @param screenH  画面高さ。
	/// @param topH     top bar の高さ。
	/// @param bottomH  bottom bar の高さ。
	/// @param minimapSize minimap の一辺の長さ。
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

		// Dialogue: 水平方向に中央寄せ、bottom bar の上。
		constexpr float dialogueH = 100.0f;
		const float dialogueW     = screenW * 0.7f;
		l.dialogue = {(screenW - dialogueW) * 0.5f,
		              screenH - bottomH - dialogueH - margin,
		              dialogueW, dialogueH};

		// Center: top bar と bottom bar の間すべて。minimap カラムは除く。
		l.center = {0.0f, topH, screenW - minimapSize - margin * 2.0f,
		            screenH - topH - bottomH};

		return l;
	}
};

// ─── Split Layout (editor-like) ──────────────────────────────────────────

/// @brief editor 風の split panel layout (left sidebar, right panel, top, bottom, center)。
struct SplitLayout
{
	sgc::Rectf left;    ///< 左 sidebar。
	sgc::Rectf right;   ///< 右 panel。
	sgc::Rectf top;     ///< 上部の toolbar / menu bar。
	sgc::Rectf bottom;  ///< 下部の status bar。
	sgc::Rectf center;  ///< メインのコンテンツ領域。

	/// @brief editor 風の split layout を計算する。
	/// @param screenW  画面幅。
	/// @param screenH  画面高さ。
	/// @param leftW    左 sidebar の幅。
	/// @param rightW   右 panel の幅。
	/// @param topH     top bar の高さ。
	/// @param bottomH  bottom bar の高さ。
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

/// @brief Dashboard layout: 等高 card の responsive な grid。
struct DashboardLayout
{
	std::vector<sgc::Rectf> cards; ///< card の rect 群 (行優先順)。

	/// @brief dashboard の card 位置を計算する。
	/// @param screenW    画面幅。
	/// @param screenH    画面高さ。
	/// @param cols       カラム数。
	/// @param cardHeight 各 card の高さ。
	/// @param gap        card 間の隙間。
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
