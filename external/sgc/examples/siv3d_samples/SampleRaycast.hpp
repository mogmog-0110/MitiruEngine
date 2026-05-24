#pragma once

/// @file SampleRaycast.hpp
/// @brief レイキャストサンプルシーン
///
/// sgc::physics::raycastAABB / raycastCircle を使用したインタラクティブなレイキャストデモ。
/// - 画面中央からマウスへ向かうレイを描画
/// - 静的なAABBと円の障害物にレイを当て、衝突点・法線を可視化
/// - Rで障害物の配置をランダム化

#include <cmath>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/physics/RayCast2D.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief レイキャスト可視化サンプルシーン
///
/// 画面中央を原点としてマウス方向にレイを飛ばし、
/// AABBおよび円との交差を可視化する。
class SampleRaycast : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief AABB障害物
	struct BoxObstacle
	{
		sgc::AABB2f aabb;      ///< AABB
		sgc::Colorf color;     ///< 描画色
	};

	/// @brief 円障害物
	struct CircleObstacle
	{
		sgc::Vec2f center;     ///< 中心座標
		float radius{0.0f};    ///< 半径
		sgc::Colorf color;     ///< 描画色
	};

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_randomSeed = 42;
		randomizeObstacles();
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rで障害物をランダム化
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_randomSeed += 7;
			randomizeObstacles();
		}

		// レイの計算
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		m_rayOrigin = sgc::Vec2f{sw * 0.5f, sh * 0.5f};

		const auto mousePos = input->mousePosition();
		const sgc::Vec2f toMouse = mousePos - m_rayOrigin;
		const float len = std::sqrt(toMouse.x * toMouse.x + toMouse.y * toMouse.y);

		if (len > 1.0f)
		{
			m_rayDirection = sgc::Vec2f{toMouse.x / len, toMouse.y / len};
		}
		else
		{
			m_rayDirection = sgc::Vec2f{1.0f, 0.0f};
		}

		// 全障害物との衝突判定 - 最も近いヒットを探す
		m_closestHit = sgc::physics::RayCastHit2Df{};
		m_hitObstacleIndex = -1;
		m_hitIsCircle = false;

		for (int i = 0; i < static_cast<int>(m_boxes.size()); ++i)
		{
			const auto hit = sgc::physics::raycastAABB(
				m_rayOrigin, m_rayDirection, m_boxes[i].aabb);
			if (hit.hit)
			{
				if (!m_closestHit.hit || hit.distance < m_closestHit.distance)
				{
					m_closestHit = hit;
					m_hitObstacleIndex = i;
					m_hitIsCircle = false;
				}
			}
		}

		for (int i = 0; i < static_cast<int>(m_circles.size()); ++i)
		{
			const auto hit = sgc::physics::raycastCircle(
				m_rayOrigin, m_rayDirection,
				m_circles[i].center, m_circles[i].radius);
			if (hit.hit)
			{
				if (!m_closestHit.hit || hit.distance < m_closestHit.distance)
				{
					m_closestHit = hit;
					m_hitObstacleIndex = i;
					m_hitIsCircle = true;
				}
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
		renderer->clearBackground(sgc::Colorf{0.05f, 0.07f, 0.12f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"RayCast 2D Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{1.0f, 0.8f, 0.3f, 1.0f});

		// AABB障害物の描画
		for (int i = 0; i < static_cast<int>(m_boxes.size()); ++i)
		{
			const auto& box = m_boxes[i];
			const bool isHit = (m_closestHit.hit && !m_hitIsCircle && m_hitObstacleIndex == i);
			const auto drawColor = isHit
				? sgc::Colorf{1.0f, 0.4f, 0.4f, 0.9f}
				: box.color;
			renderer->drawRect(box.aabb, drawColor);
			renderer->drawRectFrame(box.aabb, 1.0f,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 0.3f});
		}

		// 円障害物の描画
		for (int i = 0; i < static_cast<int>(m_circles.size()); ++i)
		{
			const auto& circle = m_circles[i];
			const bool isHit = (m_closestHit.hit && m_hitIsCircle && m_hitObstacleIndex == i);
			const auto drawColor = isHit
				? sgc::Colorf{1.0f, 0.4f, 0.4f, 0.9f}
				: circle.color;
			renderer->drawCircle(circle.center, circle.radius, drawColor);
			renderer->drawCircleFrame(circle.center, circle.radius, 1.0f,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 0.3f});
		}

		// レイの描画
		const sgc::Colorf rayColor{1.0f, 1.0f, 0.3f, 0.8f};
		if (m_closestHit.hit)
		{
			// 原点からヒットポイントまで
			renderer->drawLine(m_rayOrigin, m_closestHit.point, 2.0f, rayColor);

			// ヒットポイント以降は薄い色で延長
			const sgc::Vec2f farEnd = m_rayOrigin + m_rayDirection * RAY_MAX_LEN;
			renderer->drawLine(m_closestHit.point, farEnd, 1.0f,
				sgc::Colorf{1.0f, 1.0f, 0.3f, 0.15f});

			// ヒットポイントのマーカー
			renderer->drawCircle(m_closestHit.point, 6.0f,
				sgc::Colorf{1.0f, 0.2f, 0.2f, 1.0f});
			renderer->drawCircleFrame(m_closestHit.point, 6.0f, 2.0f,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

			// 法線ベクトルの描画
			const sgc::Vec2f normalEnd = m_closestHit.point +
				m_closestHit.normal * NORMAL_DRAW_LEN;
			renderer->drawLine(
				m_closestHit.point, normalEnd, 2.0f,
				sgc::Colorf{0.3f, 1.0f, 0.3f, 1.0f});

			// 法線先端の矢印マーカー
			renderer->drawCircle(normalEnd, 4.0f,
				sgc::Colorf{0.3f, 1.0f, 0.3f, 1.0f});
		}
		else
		{
			// ヒットなし - レイ全体を描画
			const sgc::Vec2f farEnd = m_rayOrigin + m_rayDirection * RAY_MAX_LEN;
			renderer->drawLine(m_rayOrigin, farEnd, 2.0f, rayColor);
		}

		// レイ原点のマーカー
		renderer->drawCircle(m_rayOrigin, 5.0f,
			sgc::Colorf{0.3f, 0.8f, 1.0f, 1.0f});
		renderer->drawCircleFrame(m_rayOrigin, 8.0f, 1.0f,
			sgc::Colorf{0.3f, 0.8f, 1.0f, 0.5f});

		// 情報パネル
		const float infoX = 10.0f;
		float infoY = 60.0f;
		const auto infoColor = sgc::Colorf{0.7f, 0.7f, 0.75f, 1.0f};

		if (m_closestHit.hit)
		{
			const std::string distStr = "Distance: " +
				floatToString(m_closestHit.distance);
			text->drawText(distStr, 16.0f,
				sgc::Vec2f{infoX, infoY}, infoColor);
			infoY += 22.0f;

			const std::string pointStr = "Hit: (" +
				floatToString(m_closestHit.point.x) + ", " +
				floatToString(m_closestHit.point.y) + ")";
			text->drawText(pointStr, 16.0f,
				sgc::Vec2f{infoX, infoY}, infoColor);
			infoY += 22.0f;

			const std::string normalStr = "Normal: (" +
				floatToString(m_closestHit.normal.x) + ", " +
				floatToString(m_closestHit.normal.y) + ")";
			text->drawText(normalStr, 16.0f,
				sgc::Vec2f{infoX, infoY}, infoColor);
			infoY += 22.0f;

			const std::string typeStr = m_hitIsCircle
				? "Hit: Circle" : "Hit: AABB";
			text->drawText(typeStr, 16.0f,
				sgc::Vec2f{infoX, infoY}, infoColor);
		}
		else
		{
			text->drawText("No hit", 16.0f,
				sgc::Vec2f{infoX, infoY}, infoColor);
		}

		// 操作ヒント
		text->drawText(
			"Mouse: Aim Ray  |  R: Randomize  |  ESC: Back", 14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr float RAY_MAX_LEN = 1500.0f;     ///< レイ描画最大長
	static constexpr float NORMAL_DRAW_LEN = 40.0f;    ///< 法線描画長さ
	static constexpr int NUM_BOXES = 5;                 ///< AABB障害物数
	static constexpr int NUM_CIRCLES = 4;               ///< 円障害物数

	std::vector<BoxObstacle> m_boxes;                  ///< AABB障害物群
	std::vector<CircleObstacle> m_circles;             ///< 円障害物群

	sgc::Vec2f m_rayOrigin{};                          ///< レイの始点
	sgc::Vec2f m_rayDirection{1.0f, 0.0f};             ///< レイの方向
	sgc::physics::RayCastHit2Df m_closestHit{};        ///< 最も近いヒット結果
	int m_hitObstacleIndex{-1};                        ///< ヒットした障害物のインデックス
	bool m_hitIsCircle{false};                         ///< ヒットしたのが円か
	int m_randomSeed{42};                              ///< 擬似乱数シード

	/// @brief 簡易擬似乱数を生成する
	/// @param seed シード（更新される）
	/// @return 0.0〜1.0の範囲の値
	[[nodiscard]] static float pseudoRandom(int& seed) noexcept
	{
		seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
		return static_cast<float>(seed % 1000) / 999.0f;
	}

	/// @brief 障害物をランダムに配置する
	void randomizeObstacles()
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const float cx = sw * 0.5f;
		const float cy = sh * 0.5f;

		m_boxes.clear();
		m_circles.clear();

		int seed = m_randomSeed;

		// AABB障害物を生成（画面中央を避けた位置に配置）
		for (int i = 0; i < NUM_BOXES; ++i)
		{
			BoxObstacle obs;
			const float w = 40.0f + pseudoRandom(seed) * 80.0f;
			const float h = 30.0f + pseudoRandom(seed) * 60.0f;

			float x = 60.0f + pseudoRandom(seed) * (sw - 120.0f - w);
			float y = 80.0f + pseudoRandom(seed) * (sh - 160.0f - h);

			// 中央付近に配置されたら少しずらす
			if (std::abs(x + w * 0.5f - cx) < 60.0f &&
				std::abs(y + h * 0.5f - cy) < 60.0f)
			{
				x += (x < cx) ? -80.0f : 80.0f;
			}

			obs.aabb = sgc::AABB2f{{x, y}, {x + w, y + h}};
			obs.color = sgc::Colorf{
				0.2f + pseudoRandom(seed) * 0.4f,
				0.3f + pseudoRandom(seed) * 0.4f,
				0.5f + pseudoRandom(seed) * 0.4f,
				0.7f};

			m_boxes.push_back(obs);
		}

		// 円障害物を生成
		for (int i = 0; i < NUM_CIRCLES; ++i)
		{
			CircleObstacle obs;
			obs.radius = 20.0f + pseudoRandom(seed) * 35.0f;

			float x = 80.0f + pseudoRandom(seed) * (sw - 160.0f);
			float y = 100.0f + pseudoRandom(seed) * (sh - 200.0f);

			// 中央付近を避ける
			if (std::abs(x - cx) < 60.0f && std::abs(y - cy) < 60.0f)
			{
				y += (y < cy) ? -90.0f : 90.0f;
			}

			obs.center = sgc::Vec2f{x, y};
			obs.color = sgc::Colorf{
				0.5f + pseudoRandom(seed) * 0.4f,
				0.2f + pseudoRandom(seed) * 0.4f,
				0.4f + pseudoRandom(seed) * 0.4f,
				0.7f};

			m_circles.push_back(obs);
		}
	}

	/// @brief floatを簡易文字列に変換する（小数第1位まで）
	/// @param v 値
	/// @return 文字列表現
	[[nodiscard]] static std::string floatToString(float v)
	{
		const int integer = static_cast<int>(v);
		const int frac = static_cast<int>(std::abs(v - static_cast<float>(integer)) * 10.0f + 0.5f);
		if (v < 0.0f && integer == 0)
		{
			return "-0." + std::to_string(frac);
		}
		return std::to_string(integer) + "." + std::to_string(frac);
	}
};
