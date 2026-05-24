#pragma once

/// @file SampleConfig.hpp
/// @brief ConfigManager（設定管理）サンプル
///
/// ConfigManagerのキー・バリュー設定を可視化するデモ。
/// 各種型の設定値を操作し、JSON出力を確認できる。
/// - 1キー: 音量を変更
/// - 2キー: フルスクリーンをトグル
/// - 3キー: 難易度を切り替え
/// - Rキー: 設定リセット
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/config/ConfigManager.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief ConfigManagerサンプルシーン
///
/// キー・バリュー形式のゲーム設定を操作し、
/// 設定テーブルとJSON出力をリアルタイムに表示する。
class SampleConfig : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_config = sgc::ConfigManager{};
		m_changeFlash = 0.0f;
		m_lastChanged.clear();
		setupDefaults();
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
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
			onEnter();
			return;
		}

		// 1: 音量変更（0.0 → 0.25 → 0.5 → 0.75 → 1.0 → 0.0 ...）
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			const float vol = m_config.getFloatOr("volume", 0.8f);
			const float next = (vol >= 0.99f) ? 0.0f : vol + 0.25f;
			m_config.set("volume", next);
			flashChange("volume");
		}

		// 2: フルスクリーンをトグル
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			const bool fs = m_config.getBoolOr("fullscreen", false);
			m_config.set("fullscreen", !fs);
			flashChange("fullscreen");
		}

		// 3: 難易度切り替え（Easy → Normal → Hard → Easy ...）
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			const std::string diff = m_config.getStringOr("difficulty", "Normal");
			std::string next = "Normal";
			if (diff == "Easy") next = "Normal";
			else if (diff == "Normal") next = "Hard";
			else next = "Easy";
			m_config.set("difficulty", next);
			flashChange("difficulty");
		}

		// フラッシュ減衰
		if (m_changeFlash > 0.0f)
		{
			m_changeFlash -= dt * 3.0f;
			if (m_changeFlash < 0.0f) m_changeFlash = 0.0f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.05f, 0.06f, 0.1f, 1.0f});

		// タイトル
		tr->drawText("ConfigManager - Key/Value Settings", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.6f, 1.0f, 0.8f, 1.0f});
		tr->drawText(
			"[1] Volume  [2] Fullscreen  [3] Difficulty  [R] Reset  [Esc] Back",
			14.0f, {10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// 設定テーブル
		drawSettingsTable(r, tr);

		// JSON出力
		drawJsonOutput(r, tr, sw, sh);
	}

private:
	sgc::ConfigManager m_config;       ///< 設定マネージャ
	float m_changeFlash{0.0f};         ///< 変更フラッシュエフェクト
	std::string m_lastChanged;         ///< 最後に変更されたキー名

	/// @brief デフォルト設定を登録する
	void setupDefaults()
	{
		m_config.set("volume", 0.75f);
		m_config.set("fullscreen", false);
		m_config.set("difficulty", std::string{"Normal"});
		m_config.set("maxFPS", 60);
		m_config.set("vsync", true);
		m_config.set("playerName", std::string{"Player1"});
	}

	/// @brief 変更フラッシュを発火する
	/// @param key 変更されたキー名
	void flashChange(const std::string& key)
	{
		m_changeFlash = 1.0f;
		m_lastChanged = key;
	}

	/// @brief 設定値を文字列に変換する
	/// @param key キー名
	/// @return 表示用文字列
	std::string valueToString(const std::string& key) const
	{
		if (const auto v = m_config.getInt(key))
		{
			return std::to_string(*v);
		}
		if (const auto v = m_config.getFloat(key))
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.2f", *v);
			return buf;
		}
		if (const auto v = m_config.getBool(key))
		{
			return *v ? "true" : "false";
		}
		if (const auto v = m_config.getString(key))
		{
			return "\"" + *v + "\"";
		}
		return "(unknown)";
	}

	/// @brief 設定値の型名を返す
	/// @param key キー名
	/// @return 型名文字列
	std::string typeString(const std::string& key) const
	{
		if (m_config.getInt(key)) return "int";
		if (m_config.getFloat(key)) return "float";
		if (m_config.getBool(key)) return "bool";
		if (m_config.getString(key)) return "string";
		return "?";
	}

	/// @brief 設定テーブルを描画する
	void drawSettingsTable(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const float px = 20.0f;
		const float py = 70.0f;
		const float pw = 500.0f;

		// テーブルヘッダー
		const sgc::Colorf headerCol{0.6f, 0.7f, 0.8f, 1.0f};
		const float colKey = px + 10.0f;
		const float colType = px + 180.0f;
		const float colValue = px + 260.0f;
		const float rowH = 28.0f;

		r->drawRect(sgc::AABB2f{{px, py}, {px + pw, py + 30.0f}},
			sgc::Colorf{0.1f, 0.1f, 0.18f, 0.9f});
		tr->drawText("Key", 14.0f, {colKey, py + 6.0f}, headerCol);
		tr->drawText("Type", 14.0f, {colType, py + 6.0f}, headerCol);
		tr->drawText("Value", 14.0f, {colValue, py + 6.0f}, headerCol);

		// 設定項目を表示
		const std::string keys[] =
			{"volume", "fullscreen", "difficulty", "maxFPS", "vsync", "playerName"};

		for (std::size_t i = 0; i < 6; ++i)
		{
			const float y = py + 34.0f + static_cast<float>(i) * rowH;
			const auto& key = keys[i];

			// 行背景（交互色）
			const float bgAlpha = (i % 2 == 0) ? 0.06f : 0.03f;
			r->drawRect(
				sgc::AABB2f{{px, y}, {px + pw, y + rowH}},
				sgc::Colorf{0.1f, 0.1f, 0.15f, bgAlpha + 0.5f});

			// 変更ハイライト
			if (key == m_lastChanged && m_changeFlash > 0.0f)
			{
				r->drawRect(
					sgc::AABB2f{{px, y}, {px + pw, y + rowH}},
					sgc::Colorf{0.3f, 0.8f, 0.4f, m_changeFlash * 0.2f});
			}

			// キー名
			tr->drawText(key, 14.0f, {colKey, y + 5.0f},
				sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f});

			// 型
			tr->drawText(typeString(key), 12.0f, {colType, y + 6.0f},
				sgc::Colorf{0.5f, 0.7f, 0.9f, 1.0f});

			// 値
			const sgc::Colorf valCol = (key == m_lastChanged && m_changeFlash > 0.0f)
				? sgc::Colorf{0.3f, 1.0f, 0.5f, 1.0f}
				: sgc::Colorf{1.0f, 0.9f, 0.6f, 1.0f};
			tr->drawText(valueToString(key), 14.0f,
				{colValue, y + 5.0f}, valCol);
		}

		// テーブル枠
		const float tableH = 34.0f + 6.0f * rowH;
		r->drawRectFrame(
			sgc::AABB2f{{px, py}, {px + pw, py + tableH}},
			2.0f, sgc::Colorf{0.3f, 0.5f, 0.6f, 0.7f});

		// 設定数表示
		tr->drawText(
			"Total entries: " + std::to_string(m_config.size()), 14.0f,
			{px, py + tableH + 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f, 1.0f});
	}

	/// @brief JSON出力を描画する
	void drawJsonOutput(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float sw, float sh) const
	{
		const float px = 20.0f;
		const float py = sh - 200.0f;
		const float pw = sw - 40.0f;
		const float ph = 180.0f;

		// JSON出力パネル
		r->drawRect(sgc::AABB2f{{px, py}, {px + pw, py + ph}},
			sgc::Colorf{0.03f, 0.03f, 0.06f, 0.9f});
		r->drawRectFrame(sgc::AABB2f{{px, py}, {px + pw, py + ph}},
			2.0f, sgc::Colorf{0.3f, 0.4f, 0.5f, 0.7f});

		tr->drawText("JSON Output:", 16.0f,
			{px + 10.0f, py + 8.0f},
			sgc::Colorf{0.6f, 0.8f, 0.6f, 1.0f});

		// JSON文字列を表示（簡易的に1行ずつ分割）
		const std::string json = m_config.toJson();
		float textY = py + 35.0f;
		std::size_t pos = 0;
		int lineCount = 0;

		while (pos < json.size() && lineCount < 8)
		{
			const std::size_t newline = json.find('\n', pos);
			const std::size_t end = (newline == std::string::npos)
				? json.size() : newline;
			const std::string line = json.substr(pos, end - pos);

			if (!line.empty())
			{
				tr->drawText(line, 12.0f,
					{px + 20.0f, textY},
					sgc::Colorf{0.7f, 0.9f, 0.7f, 0.9f});
				textY += 17.0f;
				++lineCount;
			}

			pos = (newline == std::string::npos) ? json.size() : newline + 1;
		}
	}
};
