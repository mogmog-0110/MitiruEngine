#pragma once

/// @file DraggableWindow.hpp
/// @brief ドラッグ可能なフローティングウィンドウ評価ユーティリティ
///
/// ドラッグ操作による移動、リサイズ、最小化、閉じるボタンを持つ
/// フローティングウィンドウの状態評価を行う。
///
/// @code
/// using namespace sgc::ui;
/// // 初期状態
/// DragWindowState winState;
/// winState.position = {100.0f, 100.0f};
/// winState.size = {300.0f, 400.0f};
///
/// // 毎フレーム:
/// auto result = evaluateDragWindow(winState, mousePos, mouseDown, mousePressed,
///                                  screenBounds, 30.0f);
/// winState = result.newState;  // 状態を更新
/// if (result.closeClicked) { /* ウィンドウを閉じる */ }
/// // result.titleBounds, result.contentBounds で描画
/// @endcode

#include "sgc/ui/WidgetState.hpp"
#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief ウィンドウのデフォルト最小サイズ
inline constexpr Vec2f DRAG_WINDOW_MIN_SIZE{150.0f, 60.0f};

/// @brief リサイズハンドルのサイズ
inline constexpr float RESIZE_HANDLE_SIZE = 16.0f;

/// @brief ウィンドウボタンのサイズ（正方形）
inline constexpr float WINDOW_BUTTON_SIZE = 20.0f;

/// @brief ドラッグウィンドウの状態（呼び出し側で保持する）
struct DragWindowState
{
	Vec2f position{100.0f, 100.0f};  ///< ウィンドウ左上座標
	Vec2f size{300.0f, 400.0f};      ///< ウィンドウサイズ
	bool isDragging{false};          ///< タイトルバーでドラッグ中か
	bool isResizing{false};          ///< リサイズ中か
	Vec2f dragOffset{};              ///< ドラッグ開始時のオフセット
	bool minimized{false};           ///< 最小化状態か
	Vec2f minSize{DRAG_WINDOW_MIN_SIZE}; ///< カスタム最小サイズ（ウィンドウ毎にオーバーライド可能）
};

/// @brief ドラッグウィンドウの評価結果
struct DragWindowResult
{
	DragWindowState newState{};      ///< 更新後の状態
	Rectf outerBounds{};             ///< ウィンドウ全体の矩形
	Rectf titleBounds{};             ///< タイトルバー矩形
	Rectf contentBounds{};           ///< コンテンツ領域矩形（最小化時はゼロサイズ）
	Rectf closeButtonBounds{};       ///< 閉じるボタンの矩形
	Rectf minimizeButtonBounds{};    ///< 最小化ボタンの矩形
	Rectf resizeHandleBounds{};      ///< リサイズハンドル矩形（右下角）
	bool closeClicked{false};        ///< 閉じるボタンが押されたか
	bool minimizeClicked{false};     ///< 最小化ボタンが押されたか
	bool titleHovered{false};        ///< タイトルバーがホバーされているか
};

