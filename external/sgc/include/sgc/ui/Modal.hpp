#pragma once

/// @file Modal.hpp
/// @brief モーダルダイアログ評価ユーティリティ
///
/// 画面中央に表示されるモーダルダイアログの
/// レイアウト計算とボタンクリック判定を行う。
///
/// @code
/// using namespace sgc::ui;
/// ModalConfig config;
/// config.screenSize = {1280.0f, 720.0f};
/// config.dialogWidth = 400.0f;
/// config.dialogHeight = 200.0f;
///
/// auto result = evaluateModal(config, mousePos, mouseDown, mousePressed);
/// if (result.okClicked) { /* 確定処理 */ }
/// if (result.cancelClicked || result.overlayClicked) { /* キャンセル */ }
/// // result で各領域を描画
/// @endcode

#include "sgc/ui/Button.hpp"

namespace sgc::ui
{

/// @brief モーダルダイアログ設定
struct ModalConfig
{
	Vec2f screenSize{1280.0f, 720.0f};  ///< 画面サイズ
	float dialogWidth = 400.0f;          ///< ダイアログ幅
	float dialogHeight = 200.0f;         ///< ダイアログ高さ
	float titleHeight = 36.0f;           ///< タイトルバー高さ
	float buttonHeight = 36.0f;          ///< ボタン高さ
	float buttonWidth = 100.0f;          ///< ボタン幅
	float padding = 12.0f;               ///< 内側パディング
	bool hasCancel = true;               ///< キャンセルボタンの有無
	bool closeOnOverlay = true;          ///< オーバーレイクリックで閉じるか
};

/// @brief モーダルダイアログ評価結果
struct ModalResult
{
	Rectf overlayBounds{};      ///< 半透明オーバーレイ矩形（画面全体）
	Rectf dialogBounds{};       ///< ダイアログ本体矩形
	Rectf titleBounds{};        ///< タイトルバー矩形
	Rectf contentBounds{};      ///< コンテンツ領域矩形
	Rectf okButtonBounds{};     ///< OKボタン矩形
	Rectf cancelButtonBounds{}; ///< キャンセルボタン矩形
	WidgetState okState{WidgetState::Normal};     ///< OKボタン状態
	WidgetState cancelState{WidgetState::Normal}; ///< キャンセルボタン状態
	bool okClicked{false};      ///< OKボタンがクリックされたか
	bool cancelClicked{false};  ///< キャンセルボタンがクリックされたか
	bool overlayClicked{false}; ///< オーバーレイがクリックされたか
};

/// @brief モーダルダイアログの状態を評価する
///
/// 画面中央にダイアログを配置し、OKボタン（とオプションのキャンセルボタン）の
/// クリック判定を行う。オーバーレイクリックも検知する。
///
/// @param config ダイアログ設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタン押下中か
/// @param mousePressed このフレームでマウスボタンが押されたか
/// @return ダイアログの矩形・ボタン状態・クリック判定
[[nodiscard]] constexpr ModalResult evaluateModal(
	const ModalConfig& config,
	const Vec2f& mousePos, bool mouseDown, bool mousePressed) noexcept
{
	ModalResult result;

	// オーバーレイ（画面全体）
	result.overlayBounds = Rectf{0.0f, 0.0f, config.screenSize.x, config.screenSize.y};

	// ダイアログを画面中央に配置
	const float dx = (config.screenSize.x - config.dialogWidth) * 0.5f;
	const float dy = (config.screenSize.y - config.dialogHeight) * 0.5f;
	result.dialogBounds = Rectf{dx, dy, config.dialogWidth, config.dialogHeight};

	// タイトルバー
	result.titleBounds = Rectf{dx, dy, config.dialogWidth, config.titleHeight};

	// コンテンツ領域（タイトル下〜ボタン上）
	const float contentY = dy + config.titleHeight + config.padding;
	const float contentH = config.dialogHeight - config.titleHeight
	                        - config.buttonHeight - config.padding * 3.0f;
	result.contentBounds = Rectf{
		dx + config.padding, contentY,
		config.dialogWidth - config.padding * 2.0f,
		(contentH > 0.0f) ? contentH : 0.0f
	};

	// ボタン領域（ダイアログ下端）
	const float buttonY = dy + config.dialogHeight - config.buttonHeight - config.padding;

	if (config.hasCancel)
	{
		// 2ボタン: 中央揃え、間隔あり
		const float totalBtnW = config.buttonWidth * 2.0f + config.padding;
		const float btnStartX = dx + (config.dialogWidth - totalBtnW) * 0.5f;

		result.cancelButtonBounds = Rectf{btnStartX, buttonY, config.buttonWidth, config.buttonHeight};
		result.okButtonBounds = Rectf{
			btnStartX + config.buttonWidth + config.padding, buttonY,
			config.buttonWidth, config.buttonHeight
		};
	}
	else
	{
		// 1ボタン: 中央揃え
		const float btnX = dx + (config.dialogWidth - config.buttonWidth) * 0.5f;
		result.okButtonBounds = Rectf{btnX, buttonY, config.buttonWidth, config.buttonHeight};
	}

	// ボタン評価
	auto okBtn = evaluateButton(result.okButtonBounds, mousePos, mouseDown, mousePressed);
	result.okState = okBtn.state;
	result.okClicked = okBtn.clicked;

	if (config.hasCancel)
	{
		auto cancelBtn = evaluateButton(result.cancelButtonBounds, mousePos, mouseDown, mousePressed);
		result.cancelState = cancelBtn.state;
		result.cancelClicked = cancelBtn.clicked;
	}

	// オーバーレイクリック判定（ダイアログ外をクリック）
	const bool onDialog = isMouseOver(mousePos, result.dialogBounds);
	result.overlayClicked = config.closeOnOverlay && mousePressed && !onDialog;

	return result;
}

} // namespace sgc::ui
