#pragma once

/// @file ComboBox.hpp
/// @brief コンボボックス評価ユーティリティ
///
/// ドロップダウン選択リストの状態と選択を一括で評価する。
/// ヘッダークリックで開閉を切り替え、項目クリックで選択を変更する。
///
/// @code
/// using namespace sgc::ui;
/// ComboBoxConfig config{bounds, selectedIdx, isOpen, 30.0f, 5, 5.0f};
/// auto result = evaluateComboBox(config, mousePos, mouseDown, mousePressed);
/// if (result.selectedIndex >= 0) { selectedIdx = result.selectedIndex; }
/// isOpen = result.isOpen;
/// @endcode

#include <algorithm>
#include <cstdint>

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief コンボボックスの最大表示項目数デフォルト値
inline constexpr float DEFAULT_MAX_VISIBLE_ITEMS = 5.0f;

/// @brief コンボボックス設定
struct ComboBoxConfig
{
	Rectf bounds;                ///< 閉じた状態のバウンズ
	int32_t selectedIndex{0};    ///< 現在の選択インデックス
	bool isOpen{false};          ///< ドロップダウンが開いているか
	float itemHeight{30.0f};     ///< 各項目の高さ
	int32_t itemCount{0};        ///< 項目数
	float maxVisibleItems{DEFAULT_MAX_VISIBLE_ITEMS}; ///< 最大表示項目数
};

/// @brief コンボボックス評価結果
struct ComboBoxResult
{
	WidgetState state{WidgetState::Normal};  ///< ウィジェットの視覚状態
	int32_t selectedIndex{-1};               ///< 新しい選択インデックス (-1 = 変更なし)
	bool isOpen{false};                      ///< 新しい開閉状態
	Rectf headerBounds;                      ///< ヘッダー部分のバウンズ
	Rectf dropdownBounds;                    ///< ドロップダウン部分のバウンズ (isOpen時のみ有効)
};

/// @brief コンボボックスの状態と選択を評価する
///
/// ヘッダー領域のクリックで開閉を切り替え、
/// ドロップダウン内の項目クリックで選択を変更する。
/// ドロップダウン外のクリックで閉じる。
///
/// @param config コンボボックスの設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @return コンボボックスの評価結果
[[nodiscard]] constexpr ComboBoxResult evaluateComboBox(
	const ComboBoxConfig& config, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed) noexcept
{
	const Rectf headerBounds = config.bounds;

	// ドロップダウン領域の計算
	const float visibleCount = (config.maxVisibleItems < static_cast<float>(config.itemCount))
		? config.maxVisibleItems
		: static_cast<float>(config.itemCount);
	const float dropdownHeight = visibleCount * config.itemHeight;
	const Rectf dropdownBounds{
		{config.bounds.x(), config.bounds.y() + config.bounds.height()},
		{config.bounds.width(), dropdownHeight}
	};

	const bool headerHovered = isMouseOver(mousePos, headerBounds);
	bool newOpen = config.isOpen;
	int32_t newSelected = -1;

	if (mousePressed)
	{
		if (headerHovered)
		{
			// ヘッダークリックで開閉切り替え
			newOpen = !config.isOpen;
		}
		else if (config.isOpen)
		{
			if (isMouseOver(mousePos, dropdownBounds))
			{
				// ドロップダウン内の項目クリック
				const float relY = mousePos.y - dropdownBounds.y();
				const int32_t clickedIdx = static_cast<int32_t>(relY / config.itemHeight);
				if (clickedIdx >= 0 && clickedIdx < config.itemCount)
				{
					newSelected = clickedIdx;
				}
				newOpen = false;
			}
			else
			{
				// ドロップダウン外クリックで閉じる
				newOpen = false;
			}
		}
	}

	const bool anyHovered = headerHovered || (config.isOpen && isMouseOver(mousePos, dropdownBounds));
	const bool pressed = anyHovered && mouseDown;
	const auto state = resolveWidgetState(true, anyHovered, pressed);

	return {state, newSelected, newOpen, headerBounds, dropdownBounds};
}

} // namespace sgc::ui
