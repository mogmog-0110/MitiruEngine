#pragma once

/// @file SampleLogger.hpp
/// @brief Loggerカスタムシンクによる画面表示デモ
///
/// ScreenSinkでログメッセージをキャプチャし、画面上にリアルタイム表示する。
/// - 1: Traceログ送出
/// - 2: Infoログ送出
/// - 3: Warnログ送出
/// - Space: Errorログ送出
/// - D: ログレベルフィルタ切り替え
/// - R: ログクリア
/// - ESCキー: メニューに戻る

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/core/Logger.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief 画面表示用ログシンク
///
/// ログメッセージを内部バッファにキャプチャし、後から参照できるようにする。
struct ScreenSink
{
	/// @brief キャプチャされたログエントリ
	struct Entry
	{
		sgc::LogLevel level;   ///< ログレベル
		std::string text;      ///< メッセージ本文
	};

	/// @brief ログメッセージを書き込む
	/// @param msg ログメッセージ
	void write(const sgc::LogMessage& msg) const
	{
		if (entries)
		{
			entries->push_back(Entry{msg.level, msg.text});
			// 最大行数を超えたら古いものを削除
			if (entries->size() > MAX_ENTRIES)
			{
				entries->erase(entries->begin());
			}
		}
	}

	static constexpr std::size_t MAX_ENTRIES = 30;            ///< 最大保持行数
	std::shared_ptr<std::vector<Entry>> entries;               ///< エントリバッファ
};

