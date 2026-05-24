#pragma once

/// @file SamplePanelStack.hpp
/// @brief Panel＋StackLayout＋Badgeのビジュアルデモ
///
/// sgc::ui::Panel、sgc::ui::StackLayout、sgc::ui::Badge を組み合わせ、
/// パネルレイアウトとスタック配置を視覚的に確認するサンプル。
/// - 左パネル: Player Stats（vstack で4行のステータス表示 + Lv.5 バッジ）
/// - 右パネル: Inventory（vstackFixed で5スロット + "New" バッジ）
/// - NUM1: vstack のスペーシング切替（8 → 16 → 24）
/// - Rキー: リセット

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/Panel.hpp"
#include "sgc/ui/StackLayout.hpp"
#include "sgc/ui/Badge.hpp"
#include "sgc/ui/Anchor.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Panel＋StackLayout＋Badgeサンプルシーン
///
/// 2つのパネルを横に並べ、左にステータス一覧（vstack）、
/// 右にインベントリスロット（vstackFixed）を表示する。
/// バッジの配置計算も合わせてデモする。
class SamplePanelStack : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_spacingIndex = 0;
	}

	/// @brief 更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// スペーシング切替
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_spacingIndex = (m_spacingIndex + 1) % SPACING_COUNT;
		}

		// リセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_spacingIndex = 0;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.06f, 0.07f, 0.10f, 1.0f});

		// タイトル
		tr->drawTextCentered(
			"Panel + StackLayout + Badge", 28.0f,
			sgc::Vec2f{sw * 0.5f, 20.0f},
			sgc::Colorf{0.3f, 0.6f, 1.0f, 1.0f});

		// 現在のスペーシング
		const float spacing = currentSpacing();
		const std::string spacingLabel =
			std::string{"Spacing: "} + std::to_string(static_cast<int>(spacing)) + "px";
		tr->drawTextCentered(
			spacingLabel, 14.0f,
			sgc::Vec2f{sw * 0.5f, 48.0f},
			sgc::Colorf{0.55f, 0.55f, 0.6f, 1.0f});

		// パネル領域の算出
		const float panelTop = 68.0f;
		const float panelGap = 20.0f;
		const float sidePad = 24.0f;
		const float totalW = sw - sidePad * 2.0f;
		const float panelW = (totalW - panelGap) * 0.5f;
		const float panelH = sh - panelTop - 36.0f;

		const sgc::Rectf leftBounds{
			{sidePad, panelTop}, {panelW, panelH}};
		const sgc::Rectf rightBounds{
			{sidePad + panelW + panelGap, panelTop}, {panelW, panelH}};

		// 左パネル描画
		drawPlayerStatsPanel(r, tr, leftBounds, spacing);

		// 右パネル描画
		drawInventoryPanel(r, tr, rightBounds, spacing);

		// 操作ヒント
		tr->drawText(
			"[1] Spacing (8/16/24)  [R] Reset  [Esc] Back",
			12.0f,
			sgc::Vec2f{10.0f, sh - 18.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	/// @brief スペーシングプリセット数
	static constexpr int SPACING_COUNT = 3;

	/// @brief スペーシングプリセット
	static constexpr std::array<float, SPACING_COUNT> SPACINGS{8.0f, 16.0f, 24.0f};

	/// @brief タイトルバー高さ
	static constexpr float TITLE_HEIGHT = 36.0f;

	/// @brief ステータス行の情報
	struct StatRow
	{
		const char* label;
		int value;
		float barRatio;          ///< バーの充填率（0～1）
		sgc::Colorf barColor;
	};

	/// @brief ステータス行テーブル
	static constexpr std::array<StatRow, 4> STAT_ROWS{{
		{"HP",  100, 1.0f,  {0.2f, 0.8f, 0.3f, 0.9f}},
		{"ATK",  25, 0.25f, {0.9f, 0.4f, 0.2f, 0.9f}},
		{"DEF",  18, 0.18f, {0.3f, 0.5f, 0.9f, 0.9f}},
		{"SPD",  12, 0.12f, {0.9f, 0.8f, 0.2f, 0.9f}},
	}};

	/// @brief インベントリアイテム情報
	struct ItemSlot
	{
		const char* name;
		sgc::Colorf color;
		bool isNew;
	};

	/// @brief インベントリアイテムテーブル
	static constexpr std::array<ItemSlot, 5> ITEM_SLOTS{{
		{"Iron Sword",   {0.6f, 0.6f, 0.7f, 0.85f}, true},
		{"Leather Armor",{0.5f, 0.35f, 0.2f, 0.85f}, false},
		{"Health Potion",{0.8f, 0.2f, 0.3f, 0.85f},  true},
		{"Mana Crystal", {0.3f, 0.3f, 0.9f, 0.85f},  false},
		{"Gold Ring",    {0.9f, 0.75f, 0.2f, 0.85f},  true},
	}};

	int m_spacingIndex{0};

	/// @brief 現在のスペーシング値を返す
	[[nodiscard]] float currentSpacing() const noexcept
	{
		return SPACINGS[static_cast<std::size_t>(m_spacingIndex)];
	}

	/// @brief パネル外枠を描画する
	static void drawPanelFrame(
		sgc::IRenderer* r,
		const sgc::Rectf& bounds,
		const sgc::Colorf& bgColor)
	{
		const sgc::AABB2f aabb{
			{bounds.x(), bounds.y()},
			{bounds.x() + bounds.width(), bounds.y() + bounds.height()}};
		r->drawRect(aabb, bgColor);
		r->drawRectFrame(aabb, 2.0f,
			sgc::Colorf{0.35f, 0.35f, 0.45f, 0.9f});
	}

	/// @brief タイトルバーを描画する
	static void drawTitleBar(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& titleBounds,
		std::string_view title,
		const sgc::Colorf& barColor)
	{
		const sgc::AABB2f aabb{
			{titleBounds.x(), titleBounds.y()},
			{titleBounds.x() + titleBounds.width(),
			 titleBounds.y() + titleBounds.height()}};
		r->drawRect(aabb, barColor);

		const sgc::Vec2f textPos{
			titleBounds.x() + titleBounds.width() * 0.5f,
			titleBounds.y() + titleBounds.height() * 0.5f};
		tr->drawTextCentered(
			std::string{title}, 18.0f, textPos,
			sgc::Colorf{0.95f, 0.95f, 0.95f, 1.0f});
	}

	/// @brief Player Statsパネルを描画する
	void drawPlayerStatsPanel(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& bounds,
		float spacing) const
	{
		// パネル背景
		drawPanelFrame(r, bounds, sgc::Colorf{0.12f, 0.12f, 0.18f, 0.95f});

		// パネル分割
		const auto padding = sgc::ui::Padding::uniform(10.0f);
		const auto panel = sgc::ui::evaluatePanel(bounds, TITLE_HEIGHT, padding);

		// タイトルバー
		drawTitleBar(r, tr, panel.titleBounds, "Player Stats",
			sgc::Colorf{0.15f, 0.25f, 0.45f, 1.0f});

		// ステータス行の高さリスト（vstack用）
		const std::array<float, 4> rowHeights{32.0f, 32.0f, 32.0f, 32.0f};
		const auto rows = sgc::ui::vstack(
			panel.contentBounds,
			std::span<const float>{rowHeights.data(), rowHeights.size()},
			spacing);

		// 各ステータス行を描画
		for (std::size_t i = 0; i < STAT_ROWS.size() && i < rows.size(); ++i)
		{
			drawStatRow(r, tr, rows[i], STAT_ROWS[i]);
		}

		// "Lv.5" バッジ（パネル右上）
		const sgc::Vec2f badgeSize{48.0f, 22.0f};
		const auto badgeRect = sgc::ui::evaluateBadge(
			bounds, sgc::ui::Anchor::TopRight,
			badgeSize, sgc::Vec2f{-6.0f, -6.0f});
		drawBadge(r, tr, badgeRect, "Lv.5",
			sgc::Colorf{0.9f, 0.3f, 0.2f, 1.0f});
	}

	/// @brief 1行のステータスを描画する
	static void drawStatRow(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& rowBounds,
		const StatRow& stat)
	{
		// 行の背景
		const sgc::AABB2f rowAABB{
			{rowBounds.x(), rowBounds.y()},
			{rowBounds.x() + rowBounds.width(),
			 rowBounds.y() + rowBounds.height()}};
		r->drawRect(rowAABB, sgc::Colorf{0.15f, 0.15f, 0.22f, 0.7f});

		// ラベル（左側）
		const float labelX = rowBounds.x() + 8.0f;
		const float textY = rowBounds.y() + rowBounds.height() * 0.5f;
		tr->drawText(
			stat.label, 14.0f,
			sgc::Vec2f{labelX, textY - 7.0f},
			sgc::Colorf{0.8f, 0.8f, 0.85f, 1.0f});

		// 値テキスト
		const std::string valStr = std::to_string(stat.value);
		const float valX = rowBounds.x() + 50.0f;
		tr->drawText(
			valStr, 14.0f,
			sgc::Vec2f{valX, textY - 7.0f},
			sgc::Colorf{0.95f, 0.95f, 0.95f, 1.0f});

		// バー（右側）
		const float barLeft = rowBounds.x() + 90.0f;
		const float barRight = rowBounds.x() + rowBounds.width() - 8.0f;
		const float barW = barRight - barLeft;
		const float barH = 12.0f;
		const float barY = textY - barH * 0.5f;

		// バー背景
		const sgc::AABB2f barBgAABB{
			{barLeft, barY}, {barLeft + barW, barY + barH}};
		r->drawRect(barBgAABB, sgc::Colorf{0.1f, 0.1f, 0.15f, 0.8f});

		// バー充填
		const float fillW = barW * stat.barRatio;
		if (fillW > 0.0f)
		{
			const sgc::AABB2f fillAABB{
				{barLeft, barY}, {barLeft + fillW, barY + barH}};
			r->drawRect(fillAABB, stat.barColor);
		}
	}

	/// @brief Inventoryパネルを描画する
	void drawInventoryPanel(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& bounds,
		float spacing) const
	{
		// パネル背景
		drawPanelFrame(r, bounds, sgc::Colorf{0.12f, 0.14f, 0.12f, 0.95f});

		// パネル分割
		const auto padding = sgc::ui::Padding::uniform(10.0f);
		const auto panel = sgc::ui::evaluatePanel(bounds, TITLE_HEIGHT, padding);

		// タイトルバー
		drawTitleBar(r, tr, panel.titleBounds, "Inventory",
			sgc::Colorf{0.25f, 0.40f, 0.20f, 1.0f});

		// vstackFixed で5スロットを均等配置
		const auto slots = sgc::ui::vstackFixed(
			panel.contentBounds, ITEM_SLOTS.size(), spacing);

		// 各アイテムスロット描画
		for (std::size_t i = 0; i < ITEM_SLOTS.size() && i < slots.size(); ++i)
		{
			drawItemSlot(r, tr, slots[i], ITEM_SLOTS[i]);
		}
	}

	/// @brief 1つのアイテムスロットを描画する
	static void drawItemSlot(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& slotBounds,
		const ItemSlot& item)
	{
		// スロット背景
		const sgc::AABB2f slotAABB{
			{slotBounds.x(), slotBounds.y()},
			{slotBounds.x() + slotBounds.width(),
			 slotBounds.y() + slotBounds.height()}};
		r->drawRect(slotAABB, item.color);
		r->drawRectFrame(slotAABB, 1.0f,
			sgc::Colorf{0.7f, 0.7f, 0.7f, 0.6f});

		// アイテム名（中央）
		const sgc::Vec2f center{
			slotBounds.x() + slotBounds.width() * 0.5f,
			slotBounds.y() + slotBounds.height() * 0.5f};
		tr->drawTextCentered(
			item.name, 14.0f, center,
			sgc::Colorf{0.95f, 0.95f, 0.95f, 1.0f});

		// "New" バッジ（badgeFromText で計算し手動描画）
		if (item.isNew)
		{
			// テキスト寸法の概算: "New" 幅≈28px, 高さ≈10px（fontSize 10）
			const float textW = 28.0f;
			const float textH = 10.0f;
			const auto badgeRect = sgc::ui::badgeFromText(
				slotBounds, sgc::ui::Anchor::TopRight,
				textW, textH, 4.0f, 2.0f,
				sgc::Vec2f{-4.0f, -4.0f});

			drawBadge(r, tr, badgeRect, "New",
				sgc::Colorf{0.95f, 0.2f, 0.2f, 1.0f});
		}
	}

	/// @brief バッジを描画する（矩形 + テキスト）
	static void drawBadge(
		sgc::IRenderer* r,
		sgc::ITextRenderer* tr,
		const sgc::Rectf& badgeRect,
		std::string_view text,
		const sgc::Colorf& bgColor)
	{
		const sgc::AABB2f aabb{
			{badgeRect.x(), badgeRect.y()},
			{badgeRect.x() + badgeRect.width(),
			 badgeRect.y() + badgeRect.height()}};
		r->drawRect(aabb, bgColor);

		const sgc::Vec2f center{
			badgeRect.x() + badgeRect.width() * 0.5f,
			badgeRect.y() + badgeRect.height() * 0.5f};
		tr->drawTextCentered(
			std::string{text}, 10.0f, center,
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
	}
};
