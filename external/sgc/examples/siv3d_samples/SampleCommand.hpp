#pragma once

/// @file SampleCommand.hpp
/// @brief Commandパターン（Undo/Redo）ビジュアルサンプル
///
/// キャンバスにクリックで円を配置し、Undo/Redoで操作を巻き戻す。
/// - クリック: 円を配置
/// - 1キー: Undo
/// - 2キー: Redo
/// - Rキー: 全クリア
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/patterns/Command.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Commandパターンサンプルシーン
///
/// クリックで色付き円をキャンバスに配置する。
/// CommandHistoryを使ってUndo/Redoを実現する。
class SampleCommand : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief 毎フレームの更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Undo
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_history.undo();
		}

		// Redo
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_history.redo();
		}

		// 全クリア
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_dots.clear();
			m_history.clear();
		}

		// クリックで円を配置
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			const auto pos = input->mousePosition();
			// キャンバス領域内のみ
			if (pos.y > CANVAS_TOP)
			{
				placeDot(pos);
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
		r->drawRect(
			sgc::AABB2f{{0.0f, 0.0f}, {sw, CANVAS_TOP}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.6f});

		tr->drawText(
			"Command Pattern - Undo/Redo", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{1.0f, 0.8f, 0.5f, 1.0f});

		tr->drawText(
			"Click: Place | 1: Undo | 2: Redo | R: Clear | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// Undo/Redoスタック情報
		const std::string stackInfo =
			"Undo: " + std::to_string(m_history.undoSize())
			+ "  Redo: " + std::to_string(m_history.redoSize())
			+ "  Dots: " + std::to_string(m_dots.size());

		tr->drawText(
			stackInfo, 16.0f,
			{10.0f, 60.0f}, sgc::Colorf{0.5f, 0.8f, 0.5f, 1.0f});

		// キャンバス上の境界線
		r->drawLine(
			{0.0f, CANVAS_TOP}, {sw, CANVAS_TOP},
			1.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.5f});

		// 円を描画
		for (const auto& dot : m_dots)
		{
			r->drawCircle({dot.x, dot.y}, DOT_RADIUS, dot.color);
			r->drawCircleFrame({dot.x, dot.y}, DOT_RADIUS, 1.5f,
				dot.color.withAlpha(0.4f));
		}

		// 空キャンバスのヒント
		if (m_dots.empty())
		{
			tr->drawTextCentered(
				"Click to place dots!", 24.0f,
				{sw * 0.5f, 350.0f},
				sgc::Colorf{0.3f, 0.3f, 0.4f, 0.5f});
		}
	}

private:
	static constexpr float CANVAS_TOP = 85.0f;  ///< キャンバス上端Y
	static constexpr float DOT_RADIUS = 15.0f;  ///< 配置する円の半径

	/// @brief 配置された円のデータ
	struct Dot
	{
		float x;
		float y;
		sgc::Colorf color;
	};

	/// @brief カラーパレット
	static sgc::Colorf paletteColor(std::size_t index)
	{
		const float hue = static_cast<float>(index % 12) * 30.0f;
		return sgc::Colorf::fromHSV(hue, 0.7f, 0.9f);
	}

	/// @brief 円を配置するコマンドを実行する
	void placeDot(const sgc::Vec2f& pos)
	{
		const std::size_t colorIdx = m_colorIndex++;
		const Dot dot{pos.x, pos.y, paletteColor(colorIdx)};

		// キャプチャ用にポインタをコピー
		auto* dots = &m_dots;
		const Dot capturedDot = dot;

		m_history.execute(sgc::makeLambdaCommand(
			[dots, capturedDot]()
			{
				dots->push_back(capturedDot);
			},
			[dots]()
			{
				if (!dots->empty())
				{
					dots->pop_back();
				}
			}
		));
	}

	std::vector<Dot> m_dots;              ///< 配置済みの円リスト
	sgc::CommandHistory m_history;         ///< コマンド履歴
	std::size_t m_colorIndex{0};           ///< カラーパレットインデックス
};