/// @brief Loggerサンプルシーン
///
/// カスタムScreenSinkでログを画面に表示し、
/// レベルフィルタリングの動作を可視化する。
class SampleLogger : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_entries = std::make_shared<std::vector<ScreenSink::Entry>>();
		m_logger.emplace();
		m_logger->clearSinks();

		ScreenSink sink;
		sink.entries = m_entries;
		m_logger->addSink(std::move(sink));
		m_logger->setLevel(sgc::LogLevel::Trace);
		m_levelIndex = 0;
		m_msgCounter = 0;
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

		// Rでログクリア
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_entries->clear();
			m_msgCounter = 0;
			return;
		}

		// D: ログレベルフィルタ切り替え
		if (input->isKeyJustPressed(KeyCode::D))
		{
			m_levelIndex = (m_levelIndex + 1) % LEVEL_COUNT;
			m_logger->setLevel(LEVELS[m_levelIndex]);
		}

		// 1: Trace
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			++m_msgCounter;
			SGC_LOG_TRACE(*m_logger, "Trace message #{}", m_msgCounter);
		}

		// 2: Info
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			++m_msgCounter;
			SGC_LOG_INFO(*m_logger, "Info: system status OK ({})", m_msgCounter);
		}

		// 3: Warn
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			++m_msgCounter;
			SGC_LOG_WARN(*m_logger, "Warning: resource usage high ({})", m_msgCounter);
		}

		// Space: Error
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			++m_msgCounter;
			SGC_LOG_ERROR(*m_logger, "Error: connection failed! ({})", m_msgCounter);
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
		renderer->clearBackground(sgc::Colorf{0.04f, 0.04f, 0.08f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"Logger Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.3f, 0.9f, 0.6f, 1.0f});

		// 現在のフィルタレベル表示
		const auto currentLevel = m_logger->getLevel();
		const auto levelStr = std::string("Filter: ")
			+ std::string(sgc::logLevelToString(currentLevel));
		text->drawText(levelStr, 20.0f,
			sgc::Vec2f{20.0f, 70.0f},
			levelColor(currentLevel));

		// レベル凡例
		drawLegend(renderer, text, sw - 180.0f, 70.0f);

		// ログ表示エリア
		const float logX = 10.0f;
		const float logY = 110.0f;
		const float logW = sw - 20.0f;
		const float logH = sh - 160.0f;

		renderer->drawRect(
			sgc::AABB2f{{logX, logY}, {logX + logW, logY + logH}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.6f});
		renderer->drawRectFrame(
			sgc::AABB2f{{logX, logY}, {logX + logW, logY + logH}},
			1.0f, sgc::Colorf{0.2f, 0.3f, 0.2f, 0.8f});

		// ログメッセージ描画
		const float lineHeight = 20.0f;
		const int maxVisible = static_cast<int>((logH - 10.0f) / lineHeight);
		const int totalEntries = static_cast<int>(m_entries->size());
		const int startIdx = (totalEntries > maxVisible)
			? totalEntries - maxVisible : 0;

		for (int i = startIdx; i < totalEntries; ++i)
		{
			const auto& entry = (*m_entries)[i];
			const float lineY = logY + 6.0f
				+ static_cast<float>(i - startIdx) * lineHeight;

			// レベルタグ
			const auto tag = "[" + std::string(sgc::logLevelToString(entry.level)) + "] ";
			const auto color = levelColor(entry.level);

			text->drawText(tag + entry.text, 14.0f,
				sgc::Vec2f{logX + 10.0f, lineY}, color);
		}

		// エントリ数
		text->drawText(
			"Messages: " + std::to_string(m_entries->size()), 14.0f,
			sgc::Vec2f{logX + 10.0f, logY + logH + 4.0f},
			sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// 操作説明
		text->drawText(
			"[1] Trace  [2] Info  [3] Warn  [Space] Error  [D] Filter  [R] Clear  [Esc] Back",
			14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr int LEVEL_COUNT = 5;  ///< フィルタレベル切り替え数

	/// @brief フィルタレベル候補
	static constexpr sgc::LogLevel LEVELS[LEVEL_COUNT] =
	{
		sgc::LogLevel::Trace,
		sgc::LogLevel::Debug,
		sgc::LogLevel::Info,
		sgc::LogLevel::Warn,
		sgc::LogLevel::Error,
	};

	std::optional<sgc::Logger> m_logger;                       ///< ロガー
	std::shared_ptr<std::vector<ScreenSink::Entry>> m_entries; ///< キャプチャバッファ
	int m_levelIndex{0};                                       ///< 現在のフィルタインデックス
	int m_msgCounter{0};                                       ///< メッセージ連番

	/// @brief レベルに対応する表示色を返す
	/// @param level ログレベル
	/// @return 描画色
	[[nodiscard]] static sgc::Colorf levelColor(sgc::LogLevel level) noexcept
	{
		switch (level)
		{
		case sgc::LogLevel::Trace: return sgc::Colorf{0.5f, 0.5f, 0.6f, 1.0f};
		case sgc::LogLevel::Debug: return sgc::Colorf{0.4f, 0.7f, 1.0f, 1.0f};
		case sgc::LogLevel::Info:  return sgc::Colorf{0.3f, 1.0f, 0.4f, 1.0f};
		case sgc::LogLevel::Warn:  return sgc::Colorf{1.0f, 0.9f, 0.3f, 1.0f};
		case sgc::LogLevel::Error: return sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f};
		case sgc::LogLevel::Fatal: return sgc::Colorf{1.0f, 0.1f, 0.5f, 1.0f};
		default: return sgc::Colorf::white();
		}
	}

	/// @brief レベル凡例を描画する
	static void drawLegend(
		sgc::IRenderer* renderer, sgc::ITextRenderer* text,
		float x, float y)
	{
		renderer->drawRect(
			sgc::AABB2f{{x, y}, {x + 170.0f, y + 100.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.5f});

		constexpr sgc::LogLevel legendLevels[] =
		{
			sgc::LogLevel::Trace, sgc::LogLevel::Info,
			sgc::LogLevel::Warn, sgc::LogLevel::Error
		};

		for (int i = 0; i < 4; ++i)
		{
			const float ly = y + 6.0f + static_cast<float>(i) * 22.0f;
			const auto color = levelColor(legendLevels[i]);
			renderer->drawRect(
				sgc::AABB2f{{x + 8.0f, ly + 2.0f}, {x + 20.0f, ly + 14.0f}},
				color);
			text->drawText(
				std::string(sgc::logLevelToString(legendLevels[i])), 14.0f,
				sgc::Vec2f{x + 28.0f, ly},
				color);
		}
	}
};
