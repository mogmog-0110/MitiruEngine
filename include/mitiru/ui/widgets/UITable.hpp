#pragma once

/// @file UITable.hpp
/// @brief Data table widget with sortable columns, selectable rows, and virtual scrolling.

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

/// @brief Column text alignment.
enum class TableAlign : std::uint8_t
{
	Left,
	Center,
	Right
};

/// @brief Column definition for a UITable.
struct UITableColumn
{
	std::string header;                    ///< Column header text.
	std::string key;                       ///< Data key for this column.
	float width = 100.0f;                  ///< Current column width.
	float minWidth = 40.0f;               ///< Minimum column width.
	bool sortable = true;                  ///< Whether column can be sorted.
	bool resizable = true;                 ///< Whether column can be resized.
	TableAlign align = TableAlign::Left;   ///< Text alignment within the column.
	std::string headerImageKey;            ///< Optional header icon image.
};

/// @brief Row data for a UITable.
struct UITableRow
{
	std::vector<std::string> cells;        ///< Cell values (one per column).
	std::any data;                         ///< Arbitrary user data.
	bool selected = false;                 ///< Selection state.
	bool enabled = true;                   ///< Whether the row is interactive.
};

/// @brief Configuration for creating a UITable.
struct UITableConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UITableColumn> columns;
	float rowHeight = 28.0f;               ///< Height of each data row.
	float headerHeight = 32.0f;            ///< Height of the header row.
	std::string headerBackgroundImageKey;   ///< Header background image.
	std::string rowBackgroundImageKey;      ///< Normal row background image.
	std::string rowAlternateImageKey;       ///< Alternating row background image.
	std::string rowHoverImageKey;           ///< Hovered row background image.
	std::string rowSelectedImageKey;        ///< Selected row background image.
	std::string separatorColor;             ///< Color string for row/column separators.
	std::string sortIndicatorImageKey;      ///< Sort direction indicator image.
	float resizeHandleWidth = 4.0f;        ///< Width of column resize handle.
	float fontSize = 13.0f;                ///< Cell text font size.
	float headerFontSize = 14.0f;          ///< Header text font size.
	int maxVisibleRows = 10;               ///< Max visible rows before scrolling.
	bool scrollable = true;                ///< Whether the table scrolls.
};

/// @brief Data table widget with sortable columns, selectable rows, and virtual scrolling.
///
/// Supports column sorting, row selection, column resizing, and efficient
/// virtual scrolling for large datasets.
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

	// Column resize state.
	int m_resizingColumn = -1;
	float m_resizeStartX = 0.0f;
	float m_resizeStartWidth = 0.0f;

	std::function<void(int)> m_onRowSelected;
	std::function<void(int, bool)> m_onColumnSorted;
	std::function<void(int, int)> m_onCellClicked;

