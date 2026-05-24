#pragma once

/// @file SampleDialogUI.hpp
/// @brief ダイアログ/モーダルパターンのデモシーン
///
/// 画面中央のボタンでモーダルダイアログを開閉する。
/// - ダイアログ表示中: 背景を半透明黒でディミング
/// - OK/Cancelボタンで結果を反映し閉じる
/// - ESC: ダイアログ開 → 閉じる（Cancel扱い）、閉 → メニューに戻る

#include <array>

#include "sgc/core/Hash.hpp"
#include "sgc/ui/Button.hpp"
#include "sgc/ui/Panel.hpp"
#include "sgc/ui/StackLayout.hpp"
#include "sgc/ui/WidgetState.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief ダイアログ/モーダルパターンのデモシーン
///
/// 「Show Dialog」ボタンでモーダルを表示し、OK/Cancelで結果を確認する。
/// ディミングオーバーレイ、パネル、hstackボタン配置を組み合わせたUI構成。
class SampleDialogUI : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_dialogOpen = false;
		m_statusText = "Click the button to open a dialog";
		m_btnStates = {};
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto& input = *getData().inputProvider;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		const auto mousePos = input.mousePosition();
		const bool mouseDown = input.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		if (m_dialogOpen)
		{
			// ── ダイアログ表示中 ──

			// ESCでダイアログを閉じる（Cancel扱い）
			if (input.isKeyJustPressed(KeyCode::ESCAPE))
			{
				m_statusText = "Action Cancelled";
				m_dialogOpen = false;
				return;
			}

			// OKボタン
			const auto okRect = okButtonRect(sw, sh);
			m_btnStates[BTN_OK] = sgc::ui::evaluateButton(
				okRect, mousePos, mouseDown, mousePressed);
			if (m_btnStates[BTN_OK].clicked)
			{
				m_statusText = "Action Confirmed";
				m_dialogOpen = false;
				return;
			}

			// Cancelボタン
			const auto cancelRect = cancelButtonRect(sw, sh);
			m_btnStates[BTN_CANCEL] = sgc::ui::evaluateButton(
				cancelRect, mousePos, mouseDown, mousePressed);
			if (m_btnStates[BTN_CANCEL].clicked)
			{
				m_statusText = "Action Cancelled";
				m_dialogOpen = false;
				return;
			}
		}
		else
		{
			// ── ダイアログ非表示 ──

			// ESCでメニューに戻る
			if (input.isKeyJustPressed(KeyCode::ESCAPE))
			{
				getSceneManager().changeScene("menu"_hash, 0.3f);
				return;
			}

			// 「Show Dialog」トリガーボタン
			const auto triggerRect = triggerButtonRect(sw, sh);
			m_btnStates[BTN_TRIGGER] = sgc::ui::evaluateButton(
				triggerRect, mousePos, mouseDown, mousePressed);
			if (m_btnStates[BTN_TRIGGER].clicked)
			{
				m_dialogOpen = true;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.08f, 0.08f, 0.10f});

		// ── タイトル ──
		t->drawTextCentered(
			"Dialog UI Demo", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── ステータステキスト ──
		t->drawTextCentered(
			m_statusText, 18.0f,
			sgc::Vec2f{sw * 0.5f, sh * 0.35f}, sgc::Colorf{0.7f, 0.8f, 0.9f});

		// ── トリガーボタン（常に描画） ──
		drawStyledButton(r, t, triggerButtonRect(sw, sh), "Show Dialog",
			m_btnStates[BTN_TRIGGER].state,
			sgc::Colorf{0.2f, 0.4f, 0.7f},
			sgc::Colorf{0.3f, 0.5f, 0.85f},
			sgc::Colorf{0.15f, 0.3f, 0.6f});

		// ── ダイアログ（モーダル表示中のみ） ──
		if (m_dialogOpen)
		{
			drawDialog(r, t, sw, sh);
		}

		// ── 操作ヒント ──
		t->drawText(
			"[Esc] Back to Menu / Close Dialog", 14.0f,
			sgc::Vec2f{20.0f, sh - 30.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});
	}

