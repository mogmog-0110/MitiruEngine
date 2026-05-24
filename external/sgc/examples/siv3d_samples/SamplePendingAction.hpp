#pragma once

/// @file SamplePendingAction.hpp
/// @brief PendingActionパターンのビジュアルデモシーン
///
/// sgc::ui::PendingAction の IMGUI ブリッジパターンを視覚的にデモする。
/// draw() const 内で trigger() し、次フレームの update() で consume() する
/// 1フレーム遅延パターンを、インジケーター付きで確認できる。
///
/// - ボタン1 "Increment": PendingAction<void> — カウンターを+1
/// - ボタン2 "Add Random": PendingAction<int> — ランダム値を合計に加算
/// - ボタン3 "Reset": PendingAction<void> — 全カウンタをリセット
/// - ペンディング状態インジケーター（1フレーム遅延の可視化）

#include <cstdlib>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Button.hpp"
#include "sgc/ui/PendingAction.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief PendingActionパターンのビジュアルデモシーン
///
/// draw() const 内でトリガーし、update() で消費するIMGUIブリッジパターンを
/// 3つのボタンとステータス表示で視覚的に解説する。
class SamplePendingAction : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_counter = 0;
		m_total = 0;
		m_lastRandomValue = 0;
		m_pendingFlashTimer = 0.0f;
		m_frameCount = 0;
		m_lastTriggerFrame = -1;
		m_lastConsumeFrame = -1;
		m_incrementAction.reset();
		m_addAction.reset();
		m_resetAction.reset();
	}

	/// @brief 更新処理 — PendingAction を消費するフレーム
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto& input = *getData().inputProvider;

		// ESCでメニューに戻る
		if (input.isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rキーで全リセット
		if (input.isKeyJustPressed(KeyCode::R))
		{
			m_counter = 0;
			m_total = 0;
			m_lastRandomValue = 0;
			m_pendingFlashTimer = 0.0f;
			m_lastTriggerFrame = -1;
			m_lastConsumeFrame = -1;
			m_incrementAction.reset();
			m_addAction.reset();
			m_resetAction.reset();
		}

		// ── PendingAction の消費（update()側） ──
		if (m_incrementAction.consume())
		{
			++m_counter;
			m_lastConsumeFrame = m_frameCount;
			m_pendingFlashTimer = FLASH_DURATION;
		}

		if (auto val = m_addAction.consumeValue())
		{
			m_total += *val;
			m_lastRandomValue = *val;
			m_lastConsumeFrame = m_frameCount;
			m_pendingFlashTimer = FLASH_DURATION;
		}

		if (m_resetAction.consume())
		{
			m_counter = 0;
			m_total = 0;
			m_lastRandomValue = 0;
			m_lastConsumeFrame = m_frameCount;
			m_pendingFlashTimer = FLASH_DURATION;
		}

		// ── ボタン評価（update()で入力処理） ──
		const auto mousePos = input.mousePosition();
		const bool mouseDown = input.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		m_btnIncrement = sgc::ui::evaluateButton(
			buttonRect(0), mousePos, mouseDown, mousePressed);

		m_btnAddRandom = sgc::ui::evaluateButton(
			buttonRect(1), mousePos, mouseDown, mousePressed);

		m_btnReset = sgc::ui::evaluateButton(
			buttonRect(2), mousePos, mouseDown, mousePressed);

		// ── ボタンクリック時に trigger() する ──
		if (m_btnIncrement.clicked)
		{
			m_incrementAction.trigger();
			m_lastTriggerFrame = m_frameCount;
		}

		if (m_btnAddRandom.clicked)
		{
			const int randomVal = (std::rand() % 100) + 1;
			m_addAction.trigger(randomVal);
			m_lastTriggerFrame = m_frameCount;
		}

		if (m_btnReset.clicked)
		{
			m_resetAction.trigger();
			m_lastTriggerFrame = m_frameCount;
		}

		// フラッシュタイマー減衰
		if (m_pendingFlashTimer > 0.0f)
		{
			m_pendingFlashTimer -= dt;
		}

		++m_frameCount;
	}

	/// @brief 描画処理
	void draw() const override
	{
		const auto& data = getData();
		const float sw = data.screenWidth;
		const float sh = data.screenHeight;

		// 背景
		data.renderer->clearBackground(BG_COLOR);

		// タイトル
		data.textRenderer->drawTextCentered(
			"PendingAction Demo", 36,
			sgc::Vec2f{sw * 0.5f, 40.0f}, ACCENT_COLOR);

		// サブタイトル（パターン説明）
		data.textRenderer->drawTextCentered(
			"IMGUI Bridge: trigger() in draw() -> consume() in update()", 14,
			sgc::Vec2f{sw * 0.5f, 70.0f}, TEXT_DIM_COLOR);

		// ── 左パネル: ボタン群 ──
		const sgc::AABB2f leftPanel{
			{PANEL_MARGIN, PANEL_TOP},
			{PANEL_DIVIDER - 10.0f, sh - PANEL_MARGIN}};
		data.renderer->drawRect(leftPanel, PANEL_COLOR);
		data.renderer->drawRectFrame(leftPanel, 1.0f, BORDER_COLOR);

		data.textRenderer->drawText(
			"Actions", 24,
			sgc::Vec2f{PANEL_MARGIN + 20.0f, PANEL_TOP + 15.0f}, TEXT_COLOR);

		drawButton(buttonRect(0), "Increment", m_btnIncrement.state);
		drawButton(buttonRect(1), "Add Random", m_btnAddRandom.state);
		drawButton(buttonRect(2), "Reset All", m_btnReset.state);

		// ── 右パネル: ステータス表示 ──
		const sgc::AABB2f rightPanel{
			{PANEL_DIVIDER + 10.0f, PANEL_TOP},
			{sw - PANEL_MARGIN, sh - PANEL_MARGIN}};
		data.renderer->drawRect(rightPanel, PANEL_COLOR);
		data.renderer->drawRectFrame(rightPanel, 1.0f, BORDER_COLOR);

		const float rx = PANEL_DIVIDER + 30.0f;

		// カウンター値（大きく表示）
		data.textRenderer->drawText(
			"Counter", 22,
			sgc::Vec2f{rx, PANEL_TOP + 15.0f}, TEXT_COLOR);

		data.textRenderer->drawText(
			std::to_string(m_counter), 48,
			sgc::Vec2f{rx + 20.0f, PANEL_TOP + 50.0f}, ACCENT_COLOR);

		// 合計値
		data.textRenderer->drawText(
			"Total (random sum)", 22,
			sgc::Vec2f{rx, PANEL_TOP + 120.0f}, TEXT_COLOR);

		data.textRenderer->drawText(
			std::to_string(m_total), 36,
			sgc::Vec2f{rx + 20.0f, PANEL_TOP + 152.0f}, VALUE_COLOR);

		// 最後の乱数値
		if (m_lastRandomValue > 0)
		{
			const std::string lastStr = "Last random: +" + std::to_string(m_lastRandomValue);
			data.textRenderer->drawText(
				lastStr, 16,
				sgc::Vec2f{rx + 20.0f, PANEL_TOP + 200.0f}, TEXT_DIM_COLOR);
		}

		// ── ペンディング状態インジケーター ──
		drawPendingIndicator(rx);

		// ── フレームタイムライン ──
		drawTimeline(rx);

		// ── 操作ヒント ──
		data.textRenderer->drawText(
			"[Esc] Menu  [R] Reset All", 14,
			sgc::Vec2f{PANEL_MARGIN + 10.0f, sh - 30.0f}, TEXT_DIM_COLOR);
	}

