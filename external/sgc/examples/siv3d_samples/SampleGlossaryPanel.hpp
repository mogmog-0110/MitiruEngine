#pragma once

/// @file SampleGlossaryPanel.hpp
/// @brief 用語辞典パネルのサンプル

#include "SharedData.hpp"
#include <sgc/ui/GlossaryPanel.hpp>
#include <sgc/ui/Button.hpp>
#include <cmath>

/// @brief 用語辞典パネルのデモ
class SampleGlossaryPanel : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	void onEnter() override
	{
		m_glossaryState.isOpen = true;
		m_glossaryState.currentPage = 0;
		m_glossaryState.totalPages = 5;
	}

	void update(float dt) override
	{
		m_time += dt;

		const auto* input = getData().inputProvider;
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			if (m_glossaryState.isOpen)
			{
				m_glossaryState.isOpen = false;
			}
			else
			{
				getSceneManager().changeScene("menu"_hash, 0.3f);
				return;
			}
		}

		const sgc::Vec2f mousePos = input->mousePosition();
		const bool mouseDown = input->isMouseButtonDown(0);
		const bool mousePressed = input->isMouseButtonPressed(0);

		if (m_glossaryState.isOpen)
		{
			const auto panelBounds = sgc::ui::glossaryCenterBounds(
				{getData().screenWidth, getData().screenHeight}, 0.65f, 0.75f);

			auto result = sgc::ui::evaluateGlossary(
				m_glossaryState, panelBounds, mousePos, mouseDown, mousePressed);
			m_glossaryState = result.newState;
			m_glossaryResult = result;
		}

		// 開くボタン
		if (!m_glossaryState.isOpen)
		{
			const sgc::Rectf openBtn{
				getData().screenWidth * 0.5f - 80.0f,
				getData().screenHeight * 0.5f - 20.0f,
				160.0f, 40.0f
			};
			auto openResult = sgc::ui::evaluateButton(openBtn, mousePos, mouseDown, mousePressed);
			m_openHovered = (openResult.state == sgc::ui::WidgetState::Hovered || openResult.state == sgc::ui::WidgetState::Pressed);
			if (openResult.clicked)
			{
				m_glossaryState.isOpen = true;
				m_glossaryState.currentPage = 0;
			}
		}

		// キーボードでのページ送り
		if (m_glossaryState.isOpen)
		{
			if (input->isKeyJustPressed(KeyCode::LEFT) && m_glossaryState.currentPage > 0)
			{
				m_glossaryState.currentPage--;
			}
			if (input->isKeyJustPressed(KeyCode::RIGHT) && m_glossaryState.currentPage < m_glossaryState.totalPages - 1)
			{
				m_glossaryState.currentPage++;
			}
		}
	}

	void draw() const override
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.12f});

		t->drawText("Glossary Panel Demo", 24.0f, {10.0f, 10.0f}, sgc::Colorf{0.9f, 0.9f, 0.95f});
		t->drawText("[Left/Right] Page  [ESC] Close/Back", 12.0f, {10.0f, 40.0f}, sgc::Colorf{0.5f, 0.5f, 0.65f});

		if (!m_glossaryState.isOpen)
		{
			// 開くボタン
			const sgc::Rectf openBtn{sw * 0.5f - 80.0f, sh * 0.5f - 20.0f, 160.0f, 40.0f};
			const auto btnColor = m_openHovered
				? sgc::Colorf{0.2f, 0.4f, 0.7f}
				: sgc::Colorf{0.1f, 0.2f, 0.4f};
			r->drawRect(sgc::AABB2f{{openBtn.x(), openBtn.y()}, {openBtn.right(), openBtn.bottom()}}, btnColor);
			r->drawRectFrame(sgc::AABB2f{{openBtn.x(), openBtn.y()}, {openBtn.right(), openBtn.bottom()}},
				1.0f, sgc::Colorf{0.4f, 0.6f, 1.0f, 0.6f});
			t->drawTextCentered("Open Glossary", 16.0f,
				{openBtn.x() + 80.0f, openBtn.y() + 20.0f}, sgc::Colorf{0.9f, 0.9f, 0.95f});
		}
		else
		{
			// オーバーレイ
			r->drawFadeOverlay(0.5f, sgc::Colorf{0.0f, 0.0f, 0.05f});

			const auto& gr = m_glossaryResult;

			// パネル背景
			r->drawRect(sgc::AABB2f{
				{gr.outerBounds.x(), gr.outerBounds.y()},
				{gr.outerBounds.right(), gr.outerBounds.bottom()}},
				sgc::Colorf{0.04f, 0.04f, 0.1f, 0.97f});
			r->drawRectFrame(sgc::AABB2f{
				{gr.outerBounds.x(), gr.outerBounds.y()},
				{gr.outerBounds.right(), gr.outerBounds.bottom()}},
				1.5f, sgc::Colorf{0.4f, 0.5f, 0.8f, 0.7f});

			// タイトル
			const char* titles[] = {"Linear Function (y=ax+b)", "Sine Function (y=sin(x))", "Cosine Function (y=cos(x))", "Quadratic (y=x^2)", "Absolute Value (y=|x|)"};
			t->drawText(titles[m_glossaryState.currentPage], 20.0f,
				{gr.titleBounds.x() + 4.0f, gr.titleBounds.y() + 8.0f},
				sgc::Colorf{1.0f, 0.9f, 0.4f});

			// 閉じるボタン
			const auto closeColor = gr.closeHovered
				? sgc::Colorf{0.8f, 0.2f, 0.2f, 0.8f}
				: sgc::Colorf{0.5f, 0.15f, 0.15f, 0.6f};
			r->drawRect(sgc::AABB2f{
				{gr.closeButtonBounds.x(), gr.closeButtonBounds.y()},
				{gr.closeButtonBounds.right(), gr.closeButtonBounds.bottom()}}, closeColor);
			t->drawTextCentered("X", 14.0f,
				{gr.closeButtonBounds.x() + 14.0f, gr.closeButtonBounds.y() + 14.0f},
				sgc::Colorf{1.0f, 1.0f, 1.0f});

			// 図表エリア（簡易グラフ描画）
			r->drawRect(sgc::AABB2f{
				{gr.diagramBounds.x(), gr.diagramBounds.y()},
				{gr.diagramBounds.right(), gr.diagramBounds.bottom()}},
				sgc::Colorf{0.02f, 0.02f, 0.06f, 0.8f});
			r->drawRectFrame(sgc::AABB2f{
				{gr.diagramBounds.x(), gr.diagramBounds.y()},
				{gr.diagramBounds.right(), gr.diagramBounds.bottom()}},
				0.5f, sgc::Colorf{0.3f, 0.3f, 0.5f, 0.5f});

			// グリッド線
			const float gx = gr.diagramBounds.x();
			const float gy = gr.diagramBounds.y();
			const float gw = gr.diagramBounds.width();
			const float gh = gr.diagramBounds.height();
			r->drawLine({gx + gw * 0.5f, gy}, {gx + gw * 0.5f, gy + gh}, 0.5f, sgc::Colorf{0.3f, 0.3f, 0.5f, 0.4f});
			r->drawLine({gx, gy + gh * 0.5f}, {gx + gw, gy + gh * 0.5f}, 0.5f, sgc::Colorf{0.3f, 0.3f, 0.5f, 0.4f});

			// 簡易関数グラフ
			drawFunctionGraph(m_glossaryState.currentPage, gr.diagramBounds);

			// 本文エリア
			const char* bodies[] = {
				"y=ax+b is a linear function.\n'a' is the slope, 'b' is the y-intercept.\nWhen a>0, the line goes up.\nWhen a<0, the line goes down.",
				"y=sin(x) creates a wave pattern.\nIt oscillates between -1 and 1.\nPeriod: 2*pi (about 6.28).\nUseful for wavy terrain!",
				"y=cos(x) is similar to sin(x).\nIt starts at y=1 when x=0.\ncos(x) = sin(x + pi/2).\nGreat for circular patterns!",
				"y=x^2 creates a parabola.\nIt opens upward, vertex at origin.\nAlways positive (y >= 0).\nUseful for arching bridges!",
				"y=|x| creates a V-shape.\nAlways positive (y >= 0).\n|x| = x when x>=0.\n|x| = -x when x<0.",
			};
			t->drawText(bodies[m_glossaryState.currentPage], 14.0f,
				{gr.bodyBounds.x() + 4.0f, gr.bodyBounds.y() + 4.0f},
				sgc::Colorf{0.8f, 0.8f, 0.9f});

			// ナビゲーション
			const auto prevColor = gr.prevHovered && m_glossaryState.currentPage > 0
				? sgc::Colorf{0.2f, 0.3f, 0.6f, 0.8f}
				: sgc::Colorf{0.1f, 0.15f, 0.3f, 0.6f};
			r->drawRect(sgc::AABB2f{
				{gr.prevButtonBounds.x(), gr.prevButtonBounds.y()},
				{gr.prevButtonBounds.right(), gr.prevButtonBounds.bottom()}}, prevColor);
			t->drawTextCentered("<", 18.0f,
				{gr.prevButtonBounds.x() + 30.0f, gr.prevButtonBounds.y() + 18.0f},
				sgc::Colorf{0.8f, 0.8f, 0.9f});

			const auto nextColor = gr.nextHovered && m_glossaryState.currentPage < m_glossaryState.totalPages - 1
				? sgc::Colorf{0.2f, 0.3f, 0.6f, 0.8f}
				: sgc::Colorf{0.1f, 0.15f, 0.3f, 0.6f};
			r->drawRect(sgc::AABB2f{
				{gr.nextButtonBounds.x(), gr.nextButtonBounds.y()},
				{gr.nextButtonBounds.right(), gr.nextButtonBounds.bottom()}}, nextColor);
			t->drawTextCentered(">", 18.0f,
				{gr.nextButtonBounds.x() + 30.0f, gr.nextButtonBounds.y() + 18.0f},
				sgc::Colorf{0.8f, 0.8f, 0.9f});

			// ページ番号
			char pageBuf[32];
			std::snprintf(pageBuf, sizeof(pageBuf), "%d / %d",
				m_glossaryState.currentPage + 1, m_glossaryState.totalPages);
			t->drawTextCentered(pageBuf, 16.0f,
				{gr.pageIndicatorBounds.x() + gr.pageIndicatorBounds.width() * 0.5f,
				 gr.pageIndicatorBounds.y() + 18.0f},
				sgc::Colorf{0.6f, 0.6f, 0.7f});
		}

		t->drawText("[ESC] Back", 12.0f, {sw - 100.0f, sh - 20.0f}, sgc::Colorf{0.4f, 0.4f, 0.5f});
	}

