#pragma once

/// @file SampleUIWidgets.hpp
/// @brief UI ウィジェット(Slider/Checkbox/ProgressBar)デモシーン
///
/// 3つのスライダー(R/G/B)でプレビュー色を制御し、
/// 3つのチェックボックスで機能トグル、プログレスバーは自動進行する。
/// - ESC: メニューに戻る

#include <algorithm>
#include <cmath>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Checkbox.hpp"
#include "sgc/ui/ProgressBar.hpp"
#include "sgc/ui/Slider.hpp"
#include "sgc/ui/WidgetState.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief UIウィジェットデモシーン
///
/// Slider, Checkbox, ProgressBar を一画面で操作・確認できる。
/// スライダーでRGB色を制御し、チェックボックスで描画オプションを切り替える。
class SampleUIWidgets : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_red = 0.5f;
		m_green = 0.3f;
		m_blue = 0.8f;
		m_dragR = false;
		m_dragG = false;
		m_dragB = false;
		m_showBorder = true;
		m_showShadow = false;
		m_roundPreview = true;
		m_progress = 0.0f;
		m_progressDir = 1.0f;
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto& input = *getData().inputProvider;

		if (input.isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		const auto mousePos = input.mousePosition();
		const bool mouseDown = input.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		// ── R スライダー ──
		auto rResult = sgc::ui::evaluateSlider(
			sliderRect(0), mousePos, mouseDown, mousePressed,
			m_red, 0.0f, 1.0f, m_dragR);
		if (rResult.changed) { m_red = rResult.value; }
		m_dragR = rResult.dragging;
		m_stateR = rResult.state;

		// ── G スライダー ──
		auto gResult = sgc::ui::evaluateSlider(
			sliderRect(1), mousePos, mouseDown, mousePressed,
			m_green, 0.0f, 1.0f, m_dragG);
		if (gResult.changed) { m_green = gResult.value; }
		m_dragG = gResult.dragging;
		m_stateG = gResult.state;

		// ── B スライダー ──
		auto bResult = sgc::ui::evaluateSlider(
			sliderRect(2), mousePos, mouseDown, mousePressed,
			m_blue, 0.0f, 1.0f, m_dragB);
		if (bResult.changed) { m_blue = bResult.value; }
		m_dragB = bResult.dragging;
		m_stateB = bResult.state;

		// ── チェックボックス ──
		auto cb1 = sgc::ui::evaluateCheckbox(
			checkboxRect(0), mousePos, mouseDown, mousePressed, m_showBorder);
		if (cb1.toggled) { m_showBorder = cb1.checked; }

		auto cb2 = sgc::ui::evaluateCheckbox(
			checkboxRect(1), mousePos, mouseDown, mousePressed, m_showShadow);
		if (cb2.toggled) { m_showShadow = cb2.checked; }

		auto cb3 = sgc::ui::evaluateCheckbox(
			checkboxRect(2), mousePos, mouseDown, mousePressed, m_roundPreview);
		if (cb3.toggled) { m_roundPreview = cb3.checked; }

		// ── プログレスバー自動進行 ──
		m_progress += dt * 0.3f * m_progressDir;
		if (m_progress >= 1.0f) { m_progress = 1.0f; m_progressDir = -1.0f; }
		if (m_progress <= 0.0f) { m_progress = 0.0f; m_progressDir = 1.0f; }
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f});

		text->drawTextCentered(
			"UI Widgets - Slider / Checkbox / ProgressBar", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		text->drawText(
			"ESC: Back to Menu", 14.0f,
			sgc::Vec2f{20.0f, 60.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── スライダー描画 ──
		const char* labels[] = {"Red", "Green", "Blue"};
		const float values[] = {m_red, m_green, m_blue};
		const sgc::Colorf sliderColors[] = {
			sgc::Colorf{1.0f, 0.3f, 0.3f},
			sgc::Colorf{0.3f, 1.0f, 0.3f},
			sgc::Colorf{0.3f, 0.3f, 1.0f}
		};

		for (int i = 0; i < 3; ++i)
		{
			const auto rect = sliderRect(i);
			const float y = rect.y();

			text->drawText(labels[i], 16.0f,
				sgc::Vec2f{SLIDER_X - 70.0f, y + 2.0f}, sliderColors[i]);

			// トラック背景
			const sgc::AABB2f track = rect.toAABB2();
			renderer->drawRect(track, sgc::Colorf{0.15f, 0.15f, 0.2f});
			renderer->drawRectFrame(track, 1.0f, sgc::Colorf{0.3f, 0.3f, 0.35f});

			// 塗りつぶし
			const float fillW = rect.width() * values[i];
			if (fillW > 0.0f)
			{
				const sgc::AABB2f fill{
					{rect.x(), rect.y()},
					{rect.x() + fillW, rect.y() + rect.height()}};
				renderer->drawRect(fill, sliderColors[i].withAlpha(0.7f));
			}

			// ノブ
			const float knobX = rect.x() + fillW;
			const float knobY = rect.y() + rect.height() * 0.5f;
			renderer->drawCircle(sgc::Vec2f{knobX, knobY}, 8.0f, sgc::Colorf::white());
			renderer->drawCircleFrame(sgc::Vec2f{knobX, knobY}, 8.0f, 2.0f, sliderColors[i]);

			// 値テキスト
			const int pct = static_cast<int>(values[i] * 255.0f + 0.5f);
			text->drawText(std::to_string(pct), 14.0f,
				sgc::Vec2f{rect.x() + rect.width() + 15.0f, y + 2.0f},
				sgc::Colorf{0.7f, 0.7f, 0.7f});
		}

		// ── プレビュー色 ──
		const sgc::Colorf previewColor{m_red, m_green, m_blue};
		const float previewX = sw * 0.5f + 80.0f;
		const float previewY = SLIDER_START_Y;
		const float previewSize = 100.0f;

		if (m_showShadow)
		{
			const sgc::AABB2f shadow{
				{previewX + 4.0f, previewY + 4.0f},
				{previewX + previewSize + 4.0f, previewY + previewSize + 4.0f}};
			renderer->drawRect(shadow, sgc::Colorf{0.0f, 0.0f, 0.0f, 0.4f});
		}

		if (m_roundPreview)
		{
			renderer->drawCircle(
				sgc::Vec2f{previewX + previewSize * 0.5f, previewY + previewSize * 0.5f},
				previewSize * 0.5f, previewColor);
			if (m_showBorder)
			{
				renderer->drawCircleFrame(
					sgc::Vec2f{previewX + previewSize * 0.5f, previewY + previewSize * 0.5f},
					previewSize * 0.5f, 2.0f, sgc::Colorf::white());
			}
		}
		else
		{
			const sgc::AABB2f previewRect{
				{previewX, previewY},
				{previewX + previewSize, previewY + previewSize}};
			renderer->drawRect(previewRect, previewColor);
			if (m_showBorder)
			{
				renderer->drawRectFrame(previewRect, 2.0f, sgc::Colorf::white());
			}
		}

		// ── チェックボックス描画 ──
		const char* cbLabels[] = {"Border", "Shadow", "Round"};
		const bool cbValues[] = {m_showBorder, m_showShadow, m_roundPreview};

		text->drawText("Options", 18.0f,
			sgc::Vec2f{CHECKBOX_X, CHECKBOX_START_Y - 30.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f});

		for (int i = 0; i < 3; ++i)
		{
			const auto rect = checkboxRect(i);
			const sgc::AABB2f box = rect.toAABB2();
			renderer->drawRectFrame(box, 2.0f, sgc::Colorf{0.5f, 0.5f, 0.7f});

			if (cbValues[i])
			{
				const sgc::AABB2f inner{
					{rect.x() + 4.0f, rect.y() + 4.0f},
					{rect.x() + rect.width() - 4.0f, rect.y() + rect.height() - 4.0f}};
				renderer->drawRect(inner, sgc::Colorf{0.4f, 0.6f, 1.0f});
			}

			text->drawText(cbLabels[i], 16.0f,
				sgc::Vec2f{rect.x() + CB_SIZE + 10.0f, rect.y() + 2.0f},
				sgc::Colorf{0.8f, 0.8f, 0.85f});
		}

		// ── プログレスバー描画 ──
		text->drawText("Auto Progress", 18.0f,
			sgc::Vec2f{40.0f, PROGRESS_Y - 30.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f});

		const sgc::Rectf pRect{40.0f, PROGRESS_Y, sw - 80.0f, 24.0f};
		const auto pbResult = sgc::ui::evaluateProgressBar(
			pRect, m_progress, 0.0f, 1.0f);

		const sgc::AABB2f barBg = pRect.toAABB2();
		renderer->drawRect(barBg, sgc::Colorf{0.12f, 0.12f, 0.15f});
		renderer->drawRectFrame(barBg, 1.0f, sgc::Colorf{0.3f, 0.3f, 0.35f});

		if (pbResult.filledWidth > 0.0f)
		{
			const sgc::Colorf barColor = previewColor.withAlpha(0.8f);
			const sgc::AABB2f fill{
				{pbResult.filledRect.x(), pbResult.filledRect.y()},
				{pbResult.filledRect.x() + pbResult.filledRect.width(),
				 pbResult.filledRect.y() + pbResult.filledRect.height()}};
			renderer->drawRect(fill, barColor);
		}

		const int pctVal = static_cast<int>(pbResult.ratio * 100.0f + 0.5f);
		text->drawText(
			std::to_string(pctVal) + "%", 14.0f,
			sgc::Vec2f{pRect.x() + pRect.width() + 10.0f, PROGRESS_Y + 3.0f},
			sgc::Colorf{0.7f, 0.7f, 0.7f});
	}

private:
	// ── レイアウト定数 ──
	static constexpr float SLIDER_X = 100.0f;
	static constexpr float SLIDER_W = 280.0f;
	static constexpr float SLIDER_H = 22.0f;
	static constexpr float SLIDER_START_Y = 100.0f;
	static constexpr float SLIDER_GAP = 50.0f;

	static constexpr float CHECKBOX_X = 100.0f;
	static constexpr float CHECKBOX_START_Y = 310.0f;
	static constexpr float CB_SIZE = 22.0f;
	static constexpr float CB_GAP = 40.0f;

	static constexpr float PROGRESS_Y = 490.0f;

	// ── メンバ変数 ──
	float m_red{0.5f};
	float m_green{0.3f};
	float m_blue{0.8f};
	bool m_dragR{false};
	bool m_dragG{false};
	bool m_dragB{false};
	sgc::ui::WidgetState m_stateR{sgc::ui::WidgetState::Normal};
	sgc::ui::WidgetState m_stateG{sgc::ui::WidgetState::Normal};
	sgc::ui::WidgetState m_stateB{sgc::ui::WidgetState::Normal};

	bool m_showBorder{true};
	bool m_showShadow{false};
	bool m_roundPreview{true};

	float m_progress{0.0f};
	float m_progressDir{1.0f};

	// ── レイアウト計算 ──

	/// @brief スライダー矩形を計算する
	[[nodiscard]] static sgc::Rectf sliderRect(int index) noexcept
	{
		const float y = SLIDER_START_Y + static_cast<float>(index) * SLIDER_GAP;
		return {SLIDER_X, y, SLIDER_W, SLIDER_H};
	}

	/// @brief チェックボックス矩形を計算する
	[[nodiscard]] static sgc::Rectf checkboxRect(int index) noexcept
	{
		const float y = CHECKBOX_START_Y + static_cast<float>(index) * CB_GAP;
		return {CHECKBOX_X, y, CB_SIZE, CB_SIZE};
	}
};
