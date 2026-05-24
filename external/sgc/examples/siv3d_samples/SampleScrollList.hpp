#pragma once

/// @file SampleScrollList.hpp
/// @brief スクロールバー付きリストビューのデモシーン
///
/// 30アイテムのリストを表示し、垂直スクロールバーで操作する。
/// ビューポートには約10アイテムが表示され、残りはスクロールでアクセスする。
/// - ESC: メニューに戻る
/// - UP/DOWN: 1アイテム分スクロール
/// - SPACE: 最下部へスクロール
/// - R: スクロール位置をリセット（最上部）

#include <algorithm>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Scrollbar.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief スクロールバー付きリストビューのデモシーン
///
/// 30アイテムのリストを垂直スクロールバーで閲覧できる。
/// ドラッグ・トラッククリック・キーボードによるスクロール操作に対応。
class SampleScrollList : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_scrollPos = 0.0f;
		m_dragging = false;
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto& input = *getData().inputProvider;

		// ── ESC: メニューに戻る ──
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
		auto result = sgc::ui::evaluateScrollbar(
			trackRect, mousePos, mouseDown, mousePressed,
			m_scrollPos, CONTENT_HEIGHT, VIEWPORT_HEIGHT, m_dragging);
		if (result.changed)
		{
			m_scrollPos = result.scrollPos;
		}
		m_dragging = result.dragging;
		m_thumbRect = result.thumbRect;

		// ── キーボード操作 ──
		const float maxScroll = sgc::ui::maxScrollPos(CONTENT_HEIGHT, VIEWPORT_HEIGHT);

		if (input.isKeyJustPressed(KeyCode::UP))
		{
			m_scrollPos = std::max(0.0f, m_scrollPos - ITEM_HEIGHT);
		}
		if (input.isKeyJustPressed(KeyCode::DOWN))
		{
			m_scrollPos = std::min(maxScroll, m_scrollPos + ITEM_HEIGHT);
		}
		if (input.isKeyJustPressed(KeyCode::SPACE))
		{
			m_scrollPos = maxScroll;
		}
		if (input.isKeyJustPressed(KeyCode::R))
		{
			m_scrollPos = 0.0f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;

		renderer->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.08f});

		// ── タイトル ──
		text->drawTextCentered(
			"Scroll List - Scrollbar Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── 操作ガイド ──
		text->drawText(
			"ESC: Back | UP/DOWN: Scroll | SPACE: Bottom | R: Reset", 12.0f,
			sgc::Vec2f{20.0f, 62.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── リストエリア背景 ──
		const sgc::AABB2f listBg{
			{LIST_X - 2.0f, LIST_Y - 2.0f},
			{LIST_X + LIST_WIDTH + SCROLLBAR_WIDTH + 2.0f, LIST_Y + VIEWPORT_HEIGHT + 2.0f}};
		renderer->drawRect(listBg, sgc::Colorf{0.08f, 0.08f, 0.12f});
		renderer->drawRectFrame(listBg, 1.0f, sgc::Colorf{0.3f, 0.3f, 0.4f});

		// ── リストアイテム描画（可視範囲のみ） ──
		const int firstVisible = static_cast<int>(m_scrollPos / ITEM_HEIGHT);
		const int lastVisible = std::min(
			ITEM_COUNT - 1,
			static_cast<int>((m_scrollPos + VIEWPORT_HEIGHT) / ITEM_HEIGHT));

		for (int i = firstVisible; i <= lastVisible; ++i)
		{
			const float itemY = LIST_Y + static_cast<float>(i) * ITEM_HEIGHT - m_scrollPos;

			// ビューポート外はスキップ
			if (itemY + ITEM_HEIGHT < LIST_Y || itemY > LIST_Y + VIEWPORT_HEIGHT)
			{
				continue;
			}

			// 描画Y座標をビューポート内にクランプ
			const float drawTop = std::max(itemY, LIST_Y);
			const float drawBottom = std::min(itemY + ITEM_HEIGHT, LIST_Y + VIEWPORT_HEIGHT);
			if (drawBottom <= drawTop)
			{
				continue;
			}

			// 交互の背景色
			const sgc::Colorf rowColor = (i % 2 == 0)
				? sgc::Colorf{0.12f, 0.12f, 0.18f}
				: sgc::Colorf{0.10f, 0.10f, 0.15f};

			const sgc::AABB2f rowRect{
				{LIST_X, drawTop},
				{LIST_X + LIST_WIDTH, drawBottom}};
			renderer->drawRect(rowRect, rowColor);

			// テキストはアイテムが完全に表示されている場合のみ描画
			if (itemY >= LIST_Y && itemY + ITEM_HEIGHT <= LIST_Y + VIEWPORT_HEIGHT)
			{
				const sgc::Colorf textColor = (i % 2 == 0)
					? sgc::Colorf{0.8f, 0.85f, 1.0f}
					: sgc::Colorf{0.75f, 0.8f, 0.95f};

				// 左端にインデックスカラーバー
				const sgc::AABB2f colorBar{
					{LIST_X, itemY + 2.0f},
					{LIST_X + 4.0f, itemY + ITEM_HEIGHT - 2.0f}};
				const float hue = static_cast<float>(i) / static_cast<float>(ITEM_COUNT);
				const sgc::Colorf barColor{
					0.5f + 0.5f * hue,
					0.4f + 0.3f * (1.0f - hue),
					0.6f + 0.4f * hue};
				renderer->drawRect(colorBar, barColor);

				text->drawText(
					"Item #" + std::to_string(i + 1), 16.0f,
					sgc::Vec2f{LIST_X + 14.0f, itemY + 7.0f}, textColor);
			}
		}

		// ── スクロールバー描画 ──
		drawScrollbar(renderer);

		// ── ステータス表示 ──
		const float maxScroll = sgc::ui::maxScrollPos(CONTENT_HEIGHT, VIEWPORT_HEIGHT);
		const int scrollPct = static_cast<int>(m_scrollPos + 0.5f);
		const int maxPct = static_cast<int>(maxScroll + 0.5f);

		text->drawText(
			"Scroll position: " + std::to_string(scrollPct) + " / " + std::to_string(maxPct),
			14.0f,
			sgc::Vec2f{LIST_X, LIST_Y + VIEWPORT_HEIGHT + 20.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f});

		const int visFirst = static_cast<int>(m_scrollPos / ITEM_HEIGHT) + 1;
		const int visLast = std::min(
			ITEM_COUNT,
			static_cast<int>((m_scrollPos + VIEWPORT_HEIGHT) / ITEM_HEIGHT) + 1);

		text->drawText(
			"Items visible: " + std::to_string(visFirst) + "-" + std::to_string(visLast)
				+ " of " + std::to_string(ITEM_COUNT),
			14.0f,
			sgc::Vec2f{LIST_X, LIST_Y + VIEWPORT_HEIGHT + 42.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f});
	}

private:
	// ── レイアウト定数 ──
	static constexpr int ITEM_COUNT = 30;             ///< 総アイテム数
	static constexpr float ITEM_HEIGHT = 32.0f;       ///< 各アイテムの高さ
	static constexpr float CONTENT_HEIGHT = static_cast<float>(ITEM_COUNT) * ITEM_HEIGHT;  ///< 960px
	static constexpr float VIEWPORT_HEIGHT = 320.0f;  ///< 表示領域の高さ（約10アイテム）
	static constexpr float LIST_X = 80.0f;            ///< リスト左端X座標
	static constexpr float LIST_Y = 90.0f;            ///< リスト上端Y座標
	static constexpr float LIST_WIDTH = 320.0f;       ///< リスト幅
	static constexpr float SCROLLBAR_WIDTH = 20.0f;   ///< スクロールバー幅

	// ── メンバ変数 ──
	float m_scrollPos{0.0f};     ///< 現在のスクロール位置
	bool m_dragging{false};      ///< スクロールバーのサムをドラッグ中か
	sgc::Rectf m_thumbRect{};    ///< サム（つまみ）の描画矩形

	/// @brief スクロールバートラック矩形を計算する
	/// @return トラック矩形（リスト右端に隣接）
	[[nodiscard]] static constexpr sgc::Rectf scrollbarTrackRect() noexcept
	{
		return {LIST_X + LIST_WIDTH, LIST_Y, SCROLLBAR_WIDTH, VIEWPORT_HEIGHT};
	}

	/// @brief スクロールバーを描画する
	/// @param renderer レンダラー
	void drawScrollbar(sgc::IRenderer* renderer) const
	{
		const auto trackRect = scrollbarTrackRect();

		// トラック背景
		const sgc::AABB2f trackBg{
			{trackRect.x(), trackRect.y()},
			{trackRect.x() + trackRect.width(), trackRect.y() + trackRect.height()}};
		renderer->drawRect(trackBg, sgc::Colorf{0.1f, 0.1f, 0.14f});
		renderer->drawRectFrame(trackBg, 1.0f, sgc::Colorf{0.25f, 0.25f, 0.35f});

		// サム（つまみ）
		if (sgc::ui::isScrollNeeded(CONTENT_HEIGHT, VIEWPORT_HEIGHT))
		{
			const sgc::Colorf thumbColor = m_dragging
				? sgc::Colorf{0.5f, 0.6f, 0.9f}
				: sgc::Colorf{0.35f, 0.4f, 0.65f};

			const sgc::AABB2f thumbBg{
				{m_thumbRect.x() + 2.0f, m_thumbRect.y() + 1.0f},
				{m_thumbRect.x() + m_thumbRect.width() - 2.0f,
				 m_thumbRect.y() + m_thumbRect.height() - 1.0f}};
			renderer->drawRect(thumbBg, thumbColor);
		}
	}
};
