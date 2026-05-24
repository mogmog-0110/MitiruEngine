#pragma once

/// @file SampleSettingsScreen.hpp
/// @brief 設定画面UIデモシーン
///
/// 複数のUIウィジェットを組み合わせて実用的な設定画面を構成する。
/// Panel + StackLayout でレイアウトし、ToggleSwitch / Slider / RadioButton を配置。
/// - ESC: メニューに戻る

#include <array>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Panel.hpp"
#include "sgc/ui/RadioButton.hpp"
#include "sgc/ui/Slider.hpp"
#include "sgc/ui/StackLayout.hpp"
#include "sgc/ui/ToggleSwitch.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief 設定画面UIデモシーン
///
/// パネル内にToggleSwitch・Slider・RadioButtonを積み重ねた設定画面。
/// 実際のゲーム設定画面に近い構成をデモする。
class SampleSettingsScreen : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_music = true;
		m_sfx = true;
		m_fullscreen = false;
		m_vsync = true;
		m_volume = 75.0f;
		m_difficulty = 1;
		m_volumeDragging = false;
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
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

		// レイアウト計算
		const auto rows = computeRows();

		updateToggleInRow(rows[0], mousePos, mouseDown, mousePressed, m_music);
		updateToggleInRow(rows[1], mousePos, mouseDown, mousePressed, m_sfx);
		updateSliderInRow(rows[2], mousePos, mouseDown, mousePressed);
		updateRadioInRow(rows[3], mousePos, mouseDown, mousePressed);
		updateToggleInRow(rows[4], mousePos, mouseDown, mousePressed, m_fullscreen);
		updateToggleInRow(rows[5], mousePos, mouseDown, mousePressed, m_vsync);
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 背景クリア
		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.11f});

		// パネル外枠
		const auto outerBounds = panelBounds(sw, sh);
		const sgc::AABB2f outerBox = outerBounds.toAABB2();
		r->drawRect(outerBox, PANEL_BG);
		r->drawRectFrame(outerBox, 2.0f, sgc::Colorf{0.3f, 0.3f, 0.4f});

		// パネル分割
		const auto padding = sgc::ui::Padding::uniform(PADDING);
		const auto panel = sgc::ui::evaluatePanel(outerBounds, TITLE_HEIGHT, padding);

		// タイトルバー
		drawTitleBar(r, text, panel.titleBounds);

		// コンテンツ行
		const auto rows = sgc::ui::vstackFixed(panel.contentBounds, ROW_COUNT, ROW_SPACING);

		// 各行の描画
		drawToggleRow(r, text, rows[0], "Music", m_music, 0);
		drawToggleRow(r, text, rows[1], "Sound Effects", m_sfx, 1);
		drawSliderRow(r, text, rows[2], "Volume", 2);
		drawRadioRow(r, text, rows[3], 3);
		drawToggleRow(r, text, rows[4], "Fullscreen", m_fullscreen, 4);
		drawToggleRow(r, text, rows[5], "VSync", m_vsync, 5);

		// 下部ヒント
		text->drawText(
			"[Esc] Back", 12.0f,
			sgc::Vec2f{outerBounds.x() + PADDING, outerBounds.y() + outerBounds.height() + 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f});
	}

