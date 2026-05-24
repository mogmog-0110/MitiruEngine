#pragma once

/// @file FocusableWidget.hpp
/// @brief フォーカス可能なウィジェットの定義
///
/// キーボード・ゲームパッドによるUI操作の基本データ型。
/// FocusManagerと組み合わせて空間ナビゲーションを実現する。
///
/// @code
/// sgc::ui::FocusableWidget widget;
/// widget.id = 1;
/// widget.bounds = sgc::Rectf{10.0f, 20.0f, 100.0f, 40.0f};
/// widget.enabled = true;
/// @endcode

#include <cstdint>
#include <optional>

#include "sgc/math/Rect.hpp"

namespace sgc::ui
{

/// @brief フォーカスID型
using FocusId = uint32_t;

/// @brief フォーカス可能なウィジェットの情報
///
/// ウィジェットの位置・サイズ・有効状態・明示的ナビゲーション先を保持する。
/// navUp/navDown/navLeft/navRight が nullopt の場合は空間ナビゲーションで自動決定される。
struct FocusableWidget
{
	FocusId id{};       ///< ウィジェット固有のID
	Rectf bounds{};     ///< ウィジェットの矩形領域
	bool enabled = true; ///< 有効フラグ（falseの場合ナビゲーションでスキップ）

	/// @brief 上方向のナビゲーション先（nulloptなら自動空間ナビゲーション）
	std::optional<FocusId> navUp{};

	/// @brief 下方向のナビゲーション先（nulloptなら自動空間ナビゲーション）
	std::optional<FocusId> navDown{};

	/// @brief 左方向のナビゲーション先（nulloptなら自動空間ナビゲーション）
	std::optional<FocusId> navLeft{};

	/// @brief 右方向のナビゲーション先（nulloptなら自動空間ナビゲーション）
	std::optional<FocusId> navRight{};
};

} // namespace sgc::ui
