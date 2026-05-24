#pragma once

/// @file SampleTextLayout.hpp
/// @brief TextLayoutによるテキスト自動サイズ計算のデモ
///
/// sgc::ui::TextLayout の各種関数を使い、テキスト内容から
/// ウィジェットサイズを自動決定する方法を視覚的に確認するサンプル。
/// - セクション1: buttonSizeFromText でボタンを自動サイズ
/// - セクション2: labelBounds でアンカー配置ラベル
/// - セクション3: fitTextInRect でフォントサイズ自動調整
/// - NUM1/NUM2: フィッティング用テキスト切替
/// - Rキー: リセット

#include <array>
#include <string>
#include <string_view>

#include "sgc/core/Hash.hpp"
#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Anchor.hpp"
#include "sgc/ui/TextLayout.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief TextLayoutサンプルシーン
///
/// テキスト内容からUI要素のサイズを自動計算する
/// TextLayout APIのビジュアルデモ。
class SampleTextLayout : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_fitTextIndex = 0;
	}

	/// @brief 更新処理
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// フィッティング用テキスト切替
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_fitTextIndex = 0;
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_fitTextIndex = 1;
		}

		// リセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_fitTextIndex = 0;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.10f, 1.0f});

		// タイトル
		tr->drawTextCentered(
			"TextLayout - Auto-Sizing Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 20.0f},
			sgc::Colorf{0.3f, 0.6f, 1.0f, 1.0f});

		// SharedDataのtextMeasureを優先、なければ近似を使用
		const ApproxTextMeasure approx;
		const sgc::ITextMeasure* measurePtr =
			getData().textMeasure ? getData().textMeasure : static_cast<const sgc::ITextMeasure*>(&approx);
		const sgc::ITextMeasure& measure = *measurePtr;

		drawButtonSection(r, tr, measure, sw);
		drawLabelSection(r, tr, measure, sw);
		drawFitSection(r, tr, measure, sw);

		// 操作ヒント
		tr->drawText(
			"[1] Short text  [2] Long text  [R] Reset  [Esc] Back",
			12.0f,
			sgc::Vec2f{10.0f, sh - 18.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	/// @brief 等幅近似によるテキストサイズ推定
	///
	/// SharedDataにtextMeasureがないため、
	/// 文字数とフォントサイズからサイズを近似する。
	struct ApproxTextMeasure : sgc::ITextMeasure
	{
		/// @brief テキストの描画サイズを推定する
		/// @param text テキスト文字列
		/// @param fontSize フォントサイズ
		/// @return 推定サイズ（幅 = 文字数 * fontSize * 0.6）
		[[nodiscard]] sgc::Vec2f measure(
			std::string_view text, float fontSize) const override
		{
			const float w = static_cast<float>(text.size()) * fontSize * 0.6f;
			return {w, fontSize};
		}

		/// @brief 行の高さを返す
		/// @param fontSize フォントサイズ
		/// @return 行の高さ（fontSize * 1.2）
		[[nodiscard]] float lineHeight(float fontSize) const override
		{
			return fontSize * 1.2f;
		}
	};

	/// @brief ボタン定義
	struct ButtonDef
	{
		const char* text;   ///< ボタンテキスト
		sgc::Colorf color;  ///< ボタン色
	};

	/// @brief ボタンセクションのボタン定義テーブル
	static constexpr std::array<ButtonDef, 3> BUTTONS{{
		{"OK",                    {0.2f, 0.6f, 0.3f, 0.85f}},
		{"Cancel Operation",     {0.6f, 0.3f, 0.2f, 0.85f}},
		{"Save and Continue",    {0.2f, 0.4f, 0.7f, 0.85f}},
	}};

	/// @brief ラベル定義
	struct LabelDef
	{
		const char* text;       ///< ラベルテキスト
		sgc::ui::Anchor anchor; ///< アンカー位置
		sgc::Colorf color;      ///< 背景色
	};

	/// @brief ラベルセクションのラベル定義テーブル
	static constexpr std::array<LabelDef, 3> LABELS{{
		{"TopLeft Label",     sgc::ui::Anchor::TopLeft,     {0.7f, 0.3f, 0.3f, 0.7f}},
		{"Center Label",      sgc::ui::Anchor::Center,      {0.3f, 0.7f, 0.3f, 0.7f}},
		{"BottomRight Label", sgc::ui::Anchor::BottomRight,  {0.3f, 0.3f, 0.7f, 0.7f}},
	}};

	/// @brief フィッティング用テキスト候補
	static constexpr std::array<const char*, 2> FIT_TEXTS{{
		"Hi",
		"Hello World Wide Web!",
	}};

	/// @brief セクション上端Y座標
	static constexpr float SECTION1_Y = 60.0f;
	static constexpr float SECTION2_Y = 230.0f;
	static constexpr float SECTION3_Y = 400.0f;

	/// @brief ボタンのフォントサイズ
	static constexpr float BUTTON_FONT = 20.0f;

	/// @brief ボタンのパディング
	static constexpr float BUTTON_H_PAD = 16.0f;
	static constexpr float BUTTON_V_PAD = 10.0f;

	/// @brief ラベルのフォントサイズ
	static constexpr float LABEL_FONT = 18.0f;

	/// @brief フィッティング矩形のサイズ
	static constexpr float FIT_RECT_W = 300.0f;
	static constexpr float FIT_RECT_H = 60.0f;

	/// @brief Main.cppで登録済みのフォントサイズ一覧
	static constexpr std::array<float, 14> REGISTERED_FONTS{{
		9.0f, 10.0f, 11.0f, 12.0f, 14.0f, 16.0f, 18.0f,
		20.0f, 22.0f, 24.0f, 28.0f, 32.0f, 36.0f, 48.0f
	}};

	/// @brief 登録済みフォントサイズのうち、指定値以下で最大のものを返す
	/// @param rawSize 生のフォントサイズ（小数を含む場合がある）
	/// @return 登録済みフォントサイズ（最小は9.0f）
	[[nodiscard]] static constexpr float snapToRegisteredFont(float rawSize) noexcept
	{
		float best = REGISTERED_FONTS[0];
		for (const float f : REGISTERED_FONTS)
		{
			if (f <= rawSize)
			{
				best = f;
			}
		}
		return best;
	}

	/// @brief 現在のフィッティング用テキストインデックス
	int m_fitTextIndex{0};

	/// @brief セクション1: 自動サイズボタンの描画
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	/// @param measure テキスト計測
	/// @param sw 画面幅
	void drawButtonSection(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		const sgc::ITextMeasure& measure, float sw) const
	{
		// セクションタイトル
		tr->drawText(
			"Section 1: Auto-sized Buttons", 16.0f,
			sgc::Vec2f{20.0f, SECTION1_Y},
			sgc::Colorf{0.8f, 0.8f, 0.4f, 1.0f});

		float curX = 40.0f;
		const float btnY = SECTION1_Y + 30.0f;

		for (const auto& btn : BUTTONS)
		{
			// buttonSizeFromText でサイズ自動計算
			const sgc::Vec2f btnSize = sgc::ui::buttonSizeFromText(
				measure, btn.text, BUTTON_FONT, BUTTON_H_PAD, BUTTON_V_PAD);

			// ボタン背景
			const sgc::AABB2f btnAABB{
				{curX, btnY},
				{curX + btnSize.x, btnY + btnSize.y}};
			r->drawRect(btnAABB, btn.color);
			r->drawRectFrame(btnAABB, 1.5f,
				sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f});

			// ボタンテキスト（中央配置）
			const sgc::Vec2f textCenter{
				curX + btnSize.x * 0.5f,
				btnY + btnSize.y * 0.5f};
			tr->drawTextCentered(
				btn.text, BUTTON_FONT, textCenter,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

			// サイズ表示（ボタン下部）
			const std::string sizeInfo =
				std::to_string(static_cast<int>(btnSize.x)) + "x"
				+ std::to_string(static_cast<int>(btnSize.y));
			tr->drawTextCentered(
				sizeInfo, 11.0f,
				sgc::Vec2f{curX + btnSize.x * 0.5f, btnY + btnSize.y + 14.0f},
				sgc::Colorf{0.6f, 0.6f, 0.65f, 1.0f});

			curX += btnSize.x + 30.0f;
		}

		// 説明テキスト
		tr->drawText(
			"buttonSizeFromText() computes width/height from text content + padding",
			12.0f,
			sgc::Vec2f{40.0f, btnY + 70.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

	/// @brief セクション2: アンカー配置ラベルの描画
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	/// @param measure テキスト計測
	/// @param sw 画面幅
	void drawLabelSection(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		const sgc::ITextMeasure& measure, float sw) const
	{
		// セクションタイトル
		tr->drawText(
			"Section 2: Centered Labels (Anchor)", 16.0f,
			sgc::Vec2f{20.0f, SECTION2_Y},
			sgc::Colorf{0.8f, 0.8f, 0.4f, 1.0f});

		// ラベル配置用のデモ領域
		const float areaX = 40.0f;
		const float areaY = SECTION2_Y + 30.0f;
		const float areaW = sw - 80.0f;
		const float areaH = 120.0f;

		// デモ領域の枠
		const sgc::AABB2f areaAABB{
			{areaX, areaY},
			{areaX + areaW, areaY + areaH}};
		r->drawRectFrame(areaAABB, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.4f, 0.6f});

		// アンカー基準位置の計算
		const std::array<sgc::Vec2f, 3> anchorPositions{{
			{areaX + 10.0f, areaY + 10.0f},                    // TopLeft
			{areaX + areaW * 0.5f, areaY + areaH * 0.5f},     // Center
			{areaX + areaW - 10.0f, areaY + areaH - 10.0f},   // BottomRight
		}};

		const sgc::ui::Margin labelPad = sgc::ui::Margin::symmetric(10.0f, 6.0f);

		for (std::size_t i = 0; i < LABELS.size(); ++i)
		{
			const auto& lbl = LABELS[i];
			const sgc::Vec2f& pos = anchorPositions[i];

			// labelBounds でアンカー基準の矩形を計算
			const sgc::Rectf bounds = sgc::ui::labelBounds(
				measure, lbl.text, LABEL_FONT,
				pos, lbl.anchor, labelPad);

			// 背景描画
			const sgc::AABB2f lblAABB{
				{bounds.x(), bounds.y()},
				{bounds.x() + bounds.width(), bounds.y() + bounds.height()}};
			r->drawRect(lblAABB, lbl.color);
			r->drawRectFrame(lblAABB, 1.0f,
				sgc::Colorf{0.9f, 0.9f, 0.9f, 0.8f});

			// テキスト描画（矩形中央）
			const sgc::Vec2f textCenter{
				bounds.x() + bounds.width() * 0.5f,
				bounds.y() + bounds.height() * 0.5f};
			tr->drawTextCentered(
				lbl.text, LABEL_FONT, textCenter,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

			// アンカーポイントにドットを描画
			r->drawCircle(pos, 4.0f,
				sgc::Colorf{1.0f, 0.7f, 0.2f, 0.9f});
		}
	}

	/// @brief セクション3: フォントサイズフィッティングの描画
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	/// @param measure テキスト計測
	/// @param sw 画面幅
	void drawFitSection(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		const sgc::ITextMeasure& measure, float sw) const
	{
		// セクションタイトル
		tr->drawText(
			"Section 3: Font Size Fitting", 16.0f,
			sgc::Vec2f{20.0f, SECTION3_Y},
			sgc::Colorf{0.8f, 0.8f, 0.4f, 1.0f});

		const float rectX = (sw - FIT_RECT_W) * 0.5f;
		const float rectY = SECTION3_Y + 35.0f;
		const sgc::Rectf targetRect{
			{rectX, rectY}, {FIT_RECT_W, FIT_RECT_H}};

		// ターゲット矩形の描画
		const sgc::AABB2f targetAABB{
			{rectX, rectY},
			{rectX + FIT_RECT_W, rectY + FIT_RECT_H}};
		r->drawRectFrame(targetAABB, 2.0f,
			sgc::Colorf{0.8f, 0.4f, 0.2f, 0.8f});

		// 背景（薄い塗りつぶし）
		r->drawRect(targetAABB,
			sgc::Colorf{0.15f, 0.12f, 0.1f, 0.5f});

		// fitTextInRect でフォントサイズを自動計算
		const std::string_view fitText = FIT_TEXTS[static_cast<std::size_t>(m_fitTextIndex)];
		const float fittedRaw = sgc::ui::fitTextInRect(
			measure, fitText, 48.0f, targetRect);

		// 登録済みフォントサイズに丸める（Siv3DTextRendererは整数サイズのみ対応）
		const float fittedSize = snapToRegisteredFont(fittedRaw);

		// フィッティングされたテキストを矩形の中央に描画
		const sgc::Vec2f rectCenter{
			rectX + FIT_RECT_W * 0.5f,
			rectY + FIT_RECT_H * 0.5f};
		tr->drawTextCentered(
			std::string{fitText}, fittedSize, rectCenter,
			sgc::Colorf{1.0f, 0.9f, 0.7f, 1.0f});

		// フォントサイズ情報の表示
		const std::string fontInfo =
			"Fitted font size: "
			+ std::to_string(static_cast<int>(fittedSize))
			+ "px  |  Text: \"" + std::string{fitText} + "\"";
		tr->drawTextCentered(
			fontInfo, 14.0f,
			sgc::Vec2f{sw * 0.5f, rectY + FIT_RECT_H + 20.0f},
			sgc::Colorf{0.6f, 0.7f, 0.9f, 1.0f});

		// 矩形サイズ情報
		const std::string rectInfo =
			"Target rect: "
			+ std::to_string(static_cast<int>(FIT_RECT_W)) + "x"
			+ std::to_string(static_cast<int>(FIT_RECT_H))
			+ "  |  Max font: 48px";
		tr->drawTextCentered(
			rectInfo, 12.0f,
			sgc::Vec2f{sw * 0.5f, rectY + FIT_RECT_H + 40.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}
};
