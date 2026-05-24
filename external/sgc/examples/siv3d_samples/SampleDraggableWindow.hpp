#pragma once

/// @file SampleDraggableWindow.hpp
/// @brief ドラッグ可能ウィンドウのサンプル

#include "SharedData.hpp"
#include <sgc/ui/DraggableWindow.hpp>
#include <sgc/ui/Button.hpp>
#include <sgc/ui/Slider.hpp>

/// @brief ドラッグ可能ウィンドウのデモ
class SampleDraggableWindow : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	void update(float dt) override
	{
		(void)dt;
		const auto* input = getData().inputProvider;
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		const sgc::Vec2f mousePos = input->mousePosition();
		const bool mouseDown = input->isMouseButtonDown(0);
		const bool mousePressed = input->isMouseButtonPressed(0);
		const sgc::Rectf screen{0.0f, 0.0f, getData().screenWidth, getData().screenHeight};

		// ウィンドウ1の評価
		auto r1 = sgc::ui::evaluateDragWindow(m_win1State, mousePos, mouseDown, mousePressed, screen, 28.0f);
		m_win1State = r1.newState;
		m_win1Result = r1;
		if (r1.closeClicked) m_win1Visible = false;
		if (r1.minimizeClicked) {} // 状態は自動更新される

		// ウィンドウ2の評価
		auto r2 = sgc::ui::evaluateDragWindow(m_win2State, mousePos, mouseDown, mousePressed, screen, 28.0f);
		m_win2State = r2.newState;
		m_win2Result = r2;
		if (r2.closeClicked) m_win2Visible = false;

		// リセットボタン
		const sgc::Rectf resetBtn{10.0f, getData().screenHeight - 40.0f, 120.0f, 30.0f};
		auto resetResult = sgc::ui::evaluateButton(resetBtn, mousePos, mouseDown, mousePressed);
		m_resetHovered = (resetResult.state == sgc::ui::WidgetState::Hovered || resetResult.state == sgc::ui::WidgetState::Pressed);
		if (resetResult.clicked)
		{
			m_win1Visible = true;
			m_win2Visible = true;
			m_win1State = sgc::ui::DragWindowState{{50.0f, 60.0f}, {280.0f, 300.0f}};
			m_win2State = sgc::ui::DragWindowState{{380.0f, 100.0f}, {320.0f, 250.0f}};
		}

		// スライダー（ウィンドウ1のコンテンツ内）
		if (m_win1Visible && !m_win1State.minimized)
		{
			const auto& cb = m_win1Result.contentBounds;
			const sgc::Rectf sliderBounds{cb.x() + 10.0f, cb.y() + 60.0f, cb.width() - 20.0f, 16.0f};
			auto sliderR = sgc::ui::evaluateSlider(sliderBounds, mousePos, mouseDown, mousePressed,
				m_sliderValue, 0.0f, 1.0f, m_sliderDragging);
			m_sliderValue = sliderR.value;
			m_sliderDragging = sliderR.dragging;
		}
	}

	void draw() const override
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;
		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.15f});

		t->drawText("Draggable Window Demo", 24.0f, {10.0f, 10.0f}, sgc::Colorf{0.9f, 0.9f, 0.95f});
		t->drawText("Drag title bar to move, resize from corner", 14.0f, {10.0f, 40.0f}, sgc::Colorf{0.5f, 0.5f, 0.65f});

		// ウィンドウ1
		if (m_win1Visible)
		{
			drawWindow(m_win1Result, "Settings", sgc::Colorf{0.0f, 0.7f, 1.0f});

			// コンテンツ
			if (!m_win1State.minimized)
			{
				const auto& cb = m_win1Result.contentBounds;
				t->drawText("Volume:", 14.0f, {cb.x() + 10.0f, cb.y() + 10.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f});

				// スライダー描画
				const sgc::Rectf sliderTrack{cb.x() + 10.0f, cb.y() + 60.0f, cb.width() - 20.0f, 16.0f};
				r->drawRect(sgc::AABB2f{{sliderTrack.x(), sliderTrack.y()},
					{sliderTrack.right(), sliderTrack.bottom()}},
					sgc::Colorf{0.2f, 0.2f, 0.3f});
				r->drawRect(sgc::AABB2f{{sliderTrack.x(), sliderTrack.y()},
					{sliderTrack.x() + sliderTrack.width() * m_sliderValue, sliderTrack.bottom()}},
					sgc::Colorf{0.0f, 0.7f, 1.0f, 0.7f});

				char buf[32];
				std::snprintf(buf, sizeof(buf), "Value: %.0f%%", m_sliderValue * 100.0f);
				t->drawText(buf, 12.0f, {cb.x() + 10.0f, cb.y() + 85.0f}, sgc::Colorf{0.6f, 0.6f, 0.7f});
			}
		}

		// ウィンドウ2
		if (m_win2Visible)
		{
			drawWindow(m_win2Result, "Info", sgc::Colorf{1.0f, 0.5f, 0.0f});

			if (!m_win2State.minimized)
			{
				const auto& cb = m_win2Result.contentBounds;
				t->drawText("This window can also", 14.0f, {cb.x() + 10.0f, cb.y() + 10.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f});
				t->drawText("be dragged and resized.", 14.0f, {cb.x() + 10.0f, cb.y() + 30.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f});
				t->drawText("Try minimize or close!", 14.0f, {cb.x() + 10.0f, cb.y() + 50.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f});
			}
		}

		// リセットボタン
		const sgc::Colorf resetColor = m_resetHovered
			? sgc::Colorf{0.3f, 0.5f, 0.7f}
			: sgc::Colorf{0.15f, 0.2f, 0.3f};
		const sgc::Rectf resetBtn{10.0f, getData().screenHeight - 40.0f, 120.0f, 30.0f};
		r->drawRect(sgc::AABB2f{{resetBtn.x(), resetBtn.y()}, {resetBtn.right(), resetBtn.bottom()}}, resetColor);
		t->drawTextCentered("Reset", 14.0f, {resetBtn.x() + 60.0f, resetBtn.y() + 15.0f}, sgc::Colorf{0.9f, 0.9f, 0.95f});

		t->drawText("[ESC] Back", 12.0f, {getData().screenWidth - 100.0f, getData().screenHeight - 20.0f}, sgc::Colorf{0.4f, 0.4f, 0.5f});
	}

