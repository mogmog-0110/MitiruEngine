#pragma once

/// @file UITable.hpp
/// @brief sortable な column / 選択可能な row / virtual scrolling を持つ data table widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief column のテキスト alignment。
enum class TableAlign : std::uint8_t
{
	Left,
	Center,
	Right
};

/// @brief UITable の column 定義。
struct UITableColumn
{
	std::string header;                    ///< column の header テキスト。
	std::string key;                       ///< この column の data key。
	float width = 100.0f;                  ///< 現在の column 幅。
	float minWidth = 40.0f;               ///< column の最小幅。
	bool sortable = true;                  ///< column を sort 可能か。
	bool resizable = true;                 ///< column を resize 可能か。
	TableAlign align = TableAlign::Left;   ///< column 内のテキスト alignment。
	std::string headerImageKey;            ///< 任意の header icon 画像。
};

/// @brief UITable の row data。
struct UITableRow
{
	std::vector<std::string> cells;        ///< cell の値 (column ごとに 1 つ)。
	std::any data;                         ///< 任意の user data。
	bool selected = false;                 ///< 選択状態。
	bool enabled = true;                   ///< row が操作可能か。
};

/// @brief UITable 生成用の設定。
struct UITableConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UITableColumn> columns;
	float rowHeight = 28.0f;               ///< 各 data row の高さ。
	float headerHeight = 32.0f;            ///< header row の高さ。
	std::string headerBackgroundImageKey;   ///< header の背景画像。
	std::string rowBackgroundImageKey;      ///< 通常 row の背景画像。
	std::string rowAlternateImageKey;       ///< 交互 row の背景画像。
	std::string rowHoverImageKey;           ///< hover 中 row の背景画像。
	std::string rowSelectedImageKey;        ///< 選択中 row の背景画像。
	std::string separatorColor;             ///< row/column separator の色文字列。
	std::string sortIndicatorImageKey;      ///< sort 方向 indicator の画像。
	float resizeHandleWidth = 4.0f;        ///< column resize handle の幅。
	float fontSize = 13.0f;                ///< cell テキストの font size。
	float headerFontSize = 14.0f;          ///< header テキストの font size。
	int maxVisibleRows = 10;               ///< scroll 前に表示する最大 row 数。
	bool scrollable = true;                ///< table が scroll するか。
};

/// @brief sortable な column / 選択可能な row / virtual scrolling を持つ data table widget。
///
/// column の sort、row の選択、column の resize、大規模 dataset 向けの効率的な
/// virtual scrolling をサポートする。
///
/// @code
///   UITableConfig cfg;
///   cfg.id = 120;
///   cfg.columns = {{"Name", "name", 150}, {"Score", "score", 80}};
///   UITable table(cfg);
///
///   table.addRow(UITableRow{{"Alice", "100"}});
///   table.addRow(UITableRow{{"Bob", "85"}});
///   table.setOnRowSelected([](int idx) { /* handle */ });
///   table.sortBy(1, false);  // sort by Score descending
/// @endcode
class UITable
{
	std::shared_ptr<UINode> m_node;
	std::vector<UITableColumn> m_columns;
	std::vector<UITableRow> m_rows;
	std::set<int> m_selectedIndices;
	int m_hoveredRow = -1;
	int m_sortColumn = -1;
	bool m_sortAscending = true;
	int m_scrollOffset = 0;
	int m_maxVisibleRows;
	float m_rowHeight;
	float m_headerHeight;
	float m_resizeHandleWidth;

	// column resize の state。
	int m_resizingColumn = -1;
	float m_resizeStartX = 0.0f;
	float m_resizeStartWidth = 0.0f;

