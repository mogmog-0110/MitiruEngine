#pragma once

/// @file SampleTooltipToast.hpp
/// @brief ツールチップとトースト通知のビジュアルデモ
///
/// sgc::ui::Tooltip と sgc::ui::Toast を使い、
/// マウスホバーによるツールチップ表示とキー操作によるトースト通知を
/// 視覚的に確認するサンプル。
/// - 上部: 3つのアクションボタンにマウスホバーでツールチップを表示
/// - 下部: SPACE/NUM1/NUM2でトースト通知を生成、Rで全クリア
/// - 最大3つのトーストをスタッキング表示

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/Anchor.hpp"
#include "sgc/ui/Button.hpp"
#include "sgc/ui/Toast.hpp"
#include "sgc/ui/Tooltip.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief ツールチップ＆トースト通知サンプルシーン
///
/// マウスホバーでボタンにツールチップを表示し、
/// キー入力でトースト通知を生成・管理する。
class SampleTooltipToast : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_hoveredWidget = -1;
		m_toasts.clear();
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

		updateWidgetHover(input);
		updateToastInput(input);
		advanceToasts(dt);
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
			"Tooltip & Toast Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 24.0f},
			sgc::Colorf{0.3f, 0.6f, 1.0f, 1.0f});

		// ウィジェットエリアの描画
		drawWidgets(r, tr);

		// ツールチップの描画
		drawTooltip(r, tr);

		// トーストの描画
		drawToasts(r, tr);

		// 操作ヒント
		tr->drawTextCentered(
			"[Space] Toast  [1] Item  [2] LevelUp  [R] Clear  [Esc] Back",
			12.0f,
			sgc::Vec2f{sw * 0.5f, sh - 14.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	// ── 定数 ──────────────────────────────────────────

	/// @brief ウィジェット数
	static constexpr int WIDGET_COUNT = 3;

	/// @brief ウィジェットの幅
	static constexpr float WIDGET_W = 140.0f;

	/// @brief ウィジェットの高さ
	static constexpr float WIDGET_H = 44.0f;

	/// @brief ウィジェット間の余白
	static constexpr float WIDGET_GAP = 30.0f;

	/// @brief ウィジェットのY位置
	static constexpr float WIDGET_Y = 100.0f;

	/// @brief ツールチップのサイズ
	static constexpr sgc::Vec2f TOOLTIP_SIZE{200.0f, 36.0f};

	/// @brief トーストのサイズ
	static constexpr sgc::Vec2f TOAST_SIZE{240.0f, 40.0f};

	/// @brief 最大同時トースト数
	static constexpr int MAX_TOASTS = 3;

	/// @brief ウィジェット定義
	struct WidgetDef
	{
		const char* label;       ///< ボタンラベル
		const char* tooltip;     ///< ツールチップテキスト
		sgc::Colorf color;       ///< ボタン色
	};

	/// @brief 3つのウィジェット定義テーブル
	static constexpr std::array<WidgetDef, WIDGET_COUNT> WIDGETS{{
		{"Attack",  "Deal 10 damage to enemy",  {0.8f, 0.25f, 0.25f, 0.9f}},
		{"Heal",    "Restore 5 HP",             {0.25f, 0.7f, 0.3f, 0.9f}},
		{"Shield",  "Block next attack",        {0.25f, 0.4f, 0.8f, 0.9f}},
	}};

	/// @brief アクティブなトースト情報
	struct ActiveToast
	{
		sgc::ui::ToastState state;  ///< トースト状態
		std::string message;        ///< 表示メッセージ
		sgc::Colorf color;          ///< 背景色
	};

	// ── メンバ変数 ────────────────────────────────────

	int m_hoveredWidget{-1};             ///< ホバー中のウィジェット（-1=なし）
	std::vector<ActiveToast> m_toasts;   ///< アクティブなトースト一覧

	// ── ウィジェット位置計算 ──────────────────────────

	/// @brief i番目のウィジェット矩形を返す
	/// @param i ウィジェットインデックス (0-2)
	/// @return ウィジェットの矩形
	[[nodiscard]] sgc::Rectf widgetBounds(int i) const noexcept
	{
		const float sw = getData().screenWidth;
		const float totalW = WIDGET_W * WIDGET_COUNT
			+ WIDGET_GAP * (WIDGET_COUNT - 1);
		const float startX = (sw - totalW) * 0.5f;
		const float x = startX + static_cast<float>(i) * (WIDGET_W + WIDGET_GAP);
		return sgc::Rectf{{x, WIDGET_Y}, {WIDGET_W, WIDGET_H}};
	}

	/// @brief 画面矩形を返す
	[[nodiscard]] sgc::Rectf screenBounds() const noexcept
	{
		return sgc::ui::screenRect(getData().screenWidth, getData().screenHeight);
	}

	// ── 更新ロジック ─────────────────────────────────

	/// @brief マウスホバーによるウィジェット選択を更新する
	/// @param input 入力プロバイダー
	void updateWidgetHover(const sgc::IInputProvider* input)
	{
		const auto mousePos = input->mousePosition();
		m_hoveredWidget = -1;

		for (int i = 0; i < WIDGET_COUNT; ++i)
		{
			if (sgc::ui::isMouseOver(mousePos, widgetBounds(i)))
			{
				m_hoveredWidget = i;
				break;
			}
		}
	}

	/// @brief トースト生成キー入力を処理する
	/// @param input 入力プロバイダー
	void updateToastInput(const sgc::IInputProvider* input)
	{
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			spawnToast("Action completed!", {0.2f, 0.5f, 0.8f, 0.9f});
		}
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			spawnToast("Item acquired!", {0.7f, 0.5f, 0.15f, 0.9f});
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			spawnToast("Level up!", {0.6f, 0.2f, 0.7f, 0.9f});
		}
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_toasts.clear();
		}
	}

	/// @brief トーストを生成する（最大数を超えたら最も古いものを除去）
	/// @param message 表示メッセージ
	/// @param color 背景色
	void spawnToast(const std::string& message, const sgc::Colorf& color)
	{
		// 最大数を超えたら先頭（最も古い）を除去
		while (static_cast<int>(m_toasts.size()) >= MAX_TOASTS)
		{
			m_toasts.erase(m_toasts.begin());
		}

		ActiveToast toast;
		toast.state = sgc::ui::ToastState{0.0f, 2.0f, 0.3f, 0.5f};
		toast.message = message;
		toast.color = color;
		m_toasts.push_back(toast);
	}

	/// @brief 全トーストの経過時間を進め、終了済みを除去する
	/// @param dt デルタタイム（秒）
	void advanceToasts(float dt)
	{
		for (auto& toast : m_toasts)
		{
			toast.state = sgc::ui::advanceToast(toast.state, dt);
		}

		// 終了済みトーストを除去
		m_toasts.erase(
			std::remove_if(m_toasts.begin(), m_toasts.end(),
				[](const ActiveToast& t)
				{
					return sgc::ui::evaluateToast(t.state).isFinished;
				}),
			m_toasts.end());
	}

	// ── 描画ロジック ─────────────────────────────────

	/// @brief 3つのウィジェットボタンを描画する
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	void drawWidgets(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		// セクションラベル
		tr->drawTextCentered(
			"Hover over buttons to see tooltips",
			14.0f,
			sgc::Vec2f{getData().screenWidth * 0.5f, WIDGET_Y - 24.0f},
			sgc::Colorf{0.6f, 0.6f, 0.65f, 1.0f});

		for (int i = 0; i < WIDGET_COUNT; ++i)
		{
			const auto bounds = widgetBounds(i);
			const auto& def = WIDGETS[static_cast<std::size_t>(i)];
			const bool hovered = (m_hoveredWidget == i);

			// ボタン背景色（ホバー時は明るく）
			sgc::Colorf bgColor = def.color;
			if (hovered)
			{
				bgColor = sgc::Colorf{
					bgColor.r + 0.15f,
					bgColor.g + 0.15f,
					bgColor.b + 0.15f,
					bgColor.a};
			}

			const sgc::AABB2f aabb{
				{bounds.x(), bounds.y()},
				{bounds.x() + bounds.width(), bounds.y() + bounds.height()}};
			r->drawRect(aabb, bgColor);
			r->drawRectFrame(aabb, 2.0f,
				sgc::Colorf{0.9f, 0.9f, 0.95f, hovered ? 1.0f : 0.5f});

			// ボタンラベル
			const sgc::Vec2f center{
				bounds.x() + bounds.width() * 0.5f,
				bounds.y() + bounds.height() * 0.5f};
			tr->drawTextCentered(
				def.label, 18.0f, center,
				sgc::Colorf{0.95f, 0.95f, 0.95f, 1.0f});
		}
	}

	/// @brief ホバー中ウィジェットのツールチップを描画する
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	void drawTooltip(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		if (m_hoveredWidget < 0 || m_hoveredWidget >= WIDGET_COUNT)
		{
			return;
		}

		const auto widget = widgetBounds(m_hoveredWidget);
		const auto& def = WIDGETS[static_cast<std::size_t>(m_hoveredWidget)];
		const auto screen = screenBounds();

		// ツールチップ配置を計算（優先: Below）
		const auto tip = sgc::ui::evaluateTooltip(
			widget, screen, TOOLTIP_SIZE,
			sgc::ui::TooltipSide::Below, 6.0f);

		// 暗い背景
		const sgc::AABB2f tipAABB{
			{tip.bounds.x(), tip.bounds.y()},
			{tip.bounds.x() + tip.bounds.width(),
			 tip.bounds.y() + tip.bounds.height()}};
		r->drawRect(tipAABB,
			sgc::Colorf{0.12f, 0.12f, 0.15f, 0.95f});
		r->drawRectFrame(tipAABB, 1.0f,
			sgc::Colorf{0.4f, 0.4f, 0.5f, 0.8f});

		// ツールチップテキスト
		const sgc::Vec2f tipCenter{
			tip.bounds.x() + tip.bounds.width() * 0.5f,
			tip.bounds.y() + tip.bounds.height() * 0.5f};
		tr->drawTextCentered(
			def.tooltip, 14.0f, tipCenter,
			sgc::Colorf{0.92f, 0.92f, 0.95f, 1.0f});
	}

	/// @brief アクティブなトーストを描画する
	/// @param r レンダラー
	/// @param tr テキストレンダラー
	void drawToasts(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		if (m_toasts.empty())
		{
			// トーストが無い場合のヒントテキスト
			tr->drawTextCentered(
				"Press SPACE / 1 / 2 to spawn toasts",
				14.0f,
				sgc::Vec2f{getData().screenWidth * 0.5f,
					getData().screenHeight * 0.6f},
				sgc::Colorf{0.5f, 0.5f, 0.55f, 0.7f});
			return;
		}

		const auto screen = screenBounds();
		const int count = static_cast<int>(m_toasts.size());

		for (int i = 0; i < count; ++i)
		{
			const auto& toast = m_toasts[static_cast<std::size_t>(i)];
			const auto result = sgc::ui::evaluateToast(toast.state);

			if (!result.isActive)
			{
				continue;
			}

			// スロット: 最新（末尾）がスロット0、古い方が上に積まれる
			const int slot = count - 1 - i;
			const auto pos = sgc::ui::toastPosition(
				screen, TOAST_SIZE, slot,
				sgc::ui::Anchor::BottomCenter, 10.0f);

			// 不透明度を反映した背景色
			const sgc::Colorf bg{
				toast.color.r, toast.color.g, toast.color.b,
				toast.color.a * result.opacity};
			const sgc::AABB2f toastAABB{
				{pos.x, pos.y},
				{pos.x + TOAST_SIZE.x, pos.y + TOAST_SIZE.y}};
			r->drawRect(toastAABB, bg);
			r->drawRectFrame(toastAABB, 1.0f,
				sgc::Colorf{0.9f, 0.9f, 0.95f, 0.6f * result.opacity});

			// テキスト
			const sgc::Vec2f textCenter{
				pos.x + TOAST_SIZE.x * 0.5f,
				pos.y + TOAST_SIZE.y * 0.5f};
			tr->drawTextCentered(
				toast.message, 16.0f, textCenter,
				sgc::Colorf{0.95f, 0.95f, 0.95f, result.opacity});
		}
	}
};
