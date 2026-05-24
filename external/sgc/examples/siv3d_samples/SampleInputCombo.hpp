#pragma once

/// @file SampleInputCombo.hpp
/// @brief InputBuffer（コンボ入力検出）ビジュアルサンプル
///
/// 格闘ゲーム風のコマンド入力を検出し、可視化する。
/// 矢印キーでコマンドを入力し、定義済みのコンボが成立したらエフェクトが表示される。
/// - 矢印キー: コマンド入力
/// - Rキー: 入力履歴クリア
/// - ESCキー: メニューに戻る

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "sgc/core/Hash.hpp"
#include "sgc/input/InputBuffer.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief InputBufferサンプルシーン
///
/// 矢印キーの入力シーケンスを記録し、
/// 定義済みのコンボパターンとマッチングする。
class SampleInputCombo : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_buffer.clear();
		m_time = 0.0f;
		m_comboFlash = 0.0f;
		m_lastCombo = "";
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

		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_buffer.clear();
			m_recentInputs.fill({});
			m_recentCount = 0;
		}

		m_time += dt;

		// キー入力を記録
		recordKeyIfPressed(input, KeyCode::DOWN, ACTION_DOWN, "Down");
		recordKeyIfPressed(input, KeyCode::RIGHT, ACTION_RIGHT, "Right");
		recordKeyIfPressed(input, KeyCode::LEFT, ACTION_LEFT, "Left");
		recordKeyIfPressed(input, KeyCode::UP, ACTION_UP, "Up");

		// コンボ判定
		checkCombos();

		// フラッシュ減衰
		if (m_comboFlash > 0.0f)
		{
			m_comboFlash -= dt * 2.0f;
			if (m_comboFlash < 0.0f)
			{
				m_comboFlash = 0.0f;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// タイトル
		tr->drawText(
			"Input Buffer - Combo Detection", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{1.0f, 0.6f, 0.8f, 1.0f});

		tr->drawText(
			"Arrow keys: Input | R: Clear | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// コンボ定義リスト
		tr->drawText("Combos:", 18.0f,
			{30.0f, 80.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

		for (std::size_t i = 0; i < COMBO_COUNT; ++i)
		{
			const float cy = 115.0f + static_cast<float>(i) * 50.0f;
			const auto& combo = COMBOS[i];

			// コンボ名
			tr->drawText(std::string{combo.name}, 16.0f,
				{50.0f, cy}, sgc::Colorf{0.9f, 0.8f, 0.5f, 1.0f});

			// コンボシーケンス（矢印記号）
			tr->drawText(std::string{combo.display}, 16.0f,
				{200.0f, cy}, sgc::Colorf{0.6f, 0.7f, 0.9f, 1.0f});
		}

		// 入力履歴
		tr->drawText("Recent Inputs:", 18.0f,
			{430.0f, 80.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

		const std::size_t showCount = (m_recentCount < MAX_RECENT)
			? m_recentCount : MAX_RECENT;

		for (std::size_t i = 0; i < showCount; ++i)
		{
			const std::size_t idx = (m_recentCount >= MAX_RECENT)
				? (m_recentCount - MAX_RECENT + i) % MAX_RECENT
				: i;
			const auto& ri = m_recentInputs[idx];

			const float iy = 115.0f + static_cast<float>(i) * 30.0f;
			const sgc::Colorf inputColor{0.5f, 0.8f, 1.0f, 1.0f};

			tr->drawText(std::string{ri.label}, 14.0f,
				{450.0f, iy}, inputColor);
		}

		// 入力キー視覚化（十字キーパッド）
		const float padCx = sw * 0.5f;
		const float padCy = sh - 120.0f;
		const float padSize = 35.0f;
		const float padGap = 5.0f;
		const auto* input = getData().inputProvider;

		drawKeyPad(r, tr, padCx, padCy - padSize - padGap, "Up",
			input->isKeyDown(KeyCode::UP));
		drawKeyPad(r, tr, padCx - padSize - padGap, padCy, "L",
			input->isKeyDown(KeyCode::LEFT));
		drawKeyPad(r, tr, padCx, padCy, "Dn",
			input->isKeyDown(KeyCode::DOWN));
		drawKeyPad(r, tr, padCx + padSize + padGap, padCy, "R",
			input->isKeyDown(KeyCode::RIGHT));

		// コンボ検出フラッシュ
		if (m_comboFlash > 0.0f)
		{
			const sgc::Colorf flashColor{1.0f, 0.9f, 0.2f, m_comboFlash};
			tr->drawTextCentered(m_lastCombo, 36.0f,
				{sw * 0.5f, 350.0f}, flashColor);

			// 背景フラッシュ
			r->drawRect(
				sgc::AABB2f{{0.0f, 330.0f}, {sw, 380.0f}},
				sgc::Colorf{1.0f, 0.9f, 0.2f, m_comboFlash * 0.15f});
		}
	}

private:
	/// @brief アクションID定義
	static constexpr std::uint64_t ACTION_DOWN = 1;
	static constexpr std::uint64_t ACTION_RIGHT = 2;
	static constexpr std::uint64_t ACTION_LEFT = 3;
	static constexpr std::uint64_t ACTION_UP = 4;

	/// @brief コンボ定義
	struct ComboDefinition
	{
		std::string_view name;       ///< コンボ名
		std::string_view display;    ///< 表示用シーケンス
		std::array<std::uint64_t, 4> sequence; ///< 入力シーケンス
		std::size_t length;          ///< シーケンス長
	};

	static constexpr std::size_t COMBO_COUNT = 4;

	/// @brief コンボ定義テーブル
	static constexpr std::array<ComboDefinition, COMBO_COUNT> COMBOS =
	{{
		{"Hadouken",   "Down Right Right",
			{ACTION_DOWN, ACTION_RIGHT, ACTION_RIGHT, 0}, 3},
		{"Shoryuken",  "Right Down Right",
			{ACTION_RIGHT, ACTION_DOWN, ACTION_RIGHT, 0}, 3},
		{"Hurricane",  "Down Left Left",
			{ACTION_DOWN, ACTION_LEFT, ACTION_LEFT, 0}, 3},
		{"Super",      "Down Right Down Right",
			{ACTION_DOWN, ACTION_RIGHT, ACTION_DOWN, ACTION_RIGHT}, 4}
	}};

	/// @brief 最近の入力表示用
	static constexpr std::size_t MAX_RECENT = 12;

	struct RecentInput
	{
		std::string_view label{};
	};

	sgc::InputBuffer<60> m_buffer;
	float m_time{0.0f};
	float m_comboFlash{0.0f};
	std::string m_lastCombo;

	std::array<RecentInput, MAX_RECENT> m_recentInputs{};
	std::size_t m_recentCount{0};

	/// @brief キー入力を記録する
	void recordKeyIfPressed(
		const sgc::IInputProvider* input,
		int keyCode, std::uint64_t actionId, std::string_view label)
	{
		if (input->isKeyJustPressed(keyCode))
		{
			m_buffer.record(actionId, m_time);
			const std::size_t idx = m_recentCount % MAX_RECENT;
			m_recentInputs[idx] = {label};
			++m_recentCount;
		}
	}

	/// @brief コンボを判定する
	void checkCombos()
	{
		for (const auto& combo : COMBOS)
		{
			const auto seq = std::span<const std::uint64_t>(
				combo.sequence.data(), combo.length);

			if (m_buffer.matchSequence(seq, 0.8f))
			{
				m_comboFlash = 1.0f;
				m_lastCombo = std::string{combo.name} + "!!";
				m_buffer.clear();
				m_recentInputs.fill({});
				m_recentCount = 0;
				break;
			}
		}
	}

	/// @brief キーパッドボタンを描画する
	static void drawKeyPad(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float cx, float cy, const char* label, bool pressed)
	{
		const float half = 15.0f;
		const sgc::AABB2f rect{
			{cx - half, cy - half}, {cx + half, cy + half}};

		if (pressed)
		{
			r->drawRect(rect, sgc::Colorf{0.3f, 0.6f, 1.0f, 0.8f});
		}
		else
		{
			r->drawRectFrame(rect, 1.5f, sgc::Colorf{0.4f, 0.4f, 0.5f, 0.6f});
		}

		tr->drawTextCentered(label, 10.0f, {cx, cy},
			pressed
				? sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}
				: sgc::Colorf{0.5f, 0.5f, 0.6f, 0.8f});
	}
};
