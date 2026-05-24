#pragma once

/// @file SampleActionMap.hpp
/// @brief ActionMap（入力アクションマッピング）ビジュアルサンプル
///
/// ActionMapでキーをゲームアクションに紐付け、状態遷移を可視化する。
/// プレイヤーキャラクターをWASD/矢印キーで操作し、
/// アクション状態（Pressed/Held/Released）をリアルタイム表示する。
/// - WASD/矢印: 移動
/// - Space: ジャンプ
/// - ESCキー: メニューに戻る

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/input/ActionMap.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief ActionMapサンプルシーン
///
/// 6つのゲームアクション（上下左右・ジャンプ・ダッシュ）の状態を
/// リアルタイムに可視化する。ActionMapがキー入力をアクションに変換する
/// 様子を直感的に理解できる。
class SampleActionMap : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_actionMap = sgc::ActionMap{};

		// アクションにキーをバインド（複数キーを同じアクションに）
		m_actionMap.bind(ACT_UP, KeyCode::UP);
		m_actionMap.bind(ACT_UP, KeyCode::W);
		m_actionMap.bind(ACT_DOWN, KeyCode::DOWN);
		m_actionMap.bind(ACT_DOWN, KeyCode::S);
		m_actionMap.bind(ACT_LEFT, KeyCode::LEFT);
		m_actionMap.bind(ACT_LEFT, KeyCode::A);
		m_actionMap.bind(ACT_RIGHT, KeyCode::RIGHT);
		m_actionMap.bind(ACT_RIGHT, KeyCode::D);
		m_actionMap.bind(ACT_JUMP, KeyCode::SPACE);

		m_charX = 400.0f;
		m_charY = 300.0f;
	}

	/// @brief 毎フレームの更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// 現在押されているキーを収集
		m_pressedKeys.clear();
		input->pollPressedKeys(m_pressedKeys);

		// ActionMapを更新
		m_actionMap.update(m_pressedKeys);

		// キャラクター移動
		constexpr float speed = 200.0f;
		if (m_actionMap.isHeld(ACT_LEFT))
		{
			m_charX -= speed * dt;
		}
		if (m_actionMap.isHeld(ACT_RIGHT))
		{
			m_charX += speed * dt;
		}
		if (m_actionMap.isHeld(ACT_UP))
		{
			m_charY -= speed * dt;
		}
		if (m_actionMap.isHeld(ACT_DOWN))
		{
			m_charY += speed * dt;
		}

		// ジャンプ（プレスでスケールバウンス）
		if (m_actionMap.isPressed(ACT_JUMP))
		{
			m_jumpScale = 1.5f;
		}

		// ジャンプスケール減衰
		if (m_jumpScale > 1.0f)
		{
			m_jumpScale -= dt * 4.0f;
			if (m_jumpScale < 1.0f)
			{
				m_jumpScale = 1.0f;
			}
		}

		// 画面内クランプ
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		if (m_charX < 20.0f) m_charX = 20.0f;
		if (m_charX > sw - 20.0f) m_charX = sw - 20.0f;
		if (m_charY < PLAY_AREA_TOP + 20.0f) m_charY = PLAY_AREA_TOP + 20.0f;
		if (m_charY > sh - 20.0f) m_charY = sh - 20.0f;
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
			"ActionMap - Input Binding", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.9f, 0.7f, 1.0f, 1.0f});

		tr->drawText(
			"WASD/Arrows: Move | Space: Jump | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// アクション状態テーブル
		drawActionTable(r, tr);

		// プレイエリア境界
		r->drawLine(
			{0.0f, PLAY_AREA_TOP}, {sw, PLAY_AREA_TOP},
			1.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.3f});

		// キャラクター
		const float charRadius = 18.0f * m_jumpScale;
		r->drawCircle({m_charX, m_charY}, charRadius,
			sgc::Colorf{0.3f, 0.7f, 1.0f, 0.8f});
		r->drawCircleFrame({m_charX, m_charY}, charRadius, 2.0f,
			sgc::Colorf{0.5f, 0.8f, 1.0f, 1.0f});

		// ジャンプエフェクト
		if (m_jumpScale > 1.05f)
		{
			const float effectR = charRadius + 10.0f;
			r->drawCircleFrame({m_charX, m_charY}, effectR, 1.5f,
				sgc::Colorf{1.0f, 1.0f, 0.5f, m_jumpScale - 1.0f});
		}
	}

