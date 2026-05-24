#pragma once

/// @file SampleOctree.hpp
/// @brief Octree（八分木）3D空間分割のトップダウン2D投影デモ
///
/// 3Dの八分木をZ座標を無視した真上からの2D投影で可視化する。
/// 挿入されたオブジェクトの位置と範囲クエリの結果を表示する。
/// - クリック: マウス位置にオブジェクト挿入（ランダムZ）
/// - Space: 範囲クエリ実行（マウス周辺）
/// - R: クリア
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/spatial/Octree.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Octreeトップダウン投影サンプルシーン
///
/// 3D八分木にオブジェクトを挿入し、XY平面への投影で可視化する。
/// Z座標は色の明るさで表現する。
class SampleOctree : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief 挿入済みオブジェクト情報（描画用）
	struct InsertedObject
	{
		sgc::AABB3f bounds;        ///< 3D境界
		sgc::Octreef::Handle handle; ///< 八分木ハンドル
		float hue;                  ///< 色相
	};

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		resetOctree();
	}

	/// @brief 毎フレームの更新処理
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

		// Rでリセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			resetOctree();
			return;
		}

		// マウス位置
		m_mousePos = input->mousePosition();

		// クリックで挿入
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			insertObjectAtMouse();
		}

		// Spaceで範囲クエリ実行
		m_queryActive = input->isKeyDown(KeyCode::SPACE);
		if (m_queryActive)
		{
			executeQuery();
		}
		else
		{
			m_queryResults.clear();
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
		renderer->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.1f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"Octree 3D (Top-Down View)", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.9f, 0.5f, 0.3f, 1.0f});

		// ワールド境界（2D投影）
		const float worldX = MARGIN;
		const float worldY = 70.0f;
		const float worldW = sw - MARGIN * 2.0f;
		const float worldH = sh - 130.0f;

		renderer->drawRectFrame(
			sgc::AABB2f{{worldX, worldY}, {worldX + worldW, worldY + worldH}},
			2.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.8f});

		// 軸ラベル
		text->drawText("X", 14.0f,
			sgc::Vec2f{worldX + worldW * 0.5f, worldY + worldH + 4.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f, 1.0f});
		text->drawText("Y", 14.0f,
			sgc::Vec2f{worldX - 16.0f, worldY + worldH * 0.5f},
			sgc::Colorf{0.6f, 0.6f, 0.7f, 1.0f});

		// グリッド線
		for (int i = 1; i < 4; ++i)
		{
			const float gx = worldX + worldW * static_cast<float>(i) / 4.0f;
			renderer->drawLine(
				sgc::Vec2f{gx, worldY},
				sgc::Vec2f{gx, worldY + worldH},
				1.0f, sgc::Colorf{0.15f, 0.15f, 0.2f, 0.5f});

			const float gy = worldY + worldH * static_cast<float>(i) / 4.0f;
			renderer->drawLine(
				sgc::Vec2f{worldX, gy},
				sgc::Vec2f{worldX + worldW, gy},
				1.0f, sgc::Colorf{0.15f, 0.15f, 0.2f, 0.5f});
		}

		// 挿入済みオブジェクトの描画（XY投影）
		for (const auto& obj : m_objects)
		{
			const float objX = worldX + remap(obj.bounds.min.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
			const float objY = worldY + remap(obj.bounds.min.y, 0.0f, WORLD_SIZE, 0.0f, worldH);
			const float objW = remap(obj.bounds.max.x - obj.bounds.min.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
			const float objH = remap(obj.bounds.max.y - obj.bounds.min.y, 0.0f, WORLD_SIZE, 0.0f, worldH);

			// Z座標で明るさを変える（深い=暗い）
			const float zNorm = (obj.bounds.min.z + obj.bounds.max.z) * 0.5f / WORLD_SIZE;
			const float brightness = 0.4f + zNorm * 0.6f;

			const auto color = sgc::Colorf::fromHSV(obj.hue, 0.7f, brightness, 0.8f);

			renderer->drawRect(
				sgc::AABB2f{{objX, objY}, {objX + objW, objY + objH}},
				color);
			renderer->drawRectFrame(
				sgc::AABB2f{{objX, objY}, {objX + objW, objY + objH}},
				1.0f, color.withAlpha(0.4f));
		}

		// クエリ結果のハイライト
		for (const auto handle : m_queryResults)
		{
			// ハンドルに対応するオブジェクトを検索
			for (const auto& obj : m_objects)
			{
				if (obj.handle == handle)
				{
					const float objX = worldX + remap(obj.bounds.min.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
					const float objY = worldY + remap(obj.bounds.min.y, 0.0f, WORLD_SIZE, 0.0f, worldH);
					const float objW = remap(obj.bounds.max.x - obj.bounds.min.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
					const float objH = remap(obj.bounds.max.y - obj.bounds.min.y, 0.0f, WORLD_SIZE, 0.0f, worldH);

					renderer->drawRectFrame(
						sgc::AABB2f{{objX, objY}, {objX + objW, objY + objH}},
						3.0f, sgc::Colorf{1.0f, 1.0f, 0.2f, 1.0f});
					break;
				}
			}
		}

		// クエリ範囲表示
		if (m_queryActive)
		{
			const float qMinX = worldX + remap(m_queryBounds.min.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
			const float qMinY = worldY + remap(m_queryBounds.min.y, 0.0f, WORLD_SIZE, 0.0f, worldH);
			const float qMaxX = worldX + remap(m_queryBounds.max.x, 0.0f, WORLD_SIZE, 0.0f, worldW);
			const float qMaxY = worldY + remap(m_queryBounds.max.y, 0.0f, WORLD_SIZE, 0.0f, worldH);

			renderer->drawRect(
				sgc::AABB2f{{qMinX, qMinY}, {qMaxX, qMaxY}},
				sgc::Colorf{0.2f, 0.4f, 0.8f, 0.15f});
			renderer->drawRectFrame(
				sgc::AABB2f{{qMinX, qMinY}, {qMaxX, qMaxY}},
				2.0f, sgc::Colorf{0.3f, 0.6f, 1.0f, 0.9f});
		}

		// 情報パネル
		renderer->drawRect(
			sgc::AABB2f{{sw - 220.0f, worldY}, {sw - MARGIN, worldY + 90.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.6f});

		text->drawText("Octree Stats", 18.0f,
			sgc::Vec2f{sw - 210.0f, worldY + 6.0f},
			sgc::Colorf{0.9f, 0.6f, 0.3f, 1.0f});

		text->drawText(
			"Objects: " + std::to_string(m_octree.size()), 14.0f,
			sgc::Vec2f{sw - 210.0f, worldY + 30.0f},
			sgc::Colorf::white());

		text->drawText(
			"Query hits: " + std::to_string(m_queryResults.size()), 14.0f,
			sgc::Vec2f{sw - 210.0f, worldY + 50.0f},
			sgc::Colorf{1.0f, 1.0f, 0.3f, 1.0f});

		text->drawText(
			"Z = brightness", 14.0f,
			sgc::Vec2f{sw - 210.0f, worldY + 70.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f, 1.0f});

		// 操作説明
		text->drawText(
			"[Click] Insert  [Space] Query  [R] Clear  [Esc] Back",
			14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr float WORLD_SIZE = 500.0f;   ///< 3Dワールドの各軸サイズ
	static constexpr float OBJ_MIN = 15.0f;        ///< オブジェクト最小サイズ
	static constexpr float OBJ_MAX = 50.0f;        ///< オブジェクト最大サイズ
	static constexpr float QUERY_RANGE = 120.0f;   ///< クエリ範囲の半径
	static constexpr float MARGIN = 20.0f;          ///< 画面マージン

	sgc::Octreef m_octree{sgc::AABB3f{
		{0.0f, 0.0f, 0.0f},
		{WORLD_SIZE, WORLD_SIZE, WORLD_SIZE}}};      ///< 八分木
	std::vector<InsertedObject> m_objects;             ///< 挿入済みオブジェクト
	std::vector<sgc::Octreef::Handle> m_queryResults;  ///< クエリ結果
	sgc::AABB3f m_queryBounds{};                        ///< クエリ境界
	sgc::Vec2f m_mousePos{};                            ///< マウス位置
	bool m_queryActive{false};                          ///< クエリ実行中か
	int m_insertCounter{0};                             ///< 挿入カウンタ

	/// @brief 八分木をリセットする
	void resetOctree()
	{
		m_octree.clear();
		m_objects.clear();
		m_queryResults.clear();
		m_insertCounter = 0;

		// 初期オブジェクトを挿入
		for (int i = 0; i < 15; ++i)
		{
			const float t = static_cast<float>(i) / 15.0f;
			const float x = t * (WORLD_SIZE - OBJ_MAX * 2) + OBJ_MAX;
			const float y = static_cast<float>((i * 97 + 13) % 400) + 50.0f;
			const float z = static_cast<float>((i * 53 + 7) % static_cast<int>(WORLD_SIZE));
			insertObjectAt(x, y, z);
		}
	}

	/// @brief マウス位置にオブジェクトを挿入する
	void insertObjectAtMouse()
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const float worldW = sw - MARGIN * 2.0f;
		const float worldH = sh - 130.0f;
		const float worldY = 70.0f;

		// スクリーン座標→ワールド座標に変換
		const float wx = remap(m_mousePos.x - MARGIN, 0.0f, worldW, 0.0f, WORLD_SIZE);
		const float wy = remap(m_mousePos.y - worldY, 0.0f, worldH, 0.0f, WORLD_SIZE);
		// ランダムなZ座標
		const float wz = static_cast<float>((m_insertCounter * 71 + 23) % static_cast<int>(WORLD_SIZE));

		insertObjectAt(wx, wy, wz);
	}

	/// @brief 指定3D位置にオブジェクトを挿入する
	void insertObjectAt(float x, float y, float z)
	{
		++m_insertCounter;

		const float sizeT = static_cast<float>(m_insertCounter * 41 % 11) / 10.0f;
		const float size = OBJ_MIN + sizeT * (OBJ_MAX - OBJ_MIN);
		const float halfSize = size * 0.5f;

		const sgc::AABB3f bounds{
			{x - halfSize, y - halfSize, z - halfSize},
			{x + halfSize, y + halfSize, z + halfSize}
		};

		const auto handle = m_octree.insert(bounds);
		const float hue = static_cast<float>(m_insertCounter * 47 % 360);
		m_objects.push_back(InsertedObject{bounds, handle, hue});
	}

	/// @brief マウス周辺の範囲クエリを実行する
	void executeQuery()
	{
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const float worldW = sw - MARGIN * 2.0f;
		const float worldH = sh - 130.0f;
		const float worldY = 70.0f;

		const float cx = remap(m_mousePos.x - MARGIN, 0.0f, worldW, 0.0f, WORLD_SIZE);
		const float cy = remap(m_mousePos.y - worldY, 0.0f, worldH, 0.0f, WORLD_SIZE);

		m_queryBounds = sgc::AABB3f{
			{cx - QUERY_RANGE, cy - QUERY_RANGE, 0.0f},
			{cx + QUERY_RANGE, cy + QUERY_RANGE, WORLD_SIZE}
		};

		m_queryResults = m_octree.queryRange(m_queryBounds);
	}

	/// @brief 値を1つの範囲から別の範囲にリマップする
	[[nodiscard]] static constexpr float remap(
		float value, float inMin, float inMax, float outMin, float outMax) noexcept
	{
		if (inMax - inMin == 0.0f) return outMin;
		return outMin + (value - inMin) / (inMax - inMin) * (outMax - outMin);
	}
};