	std::function<void(int)> m_onRowSelected;
	std::function<void(int, bool)> m_onColumnSorted;
	std::function<void(int, int)> m_onCellClicked;

public:
	/// @brief 設定から table を構築する。
	/// @param config table 設定。
	explicit UITable(const UITableConfig& config)
		: m_columns(config.columns)
		, m_maxVisibleRows(config.maxVisibleRows)
		, m_rowHeight(config.rowHeight)
		, m_headerHeight(config.headerHeight)
		, m_resizeHandleWidth(config.resizeHandleWidth)
	{
		float totalWidth = 0.0f;
		for (const auto& col : m_columns) { totalWidth += col.width; }
		const float totalHeight = config.headerHeight + config.rowHeight * config.maxVisibleRows;

		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(0.0f, 0.0f, totalWidth, totalHeight);
		data.properties["widget_type"] = "table";
		data.properties["row_height"] = std::to_string(config.rowHeight);
		data.properties["header_height"] = std::to_string(config.headerHeight);
		data.properties["header_bg_image"] = config.headerBackgroundImageKey;
		data.properties["row_bg_image"] = config.rowBackgroundImageKey;
		data.properties["row_alt_image"] = config.rowAlternateImageKey;
		data.properties["row_hover_image"] = config.rowHoverImageKey;
		data.properties["row_selected_image"] = config.rowSelectedImageKey;
		data.properties["separator_color"] = config.separatorColor;
		data.properties["sort_indicator_image"] = config.sortIndicatorImageKey;
		data.properties["resize_handle_width"] = std::to_string(config.resizeHandleWidth);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["header_font_size"] = std::to_string(config.headerFontSize);
		data.properties["scrollable"] = config.scrollable ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 総 row 数を取得する。
	[[nodiscard]] std::size_t rowCount() const noexcept { return m_rows.size(); }

	/// @brief column 定義を取得する。
	[[nodiscard]] const std::vector<UITableColumn>& columns() const noexcept { return m_columns; }

	/// @brief row data を取得する。
	[[nodiscard]] const std::vector<UITableRow>& rows() const noexcept { return m_rows; }

	/// @brief 選択中の row index 集合を取得する。
	[[nodiscard]] std::vector<int> getSelectedRows() const
	{
		return {m_selectedIndices.begin(), m_selectedIndices.end()};
	}

	/// @brief 現在の sort column index を取得する (なければ -1)。
	[[nodiscard]] int sortColumn() const noexcept { return m_sortColumn; }

	/// @brief sort 方向を取得する。
	[[nodiscard]] bool isSortAscending() const noexcept { return m_sortAscending; }

	/// @brief 表示中の row 範囲 [start, end) を取得する。
	[[nodiscard]] std::pair<int, int> visibleRange() const noexcept
	{
		const int start = m_scrollOffset;
		const int end = std::min(m_scrollOffset + m_maxVisibleRows, static_cast<int>(m_rows.size()));
		return {start, end};
	}

	// ── 設定 ──────────────────────────────────────────────────

	/// @brief row 選択時の callback を設定する。
	void setOnRowSelected(std::function<void(int)> callback) { m_onRowSelected = std::move(callback); }

	/// @brief column sort 時の callback を設定する。
	void setOnColumnSorted(std::function<void(int, bool)> callback) { m_onColumnSorted = std::move(callback); }

	/// @brief cell click 時の callback を設定する。
	void setOnCellClicked(std::function<void(int, int)> callback) { m_onCellClicked = std::move(callback); }

	// ── data 操作 ─────────────────────────────────────────────

	/// @brief row data 全体を設定する。
	/// @param rows 新しい row vector。
	void setData(std::vector<UITableRow> rows)
	{
		m_rows = std::move(rows);
		m_selectedIndices.clear();
		m_scrollOffset = 0;
		syncNodeState();
	}

	/// @brief 末尾に row を追加する。
	/// @param row 追加する row。
	void addRow(UITableRow row)
	{
		m_rows.push_back(std::move(row));
		syncNodeState();
	}

	/// @brief index 指定で row を削除する。
	/// @param index 削除する row の index。
	void removeRow(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_rows.size())) { return; }
		m_rows.erase(m_rows.begin() + index);
		m_selectedIndices.erase(index);

