#pragma once

/// @file SamplePhysics.hpp
/// @brief AABB衝突検出サンプルシーン
///
/// sgc::physics::detectAABBCollision と resolveCollision を使用した
/// 重力落下ボックスの衝突応答デモ。
/// - 10〜15個のカラーボックスが重力で落下する
/// - AABB同士の衝突検出・分離ベクトルによる解決
/// - クリックで新しいボックスを生成
/// - Rでリセット

#include <algorithm>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/physics/AABB2DCollision.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief AABB衝突検出サンプルシーン
///
/// 複数のカラーボックスが重力で落下し、
/// detectAABBCollision / resolveCollision で衝突応答する。
class SamplePhysics : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief 落下ボックス情報
	struct Box
	{
		sgc::AABB2f aabb;          ///< AABB
		sgc::Vec2f velocity{};     ///< 速度
		sgc::Colorf color;         ///< 描画色
		float width{0.0f};         ///< 幅
		float height{0.0f};        ///< 高さ
	};

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_boxes.clear();
		m_spawnCounter = 0;
		spawnInitialBoxes();
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rでリセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_boxes.clear();
			m_spawnCounter = 0;
			spawnInitialBoxes();
			return;
		}

		// クリックで新しいボックスを生成
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			spawnBoxAt(input->mousePosition());
		}

		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 重力と位置を更新
		for (auto& box : m_boxes)
		{
			box.velocity.y += GRAVITY * dt;
			box.velocity.x *= (1.0f - DRAG * dt);

			const float dx = box.velocity.x * dt;
			const float dy = box.velocity.y * dt;
			box.aabb.min.x += dx;
			box.aabb.min.y += dy;
			box.aabb.max.x += dx;
			box.aabb.max.y += dy;
		}

		// 床・壁との衝突
		for (auto& box : m_boxes)
		{
			// 床
			if (box.aabb.max.y > sh - WALL_MARGIN)
			{
				const float overlap = box.aabb.max.y - (sh - WALL_MARGIN);
				box.aabb.min.y -= overlap;
				box.aabb.max.y -= overlap;
				box.velocity.y = -box.velocity.y * RESTITUTION;
				if (box.velocity.y > -5.0f && box.velocity.y < 0.0f)
				{
					box.velocity.y = 0.0f;
				}
			}

			// 左壁
			if (box.aabb.min.x < WALL_MARGIN)
			{
				const float overlap = WALL_MARGIN - box.aabb.min.x;
				box.aabb.min.x += overlap;
				box.aabb.max.x += overlap;
				box.velocity.x = -box.velocity.x * RESTITUTION;
			}

			// 右壁
			if (box.aabb.max.x > sw - WALL_MARGIN)
			{
				const float overlap = box.aabb.max.x - (sw - WALL_MARGIN);
				box.aabb.min.x -= overlap;
				box.aabb.max.x -= overlap;
				box.velocity.x = -box.velocity.x * RESTITUTION;
			}

			// 天井
			if (box.aabb.min.y < 0.0f)
			{
				const float overlap = -box.aabb.min.y;
				box.aabb.min.y += overlap;
				box.aabb.max.y += overlap;
				box.velocity.y = -box.velocity.y * RESTITUTION;
			}
		}

		// ボックス同士のAABB衝突検出と解決
		const auto count = static_cast<int>(m_boxes.size());
		for (int i = 0; i < count; ++i)
		{
			for (int j = i + 1; j < count; ++j)
			{
				auto result = sgc::physics::detectAABBCollision(
					m_boxes[i].aabb, m_boxes[j].aabb);

				if (result.colliding)
				{
					// 両方を半分ずつ分離する
					const sgc::Vec2f halfSep = result.separationVector * 0.5f;

					m_boxes[i].aabb.min.x += halfSep.x;
					m_boxes[i].aabb.min.y += halfSep.y;
					m_boxes[i].aabb.max.x += halfSep.x;
					m_boxes[i].aabb.max.y += halfSep.y;

					m_boxes[j].aabb.min.x -= halfSep.x;
					m_boxes[j].aabb.min.y -= halfSep.y;
					m_boxes[j].aabb.max.x -= halfSep.x;
					m_boxes[j].aabb.max.y -= halfSep.y;

					// 衝突法線に沿った速度の交換（簡易）
					const float relVelN =
						(m_boxes[i].velocity.x - m_boxes[j].velocity.x) * result.normal.x +
						(m_boxes[i].velocity.y - m_boxes[j].velocity.y) * result.normal.y;

					if (relVelN > 0.0f)
					{
						const float impulse = relVelN * RESTITUTION;
						m_boxes[i].velocity.x -= impulse * result.normal.x;
						m_boxes[i].velocity.y -= impulse * result.normal.y;
						m_boxes[j].velocity.x += impulse * result.normal.x;
						m_boxes[j].velocity.y += impulse * result.normal.y;
					}
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
		renderer->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.12f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"AABB Collision Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.4f, 0.8f, 1.0f, 1.0f});

		// 壁の描画
		const auto wallColor = sgc::Colorf{0.25f, 0.28f, 0.35f, 1.0f};
		renderer->drawRect(
			sgc::AABB2f{{0.0f, sh - WALL_MARGIN}, {sw, sh}}, wallColor);
		renderer->drawRect(
			sgc::AABB2f{{0.0f, 0.0f}, {WALL_MARGIN, sh}}, wallColor);
		renderer->drawRect(
			sgc::AABB2f{{sw - WALL_MARGIN, 0.0f}, {sw, sh}}, wallColor);

		// ボックスの描画
		for (const auto& box : m_boxes)
		{
			renderer->drawRect(box.aabb, box.color);
			renderer->drawRectFrame(box.aabb, 1.0f,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 0.3f});
		}

		// 情報表示
		const std::string info = "Boxes: " + std::to_string(m_boxes.size());
		text->drawText(info, 18.0f,
			sgc::Vec2f{14.0f, 60.0f},
			sgc::Colorf{0.7f, 0.7f, 0.75f, 1.0f});

		text->drawText(
			"Click: Spawn  |  R: Reset  |  ESC: Back", 14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr float GRAVITY = 500.0f;       ///< 重力加速度
	static constexpr float RESTITUTION = 0.5f;      ///< 反発係数
	static constexpr float DRAG = 0.5f;              ///< 空気抵抗係数
	static constexpr float WALL_MARGIN = 10.0f;      ///< 壁のマージン
	static constexpr int INITIAL_BOXES = 12;         ///< 初期ボックス数
	static constexpr int MAX_BOXES = 40;             ///< 最大ボックス数
	static constexpr float MIN_SIZE = 25.0f;         ///< 最小サイズ
	static constexpr float MAX_SIZE = 55.0f;         ///< 最大サイズ

	std::vector<Box> m_boxes;    ///< ボックス群
	int m_spawnCounter{0};       ///< 生成カウンタ（擬似ランダム用）

	/// @brief 初期ボックスを生成する
	void spawnInitialBoxes()
	{
		const float sw = getData().screenWidth;

		for (int i = 0; i < INITIAL_BOXES; ++i)
		{
			// 擬似ランダムな位置に配置
			const float t = static_cast<float>(i) / static_cast<float>(INITIAL_BOXES);
			const float x = WALL_MARGIN + 30.0f + t * (sw - WALL_MARGIN * 2.0f - 80.0f);
			const float y = 60.0f + static_cast<float>(i % 4) * 40.0f;
			spawnBoxAt(sgc::Vec2f{x, y});
		}
	}

	/// @brief 指定位置にボックスを生成する
	/// @param pos 生成位置（ボックスの中心）
	void spawnBoxAt(const sgc::Vec2f& pos)
	{
		if (static_cast<int>(m_boxes.size()) >= MAX_BOXES)
		{
			m_boxes.erase(m_boxes.begin());
		}

		// 擬似ランダムなサイズ
		const float sizeT = static_cast<float>(m_spawnCounter * 37 % 11) / 10.0f;
		const float w = MIN_SIZE + sizeT * (MAX_SIZE - MIN_SIZE);
		const float h = MIN_SIZE + static_cast<float>((m_spawnCounter * 53 + 3) % 11) / 10.0f
			* (MAX_SIZE - MIN_SIZE);

		Box box;
		box.width = w;
		box.height = h;
		box.aabb = sgc::AABB2f{
			{pos.x - w * 0.5f, pos.y - h * 0.5f},
			{pos.x + w * 0.5f, pos.y + h * 0.5f}};
		box.velocity = sgc::Vec2f{0.0f, 0.0f};
		box.color = sgc::Colorf::fromHSV(
			static_cast<float>(m_spawnCounter * 41 % 360),
			0.65f, 0.85f, 0.9f);

		m_boxes.push_back(box);
		++m_spawnCounter;
	}
};
