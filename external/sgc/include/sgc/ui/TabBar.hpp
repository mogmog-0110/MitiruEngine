#pragma once

/// @file TabBar.hpp
/// @brief タブバー評価ユーティリティ
///
/// タブ切り替えUIの状態・選択・レイアウトを評価する。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateTabBar(barBounds, 3, currentTab, mousePos, mouseDown, mousePressed);
/// if (result.changed) { currentTab = result.selectedIndex; }
/// // result.tabBounds[i] で各タブの矩形を取得して描画
/// @endcode

#include "sgc/ui/WidgetState.hpp"
#include <vector>

namespace sgc::ui
{

/// @brief タブバー評価結果
struct TabBarResult
{
	int32_t selectedIndex{0};                  ///< 選択中のタブインデックス
	bool changed{false};                        ///< 選択が変わったか
	std::vector<Rectf> tabBounds;               ///< 各タブの矩形
	std::vector<WidgetState> tabStates;         ///< 各タブの視覚状態
};

/// @brief タブバーの状態を評価する
///
/// バー領域を等分してタブを配置し、クリック判定と視覚状態を算出する。
///
/// @param barBounds タブバー全体の矩形
/// @param tabCount タブの数（1以上）
/// @param currentIndex 現在選択中のタブインデックス
/// @param mousePos マウス座標
/// @param mouseDown マウスボタン押下中か
/// @param mousePressed このフレームでマウスボタンが押されたか
/// @return 選択インデックス・変化フラグ・各タブの矩形と状態
[[nodiscard]] inline TabBarResult evaluateTabBar(
	const Rectf& barBounds, int32_t tabCount, int32_t currentIndex,
	const Vec2f& mousePos, bool mouseDown, bool mousePressed) noexcept
{
	TabBarResult result;
	result.selectedIndex = currentIndex;

	if (tabCount <= 0) return result;

	const float tabWidth = barBounds.width() / static_cast<float>(tabCount);
	result.tabBounds.reserve(static_cast<size_t>(tabCount));
	result.tabStates.reserve(static_cast<size_t>(tabCount));

	for (int32_t i = 0; i < tabCount; ++i)
	{
		const Rectf bounds{
			barBounds.x() + tabWidth * static_cast<float>(i),
			barBounds.y(),
			tabWidth,
			barBounds.height()
		};
		result.tabBounds.push_back(bounds);

		const bool hovered = isMouseOver(mousePos, bounds);
		const bool pressed = hovered && mouseDown;
		const bool isCurrent = (i == currentIndex);

		if (hovered && mousePressed && !isCurrent)
		{
			result.selectedIndex = i;
			result.changed = true;
		}

		// 選択中タブはFocused状態
		if (isCurrent)
		{
			result.tabStates.push_back(WidgetState::Focused);
		}
		else
		{
			result.tabStates.push_back(resolveWidgetState(true, hovered, pressed));
		}
	}

	return result;
}

} // namespace sgc::ui
