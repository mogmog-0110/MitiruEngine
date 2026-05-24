#pragma once

/// @file SampleQuadtree.hpp
/// @brief Quadtree（四分木）空間分割の視覚化サンプル
///
/// 80個の移動矩形を四分木に毎フレーム挿入し、空間分割のグリッド線と
/// マウスカーソル近傍の範囲クエリ結果をリアルタイムで可視化する。
/// - Rキー: 矩形をランダムリセット
/// - ESCキー: メニューに戻る

#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/spatial/Quadtree.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Quadtreeビジュアルサンプルシーン
///
/// 80個の小さな移動矩形を四分木に挿入し、空間分割の様子と
/// マウス近傍クエリの結果をリアルタイムで描画する。
/// 矩形は画面端で反射しながら移動し続ける。
class SampleQuadtree : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時に矩形を初期化する
	void onEnter() override
	{
		resetElements();
	}

	/// @brief 毎フレームの更新処理
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

		// Rでリセット
		if (input.isKeyJustPressed(KeyCode::R))
		{
			resetElements();
			return;
		}

		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 各矩形を移動させ、画面端で反射する
		for (auto& rect : m_rects)
		{
			rect.x += rect.vx * dt;
			rect.y += rect.vy * dt;

			if (rect.x < 0.0f)
			{
				rect.x = 0.0f;
				rect.vx = -rect.vx;
			}
			if (rect.x + rect.w > sw)
			{
				rect.x = sw - rect.w;
				rect.vx = -rect.vx;
			}
			if (rect.y < 0.0f)
			{
				rect.y = 0.0f;
				rect.vy = -rect.vy;
			}
			if (rect.y + rect.h > sh)
			{
				rect.y = sh - rect.h;
				rect.vy = -rect.vy;
			}
		}

		// マウス位置を記録
		m_mousePos = input.mousePosition();
	}

	/// @brief 描画処理
	void draw() const override
	{
		const auto& data = getData();
		auto* renderer = data.renderer;
		auto* textRenderer = data.textRenderer;
		const float sw = data.screenWidth;
		const float sh = data.screenHeight;

		// 背景クリア
		renderer->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.1f, 1.0f});

		// 四分木を毎フレーム再構築（要素が移動するため）
		sgc::Quadtree<float> qt(sgc::AABB2f{{0.0f, 0.0f}, {sw, sh}});

		for (std::size_t i = 0; i < m_rects.size(); ++i)
		{
			const auto& r = m_rects[i];
			qt.insert(sgc::AABB2f{{r.x, r.y}, {r.x + r.w, r.y + r.h}});
		}

		// 四分木のグリッド線を描画（分割の可視化）
		drawCellRecursive(renderer, qt,
			sgc::AABB2f{{0.0f, 0.0f}, {sw, sh}}, 0);

		// マウス近傍のクエリ範囲
		constexpr float QUERY_HALF = 60.0f;
		const sgc::AABB2f queryBox{
			{m_mousePos.x - QUERY_HALF, m_mousePos.y - QUERY_HALF},
			{m_mousePos.x + QUERY_HALF, m_mousePos.y + QUERY_HALF}
		};

		// クエリ実行
		const auto found = qt.queryRange(queryBox);

		// 通常の矩形を描画
		for (std::size_t i = 0; i < m_rects.size(); ++i)
		{
			const auto& r = m_rects[i];
			const sgc::AABB2f bounds{{r.x, r.y}, {r.x + r.w, r.y + r.h}};
			renderer->drawRect(bounds, sgc::Colorf{0.3f, 0.6f, 0.9f, 0.7f});
		}

		// クエリでヒットした矩形をハイライト
		for (const auto handle : found)
		{
			const auto& bounds = qt.getBounds(handle);
			renderer->drawRect(bounds, sgc::Colorf{1.0f, 0.95f, 0.2f, 0.9f});
			renderer->drawRectFrame(bounds, 1.0f,
				sgc::Colorf{1.0f, 0.8f, 0.0f, 0.6f});
		}

		// クエリ範囲を枠線で描画
		renderer->drawRect(queryBox, sgc::Colorf{0.2f, 0.4f, 0.8f, 0.1f});
		renderer->drawRectFrame(queryBox, 2.0f,
			sgc::Colorf{0.3f, 0.6f, 1.0f, 0.9f});

		// 情報パネル
		renderer->drawRect(
			sgc::AABB2f{{0.0f, 0.0f}, {300.0f, 110.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.7f});

		textRenderer->drawText(
			"Quadtree Visualization", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.3f, 0.9f, 0.5f, 1.0f});

		textRenderer->drawText(
			"Moving Rects: " + std::to_string(m_rects.size()), 16.0f,
			{10.0f, 38.0f}, sgc::Colorf::white());

		textRenderer->drawText(
			"Found in Query: " + std::to_string(found.size()), 16.0f,
			{10.0f, 58.0f}, sgc::Colorf{1.0f, 0.95f, 0.2f, 1.0f});

		textRenderer->drawText(
			"[R] Reset  [Esc] Back", 14.0f,
			{10.0f, 84.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

private:
	static constexpr int ELEMENT_COUNT = 80;  ///< 移動矩形の数

	/// @brief 移動矩形データ
	struct MovingRect
	{
		float x{};    ///< 左上X座標
		float y{};    ///< 左上Y座標
		float vx{};   ///< X速度(px/s)
		float vy{};   ///< Y速度(px/s)
		float w{};    ///< 幅
		float h{};    ///< 高さ
	};

	std::vector<MovingRect> m_rects;  ///< 移動矩形リスト
	sgc::Vec2f m_mousePos{};          ///< マウス座標

	/// @brief 矩形をランダムに初期化する
	void resetElements()
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		std::mt19937 rng(static_cast<unsigned>(
			std::chrono::steady_clock::now().time_since_epoch().count()));

		std::uniform_real_distribution<float> distX(10.0f, sw - 30.0f);
		std::uniform_real_distribution<float> distY(10.0f, sh - 30.0f);
		std::uniform_real_distribution<float> distSize(6.0f, 18.0f);
		std::uniform_real_distribution<float> distSpeed(-120.0f, 120.0f);

		m_rects.clear();
		m_rects.reserve(ELEMENT_COUNT);

		for (int i = 0; i < ELEMENT_COUNT; ++i)
		{
			const float w = distSize(rng);
			const float h = distSize(rng);
			m_rects.push_back(MovingRect{
				distX(rng), distY(rng),
				distSpeed(rng), distSpeed(rng),
				w, h
			});
		}
	}

	/// @brief 四分木のセル境界を再帰的に推定描画する
	///
	/// 各セル内の要素数がMaxPerNode(8)を超える場合に分割を推定し、
	/// グリッド線として描画する。
	/// @param renderer 描画インターフェース
	/// @param qt 四分木
	/// @param bounds 現在の領域
	/// @param depth 現在の深度
	void drawCellRecursive(
		sgc::IRenderer* renderer,
		const sgc::Quadtree<float>& qt,
		const sgc::AABB2f& bounds,
		int depth) const
	{
		if (depth > 6)
		{
			return;
		}

		// 外枠を描画
		const float alpha = 0.12f + 0.04f * static_cast<float>(depth);
		renderer->drawRectFrame(bounds, 1.0f,
			sgc::Colorf{0.3f, 0.5f, 0.3f, alpha});

		// この領域内の要素数を確認
		const auto elements = qt.queryRange(bounds);
		if (elements.size() <= 8)
		{
			return;
		}

		// 中心で4分割
		const auto center = bounds.center();
		const sgc::AABB2f quads[4] = {
			{bounds.min, center},
			{{center.x, bounds.min.y}, {bounds.max.x, center.y}},
			{{bounds.min.x, center.y}, {center.x, bounds.max.y}},
			{center, bounds.max}
		};

		for (int i = 0; i < 4; ++i)
		{
			drawCellRecursive(renderer, qt, quads[i], depth + 1);
		}
	}
};