private:
	static constexpr float PANEL_W = 500.0f;
	static constexpr float PANEL_H = 420.0f;
	static constexpr float TITLE_HEIGHT = 36.0f;
	static constexpr float PADDING = 12.0f;
	static constexpr float ROW_SPACING = 8.0f;
	static constexpr std::size_t ROW_COUNT = 6;
	static constexpr float LABEL_RATIO = 0.4f;
	static constexpr float WIDGET_RATIO = 0.6f;
	static constexpr float TOGGLE_W = 50.0f;
	static constexpr float TOGGLE_H = 24.0f;
	static constexpr float RADIO_SIZE = 20.0f;
	static constexpr int DIFFICULTY_COUNT = 3;

	static constexpr sgc::Colorf PANEL_BG{0.12f, 0.12f, 0.15f};
	static constexpr sgc::Colorf TITLE_BAR_COLOR{0.2f, 0.3f, 0.6f};
	static constexpr sgc::Colorf ROW_BG_EVEN{0.14f, 0.14f, 0.17f};
	static constexpr sgc::Colorf ROW_BG_ODD{0.16f, 0.16f, 0.19f};
	static constexpr std::array<const char*, DIFFICULTY_COUNT> DIFF_LABELS{
		"Easy", "Normal", "Hard"
	};

	bool m_music{true};
	bool m_sfx{true};
	bool m_fullscreen{false};
	bool m_vsync{true};
	float m_volume{75.0f};
	int m_difficulty{1};
	bool m_volumeDragging{false};

	/// @brief パネル全体の矩形を計算する（画面中央配置）
	[[nodiscard]] static sgc::Rectf panelBounds(float sw, float sh) noexcept
	{
		const float px = (sw - PANEL_W) * 0.5f;
		const float py = (sh - PANEL_H) * 0.5f - 10.0f;
		return {px, py, PANEL_W, PANEL_H};
	}

	/// @brief コンテンツ行の矩形リストを計算する
	[[nodiscard]] std::vector<sgc::Rectf> computeRows() const
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const auto outer = panelBounds(sw, sh);
		const auto padding = sgc::ui::Padding::uniform(PADDING);
		const auto panel = sgc::ui::evaluatePanel(outer, TITLE_HEIGHT, padding);
		return sgc::ui::vstackFixed(panel.contentBounds, ROW_COUNT, ROW_SPACING);
	}

	/// @brief 行をラベル領域とウィジェット領域に分割する
	[[nodiscard]] static std::array<sgc::Rectf, 2> splitRow(const sgc::Rectf& row)
	{
		const float labelW = row.width() * LABEL_RATIO;
		const float widgetW = row.width() * WIDGET_RATIO;
		const float widths[] = {labelW, widgetW};
		auto cols = sgc::ui::hstack(row, std::span<const float>{widths, 2});
		return {cols[0], cols[1]};
	}

	/// @brief トグル行の入力を処理する
	void updateToggleInRow(
		const sgc::Rectf& row, const sgc::Vec2f& mousePos,
		bool mouseDown, bool mousePressed, bool& value)
	{
		const auto [labelArea, widgetArea] = splitRow(row);
		const auto toggleBounds = toggleRectInWidget(widgetArea);
		auto result = sgc::ui::evaluateToggle(
			toggleBounds, mousePos, mouseDown, mousePressed, value);
		if (result.toggled)
		{
			value = result.value;
		}
	}

	/// @brief スライダー行の入力を処理する
	void updateSliderInRow(
		const sgc::Rectf& row, const sgc::Vec2f& mousePos,
		bool mouseDown, bool mousePressed)
	{
		const auto [labelArea, widgetArea] = splitRow(row);
		const auto sliderBounds = sliderRectInWidget(widgetArea);
		auto result = sgc::ui::evaluateSlider(
			sliderBounds, mousePos, mouseDown, mousePressed,
			m_volume, 0.0f, 100.0f, m_volumeDragging);
		if (result.changed)
		{
			m_volume = result.value;
		}
		m_volumeDragging = result.dragging;
	}

	/// @brief ラジオボタン行の入力を処理する
	void updateRadioInRow(
		const sgc::Rectf& row, const sgc::Vec2f& mousePos,
		bool mouseDown, bool mousePressed)
	{
		const auto [labelArea, widgetArea] = splitRow(row);
		for (int i = 0; i < DIFFICULTY_COUNT; ++i)
		{
			const auto radioBounds = radioRectInWidget(widgetArea, i);
			auto result = sgc::ui::evaluateRadio(
				radioBounds, mousePos, mouseDown, mousePressed,
				i, m_difficulty);
			if (result.changed)
			{
				m_difficulty = result.selected;
			}
		}
	}

	/// @brief ウィジェット領域内のトグル矩形を計算する
	[[nodiscard]] static sgc::Rectf toggleRectInWidget(const sgc::Rectf& widget) noexcept
	{
		const float x = widget.x() + 8.0f;
		const float y = widget.y() + (widget.height() - TOGGLE_H) * 0.5f;
		return {x, y, TOGGLE_W, TOGGLE_H};
	}

	/// @brief ウィジェット領域内のスライダー矩形を計算する
	[[nodiscard]] static sgc::Rectf sliderRectInWidget(const sgc::Rectf& widget) noexcept
	{
		constexpr float hPad = 8.0f;
		constexpr float sliderH = 20.0f;
		const float x = widget.x() + hPad;
		const float w = widget.width() - hPad * 2.0f - 50.0f;
		const float y = widget.y() + (widget.height() - sliderH) * 0.5f;
		return {x, y, w, sliderH};
	}

	/// @brief ウィジェット領域内のラジオボタン矩形を計算する
	[[nodiscard]] static sgc::Rectf radioRectInWidget(
		const sgc::Rectf& widget, int index) noexcept
	{
		const float spacing = widget.width() / static_cast<float>(DIFFICULTY_COUNT);
		const float x = widget.x() + static_cast<float>(index) * spacing + 4.0f;
		const float y = widget.y() + (widget.height() - RADIO_SIZE) * 0.5f;
		return {x, y, RADIO_SIZE, RADIO_SIZE};
	}

	/// @brief タイトルバーを描画する
	static void drawTitleBar(
		sgc::IRenderer* r, sgc::ITextRenderer* text,
		const sgc::Rectf& titleBounds)
	{
		const sgc::AABB2f titleBox = titleBounds.toAABB2();
		r->drawRect(titleBox, TITLE_BAR_COLOR);

		const sgc::Vec2f center{
			titleBounds.x() + titleBounds.width() * 0.5f,
			titleBounds.y() + titleBounds.height() * 0.5f};
		text->drawTextCentered(
			"Settings", 20.0f, center,
			sgc::Colorf{0.95f, 0.95f, 0.95f});
	}

	/// @brief 行の背景を描画する（交互色）
	static void drawRowBackground(
		sgc::IRenderer* r, const sgc::Rectf& row, int index)
	{
		const sgc::Colorf& bg = (index % 2 == 0) ? ROW_BG_EVEN : ROW_BG_ODD;
		const sgc::AABB2f rowBox = row.toAABB2();
		r->drawRect(rowBox, bg);
	}

	/// @brief ラベルを行の左側に描画する
	static void drawLabel(
		sgc::ITextRenderer* text, const sgc::Rectf& labelArea,
		const char* label)
	{
		const float x = labelArea.x() + 12.0f;
		const float y = labelArea.y() + (labelArea.height() - 16.0f) * 0.5f;
		text->drawText(label, 16.0f, sgc::Vec2f{x, y},
			sgc::Colorf{0.85f, 0.85f, 0.9f});
	}

	/// @brief トグル付きの行を描画する
	void drawToggleRow(
		sgc::IRenderer* r, sgc::ITextRenderer* text,
		const sgc::Rectf& row, const char* label, bool isOn, int index) const
	{
		drawRowBackground(r, row, index);
		const auto [labelArea, widgetArea] = splitRow(row);
		drawLabel(text, labelArea, label);

		// トグルトラック
		const auto bounds = toggleRectInWidget(widgetArea);
		const sgc::Colorf trackColor = isOn
			? sgc::Colorf{0.2f, 0.7f, 0.3f}
			: sgc::Colorf{0.3f, 0.3f, 0.35f};
		const sgc::AABB2f trackBox = bounds.toAABB2();
		r->drawRect(trackBox, trackColor);
		r->drawRectFrame(trackBox, 1.0f, sgc::Colorf{0.4f, 0.4f, 0.45f});

		// ノブ（白い正方形）
		const auto knob = sgc::ui::toggleKnobRect(bounds, isOn);
		const sgc::AABB2f knobBox = knob.toAABB2();
		r->drawRect(knobBox, sgc::Colorf::white());

		// ON/OFF ラベル
		const char* stateText = isOn ? "ON" : "OFF";
		const sgc::Colorf stateColor = isOn
			? sgc::Colorf{0.3f, 0.9f, 0.4f}
			: sgc::Colorf{0.5f, 0.5f, 0.55f};
		text->drawText(stateText, 14.0f,
			sgc::Vec2f{bounds.x() + TOGGLE_W + 10.0f,
				bounds.y() + (TOGGLE_H - 14.0f) * 0.5f},
			stateColor);
	}

	/// @brief スライダー付きの行を描画する
	void drawSliderRow(
		sgc::IRenderer* r, sgc::ITextRenderer* text,
		const sgc::Rectf& row, const char* label, int index) const
	{
		drawRowBackground(r, row, index);
		const auto [labelArea, widgetArea] = splitRow(row);
		drawLabel(text, labelArea, label);

		// スライダートラック
		const auto bounds = sliderRectInWidget(widgetArea);
		const sgc::AABB2f trackBox = bounds.toAABB2();
		r->drawRect(trackBox, sgc::Colorf{0.15f, 0.15f, 0.2f});
		r->drawRectFrame(trackBox, 1.0f, sgc::Colorf{0.3f, 0.3f, 0.35f});

		// 塗りつぶし部分
		const float ratio = m_volume / 100.0f;
		const float fillW = bounds.width() * ratio;
		if (fillW > 0.0f)
		{
			const sgc::AABB2f fillBox{
				{bounds.x(), bounds.y()},
				{bounds.x() + fillW, bounds.y() + bounds.height()}};
			r->drawRect(fillBox, sgc::Colorf{0.3f, 0.5f, 0.9f, 0.7f});
		}

		// ハンドル（円）
		const float knobX = bounds.x() + fillW;
		const float knobY = bounds.y() + bounds.height() * 0.5f;
		r->drawCircle(sgc::Vec2f{knobX, knobY}, 8.0f, sgc::Colorf::white());
		r->drawCircleFrame(sgc::Vec2f{knobX, knobY}, 8.0f, 2.0f,
			sgc::Colorf{0.3f, 0.5f, 0.9f});

		// 値テキスト
		const int pct = static_cast<int>(m_volume + 0.5f);
		text->drawText(std::to_string(pct), 14.0f,
			sgc::Vec2f{bounds.x() + bounds.width() + 12.0f,
				bounds.y() + (bounds.height() - 14.0f) * 0.5f},
			sgc::Colorf{0.7f, 0.7f, 0.7f});
	}

	/// @brief ラジオボタン付きの行を描画する
	void drawRadioRow(
		sgc::IRenderer* r, sgc::ITextRenderer* text,
		const sgc::Rectf& row, int index) const
	{
		drawRowBackground(r, row, index);
		const auto [labelArea, widgetArea] = splitRow(row);
		drawLabel(text, labelArea, "Difficulty");

		for (int i = 0; i < DIFFICULTY_COUNT; ++i)
		{
			const auto bounds = radioRectInWidget(widgetArea, i);
			const auto center = sgc::ui::radioCircleCenter(bounds);
			const float radius = RADIO_SIZE * 0.5f;
			const bool selected = (i == m_difficulty);

			// 外枠円
			r->drawCircleFrame(center, radius, 2.0f,
				sgc::Colorf{0.5f, 0.5f, 0.7f});

			// 選択時: 内側塗りつぶし
			if (selected)
			{
				r->drawCircle(center, radius * 0.5f,
					sgc::Colorf{0.4f, 0.6f, 1.0f});
			}

			// ラベル
			const sgc::Colorf labelColor = selected
				? sgc::Colorf{0.9f, 0.9f, 1.0f}
				: sgc::Colorf{0.6f, 0.6f, 0.65f};
			text->drawText(DIFF_LABELS[static_cast<std::size_t>(i)], 14.0f,
				sgc::Vec2f{bounds.x() + RADIO_SIZE + 4.0f,
					bounds.y() + (RADIO_SIZE - 14.0f) * 0.5f},
				labelColor);
		}
	}
};
