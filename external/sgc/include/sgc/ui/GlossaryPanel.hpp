#pragma once

/// @file GlossaryPanel.hpp
/// @brief 用語辞典パネル評価ユーティリティ
///
/// ページ切り替え可能な用語辞典パネルの配置と操作を計算する。
/// タイトル、説明文、図表エリア、ページ送りボタンを含む。
///
/// @code
/// using namespace sgc::ui;
/// GlossaryState glossary;
/// glossary.currentPage = 0;
/// glossary.totalPages = 5;
///
/// auto result = evaluateGlossary(glossary, panelBounds, mousePos, mouseDown, mousePressed);
/// glossary = result.newState;
/// // result.titleBounds にタイトルを描画
/// // result.bodyBounds に説明文を描画
/// // result.diagramBounds に図表を描画
/// @endcode

#include "sgc/ui/WidgetState.hpp"
#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief 用語辞典パネルの状態（呼び出し側で保持する）
struct GlossaryState
{
	int currentPage{0};   ///< 現在のページ番号（0始まり）
	int totalPages{1};    ///< 総ページ数
	bool isOpen{false};   ///< パネルが開いているか
};

/// @brief 用語辞典パネルの評価結果
struct GlossaryResult
{
	GlossaryState newState{};       ///< 更新後の状態
	Rectf outerBounds{};            ///< パネル全体の矩形
	Rectf titleBounds{};            ///< タイトル表示領域
	Rectf bodyBounds{};             ///< 本文表示領域
	Rectf diagramBounds{};          ///< 図表表示領域
	Rectf prevButtonBounds{};       ///< 前ページボタン矩形
	Rectf nextButtonBounds{};       ///< 次ページボタン矩形
	Rectf closeButtonBounds{};      ///< 閉じるボタン矩形
	Rectf pageIndicatorBounds{};    ///< ページ番号表示領域
	bool prevClicked{false};        ///< 前ページが押されたか
	bool nextClicked{false};        ///< 次ページが押されたか
	bool closeClicked{false};       ///< 閉じるが押されたか
	bool prevHovered{false};        ///< 前ページがホバーされているか
	bool nextHovered{false};        ///< 次ページがホバーされているか
	bool closeHovered{false};       ///< 閉じるがホバーされているか
};

/// @brief 用語辞典パネルのデフォルトレイアウト定数
inline constexpr float GLOSSARY_TITLE_HEIGHT = 40.0f;
inline constexpr float GLOSSARY_NAV_HEIGHT = 36.0f;
inline constexpr float GLOSSARY_BUTTON_WIDTH = 60.0f;
inline constexpr float GLOSSARY_CLOSE_SIZE = 28.0f;
inline constexpr float GLOSSARY_PADDING = 12.0f;
inline constexpr float GLOSSARY_DIAGRAM_RATIO = 0.4f;