/// @brief ドラッグ可能なウィンドウの状態を評価する
///
/// タイトルバーのドラッグによる移動、右下角のリサイズ、
/// 閉じるボタン・最小化ボタンの操作を一括で評価する。
/// ウィンドウは画面境界内にクランプされる。
///
/// @param state 現在のウィンドウ状態
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param screenBounds 画面矩形（クランプ用）
/// @param titleHeight タイトルバーの高さ
/// @param buttonPadding ボタンのパディング
/// @return 評価結果（更新状態、各領域矩形、ボタンクリック判定）
[[nodiscard]] constexpr DragWindowResult evaluateDragWindow(
	const DragWindowState& state,
	const Vec2f& mousePos,
	bool mouseDown,
	bool mousePressed,
	const Rectf& screenBounds,
	float titleHeight = 30.0f,
	float buttonPadding = 4.0f) noexcept
{
	DragWindowResult result;
	DragWindowState newState = state;

	// --- ウィンドウ全体の矩形 ---
	const float windowH = state.minimized ? titleHeight : state.size.y;
	const Rectf windowBounds{state.position, {state.size.x, windowH}};

	// --- タイトルバー矩形 ---
	const Rectf titleBounds{state.position, {state.size.x, titleHeight}};

	// --- ボタン配置（タイトルバー右側） ---
	const float btnY = state.position.y + (titleHeight - WINDOW_BUTTON_SIZE) * 0.5f;
	const Rectf closeBtn{
		state.position.x + state.size.x - WINDOW_BUTTON_SIZE - buttonPadding,
		btnY, WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE
	};
	const Rectf minimizeBtn{
		closeBtn.x() - WINDOW_BUTTON_SIZE - buttonPadding,
		btnY, WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE
	};

	// --- リサイズハンドル（右下角） ---
	const Rectf resizeHandle{
		state.position.x + state.size.x - RESIZE_HANDLE_SIZE,
		state.position.y + state.size.y - RESIZE_HANDLE_SIZE,
		RESIZE_HANDLE_SIZE, RESIZE_HANDLE_SIZE
	};

	// --- ボタンクリック判定 ---
	if (mousePressed)
	{
		if (isMouseOver(mousePos, closeBtn))
		{
			result.closeClicked = true;
		}
		else if (isMouseOver(mousePos, minimizeBtn))
		{
			result.minimizeClicked = true;
			newState.minimized = !state.minimized;
		}
		else if (!state.minimized && isMouseOver(mousePos, resizeHandle))
		{
			newState.isResizing = true;
			newState.dragOffset = mousePos - Vec2f{
				state.position.x + state.size.x,
				state.position.y + state.size.y
			};
		}
		else if (isMouseOver(mousePos, titleBounds))
		{
			newState.isDragging = true;
			newState.dragOffset = mousePos - state.position;
		}
	}

	// --- ドラッグ継続 ---
	if (!mouseDown)
	{
		newState.isDragging = false;
		newState.isResizing = false;
	}

	// --- ドラッグ移動 ---
	if (newState.isDragging)
	{
		newState.position = mousePos - newState.dragOffset;

		// 画面内クランプ（タイトルバーが画面内に収まるように）
		if (newState.position.x < screenBounds.x())
			newState.position.x = screenBounds.x();
		if (newState.position.y < screenBounds.y())
			newState.position.y = screenBounds.y();
		if (newState.position.x + newState.size.x > screenBounds.right())
			newState.position.x = screenBounds.right() - newState.size.x;
		if (newState.position.y + titleHeight > screenBounds.bottom())
			newState.position.y = screenBounds.bottom() - titleHeight;
	}

	// --- リサイズ ---
	if (newState.isResizing)
	{
		Vec2f newBottomRight = mousePos - newState.dragOffset;
		Vec2f newSize{
			newBottomRight.x - newState.position.x,
			newBottomRight.y - newState.position.y
		};

		// 最小サイズを保証（カスタム最小サイズを使用）
		if (newSize.x < newState.minSize.x) newSize.x = newState.minSize.x;
		if (newSize.y < newState.minSize.y) newSize.y = newState.minSize.y;

		// 画面内クランプ
		if (newState.position.x + newSize.x > screenBounds.right())
			newSize.x = screenBounds.right() - newState.position.x;
		if (newState.position.y + newSize.y > screenBounds.bottom())
			newSize.y = screenBounds.bottom() - newState.position.y;

		newState.size = newSize;
	}

	// --- コンテンツ領域 ---
	const Rectf contentBounds = state.minimized
		? Rectf{0.0f, 0.0f, 0.0f, 0.0f}
		: Rectf{
			newState.position.x,
			newState.position.y + titleHeight,
			newState.size.x,
			newState.size.y - titleHeight
		};

	// --- 結果を構築 ---
	const float finalH = newState.minimized ? titleHeight : newState.size.y;
	result.newState = newState;
	result.outerBounds = Rectf{newState.position, {newState.size.x, finalH}};
	result.titleBounds = Rectf{newState.position, {newState.size.x, titleHeight}};
	result.contentBounds = contentBounds;
	result.closeButtonBounds = Rectf{
		newState.position.x + newState.size.x - WINDOW_BUTTON_SIZE - buttonPadding,
		newState.position.y + (titleHeight - WINDOW_BUTTON_SIZE) * 0.5f,
		WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE
	};
	result.minimizeButtonBounds = Rectf{
		result.closeButtonBounds.x() - WINDOW_BUTTON_SIZE - buttonPadding,
		result.closeButtonBounds.y(),
		WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE
	};
	result.resizeHandleBounds = newState.minimized
		? Rectf{0.0f, 0.0f, 0.0f, 0.0f}
		: Rectf{
			newState.position.x + newState.size.x - RESIZE_HANDLE_SIZE,
			newState.position.y + newState.size.y - RESIZE_HANDLE_SIZE,
			RESIZE_HANDLE_SIZE, RESIZE_HANDLE_SIZE
		};
	result.titleHovered = isMouseOver(mousePos, result.titleBounds);

	return result;
}

} // namespace sgc::ui