		// 削除した row より上の selected index を調整する。
		std::set<int> adjusted;
		for (const int idx : m_selectedIndices)
		{
			adjusted.insert(idx > index ? idx - 1 : idx);
		}
		m_selectedIndices = std::move(adjusted);
		syncNodeState();
	}

	/// @brief 全 row を消去する。
	void clearData()
	{
		m_rows.clear();
		m_selectedIndices.clear();
		m_scrollOffset = 0;
		syncNodeState();
	}

	// ── sort ──────────────────────────────────────────────────

	/// @brief column を基準に table を sort する。
	/// @param column column の index。
	/// @param ascending 昇順なら true、降順なら false。
	void sortBy(int column, bool ascending)
	{
		if (column < 0 || column >= static_cast<int>(m_columns.size())) { return; }
		if (!m_columns[static_cast<std::size_t>(column)].sortable) { return; }

		m_sortColumn = column;
		m_sortAscending = ascending;

		const auto col = static_cast<std::size_t>(column);
		std::stable_sort(m_rows.begin(), m_rows.end(),
			[col, ascending](const UITableRow& a, const UITableRow& b)
			{
				const auto& cellA = (col < a.cells.size()) ? a.cells[col] : std::string{};
				const auto& cellB = (col < b.cells.size()) ? b.cells[col] : std::string{};
				return ascending ? (cellA < cellB) : (cellA > cellB);
			});

		m_selectedIndices.clear();
		syncNodeState();
		if (m_onColumnSorted) { m_onColumnSorted(column, ascending); }
	}

	// ── 選択 ──────────────────────────────────────────────────

	/// @brief index 指定で row を選択する。
	/// @param index row の index。
	void selectRow(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_rows.size())) { return; }
		if (!m_rows[static_cast<std::size_t>(index)].enabled) { return; }

		m_selectedIndices.clear();
		m_selectedIndices.insert(index);
		m_rows[static_cast<std::size_t>(index)].selected = true;
		syncNodeState();
		if (m_onRowSelected) { m_onRowSelected(index); }
	}

	/// @brief 全 row の選択を解除する。
	void clearSelection()
	{
		for (auto& row : m_rows) { row.selected = false; }
		m_selectedIndices.clear();
		syncNodeState();
	}

	// ── 操作 ──────────────────────────────────────────────────

	/// @brief 特定 cell への click を処理する。
	/// @param row row の index。
	/// @param col column の index。
	void clickCell(int row, int col)
	{
		if (row < 0 || row >= static_cast<int>(m_rows.size())) { return; }
		if (col < 0 || col >= static_cast<int>(m_columns.size())) { return; }

		selectRow(row);
		if (m_onCellClicked) { m_onCellClicked(row, col); }
	}

	/// @brief column header への click を処理する (sort を toggle)。
	/// @param column column の index。
	void clickHeader(int column)
	{
		if (column < 0 || column >= static_cast<int>(m_columns.size())) { return; }
		if (!m_columns[static_cast<std::size_t>(column)].sortable) { return; }

		const bool ascending = (m_sortColumn == column) ? !m_sortAscending : true;
		sortBy(column, ascending);
	}

	/// @brief hover 中の row index を設定する (なしは -1)。
	/// @param index row の index。
	void setHoveredRow(int index)
	{
		m_hoveredRow = index;
		m_node->setProperty("hovered_row", std::to_string(m_hoveredRow));
	}

	/// @brief column resize の drag を開始する。
	/// @param column column の index。
	/// @param startX 開始時の pointer X 位置。
	void beginColumnResize(int column, float startX)
	{
		if (column < 0 || column >= static_cast<int>(m_columns.size())) { return; }
		if (!m_columns[static_cast<std::size_t>(column)].resizable) { return; }

		m_resizingColumn = column;
		m_resizeStartX = startX;
		m_resizeStartWidth = m_columns[static_cast<std::size_t>(column)].width;
	}

	/// @brief column resize の drag を更新する。
	/// @param currentX 現在の pointer X 位置。
	void updateColumnResize(float currentX)
	{
		if (m_resizingColumn < 0) { return; }

		const auto col = static_cast<std::size_t>(m_resizingColumn);
		const float delta = currentX - m_resizeStartX;
		const float newWidth = std::max(m_columns[col].minWidth, m_resizeStartWidth + delta);
		m_columns[col].width = newWidth;
		syncNodeState();
	}

	/// @brief column resize の drag を終了する。
	void endColumnResize()
	{
		m_resizingColumn = -1;
	}

	/// @brief 指定 row 数だけ scroll する。
	/// @param delta 正 = 下へ scroll、負 = 上へ scroll。
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_rows.size()) - m_maxVisibleRows);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		syncNodeState();
	}

private:
	/// @brief alignment を文字列に変換する。
	[[nodiscard]] static const char* alignToString(TableAlign a) noexcept
	{
		switch (a)
		{
		case TableAlign::Left:   return "left";
		case TableAlign::Center: return "center";
		case TableAlign::Right:  return "right";
		}
		return "left";
	}

	/// @brief state を UINode に同期する。
	void syncNodeState()
	{
		m_node->setProperty("row_count", std::to_string(m_rows.size()));
		m_node->setProperty("column_count", std::to_string(m_columns.size()));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("sort_column", std::to_string(m_sortColumn));
		m_node->setProperty("sort_ascending", m_sortAscending ? "true" : "false");
		m_node->setProperty("hovered_row", std::to_string(m_hoveredRow));

		// column 情報を encode する。
		for (std::size_t c = 0; c < m_columns.size(); ++c)
		{
			const auto prefix = "col_" + std::to_string(c) + "_";
			m_node->setProperty(prefix + "header", m_columns[c].header);
			m_node->setProperty(prefix + "key", m_columns[c].key);
			m_node->setProperty(prefix + "width", std::to_string(m_columns[c].width));
			m_node->setProperty(prefix + "align", alignToString(m_columns[c].align));
			m_node->setProperty(prefix + "header_image", m_columns[c].headerImageKey);
		}

		// 選択中の index を encode する。
		std::string selStr;
		for (const auto idx : m_selectedIndices)
		{
			if (!selStr.empty()) { selStr += ","; }
			selStr += std::to_string(idx);
		}
		m_node->setProperty("selected", selStr);

		// 表示中の row を encode する。
		const auto [start, end] = visibleRange();
		m_node->setProperty("visible_start", std::to_string(start));
		m_node->setProperty("visible_end", std::to_string(end));

		for (int i = start; i < end; ++i)
		{
			const auto rowPrefix = "row_" + std::to_string(i - start) + "_";
			const auto& row = m_rows[static_cast<std::size_t>(i)];
			for (std::size_t c = 0; c < m_columns.size() && c < row.cells.size(); ++c)
			{
				m_node->setProperty(rowPrefix + "cell_" + std::to_string(c), row.cells[c]);
			}
			m_node->setProperty(rowPrefix + "selected", row.selected ? "true" : "false");
			m_node->setProperty(rowPrefix + "enabled", row.enabled ? "true" : "false");
		}
	}
};

} // namespace mitiru::ui
