#pragma once

/// @file SampleGrid.hpp
/// @brief A*経路探索ビジュアルサンプル
///
/// 2Dグリッド上でA*パスファインディングをリアルタイム可視化する。
/// - クリック: 壁の配置/除去
/// - 1キー: スタート地点を設定
/// - 2キー: ゴール地点を設定
/// - Rキー: グリッドをリセット
/// - ESCキー: メニューに戻る

#include <cstdint>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/spatial/Grid.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief A*経路探索サンプルシーン
///
/// 20x15のグリッド上でクリックして壁を配置し、
/// A*アルゴリズムでスタートからゴールまでの最短経路を可視化する。
class SampleGrid : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_grid = sgc::Grid<std::uint8_t>(GRID_W, GRID_H, CELL_EMPTY);
		m_start = {1, 1};
		m_goal = {GRID_W - 2, GRID_H - 2};
		recalcPath();
	}

	/// @brief 毎フレームの更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_grid.fill(CELL_EMPTY);
			m_start = {1, 1};
			m_goal = {GRID_W - 2, GRID_H - 2};
			recalcPath();
		}

		const auto mousePos = input->mousePosition();
		const auto coord = mouseToCellCoord(mousePos);

		m_hoverCoord = coord;

		// 1キー: スタート設定
		if (input->isKeyJustPressed(KeyCode::NUM1) && m_grid.isValid(coord.x, coord.y))
		{
			m_start = coord;
			m_grid.at(coord) = CELL_EMPTY;
			recalcPath();
		}

		// 2キー: ゴール設定
		if (input->isKeyJustPressed(KeyCode::NUM2) && m_grid.isValid(coord.x, coord.y))
		{
			m_goal = coord;
			m_grid.at(coord) = CELL_EMPTY;
			recalcPath();
		}

		// クリックで壁トグル
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			if (m_grid.isValid(coord.x, coord.y)
				&& !(coord == m_start) && !(coord == m_goal))
			{
				auto& cell = m_grid.at(coord);
				cell = (cell == CELL_WALL) ? CELL_EMPTY : CELL_WALL;
				recalcPath();
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// 情報パネル
		tr->drawText(
			"A* Pathfinding", 22.0f,
			{10.0f, 8.0f}, sgc::Colorf{0.5f, 1.0f, 0.7f, 1.0f});

		tr->drawText(
			"Click: Wall | 1: Start | 2: Goal | R: Reset | ESC: Back", 14.0f,
			{10.0f, 36.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		const std::string pathInfo = m_path.empty()
			? "No path found"
			: ("Path length: " + std::to_string(m_path.size()));
		tr->drawText(pathInfo, 14.0f,
			{sw - 200.0f, 36.0f},
			m_path.empty()
				? sgc::Colorf{1.0f, 0.4f, 0.4f, 1.0f}
				: sgc::Colorf{0.4f, 1.0f, 0.6f, 1.0f});

		// グリッド描画
		for (int gy = 0; gy < GRID_H; ++gy)
		{
			for (int gx = 0; gx < GRID_W; ++gx)
			{
				const float px = GRID_OFFSET_X + static_cast<float>(gx) * CELL_SIZE;
				const float py = GRID_OFFSET_Y + static_cast<float>(gy) * CELL_SIZE;
				const sgc::AABB2f cellRect{
					{px + 1.0f, py + 1.0f},
					{px + CELL_SIZE - 1.0f, py + CELL_SIZE - 1.0f}};

				const sgc::GridCoord gc{gx, gy};

				if (gc == m_start)
				{
					r->drawRect(cellRect, COLOR_START);
				}
				else if (gc == m_goal)
				{
					r->drawRect(cellRect, COLOR_GOAL);
				}
				else if (m_grid.at(gx, gy) == CELL_WALL)
				{
					r->drawRect(cellRect, COLOR_WALL);
				}
				else if (isOnPath(gc))
				{
					r->drawRect(cellRect, COLOR_PATH);
				}
				else
				{
					r->drawRect(cellRect, COLOR_EMPTY);
				}

				// ホバーハイライト
				if (gc == m_hoverCoord && m_grid.isValid(gc.x, gc.y))
				{
					r->drawRectFrame(cellRect, 2.0f,
						sgc::Colorf{1.0f, 1.0f, 1.0f, 0.5f});
				}
			}
		}

		// 凡例
		const float legendY = getData().screenHeight - 25.0f;
		drawLegendItem(r, tr, 20.0f, legendY, COLOR_START, "Start");
		drawLegendItem(r, tr, 110.0f, legendY, COLOR_GOAL, "Goal");
		drawLegendItem(r, tr, 190.0f, legendY, COLOR_WALL, "Wall");
		drawLegendItem(r, tr, 270.0f, legendY, COLOR_PATH, "Path");
	}

private:
	static constexpr int GRID_W = 24;     ///< グリッド横セル数
	static constexpr int GRID_H = 16;     ///< グリッド縦セル数
	static constexpr float CELL_SIZE = 30.0f;  ///< セルサイズ(px)
	static constexpr float GRID_OFFSET_X = 40.0f;  ///< グリッド描画開始X
	static constexpr float GRID_OFFSET_Y = 60.0f;   ///< グリッド描画開始Y

	static constexpr std::uint8_t CELL_EMPTY = 0;  ///< 空セル
	static constexpr std::uint8_t CELL_WALL = 1;   ///< 壁セル

	static constexpr sgc::Colorf COLOR_EMPTY{0.12f, 0.12f, 0.18f, 1.0f};
	static constexpr sgc::Colorf COLOR_WALL{0.5f, 0.5f, 0.55f, 1.0f};
	static constexpr sgc::Colorf COLOR_START{0.2f, 0.9f, 0.3f, 1.0f};
	static constexpr sgc::Colorf COLOR_GOAL{0.9f, 0.2f, 0.2f, 1.0f};
	static constexpr sgc::Colorf COLOR_PATH{0.3f, 0.6f, 1.0f, 0.8f};

	sgc::Grid<std::uint8_t> m_grid{GRID_W, GRID_H, CELL_EMPTY};
	sgc::GridCoord m_start{1, 1};
	sgc::GridCoord m_goal{GRID_W - 2, GRID_H - 2};
	sgc::GridCoord m_hoverCoord{-1, -1};
	std::vector<sgc::GridCoord> m_path;

	/// @brief マウス座標をグリッド座標に変換する
	[[nodiscard]] sgc::GridCoord mouseToCellCoord(const sgc::Vec2f& pos) const
	{
		const int gx = static_cast<int>((pos.x - GRID_OFFSET_X) / CELL_SIZE);
		const int gy = static_cast<int>((pos.y - GRID_OFFSET_Y) / CELL_SIZE);
		return {gx, gy};
	}

	/// @brief 指定座標がパス上か判定する
	[[nodiscard]] bool isOnPath(const sgc::GridCoord& coord) const
	{
		for (const auto& p : m_path)
		{
			if (p == coord)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief A*パスを再計算する
	void recalcPath()
	{
		m_path = sgc::findPathAStar<std::uint8_t>(
			m_grid, m_start, m_goal,
			[](sgc::GridCoord /*from*/, sgc::GridCoord /*to*/,
				const std::uint8_t& cell) -> float
			{
				return (cell == CELL_WALL) ? -1.0f : 1.0f;
			},
			sgc::heuristic::manhattan);
	}

	/// @brief 凡例アイテムを描画する
	static void drawLegendItem(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float x, float y, const sgc::Colorf& color, const char* label)
	{
		r->drawRect(
			sgc::AABB2f{{x, y}, {x + 14.0f, y + 14.0f}}, color);
		tr->drawText(label, 12.0f,
			{x + 18.0f, y}, sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f});
	}
};
