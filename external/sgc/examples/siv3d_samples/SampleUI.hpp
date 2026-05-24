#pragma once

/// @file SampleUI.hpp
/// @brief UIウィジェット総合サンプルシーン
///
/// sgc::uiの全ウィジェット（Button, Slider, Checkbox, ProgressBar）を
/// 統合したインタラクティブなデモ。
/// - 左列: 3つのボタン（ホバー/押下状態の視覚的フィードバック）
/// - 右エリア: スライダー（0〜100）、チェックボックス、プログレスバー
/// - スライダー値がプログレスバーに反映される
/// - チェックボックスでダーク/ライトテーマを切り替え
/// - Theme::dark() / Theme::light() でカラーテーマを適用

#include <algorithm>
#include <cmath>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Button.hpp"
#include "sgc/ui/Checkbox.hpp"
#include "sgc/ui/ProgressBar.hpp"
#include "sgc/ui/Slider.hpp"
#include "sgc/ui/Theme.hpp"
#include "sgc/ui/WidgetState.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief UIウィジェット総合サンプルシーン
///
/// Button, Slider, Checkbox, ProgressBar を一画面で操作・確認できる。
/// evaluateXxx() で状態を評価し、renderer / textRenderer で手動描画する。
class SampleUI : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_sliderValue = 50.0f;
		m_sliderDragging = false;
		m_darkTheme = true;
		m_clickCount = 0;
		m_lastButtonMsg = "Click a button!";
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

		// ── ボタン評価（左列: 3つ） ──
		m_btnAction = sgc::ui::evaluateButton(
			buttonRect(0), mousePos, mouseDown, mousePressed);
		if (m_btnAction.clicked)
		{
			++m_clickCount;
			m_lastButtonMsg = "Action! (x" + std::to_string(m_clickCount) + ")";
		}

		m_btnReset = sgc::ui::evaluateButton(
			buttonRect(1), mousePos, mouseDown, mousePressed);
		if (m_btnReset.clicked)
		{
			m_sliderValue = 50.0f;
			m_clickCount = 0;
			m_lastButtonMsg = "Reset!";
		}

		m_btnDisabled = sgc::ui::evaluateButton(
			buttonRect(2), mousePos, mouseDown, mousePressed, false);

		// ── スライダー評価（右エリア） ──
		const auto slResult = sgc::ui::evaluateSlider(
			sliderBounds(), mousePos, mouseDown, mousePressed,
			m_sliderValue, SLIDER_MIN, SLIDER_MAX, m_sliderDragging);
		if (slResult.changed)
		{
			m_sliderValue = slResult.value;
		}
		m_sliderDragging = slResult.dragging;
		m_sliderState = slResult.state;

		// ── チェックボックス評価 ──
		const auto cbResult = sgc::ui::evaluateCheckbox(
			checkboxBounds(), mousePos, mouseDown, mousePressed, m_darkTheme);
		if (cbResult.toggled)
		{
			m_darkTheme = cbResult.checked;
		}
		m_checkboxState = cbResult.state;
	}

	/// @brief 描画処理
	void draw() const override
	{
		const auto& data = getData();
		const auto theme = m_darkTheme
			? sgc::ui::Theme::dark() : sgc::ui::Theme::light();
		const float sw = data.screenWidth;
		const float sh = data.screenHeight;

		// 背景
		data.renderer->clearBackground(theme.background);

		// タイトル
		data.textRenderer->drawTextCentered(
			"UI Widgets Gallery", theme.fontSizeTitle,
			sgc::Vec2f{sw * 0.5f, 45.0f}, theme.accent);

		// ── パネル背景 ──
		const sgc::AABB2f panelLeft{
			{PANEL_MARGIN, PANEL_TOP},
			{COLUMN_DIVIDER - 10.0f, sh - PANEL_MARGIN}};
		data.renderer->drawRect(panelLeft,
			sgc::Colorf{theme.surface.r, theme.surface.g, theme.surface.b, 0.85f});
		data.renderer->drawRectFrame(panelLeft, 1.0f, theme.accent);

		const sgc::AABB2f panelRight{
			{COLUMN_DIVIDER + 10.0f, PANEL_TOP},
			{sw - PANEL_MARGIN, sh - PANEL_MARGIN}};
		data.renderer->drawRect(panelRight,
			sgc::Colorf{theme.surface.r, theme.surface.g, theme.surface.b, 0.85f});
		data.renderer->drawRectFrame(panelRight, 1.0f, theme.accent);

		// ── 左列: ボタン ──
		data.textRenderer->drawText(
			"Buttons", theme.fontSizeBody,
			sgc::Vec2f{PANEL_MARGIN + 20.0f, PANEL_TOP + 15.0f}, theme.text);

		drawButton(buttonRect(0), "Action", m_btnAction.state, theme);
		drawButton(buttonRect(1), "Reset", m_btnReset.state, theme);
		drawButton(buttonRect(2), "Disabled", m_btnDisabled.state, theme);

		// ボタン結果メッセージ
		data.textRenderer->drawText(
			m_lastButtonMsg, theme.fontSizeSmall,
			sgc::Vec2f{PANEL_MARGIN + 30.0f, BUTTON_Y_START + 3.0f * BUTTON_SPACING + 5.0f},
			theme.accent);

		// ── 右エリア: スライダー ──
		const float rightX = COLUMN_DIVIDER + 30.0f;
		data.textRenderer->drawText(
			"Slider", theme.fontSizeBody,
			sgc::Vec2f{rightX, PANEL_TOP + 15.0f}, theme.text);

		drawSlider(theme);

		// スライダー値表示
		const int intVal = static_cast<int>(m_sliderValue + 0.5f);
		const std::string valStr = "Value: " + std::to_string(intVal);
		data.textRenderer->drawText(
			valStr, theme.fontSizeSmall,
			sgc::Vec2f{rightX, SLIDER_Y + SLIDER_H + 10.0f}, theme.textSecondary);

		// ── 右エリア: チェックボックス ──
		data.textRenderer->drawText(
			"Theme", theme.fontSizeBody,
			sgc::Vec2f{rightX, CHECKBOX_Y - 30.0f}, theme.text);

		drawCheckbox(theme);

		// ── 右エリア: プログレスバー ──
		data.textRenderer->drawText(
			"Progress", theme.fontSizeBody,
			sgc::Vec2f{rightX, PROGRESS_Y - 30.0f}, theme.text);

		drawProgressBar(theme);

		// 進行率テキスト
		const auto pbResult = sgc::ui::evaluateProgressBar(
			progressBounds(), m_sliderValue, SLIDER_MIN, SLIDER_MAX);
		const int pct = static_cast<int>(pbResult.ratio * 100.0f + 0.5f);
		data.textRenderer->drawText(
			std::to_string(pct) + "%", theme.fontSizeSmall,
			sgc::Vec2f{rightX + WIDGET_WIDTH + 12.0f, PROGRESS_Y + 4.0f},
			theme.textSecondary);

		// ── 操作ヒント ──
		data.textRenderer->drawText(
			"[Esc] Back to Menu", 14.0f,
			sgc::Vec2f{PANEL_MARGIN + 10.0f, sh - 30.0f}, theme.textSecondary);
	}

