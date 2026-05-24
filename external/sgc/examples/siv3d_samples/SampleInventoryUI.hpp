#pragma once

/// @file SampleInventoryUI.hpp
/// @brief インベントリグリッドUIのデモシーン
///
/// 6列x4行のアイテムグリッドを表示し、スクロールバーでオーバーフローに対応する。
/// - 各スロット: 60x60の矩形にアイテムカラーアイコンと枠線
/// - バッジ: 右上に個数表示（evaluateBadge使用）
/// - ツールチップ: ホバー時にアイテム名と説明を表示（evaluateTooltip使用）
/// - スクロールバー: 30アイテム中24が可視、残りはスクロールでアクセス
/// - ESC: メニューに戻る

#include <algorithm>
#include <array>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/Badge.hpp"
#include "sgc/ui/Panel.hpp"
#include "sgc/ui/Scrollbar.hpp"
#include "sgc/ui/Tooltip.hpp"
#include "sgc/ui/WidgetState.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief インベントリグリッドUIのデモシーン
///
/// 30種のアイテムを6列グリッドで表示し、ホバーでツールチップ、
/// バッジで個数、スクロールバーでオーバーフロー分を閲覧できる。
class SampleInventoryUI : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_hoveredSlot = -1;
		m_scrollPos = 0.0f;
		m_scrollDragging = false;
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto& input = *getData().inputProvider;

		// ESCでメニューに戻る
		if (input.isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		const auto mousePos = input.mousePosition();
		const bool mouseDown = input.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		// ── スクロールバー評価 ──
		const auto trackRect = scrollbarTrackRect();
		auto sbResult = sgc::ui::evaluateScrollbar(
			trackRect, mousePos, mouseDown, mousePressed,
			m_scrollPos, contentHeight(), GRID_VIEWPORT_H, m_scrollDragging);
		if (sbResult.changed)
		{
			m_scrollPos = sbResult.scrollPos;
		}
		m_scrollDragging = sbResult.dragging;
		m_thumbRect = sbResult.thumbRect;

		// ── ホバースロット判定 ──
		m_hoveredSlot = -1;
		for (int i = 0; i < ITEM_COUNT; ++i)
		{
			const int row = i / COLS;
			const int col = i % COLS;
			const float slotY = GRID_Y + static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP) - m_scrollPos;

			// ビューポート外はスキップ
			if (slotY + SLOT_SIZE < GRID_Y || slotY > GRID_Y + GRID_VIEWPORT_H)
			{
				continue;
			}

			const float slotX = GRID_X + static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
			const sgc::Rectf slotRect{slotX, slotY, SLOT_SIZE, SLOT_SIZE};
			if (sgc::ui::isMouseOver(mousePos, slotRect))
			{
				m_hoveredSlot = i;
				break;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.10f});

		// ── タイトル ──
		t->drawTextCentered(
			"Inventory UI Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── パネル ──
		const sgc::Rectf panelOuter{PANEL_X, PANEL_Y, PANEL_W, PANEL_H};
		const auto panel = sgc::ui::evaluatePanel(
			panelOuter, TITLE_HEIGHT, sgc::ui::Padding::uniform(8.0f));

		// パネル背景
		const sgc::AABB2f panelBg{
			{panelOuter.x(), panelOuter.y()},
			{panelOuter.right(), panelOuter.bottom()}};
		r->drawRect(panelBg, sgc::Colorf{0.12f, 0.12f, 0.16f, 0.95f});
		r->drawRectFrame(panelBg, 1.0f, sgc::Colorf{0.3f, 0.5f, 0.8f});

		// タイトルバー
		const sgc::AABB2f titleBg{
			{panel.titleBounds.x(), panel.titleBounds.y()},
			{panel.titleBounds.right(), panel.titleBounds.bottom()}};
		r->drawRect(titleBg, sgc::Colorf{0.15f, 0.2f, 0.35f});
		t->drawTextCentered(
			"Inventory", 20.0f,
			panel.titleBounds.center(), sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── グリッド描画 ──
		for (int i = 0; i < ITEM_COUNT; ++i)
		{
			const int row = i / COLS;
			const int col = i % COLS;
			const float slotY = GRID_Y + static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP) - m_scrollPos;

			// ビューポート外はスキップ
			if (slotY + SLOT_SIZE < GRID_Y || slotY > GRID_Y + GRID_VIEWPORT_H)
			{
				continue;
			}

			// 部分的にはみ出すスロットもスキップ（クリッピング簡易対応）
			if (slotY < GRID_Y || slotY + SLOT_SIZE > GRID_Y + GRID_VIEWPORT_H)
			{
				continue;
			}

			const float slotX = GRID_X + static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
			drawSlot(r, t, i, slotX, slotY);
		}

		// ── スクロールバー描画 ──
		drawScrollbar(r);

		// ── ツールチップ描画（最前面） ──
		if (m_hoveredSlot >= 0 && m_hoveredSlot < ITEM_COUNT)
		{
			drawTooltip(r, t, sw, sh);
		}

		// ── 操作ヒント ──
		t->drawText(
			"[Esc] Back to Menu", 14.0f,
			sgc::Vec2f{20.0f, sh - 30.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});
	}