private:
	// ── レイアウト定数 ──
	static constexpr float PANEL_MARGIN = 20.0f;       ///< パネル外側マージン
	static constexpr float PANEL_TOP = 90.0f;           ///< パネル上端Y
	static constexpr float PANEL_DIVIDER = 300.0f;      ///< 左右パネル境界X

	static constexpr float BUTTON_X = 45.0f;            ///< ボタンX座標
	static constexpr float BUTTON_Y_START = 150.0f;     ///< 最初のボタンY
	static constexpr float BUTTON_W = 210.0f;           ///< ボタン幅
	static constexpr float BUTTON_H = 45.0f;            ///< ボタン高さ
	static constexpr float BUTTON_SPACING = 65.0f;      ///< ボタン間隔

	static constexpr float FLASH_DURATION = 0.3f;       ///< フラッシュ持続時間（秒）

	// ── カラー定数 ──
	static constexpr sgc::Colorf BG_COLOR{0.08f, 0.08f, 0.12f, 1.0f};       ///< 背景色
	static constexpr sgc::Colorf PANEL_COLOR{0.12f, 0.12f, 0.18f, 0.9f};    ///< パネル背景色
	static constexpr sgc::Colorf BORDER_COLOR{0.3f, 0.35f, 0.5f, 1.0f};     ///< 枠線色
	static constexpr sgc::Colorf TEXT_COLOR{0.9f, 0.9f, 0.95f, 1.0f};       ///< テキスト色
	static constexpr sgc::Colorf TEXT_DIM_COLOR{0.5f, 0.5f, 0.6f, 1.0f};    ///< 薄いテキスト色
	static constexpr sgc::Colorf ACCENT_COLOR{0.3f, 0.7f, 1.0f, 1.0f};      ///< アクセント色
	static constexpr sgc::Colorf VALUE_COLOR{0.4f, 0.9f, 0.5f, 1.0f};       ///< 値表示色
	static constexpr sgc::Colorf PENDING_ON{1.0f, 0.8f, 0.2f, 1.0f};        ///< ペンディング点灯色
	static constexpr sgc::Colorf PENDING_OFF{0.25f, 0.25f, 0.3f, 1.0f};     ///< ペンディング消灯色
	static constexpr sgc::Colorf TRIGGER_COLOR{1.0f, 0.5f, 0.2f, 1.0f};     ///< トリガー色
	static constexpr sgc::Colorf CONSUME_COLOR{0.3f, 1.0f, 0.5f, 1.0f};     ///< 消費色

	// ── ボタン色 ──
	static constexpr sgc::Colorf BTN_NORMAL{0.18f, 0.2f, 0.28f, 1.0f};      ///< ボタン通常色
	static constexpr sgc::Colorf BTN_HOVERED{0.25f, 0.28f, 0.38f, 1.0f};    ///< ボタンホバー色
	static constexpr sgc::Colorf BTN_PRESSED{0.15f, 0.16f, 0.22f, 1.0f};    ///< ボタン押下色

	// ── mutable PendingAction（draw() const から trigger() するため） ──
	mutable sgc::ui::PendingAction<void> m_incrementAction; ///< カウンター増加アクション
	mutable sgc::ui::PendingAction<int> m_addAction;        ///< ランダム加算アクション
	mutable sgc::ui::PendingAction<void> m_resetAction;     ///< リセットアクション

	// ── 状態 ──
	int m_counter{0};                          ///< カウンター値
	int m_total{0};                            ///< ランダム値の合計
	int m_lastRandomValue{0};                  ///< 最後に追加された乱数値
	float m_pendingFlashTimer{0.0f};           ///< 消費フラッシュの残り時間
	int m_frameCount{0};                       ///< フレームカウンター
	int m_lastTriggerFrame{-1};                ///< 最後にtrigger()したフレーム
	int m_lastConsumeFrame{-1};                ///< 最後にconsume()したフレーム

	sgc::ui::ButtonResult m_btnIncrement{};    ///< Incrementボタン評価結果
	sgc::ui::ButtonResult m_btnAddRandom{};    ///< Add Randomボタン評価結果
	sgc::ui::ButtonResult m_btnReset{};        ///< Resetボタン評価結果

	// ── レイアウト計算 ──

	/// @brief ボタン矩形を計算する
	/// @param index ボタンインデックス (0, 1, 2)
	/// @return ボタンの矩形
	[[nodiscard]] static sgc::Rectf buttonRect(int index) noexcept
	{
		const float y = BUTTON_Y_START + static_cast<float>(index) * BUTTON_SPACING;
		return {BUTTON_X, y, BUTTON_W, BUTTON_H};
	}

	// ── 描画ヘルパー ──

	/// @brief ボタンを描画する
	/// @param rect ボタン矩形
	/// @param label ラベルテキスト
	/// @param state ウィジェット状態
	void drawButton(const sgc::Rectf& rect, const char* label,
		sgc::ui::WidgetState state) const
	{
		const auto& data = getData();
		const sgc::AABB2f btnAABB{
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()}};

		// 状態に応じた背景色
		sgc::Colorf bgColor = BTN_NORMAL;
		switch (state)
		{
		case sgc::ui::WidgetState::Hovered:
			bgColor = BTN_HOVERED;
			break;
		case sgc::ui::WidgetState::Pressed:
			bgColor = BTN_PRESSED;
			break;
		default:
			break;
		}
		data.renderer->drawRect(btnAABB, bgColor);

		// 枠線
		const float borderW = (state == sgc::ui::WidgetState::Hovered) ? 2.0f : 1.0f;
		data.renderer->drawRectFrame(btnAABB, borderW, ACCENT_COLOR);

		// ラベル
		data.textRenderer->drawTextCentered(
			label, 20,
			sgc::Vec2f{rect.x() + rect.width() * 0.5f,
			           rect.y() + rect.height() * 0.5f},
			TEXT_COLOR);
	}

	/// @brief ペンディング状態インジケーターを描画する
	///
	/// 各PendingActionのisPending()状態をリアルタイムで表示し、
	/// trigger()からconsume()までの1フレーム遅延を可視化する。
	/// @param startX 描画開始X座標
	void drawPendingIndicator(float startX) const
	{
		const auto& data = getData();
		const float y = PANEL_TOP + 240.0f;

		data.textRenderer->drawText(
			"Pending Status", 22,
			sgc::Vec2f{startX, y}, TEXT_COLOR);

		// 各アクションのペンディング状態ランプ
		const float lampY = y + 35.0f;
		const float lampSpacing = 120.0f;

		drawPendingLamp(startX + 10.0f, lampY, "Incr",
			m_incrementAction.isPending());
		drawPendingLamp(startX + 10.0f + lampSpacing, lampY, "Add",
			m_addAction.isPending());
		drawPendingLamp(startX + 10.0f + lampSpacing * 2.0f, lampY, "Reset",
			m_resetAction.isPending());

		// フラッシュインジケーター（消費直後に光る）
		if (m_pendingFlashTimer > 0.0f)
		{
			const float alpha = m_pendingFlashTimer / FLASH_DURATION;
			const sgc::Colorf flashColor{
				CONSUME_COLOR.r, CONSUME_COLOR.g, CONSUME_COLOR.b, alpha};
			data.textRenderer->drawText(
				"Consumed!", 18,
				sgc::Vec2f{startX + 10.0f, lampY + 40.0f}, flashColor);
		}
	}

	/// @brief ペンディング状態ランプを1つ描画する
	/// @param x ランプ中心X座標
	/// @param y ランプ中心Y座標
	/// @param label ラベル
	/// @param isPending ペンディング中か
	void drawPendingLamp(float x, float y,
		const char* label, bool isPending) const
	{
		const auto& data = getData();

		// ランプ円
		const sgc::Colorf lampColor = isPending ? PENDING_ON : PENDING_OFF;
		data.renderer->drawCircle(
			sgc::Vec2f{x + 10.0f, y + 8.0f}, 8.0f, lampColor);

		// ラベル
		data.textRenderer->drawText(
			label, 14,
			sgc::Vec2f{x + 24.0f, y}, TEXT_DIM_COLOR);
	}

	/// @brief フレームタイムラインを描画する
	///
	/// trigger()とconsume()が発生したフレームを色分けして表示し、
	/// 1フレーム遅延パターンを時系列で確認できるようにする。
	/// @param startX 描画開始X座標
	void drawTimeline(float startX) const
	{
		const auto& data = getData();
		const float y = PANEL_TOP + 360.0f;

		data.textRenderer->drawText(
			"Frame Timeline", 22,
			sgc::Vec2f{startX, y}, TEXT_COLOR);

		// タイムラインの凡例
		const float legendY = y + 30.0f;
		data.renderer->drawCircle(
			sgc::Vec2f{startX + 10.0f, legendY + 6.0f}, 5.0f, TRIGGER_COLOR);
		data.textRenderer->drawText(
			"trigger()", 12,
			sgc::Vec2f{startX + 22.0f, legendY}, TEXT_DIM_COLOR);

		data.renderer->drawCircle(
			sgc::Vec2f{startX + 110.0f, legendY + 6.0f}, 5.0f, CONSUME_COLOR);
		data.textRenderer->drawText(
			"consume()", 12,
			sgc::Vec2f{startX + 122.0f, legendY}, TEXT_DIM_COLOR);

		// タイムラインバー（最近のフレームを表示）
		const float barY = legendY + 28.0f;
		const float barWidth = 350.0f;
		const int visibleFrames = 30;
		const float cellW = barWidth / static_cast<float>(visibleFrames);

		// 背景バー
		const sgc::AABB2f barBg{
			{startX, barY},
			{startX + barWidth, barY + 24.0f}};
		data.renderer->drawRect(barBg, sgc::Colorf{0.1f, 0.1f, 0.14f, 1.0f});
		data.renderer->drawRectFrame(barBg, 1.0f, BORDER_COLOR);

		// フレームセルを描画
		for (int i = 0; i < visibleFrames; ++i)
		{
			const int frame = m_frameCount - visibleFrames + i;
			if (frame < 0)
			{
				continue;
			}

			const float cellX = startX + static_cast<float>(i) * cellW;

			// trigger()フレームをマーク
			if (frame == m_lastTriggerFrame)
			{
				const sgc::AABB2f cellRect{
					{cellX, barY},
					{cellX + cellW, barY + 24.0f}};
				data.renderer->drawRect(cellRect, TRIGGER_COLOR);
			}

			// consume()フレームをマーク
			if (frame == m_lastConsumeFrame)
			{
				const sgc::AABB2f cellRect{
					{cellX, barY},
					{cellX + cellW, barY + 24.0f}};
				data.renderer->drawRect(cellRect, CONSUME_COLOR);
			}
		}

		// 現在フレーム位置の三角マーカー
		const float currentX = startX + static_cast<float>(visibleFrames - 1) * cellW
			+ cellW * 0.5f;
		data.textRenderer->drawText(
			"now", 10,
			sgc::Vec2f{currentX - 8.0f, barY + 26.0f}, TEXT_DIM_COLOR);

		// パターン説明
		data.textRenderer->drawText(
			"trigger() and consume() happen in adjacent frames", 12,
			sgc::Vec2f{startX, barY + 46.0f}, TEXT_DIM_COLOR);
	}
};
