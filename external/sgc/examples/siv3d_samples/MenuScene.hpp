#pragma once

/// @file MenuScene.hpp
/// @brief SGCサンプルギャラリーのメニューシーン
///
/// カテゴリタブと縦スクロール対応のグリッドボタンで各サンプルシーンへ遷移する。
/// evaluateButtonによるホバー・プレス状態の視覚フィードバック付き。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Button.hpp"

#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief カテゴリ列挙
enum class Category : std::uint8_t
{
	All, Core, Math, Physics, UI, ECS, Patterns, Scene, Spatial, Net,
	COUNT
};

/// @brief メニューシーン
///
/// 48個のサンプルへのナビゲーションボタンをカテゴリ別にグリッド表示する。
class MenuScene : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		rebuildFilteredIndices();
	}

	/// @brief 更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;
		const auto mousePos = input->mousePosition();
		const bool mouseDown = input->isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);
		const float sh = getData().screenHeight;

		// --- カテゴリタブのクリック判定 ---
		for (std::size_t t = 0; t < TAB_COUNT; ++t)
		{
			const auto tabRect = calcTabRect(t);
			const auto tabResult = sgc::ui::evaluateButton(tabRect, mousePos, mouseDown, mousePressed);
			m_tabStates[t] = tabResult.state;

			if (tabResult.clicked && static_cast<int>(t) != m_selectedCategory)
			{
				m_selectedCategory = static_cast<int>(t);
				m_scrollOffset = 0.0f;
				rebuildFilteredIndices();
			}
		}

		// --- スクロール（上下キー） ---
		const float maxScroll = calcMaxScroll(sh);
		if (input->isKeyDown("Up"_hash))
		{
			m_scrollOffset = std::max(0.0f, m_scrollOffset - SCROLL_SPEED);
		}
		if (input->isKeyDown("Down"_hash))
		{
			m_scrollOffset = std::min(maxScroll, m_scrollOffset + SCROLL_SPEED);
		}
		m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxScroll);

		// --- ボタンの評価 ---
		for (std::size_t fi = 0; fi < m_filteredIndices.size(); ++fi)
		{
			const auto idx = m_filteredIndices[fi];
			const auto bounds = calcButtonRect(fi);

			// 画面外のボタンはスキップ
			if (bounds.y() + BUTTON_H < GRID_TOP || bounds.y() > sh - FOOTER_H)
			{
				m_buttonStates[idx] = sgc::ui::WidgetState::Normal;
				continue;
			}

			const auto result = sgc::ui::evaluateButton(bounds, mousePos, mouseDown, mousePressed);
			m_buttonStates[idx] = result.state;

			if (result.clicked)
			{
				getSceneManager().changeScene(SCENE_IDS[idx], 0.3f);
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 背景
		renderer->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.12f, 1.0f});

		// タイトル
		text->drawTextCentered("SGC Sample Gallery", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.85f, 1.0f, 1.0f});

		// カテゴリタブ描画
		for (std::size_t t = 0; t < TAB_COUNT; ++t)
		{
			drawTab(t);
		}

		// ボタン描画（可視範囲のみ）
		for (std::size_t fi = 0; fi < m_filteredIndices.size(); ++fi)
		{
			const auto bounds = calcButtonRect(fi);
			if (bounds.y() + BUTTON_H < GRID_TOP || bounds.y() > sh - FOOTER_H)
			{
				continue;
			}
			drawButton(m_filteredIndices[fi], bounds);
		}

		// フッター
		text->drawTextCentered(
			"Click a sample | Up/Down to scroll | ESC to return", 14.0f,
			sgc::Vec2f{sw * 0.5f, sh - 16.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f, 1.0f});
	}