private:
	// ── レイアウト定数 ──
	static constexpr float PANEL_MARGIN = 20.0f;       ///< パネル外側マージン
	static constexpr float PANEL_TOP = 80.0f;           ///< パネル上端Y
	static constexpr float COLUMN_DIVIDER = 310.0f;     ///< 左右列の境界X

	static constexpr float BUTTON_X = 50.0f;            ///< ボタンX位置
	static constexpr float BUTTON_Y_START = 140.0f;     ///< 最初のボタンY
	static constexpr float BUTTON_W = 200.0f;           ///< ボタン幅
	static constexpr float BUTTON_H = 45.0f;            ///< ボタン高さ
	static constexpr float BUTTON_SPACING = 60.0f;      ///< ボタン間隔

	static constexpr float WIDGET_WIDTH = 350.0f;       ///< 右エリアウィジェット幅
	static constexpr float SLIDER_Y = 130.0f;           ///< スライダーY
	static constexpr float SLIDER_H = 28.0f;            ///< スライダー高さ
	static constexpr float SLIDER_MIN = 0.0f;           ///< スライダー最小値
	static constexpr float SLIDER_MAX = 100.0f;         ///< スライダー最大値

	static constexpr float CHECKBOX_Y = 250.0f;         ///< チェックボックスY
	static constexpr float CHECKBOX_SIZE = 26.0f;       ///< チェックボックスサイズ

	static constexpr float PROGRESS_Y = 340.0f;         ///< プログレスバーY
	static constexpr float PROGRESS_H = 30.0f;          ///< プログレスバー高さ

	// ── メンバ変数 ──
	float m_sliderValue{50.0f};                         ///< スライダー現在値
	bool m_sliderDragging{false};                       ///< スライダードラッグ中か
	sgc::ui::WidgetState m_sliderState{sgc::ui::WidgetState::Normal};  ///< スライダー状態
	bool m_darkTheme{true};                             ///< ダークテーマか
	sgc::ui::WidgetState m_checkboxState{sgc::ui::WidgetState::Normal}; ///< チェックボックス状態
	int m_clickCount{0};                                ///< アクションボタンクリック回数
	std::string m_lastButtonMsg{"Click a button!"};     ///< ボタンメッセージ

	sgc::ui::ButtonResult m_btnAction{};                ///< アクションボタン結果
	sgc::ui::ButtonResult m_btnReset{};                 ///< リセットボタン結果
	sgc::ui::ButtonResult m_btnDisabled{};              ///< 無効ボタン結果

	// ── レイアウト計算 ──

	/// @brief ボタン矩形を計算する
	/// @param index ボタンインデックス (0, 1, 2)
	[[nodiscard]] static sgc::Rectf buttonRect(int index) noexcept
	{
		const float y = BUTTON_Y_START + static_cast<float>(index) * BUTTON_SPACING;
		return {BUTTON_X, y, BUTTON_W, BUTTON_H};
	}

	/// @brief スライダー矩形を取得する
	[[nodiscard]] static sgc::Rectf sliderBounds() noexcept
	{
		return {COLUMN_DIVIDER + 30.0f, SLIDER_Y, WIDGET_WIDTH, SLIDER_H};
	}

	/// @brief チェックボックス矩形を取得する
	[[nodiscard]] static sgc::Rectf checkboxBounds() noexcept
	{
		return {COLUMN_DIVIDER + 30.0f, CHECKBOX_Y, CHECKBOX_SIZE, CHECKBOX_SIZE};
	}

	/// @brief プログレスバー矩形を取得する
	[[nodiscard]] static sgc::Rectf progressBounds() noexcept
	{
		return {COLUMN_DIVIDER + 30.0f, PROGRESS_Y, WIDGET_WIDTH, PROGRESS_H};
	}

	// ── 描画ヘルパー ──

	/// @brief ボタンを描画する
	/// @param rect ボタン矩形
	/// @param label ラベルテキスト
	/// @param state ウィジェット状態
	/// @param theme テーマ
	void drawButton(const sgc::Rectf& rect, const char* label,
		sgc::ui::WidgetState state, const sgc::ui::Theme& theme) const
	{
		const auto& data = getData();
		const sgc::AABB2f btnAABB{
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()}};

		const auto color = theme.button.colorFor(state);
		data.renderer->drawRect(btnAABB, color);

		// 状態に応じた枠線の太さと色
		const float borderThickness = (state == sgc::ui::WidgetState::Hovered) ? 2.0f : 1.0f;
		const auto borderColor = (state == sgc::ui::WidgetState::Disabled)
			? sgc::Colorf{0.3f, 0.3f, 0.3f, 0.5f}
			: theme.accent;
		data.renderer->drawRectFrame(btnAABB, borderThickness, borderColor);

		// ラベル描画
		const auto textColor = (state == sgc::ui::WidgetState::Disabled)
			? theme.textSecondary : theme.text;
		data.textRenderer->drawTextCentered(
			label, 20.0f,
			sgc::Vec2f{rect.x() + rect.width() * 0.5f,
			           rect.y() + rect.height() * 0.5f},
			textColor);

		// 状態インジケータ（右端に小さな丸）
		const float indicatorX = rect.x() + rect.width() - 14.0f;
		const float indicatorY = rect.y() + rect.height() * 0.5f;
		sgc::Colorf indicatorColor;
		switch (state)
		{
		case sgc::ui::WidgetState::Hovered:
			indicatorColor = sgc::Colorf{1.0f, 0.9f, 0.3f, 1.0f};
			break;
		case sgc::ui::WidgetState::Pressed:
			indicatorColor = sgc::Colorf{0.3f, 1.0f, 0.3f, 1.0f};
			break;
		case sgc::ui::WidgetState::Disabled:
			indicatorColor = sgc::Colorf{0.4f, 0.4f, 0.4f, 0.5f};
			break;
		default:
			indicatorColor = sgc::Colorf{0.5f, 0.5f, 0.5f, 0.5f};
			break;
		}
		data.renderer->drawCircle(
			sgc::Vec2f{indicatorX, indicatorY}, 4.0f, indicatorColor);
	}

	/// @brief スライダーを描画する
	/// @param theme テーマ
	void drawSlider(const sgc::ui::Theme& theme) const
	{
		const auto& data = getData();
		const auto rect = sliderBounds();

		// トラック背景
		const sgc::AABB2f trackAABB{
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()}};
		data.renderer->drawRect(trackAABB,
			sgc::Colorf{0.15f, 0.15f, 0.18f, 0.9f});
		data.renderer->drawRectFrame(trackAABB, 1.0f,
			sgc::Colorf{0.4f, 0.4f, 0.45f, 1.0f});

		// 塗りつぶし部分
		const float ratio = (m_sliderValue - SLIDER_MIN) / (SLIDER_MAX - SLIDER_MIN);
		const float fillW = rect.width() * ratio;
		if (fillW > 0.0f)
		{
			const sgc::AABB2f fillAABB{
				{rect.x(), rect.y()},
				{rect.x() + fillW, rect.y() + rect.height()}};
			data.renderer->drawRect(fillAABB, theme.accent);
		}

		// つまみ（ノブ）
		const float knobX = rect.x() + fillW;
		const float knobCY = rect.y() + rect.height() * 0.5f;
		const float knobRadius = (m_sliderState == sgc::ui::WidgetState::Pressed)
			? 12.0f : 10.0f;
		data.renderer->drawCircle(
			sgc::Vec2f{knobX, knobCY}, knobRadius,
			sgc::Colorf::white());
		data.renderer->drawCircleFrame(
			sgc::Vec2f{knobX, knobCY}, knobRadius, 2.0f, theme.accent);
	}

	/// @brief チェックボックスを描画する
	/// @param theme テーマ
	void drawCheckbox(const sgc::ui::Theme& theme) const
	{
		const auto& data = getData();
		const auto rect = checkboxBounds();
		const sgc::AABB2f boxAABB{
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()}};

		// 外枠
		const float borderW = (m_checkboxState == sgc::ui::WidgetState::Hovered)
			? 2.5f : 2.0f;
		data.renderer->drawRectFrame(boxAABB, borderW, theme.accent);

		// チェック済みなら内部を塗りつぶし
		if (m_darkTheme)
		{
			const sgc::AABB2f innerAABB{
				{rect.x() + 5.0f, rect.y() + 5.0f},
				{rect.x() + rect.width() - 5.0f, rect.y() + rect.height() - 5.0f}};
			data.renderer->drawRect(innerAABB, theme.accent);
		}

		// ラベル
		data.textRenderer->drawText(
			"Dark Theme", theme.fontSizeSmall,
			sgc::Vec2f{rect.x() + CHECKBOX_SIZE + 12.0f, rect.y() + 4.0f},
			theme.text);
	}

	/// @brief プログレスバーを描画する
	/// @param theme テーマ
	void drawProgressBar(const sgc::ui::Theme& theme) const
	{
		const auto& data = getData();
		const auto rect = progressBounds();

		// 背景
		const sgc::AABB2f barAABB{
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()}};
		data.renderer->drawRect(barAABB,
			sgc::Colorf{0.12f, 0.12f, 0.15f, 0.9f});
		data.renderer->drawRectFrame(barAABB, 1.0f,
			sgc::Colorf{0.4f, 0.4f, 0.45f, 1.0f});

		// evaluateProgressBarで進行率を算出
		const auto pbResult = sgc::ui::evaluateProgressBar(
			rect, m_sliderValue, SLIDER_MIN, SLIDER_MAX);

		// 塗りつぶし部分（色はratioに応じてグラデーション）
		if (pbResult.filledWidth > 0.0f)
		{
			const sgc::Colorf barColor{
				0.2f + 0.5f * pbResult.ratio,
				0.7f - 0.2f * pbResult.ratio,
				0.9f - 0.5f * pbResult.ratio,
				1.0f};
			const sgc::AABB2f fillAABB{
				{pbResult.filledRect.x(), pbResult.filledRect.y()},
				{pbResult.filledRect.x() + pbResult.filledRect.width(),
				 pbResult.filledRect.y() + pbResult.filledRect.height()}};
			data.renderer->drawRect(fillAABB, barColor);
		}
	}
};
