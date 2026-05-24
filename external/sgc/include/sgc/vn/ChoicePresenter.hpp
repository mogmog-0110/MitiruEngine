#pragma once

/// @file ChoicePresenter.hpp
/// @brief 選択肢UI評価
///
/// ビジュアルノベルの選択肢をステートレスに評価する。
/// マウス・キーボード・ゲームパッド入力に対応する。
///
/// @code
/// using namespace sgc::vn;
/// std::vector<Choice> choices = {{"Yes", "yes_id"}, {"No", "no_id"}};
/// std::vector<sgc::Rectf> bounds = {{0, 0, 200, 40}, {0, 50, 200, 40}};
/// ChoiceInput input;
/// input.mousePos = {100.0f, 20.0f};
/// input.mousePressed = true;
/// auto result = evaluateChoices(choices, bounds, input, 0);
/// // result.selectedIndex == 0
/// @endcode

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::vn
{

/// @brief 選択肢定義
struct Choice
{
	std::string text;         ///< 選択肢テキスト
	std::string id;           ///< 分岐用ID
	bool enabled = true;      ///< 選択可能か
};

/// @brief 選択肢への入力
struct ChoiceInput
{
	Vec2f mousePos{};         ///< マウス座標
	bool mousePressed = false; ///< マウスボタンが押されたか
	int keySelection = -1;     ///< 数字キーによる直接選択（-1=なし、0-N=選択）
	bool upPressed = false;    ///< 上キーが押されたか
	bool downPressed = false;  ///< 下キーが押されたか
	bool confirmPressed = false; ///< 決定キーが押されたか
};

/// @brief 選択肢評価結果
struct ChoiceResult
{
	int hoveredIndex = -1;     ///< マウスホバー中の選択肢インデックス
	int selectedIndex = -1;    ///< 選択された選択肢インデックス（-1=未選択）
	int focusedIndex = 0;      ///< キーボード/ゲームパッドのフォーカス位置
};

/// @brief 選択肢を評価する
///
/// マウスホバー・クリック・キーボード上下・数字キー直接選択・決定キーを
/// 統合的に評価し、結果を返す。
///
/// @param choices 選択肢配列
/// @param choiceBounds 各選択肢の矩形配列
/// @param input 入力情報
/// @param previousFocusIndex 前フレームのフォーカス位置
/// @return 評価結果
[[nodiscard]] inline ChoiceResult evaluateChoices(
	std::span<const Choice> choices,
	const std::vector<Rectf>& choiceBounds,
	const ChoiceInput& input,
	int previousFocusIndex)
{
	ChoiceResult result;
	const int count = static_cast<int>(choices.size());

	if (count == 0)
	{
		return result;
	}

	// フォーカス位置の更新
	result.focusedIndex = previousFocusIndex;

	// 上下キーによるフォーカス移動
	if (input.upPressed)
	{
		result.focusedIndex = previousFocusIndex - 1;
		if (result.focusedIndex < 0)
		{
			result.focusedIndex = count - 1;
		}
	}
	else if (input.downPressed)
	{
		result.focusedIndex = previousFocusIndex + 1;
		if (result.focusedIndex >= count)
		{
			result.focusedIndex = 0;
		}
	}

	// 無効な選択肢をスキップ（有効な選択肢がない場合はそのまま）
	if (!choices[static_cast<std::size_t>(result.focusedIndex)].enabled)
	{
		// 次の有効な選択肢を探す
		for (int i = 0; i < count; ++i)
		{
			const int idx = (result.focusedIndex + i) % count;
			if (choices[static_cast<std::size_t>(idx)].enabled)
			{
				result.focusedIndex = idx;
				break;
			}
		}
	}

	// マウスホバー判定
	for (int i = 0; i < count && i < static_cast<int>(choiceBounds.size()); ++i)
	{
		const auto& bounds = choiceBounds[static_cast<std::size_t>(i)];
		if (bounds.contains(input.mousePos))
		{
			result.hoveredIndex = i;
			break;
		}
	}

	// マウスクリックによる選択
	if (input.mousePressed && result.hoveredIndex >= 0)
	{
		const auto idx = static_cast<std::size_t>(result.hoveredIndex);
		if (choices[idx].enabled)
		{
			result.selectedIndex = result.hoveredIndex;
			return result;
		}
	}

	// 数字キーによる直接選択
	if (input.keySelection >= 0 && input.keySelection < count)
	{
		const auto idx = static_cast<std::size_t>(input.keySelection);
		if (choices[idx].enabled)
		{
			result.selectedIndex = input.keySelection;
			return result;
		}
	}

	// 決定キーによる選択
	if (input.confirmPressed && result.focusedIndex >= 0 && result.focusedIndex < count)
	{
		const auto idx = static_cast<std::size_t>(result.focusedIndex);
		if (choices[idx].enabled)
		{
			result.selectedIndex = result.focusedIndex;
		}
	}

	return result;
}

} // namespace sgc::vn