/// @brief 用語辞典パネルの状態を評価する
///
/// パネル全体の矩形からタイトル、本文、図表、ナビゲーションの
/// 各領域を計算し、ボタンクリックとページ遷移を処理する。
///
/// レイアウト構成（上から下）:
/// - タイトルバー（閉じるボタン付き）
/// - 図表エリア（全高の40%）
/// - 本文エリア（残りの高さ）
/// - ナビゲーションバー（←  1/5  →）
///
/// @param state 現在の状態
/// @param panelBounds パネル全体の矩形
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param diagramRatio 図表エリアの高さ比率（デフォルト: 0.4）
/// @return 評価結果
[[nodiscard]] constexpr GlossaryResult evaluateGlossary(
	const GlossaryState& state,
	const Rectf& panelBounds,
	const Vec2f& mousePos,
	bool mouseDown,
	bool mousePressed,
	float diagramRatio = GLOSSARY_DIAGRAM_RATIO) noexcept
{
	(void)mouseDown;  // 現在未使用

	GlossaryResult result;
	GlossaryState newState = state;

	result.outerBounds = panelBounds;

	const float pad = GLOSSARY_PADDING;
	const float innerX = panelBounds.x() + pad;
	const float innerW = panelBounds.width() - pad * 2.0f;

	// --- タイトルバー ---
	result.titleBounds = Rectf{
		innerX, panelBounds.y() + pad,
		innerW - GLOSSARY_CLOSE_SIZE - pad, GLOSSARY_TITLE_HEIGHT
	};

	// --- 閉じるボタン（タイトルバー右端） ---
	result.closeButtonBounds = Rectf{
		panelBounds.x() + panelBounds.width() - GLOSSARY_CLOSE_SIZE - pad,
		panelBounds.y() + pad + (GLOSSARY_TITLE_HEIGHT - GLOSSARY_CLOSE_SIZE) * 0.5f,
		GLOSSARY_CLOSE_SIZE, GLOSSARY_CLOSE_SIZE
	};

	// --- ナビゲーションバー（最下部） ---
	const float navY = panelBounds.y() + panelBounds.height() - GLOSSARY_NAV_HEIGHT - pad;

	result.prevButtonBounds = Rectf{
		innerX, navY, GLOSSARY_BUTTON_WIDTH, GLOSSARY_NAV_HEIGHT
	};
	result.nextButtonBounds = Rectf{
		innerX + innerW - GLOSSARY_BUTTON_WIDTH, navY,
		GLOSSARY_BUTTON_WIDTH, GLOSSARY_NAV_HEIGHT
	};
	result.pageIndicatorBounds = Rectf{
		innerX + GLOSSARY_BUTTON_WIDTH + pad, navY,
		innerW - GLOSSARY_BUTTON_WIDTH * 2.0f - pad * 2.0f, GLOSSARY_NAV_HEIGHT
	};

	// --- コンテンツ領域（タイトルとナビの間） ---
	const float contentY = panelBounds.y() + pad + GLOSSARY_TITLE_HEIGHT + pad;
	const float contentH = navY - contentY - pad;

	if (contentH > 0.0f)
	{
		// 図表エリア（上部、比率で分割）
		const float diagramH = contentH * diagramRatio;
		result.diagramBounds = Rectf{
			innerX, contentY, innerW, diagramH
		};

		// 本文エリア（下部）
		result.bodyBounds = Rectf{
			innerX, contentY + diagramH + pad * 0.5f,
			innerW, contentH - diagramH - pad * 0.5f
		};
	}

	// --- ボタンクリック判定 ---
	result.closeHovered = isMouseOver(mousePos, result.closeButtonBounds);
	result.prevHovered = isMouseOver(mousePos, result.prevButtonBounds);
	result.nextHovered = isMouseOver(mousePos, result.nextButtonBounds);

	if (mousePressed)
	{
		if (result.closeHovered)
		{
			result.closeClicked = true;
			newState.isOpen = false;
		}
		else if (result.prevHovered && state.currentPage > 0)
		{
			result.prevClicked = true;
			newState.currentPage = state.currentPage - 1;
		}
		else if (result.nextHovered && state.currentPage < state.totalPages - 1)
		{
			result.nextClicked = true;
			newState.currentPage = state.currentPage + 1;
		}
	}

	result.newState = newState;
	return result;
}

/// @brief 用語辞典パネルを画面中央に配置する矩形を計算する
///
/// @param screenSize 画面サイズ
/// @param panelWidth パネル幅（デフォルト: 画面幅の60%）
/// @param panelHeight パネル高さ（デフォルト: 画面高さの70%）
/// @return パネル矩形
[[nodiscard]] constexpr Rectf glossaryCenterBounds(
	const Vec2f& screenSize,
	float widthRatio = 0.6f,
	float heightRatio = 0.7f) noexcept
{
	const float w = screenSize.x * widthRatio;
	const float h = screenSize.y * heightRatio;
	return Rectf{
		(screenSize.x - w) * 0.5f,
		(screenSize.y - h) * 0.5f,
		w, h
	};
}

} // namespace sgc::ui