private:
	/// @brief アイテム情報
	struct Item
	{
		const char* name;   ///< アイテム名
		const char* desc;   ///< 説明文
		sgc::Colorf color;  ///< アイコン色
		int count;           ///< 所持数
	};

	/// @brief 定義済みアイテム一覧（30種）
	// clang-format off
	static constexpr std::array<Item, 30> ITEMS{{
		{"Sword",    "A sharp steel blade",    {0.7f, 0.7f, 0.8f},  1},
		{"Shield",   "Blocks incoming attacks", {0.5f, 0.4f, 0.3f},  1},
		{"Potion",   "Restores 50 HP",          {0.9f, 0.2f, 0.3f},  5},
		{"Arrow",    "Ammunition for bows",     {0.6f, 0.5f, 0.3f}, 20},
		{"Gem",      "A sparkling gemstone",    {0.3f, 0.8f, 0.9f},  3},
		{"Scroll",   "Contains ancient magic",  {0.9f, 0.85f, 0.6f}, 2},
		{"Ring",     "Grants +5 defense",       {0.9f, 0.8f, 0.2f},  1},
		{"Armor",    "Heavy plate protection",  {0.5f, 0.5f, 0.6f},  1},
		{"Helmet",   "Protects the head",       {0.55f, 0.55f, 0.6f}, 1},
		{"Boots",    "Faster movement",         {0.4f, 0.3f, 0.25f}, 1},
		{"Gloves",   "Grip gauntlets",          {0.45f, 0.35f, 0.3f}, 1},
		{"Cloak",    "Stealth bonus",           {0.2f, 0.15f, 0.3f}, 1},
		{"Staff",    "Magical energy channel",  {0.5f, 0.3f, 0.7f},  1},
		{"Wand",     "Lightweight casting tool", {0.7f, 0.4f, 0.9f}, 1},
		{"Bow",      "Long-range weapon",       {0.45f, 0.35f, 0.2f}, 1},
		{"Axe",      "Heavy chopping weapon",   {0.6f, 0.4f, 0.3f},  1},
		{"Dagger",   "Quick stabbing weapon",   {0.65f, 0.65f, 0.7f}, 2},
		{"Hammer",   "Crushing blunt weapon",   {0.5f, 0.45f, 0.4f}, 1},
		{"Lantern",  "Illuminates dark areas",  {0.9f, 0.8f, 0.3f},  1},
		{"Map",      "Reveals nearby terrain",  {0.8f, 0.75f, 0.6f}, 1},
		{"Compass",  "Points north always",     {0.4f, 0.6f, 0.5f},  1},
		{"Key",      "Opens locked doors",      {0.85f, 0.75f, 0.2f}, 3},
		{"Coin",     "Standard gold currency",  {0.9f, 0.85f, 0.1f}, 99},
		{"Crystal",  "Glows with inner light",  {0.6f, 0.9f, 0.95f}, 7},
		{"Feather",  "Light, used in crafts",   {0.9f, 0.9f, 0.85f}, 12},
		{"Bone",     "Crafting material",       {0.8f, 0.8f, 0.75f}, 8},
		{"Herb",     "Used for alchemy",        {0.3f, 0.7f, 0.3f}, 15},
		{"Mushroom", "Edible fungi",            {0.7f, 0.3f, 0.3f},  6},
		{"Ore",      "Raw metal for smithing",  {0.5f, 0.5f, 0.55f}, 10},
		{"Wood",     "Basic building material", {0.55f, 0.4f, 0.2f}, 25},
	}};
	// clang-format on

	// ── レイアウト定数 ──
	static constexpr int ITEM_COUNT = 30;
	static constexpr int COLS = 6;
	static constexpr int VISIBLE_ROWS = 4;
	static constexpr float SLOT_SIZE = 60.0f;
	static constexpr float SLOT_GAP = 6.0f;
	static constexpr float ICON_MARGIN = 6.0f;
	static constexpr float TITLE_HEIGHT = 36.0f;
	static constexpr float PANEL_X = 100.0f;
	static constexpr float PANEL_Y = 70.0f;
	static constexpr float SCROLLBAR_WIDTH = 16.0f;
	static constexpr float GRID_W = static_cast<float>(COLS) * (SLOT_SIZE + SLOT_GAP) - SLOT_GAP;
	static constexpr float GRID_VIEWPORT_H = static_cast<float>(VISIBLE_ROWS) * (SLOT_SIZE + SLOT_GAP) - SLOT_GAP;
	static constexpr float PANEL_W = GRID_W + SCROLLBAR_WIDTH + 36.0f;
	static constexpr float PANEL_H = TITLE_HEIGHT + GRID_VIEWPORT_H + 28.0f;
	static constexpr float GRID_X = PANEL_X + 10.0f;
	static constexpr float GRID_Y = PANEL_Y + TITLE_HEIGHT + 10.0f;

	/// @brief コンテンツ全体の高さ（全行分）
	[[nodiscard]] static constexpr float contentHeight() noexcept
	{
		const int totalRows = (ITEM_COUNT + COLS - 1) / COLS;
		return static_cast<float>(totalRows) * (SLOT_SIZE + SLOT_GAP) - SLOT_GAP;
	}

	int m_hoveredSlot{-1};
	float m_scrollPos{0.0f};
	bool m_scrollDragging{false};
	sgc::Rectf m_thumbRect{};

	/// @brief スクロールバートラック矩形
	[[nodiscard]] static constexpr sgc::Rectf scrollbarTrackRect() noexcept
	{
		return {GRID_X + GRID_W + 4.0f, GRID_Y, SCROLLBAR_WIDTH, GRID_VIEWPORT_H};
	}

	/// @brief 単一スロットを描画する
	void drawSlot(sgc::IRenderer* r, sgc::ITextRenderer* t,
		int index, float slotX, float slotY) const
	{
		const auto& item = ITEMS[static_cast<std::size_t>(index)];
		const bool hov = (index == m_hoveredSlot);
		const sgc::AABB2f slotBg{{slotX, slotY}, {slotX + SLOT_SIZE, slotY + SLOT_SIZE}};

		r->drawRect(slotBg, hov ? sgc::Colorf{0.22f, 0.25f, 0.35f} : sgc::Colorf{0.15f, 0.15f, 0.2f});
		r->drawRectFrame(slotBg, hov ? 2.0f : 1.0f,
			hov ? sgc::Colorf{0.4f, 0.6f, 1.0f} : sgc::Colorf{0.3f, 0.3f, 0.4f});

		// アイテムアイコン（内側の色付き矩形）
		const sgc::AABB2f iconBg{
			{slotX + ICON_MARGIN, slotY + ICON_MARGIN},
			{slotX + SLOT_SIZE - ICON_MARGIN, slotY + SLOT_SIZE - ICON_MARGIN}};
		r->drawRect(iconBg, item.color);

		// バッジ（右上に個数表示）
		if (item.count > 1)
		{
			const sgc::Rectf slotRect{slotX, slotY, SLOT_SIZE, SLOT_SIZE};
			const auto badgeRect = sgc::ui::evaluateBadge(
				slotRect, sgc::ui::Anchor::TopRight, {20.0f, 16.0f}, {-2.0f, 2.0f});
			const sgc::AABB2f badgeBg{{badgeRect.x(), badgeRect.y()}, {badgeRect.right(), badgeRect.bottom()}};
			r->drawRect(badgeBg, sgc::Colorf{0.8f, 0.2f, 0.2f, 0.9f});
			t->drawTextCentered(std::to_string(item.count), 10.0f, badgeRect.center(), sgc::Colorf::white());
		}
	}

	/// @brief スクロールバーを描画する
	void drawScrollbar(sgc::IRenderer* r) const
	{
		const auto track = scrollbarTrackRect();
		const sgc::AABB2f trackBg{{track.x(), track.y()}, {track.right(), track.bottom()}};
		r->drawRect(trackBg, sgc::Colorf{0.1f, 0.1f, 0.14f});
		r->drawRectFrame(trackBg, 1.0f, sgc::Colorf{0.25f, 0.25f, 0.35f});

		if (sgc::ui::isScrollNeeded(contentHeight(), GRID_VIEWPORT_H))
		{
			const auto tc = m_scrollDragging ? sgc::Colorf{0.5f, 0.6f, 0.9f} : sgc::Colorf{0.35f, 0.4f, 0.65f};
			const sgc::AABB2f tb{{m_thumbRect.x() + 2.0f, m_thumbRect.y() + 1.0f},
				{m_thumbRect.right() - 2.0f, m_thumbRect.bottom() - 1.0f}};
			r->drawRect(tb, tc);
		}
	}

	/// @brief ツールチップを描画する
	void drawTooltip(sgc::IRenderer* r, sgc::ITextRenderer* t, float sw, float sh) const
	{
		const auto& item = ITEMS[static_cast<std::size_t>(m_hoveredSlot)];
		const int row = m_hoveredSlot / COLS;
		const int col = m_hoveredSlot % COLS;
		const float slotX = GRID_X + static_cast<float>(col) * (SLOT_SIZE + SLOT_GAP);
		const float slotY = GRID_Y + static_cast<float>(row) * (SLOT_SIZE + SLOT_GAP) - m_scrollPos;

		const auto tipResult = sgc::ui::evaluateTooltip(
			sgc::Rectf{slotX, slotY, SLOT_SIZE, SLOT_SIZE},
			sgc::Rectf{0.0f, 0.0f, sw, sh},
			sgc::Vec2f{160.0f, 48.0f}, sgc::ui::TooltipSide::Below, 6.0f);

		const sgc::AABB2f tipBg{
			{tipResult.bounds.x(), tipResult.bounds.y()},
			{tipResult.bounds.right(), tipResult.bounds.bottom()}};
		r->drawRect(tipBg, sgc::Colorf{0.1f, 0.1f, 0.15f, 0.95f});
		r->drawRectFrame(tipBg, 1.0f, sgc::Colorf{0.4f, 0.5f, 0.8f});

		t->drawText(item.name, 14.0f,
			sgc::Vec2f{tipResult.bounds.x() + 8.0f, tipResult.bounds.y() + 4.0f},
			sgc::Colorf{0.95f, 0.95f, 1.0f});
		t->drawText(item.desc, 11.0f,
			sgc::Vec2f{tipResult.bounds.x() + 8.0f, tipResult.bounds.y() + 24.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f});
	}
};
