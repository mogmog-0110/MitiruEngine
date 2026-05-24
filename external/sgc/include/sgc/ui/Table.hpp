#pragma once

/// @file Table.hpp
/// @brief テーブル評価ユーティリティ
///
/// テーブル形式のデータ表示の状態を評価する。
/// 行選択、ヘッダーソート、スクロール操作をフレームワーク非依存で判定する。
///
/// @code
/// using namespace sgc::ui;
/// float widths[] = {100.0f, 200.0f, 80.0f};
/// TableConfig config{bounds, 3, 10, widths, 30.0f, 25.0f, -1, -1, true, 0.0f};
/// auto result = evaluateTable(config, mousePos, mouseDown, mousePressed, scrollDelta);
/// if (result.clickedRow >= 0) { selectedRow = result.clickedRow; }
/// @endcode

#include <cstdint>

#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief テーブル設定
struct TableConfig
{
	Rectf bounds;                   ///< テーブル全体のバウンズ
	int32_t columnCount{0};         ///< 列数
	int32_t rowCount{0};            ///< 行数
	const float* columnWidths{nullptr}; ///< 各列の幅配列
	float headerHeight{30.0f};      ///< ヘッダー行の高さ
	float rowHeight{25.0f};         ///< データ行の高さ
	int32_t selectedRow{-1};        ///< 選択中の行 (-1 = なし)
	int32_t sortColumn{-1};         ///< ソート列 (-1 = なし)
	bool sortAscending{true};       ///< 昇順ソートか
	float scrollOffset{0.0f};       ///< スクロールオフセット
};

/// @brief テーブル評価結果
struct TableResult
{
	WidgetState state{WidgetState::Normal};  ///< ウィジェットの視覚状態
	int32_t hoveredRow{-1};                  ///< ホバー中の行 (-1 = なし)
	int32_t clickedRow{-1};                  ///< クリックされた行 (-1 = なし)
	int32_t clickedColumn{-1};               ///< クリックされた列 (-1 = なし)
	int32_t sortColumn{-1};                  ///< ソート列（ヘッダークリック時に更新）
	bool sortAscending{true};                ///< 昇順ソートか
	float scrollOffset{0.0f};                ///< スクロールオフセット
	Rectf headerBounds;                      ///< ヘッダー領域のバウンズ
	Rectf bodyBounds;                        ///< データ領域のバウンズ
};

/// @brief テーブルの状態を評価する
///
/// ヘッダークリックでソート列を切り替え、
/// データ行のクリックで行選択、スクロールでオフセット変更を行う。
///
/// @param config テーブルの設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param scrollDelta マウスホイールのスクロール量
/// @return テーブルの評価結果
[[nodiscard]] constexpr TableResult evaluateTable(
	const TableConfig& config, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	float scrollDelta) noexcept
{
	const Rectf headerBounds{
		config.bounds.position,
		{config.bounds.width(), config.headerHeight}
	};
	const Rectf bodyBounds{
		{config.bounds.x(), config.bounds.y() + config.headerHeight},
		{config.bounds.width(), config.bounds.height() - config.headerHeight}
	};

	const bool hovered = isMouseOver(mousePos, config.bounds);
	int32_t hoveredRow = -1;
	int32_t clickedRow = -1;
	int32_t clickedColumn = -1;
	int32_t sortCol = config.sortColumn;
	bool sortAsc = config.sortAscending;
	float scroll = config.scrollOffset;

	// ヘッダークリック判定
	if (mousePressed && isMouseOver(mousePos, headerBounds))
	{
		float colX = config.bounds.x();
		for (int32_t c = 0; c < config.columnCount; ++c)
		{
			const float colW = config.columnWidths[c];
			const Rectf colRect{{colX, headerBounds.y()}, {colW, config.headerHeight}};
			if (isMouseOver(mousePos, colRect))
			{
				if (sortCol == c)
				{
					sortAsc = !sortAsc;
				}
				else
				{
					sortCol = c;
					sortAsc = true;
				}
				clickedColumn = c;
				break;
			}
			colX += colW;
		}
	}

	// データ行のホバー・クリック判定
	if (isMouseOver(mousePos, bodyBounds))
	{
		const float relY = mousePos.y - bodyBounds.y() + scroll;
		const int32_t rowIdx = static_cast<int32_t>(relY / config.rowHeight);
		if (rowIdx >= 0 && rowIdx < config.rowCount)
		{
			hoveredRow = rowIdx;
			if (mousePressed)
			{
				clickedRow = rowIdx;
				// クリック列の判定
				float colX = config.bounds.x();
				for (int32_t c = 0; c < config.columnCount; ++c)
				{
					const float colW = config.columnWidths[c];
					if (mousePos.x >= colX && mousePos.x < colX + colW)
					{
						clickedColumn = c;
						break;
					}
					colX += colW;
				}
			}
		}
	}

	// スクロール処理
	if (hovered)
	{
		scroll -= scrollDelta;
		const float maxScroll = static_cast<float>(config.rowCount) * config.rowHeight - bodyBounds.height();
		if (scroll < 0.0f)
		{
			scroll = 0.0f;
		}
		if (maxScroll > 0.0f && scroll > maxScroll)
		{
			scroll = maxScroll;
		}
		if (maxScroll <= 0.0f)
		{
			scroll = 0.0f;
		}
	}

	const bool pressed = hovered && mouseDown;
	const auto state = resolveWidgetState(true, hovered, pressed);

	return {state, hoveredRow, clickedRow, clickedColumn, sortCol, sortAsc,
	        scroll, headerBounds, bodyBounds};
}

/// @brief 特定セルのバウンズを取得する
///
/// @param config テーブルの設定
/// @param result テーブルの評価結果
/// @param row 行インデックス
/// @param col 列インデックス
/// @return セルのバウンズ
[[nodiscard]] constexpr Rectf tableCellBounds(
	const TableConfig& config, const TableResult& result,
	int32_t row, int32_t col) noexcept
{
	float colX = config.bounds.x();
	for (int32_t c = 0; c < col; ++c)
	{
		colX += config.columnWidths[c];
	}
	const float cellY = result.bodyBounds.y() +
		static_cast<float>(row) * config.rowHeight - result.scrollOffset;

	return Rectf{{colX, cellY}, {config.columnWidths[col], config.rowHeight}};
}

} // namespace sgc::ui