private:
	sgc::ui::DragWindowState m_win1State{{50.0f, 60.0f}, {280.0f, 300.0f}};
	sgc::ui::DragWindowState m_win2State{{380.0f, 100.0f}, {320.0f, 250.0f}};
	sgc::ui::DragWindowResult m_win1Result;
	sgc::ui::DragWindowResult m_win2Result;
	bool m_win1Visible = true;
	bool m_win2Visible = true;
	float m_sliderValue = 0.5f;
	bool m_sliderDragging = false;
	bool m_resetHovered = false;

	/// @brief ウィンドウの共通描画
	void drawWindow(const sgc::ui::DragWindowResult& wr, const char* title, const sgc::Colorf& accent) const
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;

		// 背景
		r->drawRect(sgc::AABB2f{
			{wr.outerBounds.x(), wr.outerBounds.y()},
			{wr.outerBounds.right(), wr.outerBounds.bottom()}},
			sgc::Colorf{0.05f, 0.05f, 0.1f, 0.95f});

		// 枠線
		r->drawRectFrame(sgc::AABB2f{
			{wr.outerBounds.x(), wr.outerBounds.y()},
			{wr.outerBounds.right(), wr.outerBounds.bottom()}},
			1.5f, sgc::Colorf{accent.r, accent.g, accent.b, 0.6f});

		// タイトルバー背景
		r->drawRect(sgc::AABB2f{
			{wr.titleBounds.x(), wr.titleBounds.y()},
			{wr.titleBounds.right(), wr.titleBounds.bottom()}},
			sgc::Colorf{accent.r * 0.3f, accent.g * 0.3f, accent.b * 0.3f, 0.8f});

		// タイトルテキスト
		t->drawText(title, 14.0f, {wr.titleBounds.x() + 8.0f, wr.titleBounds.y() + 7.0f},
			sgc::Colorf{0.9f, 0.9f, 0.95f});

		// 閉じるボタン
		r->drawRect(sgc::AABB2f{
			{wr.closeButtonBounds.x(), wr.closeButtonBounds.y()},
			{wr.closeButtonBounds.right(), wr.closeButtonBounds.bottom()}},
			sgc::Colorf{0.8f, 0.2f, 0.2f, 0.6f});
		t->drawTextCentered("X", 12.0f,
			{wr.closeButtonBounds.x() + 10.0f, wr.closeButtonBounds.y() + 10.0f},
			sgc::Colorf{1.0f, 1.0f, 1.0f});

		// 最小化ボタン
		r->drawRect(sgc::AABB2f{
			{wr.minimizeButtonBounds.x(), wr.minimizeButtonBounds.y()},
			{wr.minimizeButtonBounds.right(), wr.minimizeButtonBounds.bottom()}},
			sgc::Colorf{0.5f, 0.5f, 0.2f, 0.6f});
		t->drawTextCentered("_", 12.0f,
			{wr.minimizeButtonBounds.x() + 10.0f, wr.minimizeButtonBounds.y() + 10.0f},
			sgc::Colorf{1.0f, 1.0f, 1.0f});

		// リサイズハンドル
		if (wr.resizeHandleBounds.width() > 0.0f)
		{
			r->drawLine(
				{wr.resizeHandleBounds.right() - 3.0f, wr.resizeHandleBounds.bottom()},
				{wr.resizeHandleBounds.right(), wr.resizeHandleBounds.bottom() - 3.0f},
				1.0f, sgc::Colorf{accent.r, accent.g, accent.b, 0.5f});
			r->drawLine(
				{wr.resizeHandleBounds.right() - 7.0f, wr.resizeHandleBounds.bottom()},
				{wr.resizeHandleBounds.right(), wr.resizeHandleBounds.bottom() - 7.0f},
				1.0f, sgc::Colorf{accent.r, accent.g, accent.b, 0.5f});
		}
	}
};