private:
	static constexpr std::size_t BUTTON_COUNT = 48;  ///< サンプル数
	static constexpr int COLUMNS = 5;                ///< グリッド列数
	static constexpr float BUTTON_W = 148.0f;        ///< ボタン幅
	static constexpr float BUTTON_H = 48.0f;         ///< ボタン高さ
	static constexpr float GAP_X = 8.0f;             ///< 水平間隔
	static constexpr float GAP_Y = 8.0f;             ///< 垂直間隔
	static constexpr float GRID_TOP = 88.0f;         ///< グリッド開始Y（タブの下）
	static constexpr float TAB_Y = 50.0f;            ///< タブ行Y座標
	static constexpr float TAB_W = 70.0f;            ///< タブ幅
	static constexpr float TAB_H = 24.0f;            ///< タブ高さ
	static constexpr float TAB_GAP = 4.0f;           ///< タブ間隔
	static constexpr float FOOTER_H = 36.0f;         ///< フッター高さ
	static constexpr float SCROLL_SPEED = 60.0f;     ///< スクロール速度(px/frame)
	static constexpr std::size_t TAB_COUNT = static_cast<std::size_t>(Category::COUNT);

	/// @brief カテゴリ名
	static constexpr std::array<std::string_view, TAB_COUNT> TAB_LABELS =
	{
		"All", "Core", "Math", "Physics", "UI",
		"ECS", "Patterns", "Scene", "Spatial", "Net"
	};

	/// @brief ボタンラベル
	static constexpr std::array<std::string_view, BUTTON_COUNT> LABELS =
	{
		"Tween Anim",    "Particles",      "Physics",        "Raycast",       "ECS",
		"UI Widgets",    "Quadtree",       "State Machine",  "Object Pool",   "Behavior Tree",
		"Math Visual",   "Scene Fade",     "Command Undo",   "A* Pathfind",   "Event System",
		"Coroutines",    "Input Combo",    "ActionMap",       "Debug Overlay", "UI Slider/CB",
		"Tween Timeline","Noise Visual",   "Input Mode",      "Observer",      "ServiceLocator",
		"Thread Pool",   "Timer",          "Camera",          "Config",        "FixedTimestep",
		"Memory Alloc",  "State Sync",     "Logger",          "MessageChannel","Octree 3D",
		"HUD Layout",   "PendingAction",  "Toggle/Radio",   "Panel/Stack",   "Tooltip/Toast",
		"Scroll List",  "Text Layout",    "Settings",        "Inventory UI",  "Dialog UI",
		"Drag Window",  "Speech Bubble",  "Glossary Panel"
	};

	/// @brief シーンID
	inline static const std::array<std::uint64_t, BUTTON_COUNT> SCENE_IDS =
	{
		"tween"_hash,     "particle"_hash,  "physics"_hash,    "raycast"_hash,    "ecs"_hash,
		"ui"_hash,        "quadtree"_hash,  "state"_hash,      "pool"_hash,       "ai"_hash,
		"math"_hash,      "fade"_hash,      "command"_hash,    "grid"_hash,       "event"_hash,
		"coroutine"_hash, "combo"_hash,     "actionmap"_hash,  "debug"_hash,      "uiwidgets"_hash,
		"tweentl"_hash,   "noise"_hash,     "inputmode"_hash,  "observer"_hash,   "svclocator"_hash,
		"threadpool"_hash,"timer"_hash,     "camera"_hash,     "config"_hash,     "fixedts"_hash,
		"memory"_hash,    "statesync"_hash, "logger"_hash,     "msgchannel"_hash, "octree"_hash,
		"hudlayout"_hash, "pending"_hash,   "toggleradio"_hash,"panelstack"_hash, "tiptoast"_hash,
		"scrolllist"_hash,"textlayout"_hash,"settings"_hash,   "inventory"_hash,  "dialog"_hash,
		"drag_window"_hash,"speech_bubble"_hash,"glossary_panel"_hash
	};

	/// @brief 各ボタンのカテゴリ
	static constexpr std::array<Category, BUTTON_COUNT> CATEGORIES =
	{
		Category::Core,     Category::Core,     Category::Physics,  Category::Physics,  Category::ECS,
		Category::UI,       Category::Spatial,  Category::Patterns, Category::Patterns, Category::Patterns,
		Category::Math,     Category::Scene,    Category::Patterns, Category::Math,     Category::Patterns,
		Category::Core,     Category::Scene,    Category::Scene,    Category::Scene,    Category::UI,
		Category::Net,      Category::Math,     Category::Scene,    Category::Core,     Category::Core,
		Category::Core,     Category::Core,     Category::Scene,    Category::Core,     Category::Physics,
		Category::Core,     Category::Net,      Category::Core,     Category::Net,      Category::Spatial,
		Category::UI,       Category::UI,       Category::UI,       Category::UI,       Category::UI,
		Category::UI,       Category::UI,       Category::UI,       Category::UI,       Category::UI,
		Category::UI,       Category::UI,       Category::UI
	};

	/// @brief 各ボタンの視覚状態
	std::array<sgc::ui::WidgetState, BUTTON_COUNT> m_buttonStates{};
	/// @brief タブの視覚状態
	std::array<sgc::ui::WidgetState, TAB_COUNT> m_tabStates{};
	/// @brief 選択中カテゴリ（0=All）
	int m_selectedCategory{0};
	/// @brief 縦スクロールオフセット
	float m_scrollOffset{0.0f};
	/// @brief フィルタ後のボタンインデックス
	std::vector<std::size_t> m_filteredIndices;

	/// @brief カテゴリでフィルタしたインデックスを再構築する
	void rebuildFilteredIndices()
	{
		m_filteredIndices.clear();
		const auto cat = static_cast<Category>(m_selectedCategory);
		for (std::size_t i = 0; i < BUTTON_COUNT; ++i)
		{
			if (cat == Category::All || CATEGORIES[i] == cat)
			{
				m_filteredIndices.push_back(i);
			}
		}
	}

	/// @brief 最大スクロール量を計算する
	[[nodiscard]] float calcMaxScroll(float screenH) const
	{
		const auto count = m_filteredIndices.size();
		const int rows = (static_cast<int>(count) + COLUMNS - 1) / COLUMNS;
		const float totalH = static_cast<float>(rows) * (BUTTON_H + GAP_Y) - GAP_Y;
		const float visibleH = screenH - GRID_TOP - FOOTER_H;
		return std::max(0.0f, totalH - visibleH);
	}

	/// @brief タブ矩形を計算する
	[[nodiscard]] sgc::Rectf calcTabRect(std::size_t tabIndex) const
	{
		const float sw = getData().screenWidth;
		const float totalW = static_cast<float>(TAB_COUNT) * TAB_W
			+ static_cast<float>(TAB_COUNT - 1) * TAB_GAP;
		const float startX = (sw - totalW) * 0.5f;
		const float x = startX + static_cast<float>(tabIndex) * (TAB_W + TAB_GAP);
		return sgc::Rectf{x, TAB_Y, TAB_W, TAB_H};
	}

	/// @brief ボタン矩形を計算する（フィルタ後のインデックス基準）
	[[nodiscard]] sgc::Rectf calcButtonRect(std::size_t filteredIndex) const
	{
		const float sw = getData().screenWidth;
		const int col = static_cast<int>(filteredIndex) % COLUMNS;
		const int row = static_cast<int>(filteredIndex) / COLUMNS;

		const float totalW = COLUMNS * BUTTON_W + (COLUMNS - 1) * GAP_X;
		const float startX = (sw - totalW) * 0.5f;

		const float x = startX + static_cast<float>(col) * (BUTTON_W + GAP_X);
		const float y = GRID_TOP + static_cast<float>(row) * (BUTTON_H + GAP_Y) - m_scrollOffset;

		return sgc::Rectf{x, y, BUTTON_W, BUTTON_H};
	}

	/// @brief ボタンのアクセントカラー（色相を均等分配）
	static sgc::Colorf buttonAccentColor(std::size_t index)
	{
		const float hue = static_cast<float>(index)
			* (360.0f / static_cast<float>(BUTTON_COUNT));
		return sgc::Colorf::fromHSV(hue, 0.6f, 0.8f);
	}

	/// @brief タブを1つ描画する
	void drawTab(std::size_t tabIndex) const
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const auto bounds = calcTabRect(tabIndex);
		const bool selected = (static_cast<int>(tabIndex) == m_selectedCategory);
		const auto state = m_tabStates[tabIndex];

		sgc::Colorf bgColor{0.12f, 0.12f, 0.18f, 1.0f};
		sgc::Colorf labelColor{0.6f, 0.6f, 0.7f, 1.0f};

		if (selected)
		{
			bgColor = sgc::Colorf{0.25f, 0.2f, 0.4f, 1.0f};
			labelColor = sgc::Colorf{0.95f, 0.9f, 1.0f, 1.0f};
		}
		else if (state == sgc::ui::WidgetState::Hovered)
		{
			bgColor = sgc::Colorf{0.18f, 0.16f, 0.28f, 1.0f};
			labelColor = sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f};
		}

		const auto aabb = bounds.toAABB2();
		renderer->drawRect(aabb, bgColor);

		if (selected)
		{
			const sgc::AABB2f bottomLine{
				{bounds.x(), bounds.y() + bounds.height() - 2.0f},
				{bounds.x() + bounds.width(), bounds.y() + bounds.height()}
			};
			renderer->drawRect(bottomLine, sgc::Colorf{0.7f, 0.5f, 1.0f, 1.0f});
		}

		text->drawTextCentered(
			std::string{TAB_LABELS[tabIndex]}, 12.0f,
			bounds.center(), labelColor);
	}

	/// @brief ボタンを1つ描画する
	void drawButton(std::size_t index, const sgc::Rectf& bounds) const
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const auto state = m_buttonStates[index];
		const auto accent = buttonAccentColor(index);

		// 状態に応じた色
		sgc::Colorf bgColor{0.15f, 0.15f, 0.2f, 1.0f};
		sgc::Colorf borderColor = accent.withAlpha(0.4f);
		sgc::Colorf labelColor{0.8f, 0.8f, 0.85f, 1.0f};

		if (state == sgc::ui::WidgetState::Hovered)
		{
			bgColor = accent.withAlpha(0.25f);
			borderColor = accent.withAlpha(0.8f);
			labelColor = sgc::Colorf::white();
		}
		else if (state == sgc::ui::WidgetState::Pressed)
		{
			bgColor = accent.withAlpha(0.45f);
			borderColor = accent;
			labelColor = sgc::Colorf::white();
		}

		// 背景矩形
		const auto aabb = bounds.toAABB2();
		renderer->drawRect(aabb, bgColor);
		renderer->drawRectFrame(aabb, 2.0f, borderColor);

		// ホバー・プレス時に上部アクセントライン
		if (state == sgc::ui::WidgetState::Hovered ||
			state == sgc::ui::WidgetState::Pressed)
		{
			const sgc::AABB2f topLine{
				{bounds.x(), bounds.y()},
				{bounds.x() + bounds.width(), bounds.y() + 3.0f}
			};
			renderer->drawRect(topLine, accent);
		}

		// ラベル
		text->drawTextCentered(
			std::string{LABELS[index]}, 12.0f,
			bounds.center(), labelColor);
	}
};