private:
	float m_time = 0.0f;
	sgc::ui::GlossaryState m_glossaryState;
	sgc::ui::GlossaryResult m_glossaryResult;
	bool m_openHovered = false;

	/// @brief 簡易関数グラフ描画
	void drawFunctionGraph(int page, const sgc::Rectf& area) const
	{
		auto* r = getData().renderer;
		const float cx = area.x() + area.width() * 0.5f;
		const float cy = area.y() + area.height() * 0.5f;
		const float scaleX = area.width() * 0.08f;
		const float scaleY = area.height() * 0.15f;
		const sgc::Colorf lineColor{0.0f, 0.9f, 1.0f, 0.9f};

		for (float x = -5.0f; x < 4.9f; x += 0.1f)
		{
			float y1 = 0.0f, y2 = 0.0f;
			const float x2 = x + 0.1f;

			switch (page)
			{
			case 0: y1 = x; y2 = x2; break;              // y=x
			case 1: y1 = std::sin(x); y2 = std::sin(x2); break;
			case 2: y1 = std::cos(x); y2 = std::cos(x2); break;
			case 3: y1 = x * x * 0.3f; y2 = x2 * x2 * 0.3f; break;
			case 4: y1 = std::abs(x); y2 = std::abs(x2); break;
			}

			r->drawLine(
				{cx + x * scaleX, cy - y1 * scaleY},
				{cx + x2 * scaleX, cy - y2 * scaleY},
				1.5f, lineColor);
		}
	}
};