private:
	/// @brief アクションID定義
	static constexpr sgc::ActionId ACT_UP = 1;
	static constexpr sgc::ActionId ACT_DOWN = 2;
	static constexpr sgc::ActionId ACT_LEFT = 3;
	static constexpr sgc::ActionId ACT_RIGHT = 4;
	static constexpr sgc::ActionId ACT_JUMP = 5;

	static constexpr float PLAY_AREA_TOP = 200.0f;  ///< プレイエリア上端

	/// @brief アクション表示情報
	struct ActionInfo
	{
		std::string_view name;   ///< アクション名
		sgc::ActionId id;        ///< アクションID
		std::string_view keys;   ///< バインドキー表示
	};

	static constexpr std::size_t ACTION_COUNT = 5;

	static constexpr std::array<ActionInfo, ACTION_COUNT> ACTIONS =
	{{
		{"Move Up",    ACT_UP,    "W / Up"},
		{"Move Down",  ACT_DOWN,  "S / Down"},
		{"Move Left",  ACT_LEFT,  "A / Left"},
		{"Move Right", ACT_RIGHT, "D / Right"},
		{"Jump",       ACT_JUMP,  "Space"}
	}};

	sgc::ActionMap m_actionMap;
	std::vector<int> m_pressedKeys;
	float m_charX{400.0f};
	float m_charY{350.0f};
	float m_jumpScale{1.0f};

	/// @brief アクション状態テーブルを描画する
	void drawActionTable(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const float tableX = 30.0f;
		const float tableY = 70.0f;
		const float rowH = 22.0f;
		const float colAction = tableX;
		const float colKeys = tableX + 120.0f;
		const float colState = tableX + 250.0f;
		const float colVisual = tableX + 380.0f;

		// ヘッダー
		const sgc::Colorf headerColor{0.7f, 0.7f, 0.8f, 1.0f};
		tr->drawText("Action", 14.0f, {colAction, tableY}, headerColor);
		tr->drawText("Keys", 14.0f, {colKeys, tableY}, headerColor);
		tr->drawText("State", 14.0f, {colState, tableY}, headerColor);
		tr->drawText("Visual", 14.0f, {colVisual, tableY}, headerColor);

		for (std::size_t i = 0; i < ACTION_COUNT; ++i)
		{
			const float y = tableY + 22.0f + static_cast<float>(i) * rowH;
			const auto& act = ACTIONS[i];
			const auto state = m_actionMap.state(act.id);

			// アクション名
			tr->drawText(std::string{act.name}, 12.0f,
				{colAction, y}, sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

			// バインドキー
			tr->drawText(std::string{act.keys}, 12.0f,
				{colKeys, y}, sgc::Colorf{0.6f, 0.6f, 0.7f, 1.0f});

			// 状態テキスト
			const char* stateText = "None";
			sgc::Colorf stateColor{0.4f, 0.4f, 0.4f, 1.0f};

			if (state == sgc::ActionState::Pressed)
			{
				stateText = "Pressed";
				stateColor = {0.2f, 1.0f, 0.4f, 1.0f};
			}
			else if (state == sgc::ActionState::Held)
			{
				stateText = "Held";
				stateColor = {1.0f, 0.8f, 0.2f, 1.0f};
			}
			else if (state == sgc::ActionState::Released)
			{
				stateText = "Released";
				stateColor = {1.0f, 0.3f, 0.3f, 1.0f};
			}

			tr->drawText(stateText, 12.0f, {colState, y}, stateColor);

			// ビジュアルバー
			const float barW = 120.0f;
			const float barH = 12.0f;
			const sgc::AABB2f barBg{
				{colVisual, y + 2.0f},
				{colVisual + barW, y + 2.0f + barH}};
			r->drawRect(barBg, sgc::Colorf{0.15f, 0.15f, 0.2f, 1.0f});

			if (state == sgc::ActionState::Pressed ||
				state == sgc::ActionState::Held)
			{
				r->drawRect(
					sgc::AABB2f{
						{colVisual, y + 2.0f},
						{colVisual + barW, y + 2.0f + barH}},
					stateColor.withAlpha(0.5f));
			}
		}
	}
};