public:
	/// @brief Construct a table from configuration.
	/// @param config Table configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the total row count.
	[[nodiscard]] std::size_t rowCount() const noexcept { return m_rows.size(); }

	/// @brief Get the column definitions.
	[[nodiscard]] const std::vector<UITableColumn>& columns() const noexcept { return m_columns; }

	/// @brief Get the row data.
	[[nodiscard]] const std::vector<UITableRow>& rows() const noexcept { return m_rows; }

	/// @brief Get the set of selected row indices.
	[[nodiscard]] std::vector<int> getSelectedRows() const
	{
		return {m_selectedIndices.begin(), m_selectedIndices.end()};
	}

	/// @brief Get the current sort column index (-1 if none).
	[[nodiscard]] int sortColumn() const noexcept { return m_sortColumn; }

	/// @brief Get the sort direction.
	[[nodiscard]] bool isSortAscending() const noexcept { return m_sortAscending; }

	/// @brief Get the visible row range [start, end).
	[[nodiscard]] std::pair<int, int> visibleRange() const noexcept
	{
		const int start = m_scrollOffset;
		const int end = std::min(m_scrollOffset + m_maxVisibleRows, static_cast<int>(m_rows.size()));
		return {start, end};
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the row-selected callback.
	void setOnRowSelected(std::function<void(int)> callback) { m_onRowSelected = std::move(callback); }

	/// @brief Set the column-sorted callback.
	void setOnColumnSorted(std::function<void(int, bool)> callback) { m_onColumnSorted = std::move(callback); }

	/// @brief Set the cell-clicked callback.
	void setOnCellClicked(std::function<void(int, int)> callback) { m_onCellClicked = std::move(callback); }

	// ── Data Manipulation ────────────────────────────────────

	/// @brief Set the complete row data.
	/// @param rows New row vector.
	void setData(std::vector<UITableRow> rows)
	{
		m_rows = std::move(rows);
		m_selectedIndices.clear();
		m_scrollOffset = 0;
		syncNodeState();
	}

	/// @brief Add a row to the end.
	/// @param row Row to add.
	void addRow(UITableRow row)
	{
		m_rows.push_back(std::move(row));
		syncNodeState();
	}

	/// @brief Remove a row by index.
	/// @param index Row index to remove.
	void removeRow(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_rows.size())) { return; }
		m_rows.erase(m_rows.begin() + index);
		m_selectedIndices.erase(index);

		// Adjust selected indices above removed row.
		std::set<int> adjusted;
		for (const int idx : m_selectedIndices)
		{
			adjusted.insert(idx > index ? idx - 1 : idx);
		}
		m_selectedIndices = std::move(adjusted);
		syncNodeState();
	}

	/// @brief Clear all rows.
	void clearData()
	{
		m_rows.clear();
		m_selectedIndices.clear();
		m_scrollOffset = 0;
		syncNodeState();
	}

	// ── Sorting ──────────────────────────────────────────────

	/// @brief Sort the table by a column.
	/// @param column Column index.
	/// @param ascending True for ascending, false for descending.
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

	// ── Selection ────────────────────────────────────────────

	/// @brief Select a row by index.
	/// @param index Row index.
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

	/// @brief Deselect all rows.
	void clearSelection()
	{
		for (auto& row : m_rows) { row.selected = false; }
		m_selectedIndices.clear();
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Handle a click on a specific cell.
	/// @param row Row index.
	/// @param col Column index.
	void clickCell(int row, int col)
	{
		if (row < 0 || row >= static_cast<int>(m_rows.size())) { return; }
		if (col < 0 || col >= static_cast<int>(m_columns.size())) { return; }

		selectRow(row);
		if (m_onCellClicked) { m_onCellClicked(row, col); }
	}

	/// @brief Handle a click on a column header (toggles sort).
	/// @param column Column index.
	void clickHeader(int column)
	{
		if (column < 0 || column >= static_cast<int>(m_columns.size())) { return; }
		if (!m_columns[static_cast<std::size_t>(column)].sortable) { return; }

		const bool ascending = (m_sortColumn == column) ? !m_sortAscending : true;
		sortBy(column, ascending);
	}

	/// @brief Set the hovered row index (-1 for none).
	/// @param index Row index.
	void setHoveredRow(int index)
	{
		m_hoveredRow = index;
		m_node->setProperty("hovered_row", std::to_string(m_hoveredRow));
	}

	/// @brief Begin column resize drag.
	/// @param column Column index.
	/// @param startX Starting pointer X position.
	void beginColumnResize(int column, float startX)
	{
		if (column < 0 || column >= static_cast<int>(m_columns.size())) { return; }
		if (!m_columns[static_cast<std::size_t>(column)].resizable) { return; }

		m_resizingColumn = column;
		m_resizeStartX = startX;
		m_resizeStartWidth = m_columns[static_cast<std::size_t>(column)].width;
	}

	/// @brief Update column resize drag.
	/// @param currentX Current pointer X position.
	void updateColumnResize(float currentX)
	{
		if (m_resizingColumn < 0) { return; }

		const auto col = static_cast<std::size_t>(m_resizingColumn);
		const float delta = currentX - m_resizeStartX;
		const float newWidth = std::max(m_columns[col].minWidth, m_resizeStartWidth + delta);
		m_columns[col].width = newWidth;
		syncNodeState();
	}

	/// @brief End column resize drag.
	void endColumnResize()
	{
		m_resizingColumn = -1;
	}

	/// @brief Scroll by a number of rows.
	/// @param delta Positive = scroll down, negative = scroll up.
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_rows.size()) - m_maxVisibleRows);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		syncNodeState();
	}

private:
	/// @brief Convert alignment to string.
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

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setProperty("row_count", std::to_string(m_rows.size()));
		m_node->setProperty("column_count", std::to_string(m_columns.size()));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("sort_column", std::to_string(m_sortColumn));
		m_node->setProperty("sort_ascending", m_sortAscending ? "true" : "false");
		m_node->setProperty("hovered_row", std::to_string(m_hoveredRow));

		// Encode column info.
		for (std::size_t c = 0; c < m_columns.size(); ++c)
		{
			const auto prefix = "col_" + std::to_string(c) + "_";
			m_node->setProperty(prefix + "header", m_columns[c].header);
			m_node->setProperty(prefix + "key", m_columns[c].key);
			m_node->setProperty(prefix + "width", std::to_string(m_columns[c].width));
			m_node->setProperty(prefix + "align", alignToString(m_columns[c].align));
			m_node->setProperty(prefix + "header_image", m_columns[c].headerImageKey);
		}

		// Encode selected indices.
		std::string selStr;
		for (const auto idx : m_selectedIndices)
		{
			if (!selStr.empty()) { selStr += ","; }
			selStr += std::to_string(idx);
		}
		m_node->setProperty("selected", selStr);

		// Encode visible rows.
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
