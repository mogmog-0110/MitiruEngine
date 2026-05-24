#pragma once

/// @file TextInput.hpp
/// @brief テキスト入力フィールド評価ユーティリティ
///
/// テキスト入力のフォーカス、カーソル移動、選択操作を評価する。
/// 実際のテキスト編集は呼び出し元で行い、本関数はアクションを返す。
///
/// @code
/// using namespace sgc::ui;
/// TextInputConfig config{bounds, text, cursorPos, -1, -1, isFocused, 100};
/// auto result = evaluateTextInput(config, mousePos, mouseDown, mousePressed,
///     tabPressed, shiftHeld, ctrlHeld, left, right, home, end, del, bs);
/// isFocused = result.isFocused;
/// cursorPos = result.cursorPos;
/// @endcode

#include <cstdint>
#include <string_view>

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief テキスト入力のパディング
inline constexpr float TEXT_INPUT_PADDING = 4.0f;

/// @brief テキスト入力設定
struct TextInputConfig
{
	Rectf bounds;                   ///< 入力フィールド全体のバウンズ
	std::string_view text;          ///< 現在のテキスト
	int32_t cursorPos{0};           ///< カーソル位置
	int32_t selectionStart{-1};     ///< 選択開始 (-1 = 選択なし)
	int32_t selectionEnd{-1};       ///< 選択終了
	bool isFocused{false};          ///< フォーカス中か
	int32_t maxLength{0};           ///< 最大文字数 (0 = 無制限)
};

/// @brief テキスト入力アクション
enum class TextInputAction
{
	None,             ///< アクションなし
	MoveCursorLeft,   ///< カーソル左移動
	MoveCursorRight,  ///< カーソル右移動
	MoveToStart,      ///< 先頭へ移動
	MoveToEnd,        ///< 末尾へ移動
	Delete,           ///< Delete操作
	Backspace,        ///< Backspace操作
	SelectAll,        ///< 全選択 (Ctrl+A)
	Copy,             ///< コピー (Ctrl+C)
	Paste,            ///< ペースト (Ctrl+V)
	Cut               ///< カット (Ctrl+X)
};

/// @brief テキスト入力評価結果
struct TextInputResult
{
	WidgetState state{WidgetState::Normal};  ///< ウィジェットの視覚状態
	bool isFocused{false};                    ///< フォーカス状態
	int32_t cursorPos{0};                     ///< カーソル位置
	int32_t selectionStart{-1};               ///< 選択開始
	int32_t selectionEnd{-1};                 ///< 選択終了
	TextInputAction action{TextInputAction::None}; ///< 発生したアクション
	Rectf textAreaBounds;                     ///< テキスト描画エリア
	Rectf cursorRect;                         ///< カーソルの矩形
};

/// @brief テキスト入力フィールドの状態を評価する
///
/// フォーカス管理、カーソル移動、テキスト選択、キーボードショートカットを評価する。
/// 実際のテキスト変更は行わず、アクションとして結果を返す。
///
/// @param config テキスト入力の設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param tabPressed Tabキーが押されたか
/// @param shiftHeld Shiftキーが押下中か
/// @param ctrlHeld Ctrlキーが押下中か
/// @param leftArrow 左矢印キーが押されたか
/// @param rightArrow 右矢印キーが押されたか
/// @param homeKey Homeキーが押されたか
/// @param endKey Endキーが押されたか
/// @param deleteKey Deleteキーが押されたか
/// @param backspaceKey Backspaceキーが押されたか
/// @return テキスト入力の評価結果
[[nodiscard]] constexpr TextInputResult evaluateTextInput(
	const TextInputConfig& config, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	bool tabPressed, bool shiftHeld, bool ctrlHeld,
	bool leftArrow, bool rightArrow,
	bool homeKey, bool endKey,
	bool deleteKey, bool backspaceKey) noexcept
{
	const bool hovered = isMouseOver(mousePos, config.bounds);
	bool focused = config.isFocused;

	// フォーカス管理
	if (mousePressed)
	{
		focused = hovered;
	}
	if (tabPressed && !shiftHeld)
	{
		focused = false;
	}

	// テキストエリアのバウンズ（パディング適用）
	const Rectf textArea{
		{config.bounds.x() + TEXT_INPUT_PADDING, config.bounds.y() + TEXT_INPUT_PADDING},
		{config.bounds.width() - TEXT_INPUT_PADDING * 2.0f,
		 config.bounds.height() - TEXT_INPUT_PADDING * 2.0f}
	};

	int32_t cursor = config.cursorPos;
	int32_t selStart = config.selectionStart;
	int32_t selEnd = config.selectionEnd;
	TextInputAction action = TextInputAction::None;
	const int32_t textLen = static_cast<int32_t>(config.text.size());

	// キーボード入力処理（フォーカス時のみ）
	if (focused)
	{
		if (ctrlHeld)
		{
			// Ctrl+A: 全選択フラグ設定（実際の選択範囲設定はここで）
			// 注意: Ctrl+C/V/Xは文字判定が必要なため、アクションとして返す
			// ここでは左右矢印など他のキーと区別するため先に判定
		}

		if (leftArrow)
		{
			action = TextInputAction::MoveCursorLeft;
			if (cursor > 0)
			{
				cursor = cursor - 1;
			}
			if (!shiftHeld)
			{
				selStart = -1;
				selEnd = -1;
			}
		}
		else if (rightArrow)
		{
			action = TextInputAction::MoveCursorRight;
			if (cursor < textLen)
			{
				cursor = cursor + 1;
			}
			if (!shiftHeld)
			{
				selStart = -1;
				selEnd = -1;
			}
		}
		else if (homeKey)
		{
			action = TextInputAction::MoveToStart;
			cursor = 0;
			if (!shiftHeld)
			{
				selStart = -1;
				selEnd = -1;
			}
		}
		else if (endKey)
		{
			action = TextInputAction::MoveToEnd;
			cursor = textLen;
			if (!shiftHeld)
			{
				selStart = -1;
				selEnd = -1;
			}
		}
		else if (deleteKey)
		{
			action = TextInputAction::Delete;
		}
		else if (backspaceKey)
		{
			action = TextInputAction::Backspace;
			if (cursor > 0 && selStart < 0)
			{
				cursor = cursor - 1;
			}
		}
	}

	// カーソル矩形の計算（簡易: 固定幅1px）
	const float cursorX = textArea.x() + static_cast<float>(cursor) * 8.0f; // 仮の文字幅
	const Rectf cursorRect{
		{cursorX, textArea.y()},
		{1.0f, textArea.height()}
	};

	const bool pressed = hovered && mouseDown;
	const auto state = resolveWidgetState(true, hovered, pressed, focused);

	return {state, focused, cursor, selStart, selEnd, action, textArea, cursorRect};
}

} // namespace sgc::ui