private:
	// ── ボタンインデックス ──
	static constexpr int BTN_TRIGGER = 0;  ///< トリガーボタン
	static constexpr int BTN_OK = 1;       ///< OKボタン
	static constexpr int BTN_CANCEL = 2;   ///< Cancelボタン

	// ── ダイアログレイアウト定数 ──
	static constexpr float DIALOG_W = 400.0f;      ///< ダイアログ幅
	static constexpr float DIALOG_H = 200.0f;      ///< ダイアログ高さ
	static constexpr float DIALOG_TITLE_H = 40.0f; ///< ダイアログタイトル高さ
	static constexpr float BTN_W = 120.0f;          ///< ダイアログ内ボタン幅
	static constexpr float BTN_H = 38.0f;           ///< ダイアログ内ボタン高さ
	static constexpr float BTN_SPACING = 20.0f;     ///< ボタン間隔
	static constexpr float TRIGGER_W = 200.0f;      ///< トリガーボタン幅
	static constexpr float TRIGGER_H = 48.0f;       ///< トリガーボタン高さ

	// ── メンバ変数 ──
	bool m_dialogOpen{false};                        ///< ダイアログ表示中か
	const char* m_statusText{"Click the button to open a dialog"};  ///< ステータス文字列
	std::array<sgc::ui::ButtonResult, 3> m_btnStates{}; ///< 各ボタン評価結果

	// ── レイアウト計算 ──

	/// @brief トリガーボタン矩形（画面中央）
	[[nodiscard]] static sgc::Rectf triggerButtonRect(float sw, float sh) noexcept
	{
		return sgc::Rectf::fromCenter(
			sgc::Vec2f{sw * 0.5f, sh * 0.5f},
			sgc::Vec2f{TRIGGER_W, TRIGGER_H});
	}

	/// @brief ダイアログ外枠矩形（画面中央）
	[[nodiscard]] static sgc::Rectf dialogRect(float sw, float sh) noexcept
	{
		return sgc::Rectf::fromCenter(
			sgc::Vec2f{sw * 0.5f, sh * 0.5f},
			sgc::Vec2f{DIALOG_W, DIALOG_H});
	}

	/// @brief ダイアログ内ボタン領域（下部中央に横並び）
	[[nodiscard]] static sgc::Rectf buttonAreaRect(float sw, float sh) noexcept
	{
		const auto dlg = dialogRect(sw, sh);
		const float totalW = BTN_W * 2.0f + BTN_SPACING;
		const float areaX = dlg.x() + (dlg.width() - totalW) * 0.5f;
		const float areaY = dlg.bottom() - BTN_H - 20.0f;
		return {areaX, areaY, totalW, BTN_H};
	}

	/// @brief OKボタン矩形
	[[nodiscard]] static sgc::Rectf okButtonRect(float sw, float sh) noexcept
	{
		const auto area = buttonAreaRect(sw, sh);
		const auto buttons = sgc::ui::hstackFixed(area, 2, BTN_SPACING);
		return buttons[0];
	}

	/// @brief Cancelボタン矩形
	[[nodiscard]] static sgc::Rectf cancelButtonRect(float sw, float sh) noexcept
	{
		const auto area = buttonAreaRect(sw, sh);
		const auto buttons = sgc::ui::hstackFixed(area, 2, BTN_SPACING);
		return buttons[1];
	}

	// ── 描画ヘルパー ──

	/// @brief ダイアログ全体を描画する
	/// @param r レンダラー
	/// @param t テキストレンダラー
	/// @param sw 画面幅
	/// @param sh 画面高さ
	void drawDialog(sgc::IRenderer* r, sgc::ITextRenderer* t,
		float sw, float sh) const
	{
		// ── ディミングオーバーレイ ──
		r->drawFadeOverlay(0.5f, sgc::Colorf::black());

		// ── ダイアログパネル ──
		const auto dlg = dialogRect(sw, sh);
		const auto panel = sgc::ui::evaluatePanel(
			dlg, DIALOG_TITLE_H, sgc::ui::Padding::uniform(16.0f));

		// ダイアログ背景
		const sgc::AABB2f dlgBg{
			{dlg.x(), dlg.y()}, {dlg.right(), dlg.bottom()}};
		r->drawRect(dlgBg, sgc::Colorf{0.14f, 0.14f, 0.18f, 0.98f});
		r->drawRectFrame(dlgBg, 2.0f, sgc::Colorf{0.3f, 0.5f, 0.8f});

		// タイトルバー
		const sgc::AABB2f titleBg{
			{panel.titleBounds.x(), panel.titleBounds.y()},
			{panel.titleBounds.right(), panel.titleBounds.bottom()}};
		r->drawRect(titleBg, sgc::Colorf{0.18f, 0.25f, 0.4f});
		t->drawTextCentered(
			"Confirm Action", 20.0f,
			panel.titleBounds.center(), sgc::Colorf{0.95f, 0.95f, 1.0f});

		// 本文テキスト
		t->drawTextCentered(
			"Are you sure?", 16.0f,
			sgc::Vec2f{dlg.x() + dlg.width() * 0.5f,
			           dlg.y() + DIALOG_TITLE_H + 40.0f},
			sgc::Colorf{0.8f, 0.8f, 0.85f});

		// ── ボタン描画 ──
		// OKボタン（緑アクセント）
		drawStyledButton(r, t, okButtonRect(sw, sh), "OK",
			m_btnStates[BTN_OK].state,
			sgc::Colorf{0.15f, 0.4f, 0.2f},
			sgc::Colorf{0.2f, 0.55f, 0.3f},
			sgc::Colorf{0.1f, 0.3f, 0.15f});

		// Cancelボタン（赤アクセント）
		drawStyledButton(r, t, cancelButtonRect(sw, sh), "Cancel",
			m_btnStates[BTN_CANCEL].state,
			sgc::Colorf{0.5f, 0.15f, 0.15f},
			sgc::Colorf{0.65f, 0.2f, 0.2f},
			sgc::Colorf{0.4f, 0.1f, 0.1f});
	}

	/// @brief 色付きボタンを描画する
	/// @param r レンダラー
	/// @param t テキストレンダラー
	/// @param rect ボタン矩形
	/// @param label ラベル文字列
	/// @param state ウィジェット状態
	/// @param normal 通常色
	/// @param hovered ホバー色
	/// @param pressed 押下色
	static void drawStyledButton(
		sgc::IRenderer* r, sgc::ITextRenderer* t,
		const sgc::Rectf& rect, const char* label,
		sgc::ui::WidgetState state,
		const sgc::Colorf& normal,
		const sgc::Colorf& hovered,
		const sgc::Colorf& pressed)
	{
		// 状態に応じた背景色を選択
		sgc::Colorf bgColor = normal;
		switch (state)
		{
		case sgc::ui::WidgetState::Hovered:
			bgColor = hovered;
			break;
		case sgc::ui::WidgetState::Pressed:
			bgColor = pressed;
			break;
		default:
			break;
		}

		const sgc::AABB2f btnBg{
			{rect.x(), rect.y()}, {rect.right(), rect.bottom()}};
		r->drawRect(btnBg, bgColor);
		r->drawRectFrame(btnBg, 1.0f,
			sgc::Colorf{bgColor.r + 0.15f, bgColor.g + 0.15f, bgColor.b + 0.15f});

		t->drawTextCentered(
			label, 18.0f,
			rect.center(), sgc::Colorf{0.95f, 0.95f, 0.95f});
	}
};
