#pragma once

/// @file UIInventoryGrid.hpp
/// @brief Drag-and-drop item grid widget for inventory systems.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Item rarity tier (affects visual border styling).
enum class ItemRarity : std::uint8_t
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary
};

/// @brief Grid coordinate (column, row).
struct GridPos
{
	std::uint32_t col = 0;
	std::uint32_t row = 0;

	[[nodiscard]] bool operator==(const GridPos& other) const noexcept
	{
		return col == other.col && row == other.row;
	}

	[[nodiscard]] bool operator!=(const GridPos& other) const noexcept
	{
		return !(*this == other);
	}
};

/// @brief Data for an item that can be placed in the inventory grid.
struct UIInventoryItem
{
	std::uint32_t id = 0;
	std::string iconImageKey;
	std::string name;
	std::string description;
	std::uint32_t stackCount = 1;
	std::uint32_t maxStack = 1;
	ItemRarity rarity = ItemRarity::Common;
};

/// @brief Configuration for creating a UIInventoryGrid.
struct UIInventoryGridConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::uint32_t columns = 8;
	std::uint32_t rows = 4;
	float cellSize = 48.0f;
	float cellSpacing = 2.0f;
	float padding = 4.0f;
	std::string backgroundImageKey;
	std::string cellImageKey;
	std::string cellHoverImageKey;
	std::string cellSelectedImageKey;
	float dragGhostAlpha = 0.6f;
	float stackCountFontSize = 10.0f;
};

/// @brief Drag-and-drop inventory grid widget.
///
/// Manages a grid of item cells supporting drag & drop, stack merging,
/// hover tooltips, and multi-grid transfers.
///
/// @code
///   UIInventoryGridConfig cfg;
///   cfg.id = 300;
///   cfg.columns = 8;
///   cfg.rows = 4;
///   UIInventoryGrid grid(cfg);
///
///   UIInventoryItem sword;
///   sword.id = 1;
///   sword.iconImageKey = "icon_sword";
///   sword.name = "Iron Sword";
///   grid.setItem(0, 0, sword);
///
///   grid.setOnItemMoved([](GridPos from, GridPos to) { /* update logic */ });
/// @endcode
class UIInventoryGrid
{
	std::shared_ptr<UINode> m_node;
	UIInventoryGridConfig m_config;

	/// @brief Cells stored in row-major order.
	std::vector<std::optional<UIInventoryItem>> m_cells;

	std::int32_t m_hoveredCol = -1;
	std::int32_t m_hoveredRow = -1;
	std::int32_t m_selectedCol = -1;
	std::int32_t m_selectedRow = -1;

	// Drag state
	bool m_dragging = false;
	GridPos m_dragOrigin{};
	float m_dragGhostX = 0.0f;
	float m_dragGhostY = 0.0f;

	std::function<void(GridPos, GridPos)> m_onItemMoved;
	std::function<void(const UIInventoryItem&, GridPos)> m_onItemDropped;
	std::function<void(const UIInventoryItem&)> m_onItemRightClicked;
	std::function<void(const UIInventoryItem&, GridPos)> m_onItemHovered;

public:
	/// @brief Construct an inventory grid from configuration.
	/// @param config Grid configuration.
	explicit UIInventoryGrid(const UIInventoryGridConfig& config)
		: m_config(config)
		, m_cells(static_cast<std::size_t>(config.columns) * config.rows)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Inventory;
		data.bounds = sgc::Rectf(0.0f, 0.0f, totalWidth(), totalHeight());
		data.properties["widget_type"] = "inventory_grid";
		data.properties["columns"] = std::to_string(config.columns);
		data.properties["rows"] = std::to_string(config.rows);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["cell_image"] = config.cellImageKey;
		data.properties["cell_hover_image"] = config.cellHoverImageKey;
		data.properties["cell_selected_image"] = config.cellSelectedImageKey;
		data.properties["drag_ghost_alpha"] = std::to_string(config.dragGhostAlpha);
		data.properties["stack_font_size"] = std::to_string(config.stackCountFontSize);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get column count.
	[[nodiscard]] std::uint32_t columns() const noexcept { return m_config.columns; }

	/// @brief Get row count.
	[[nodiscard]] std::uint32_t rows() const noexcept { return m_config.rows; }

	/// @brief Compute total width.
	[[nodiscard]] float totalWidth() const noexcept
	{
		const auto n = static_cast<float>(m_config.columns);
		return 2.0f * m_config.padding + n * m_config.cellSize + (n - 1.0f) * m_config.cellSpacing;
	}

	/// @brief Compute total height.
	[[nodiscard]] float totalHeight() const noexcept
	{
		const auto n = static_cast<float>(m_config.rows);
		return 2.0f * m_config.padding + n * m_config.cellSize + (n - 1.0f) * m_config.cellSpacing;
	}

	/// @brief Get the bounds of a cell in local grid space.
	[[nodiscard]] sgc::Rectf cellBounds(std::uint32_t col, std::uint32_t row) const noexcept
	{
		const float x = m_config.padding + static_cast<float>(col) * (m_config.cellSize + m_config.cellSpacing);
		const float y = m_config.padding + static_cast<float>(row) * (m_config.cellSize + m_config.cellSpacing);
		return sgc::Rectf(x, y, m_config.cellSize, m_config.cellSize);
	}

	/// @brief Check if a grid position is valid.
	[[nodiscard]] bool isValidPos(std::uint32_t col, std::uint32_t row) const noexcept
	{
		return col < m_config.columns && row < m_config.rows;
	}

	/// @brief Check if currently dragging an item.
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief Get the drag origin position.
	[[nodiscard]] GridPos dragOrigin() const noexcept { return m_dragOrigin; }

	/// @brief Get the drag ghost screen position.
	[[nodiscard]] float dragGhostX() const noexcept { return m_dragGhostX; }
	[[nodiscard]] float dragGhostY() const noexcept { return m_dragGhostY; }

	// ── Item management ──────────────────────────────────────

	/// @brief Get item at position, or nullopt if empty.
	[[nodiscard]] std::optional<UIInventoryItem> getItem(std::uint32_t col, std::uint32_t row) const
	{
		if (!isValidPos(col, row)) { return std::nullopt; }
		return m_cells[cellIndex(col, row)];
	}

	/// @brief Set an item at the given position.
	void setItem(std::uint32_t col, std::uint32_t row, const UIInventoryItem& item)
	{
		if (!isValidPos(col, row)) { return; }
		m_cells[cellIndex(col, row)] = item;
		syncCellProperty(col, row);
	}

	/// @brief Remove and return the item at the given position.
	std::optional<UIInventoryItem> removeItem(std::uint32_t col, std::uint32_t row)
	{
		if (!isValidPos(col, row)) { return std::nullopt; }
		auto& cell = m_cells[cellIndex(col, row)];
		auto result = std::move(cell);
		cell.reset();
		syncCellProperty(col, row);
		return result;
	}

	/// @brief Swap items between two grid positions.
	void swapItems(GridPos from, GridPos to)
	{
		if (!isValidPos(from.col, from.row) || !isValidPos(to.col, to.row)) { return; }
		std::swap(m_cells[cellIndex(from.col, from.row)], m_cells[cellIndex(to.col, to.row)]);
		syncCellProperty(from.col, from.row);
		syncCellProperty(to.col, to.row);
	}

	/// @brief Move an item from one position to another, with stack merging support.
	/// @return True if the move (or merge) was performed.
	bool moveItem(GridPos from, GridPos to)
	{
		if (!isValidPos(from.col, from.row) || !isValidPos(to.col, to.row)) { return false; }
		if (from == to) { return false; }

		auto& srcCell = m_cells[cellIndex(from.col, from.row)];
		auto& dstCell = m_cells[cellIndex(to.col, to.row)];

		if (!srcCell.has_value()) { return false; }

		// Stack merging: same item id and stackable
		if (dstCell.has_value() && dstCell->id == srcCell->id && dstCell->maxStack > 1)
		{
			const std::uint32_t space = dstCell->maxStack - dstCell->stackCount;
			if (space > 0)
			{
				const std::uint32_t transfer = std::min(space, srcCell->stackCount);
				dstCell->stackCount += transfer;
				srcCell->stackCount -= transfer;
				if (srcCell->stackCount == 0)
				{
					srcCell.reset();
				}
				syncCellProperty(from.col, from.row);
				syncCellProperty(to.col, to.row);
				if (m_onItemMoved) { m_onItemMoved(from, to); }
				return true;
			}
			// No space for merge — swap instead
			swapItems(from, to);
			if (m_onItemMoved) { m_onItemMoved(from, to); }
			return true;
		}

		if (dstCell.has_value())
		{
			// Different items — swap
			swapItems(from, to);
		}
		else
		{
			// Empty destination — move
			dstCell = std::move(srcCell);
			srcCell.reset();
			syncCellProperty(from.col, from.row);
			syncCellProperty(to.col, to.row);
		}

		if (m_onItemMoved) { m_onItemMoved(from, to); }
		return true;
	}

	/// @brief Accept an item dropped from an external source (e.g. another grid).
	/// @param item The item being dropped.
	/// @param col Destination column.
	/// @param row Destination row.
	/// @return True if the item was placed.
	bool acceptExternalDrop(const UIInventoryItem& item, std::uint32_t col, std::uint32_t row)
	{
		if (!isValidPos(col, row)) { return false; }
		auto& cell = m_cells[cellIndex(col, row)];

		// Stack merge with existing
		if (cell.has_value() && cell->id == item.id && cell->maxStack > 1)
		{
			const std::uint32_t space = cell->maxStack - cell->stackCount;
			if (space > 0)
			{
				cell->stackCount += std::min(space, item.stackCount);
				syncCellProperty(col, row);
				if (m_onItemDropped) { m_onItemDropped(item, {col, row}); }
				return true;
			}
			return false;
		}

		if (cell.has_value()) { return false; } // occupied

		cell = item;
		syncCellProperty(col, row);
		if (m_onItemDropped) { m_onItemDropped(item, {col, row}); }
		return true;
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback for when an item is moved within this grid.
	void setOnItemMoved(std::function<void(GridPos, GridPos)> callback)
	{
		m_onItemMoved = std::move(callback);
	}

	/// @brief Set callback for when an item is dropped onto this grid.
	void setOnItemDropped(std::function<void(const UIInventoryItem&, GridPos)> callback)
	{
		m_onItemDropped = std::move(callback);
	}

	/// @brief Set callback for when an item is right-clicked.
	void setOnItemRightClicked(std::function<void(const UIInventoryItem&)> callback)
	{
		m_onItemRightClicked = std::move(callback);
	}

	/// @brief Set callback for when an item is hovered (for tooltip).
	void setOnItemHovered(std::function<void(const UIInventoryItem&, GridPos)> callback)
	{
		m_onItemHovered = std::move(callback);
	}

	// ── Interaction (called by event system) ─────────────────

	/// @brief Called when pointer moves over the grid.
	/// @param localX X in local grid space.
	/// @param localY Y in local grid space.
	void onPointerMove(float localX, float localY)
	{
		const auto [col, row] = hitTestCell(localX, localY);
		m_hoveredCol = col;
		m_hoveredRow = row;

		if (m_dragging)
		{
			m_dragGhostX = localX;
			m_dragGhostY = localY;
		}

		// Tooltip notification
		if (col >= 0 && row >= 0 && m_onItemHovered)
		{
			const auto& cell = m_cells[cellIndex(
				static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row))];
			if (cell.has_value())
			{
				m_onItemHovered(*cell, {static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row)});
			}
		}

		syncNodeState();
	}

	/// @brief Called when pointer leaves the grid area.
	void onPointerLeave()
	{
		m_hoveredCol = -1;
		m_hoveredRow = -1;
		syncNodeState();
	}

	/// @brief Called when pointer is pressed down on the grid.
	/// @param localX X in local grid space.
	/// @param localY Y in local grid space.
	void onPointerDown(float localX, float localY)
	{
		const auto [col, row] = hitTestCell(localX, localY);
		if (col < 0 || row < 0) { return; }

		const auto uc = static_cast<std::uint32_t>(col);
		const auto ur = static_cast<std::uint32_t>(row);

		if (m_cells[cellIndex(uc, ur)].has_value())
		{
			m_dragging = true;
			m_dragOrigin = {uc, ur};
			m_dragGhostX = localX;
			m_dragGhostY = localY;
			m_selectedCol = col;
			m_selectedRow = row;
		}
		syncNodeState();
	}

	/// @brief Called when pointer is released.
	/// @param localX X in local grid space.
	/// @param localY Y in local grid space.
	void onPointerUp(float localX, float localY)
	{
		if (m_dragging)
		{
			const auto [col, row] = hitTestCell(localX, localY);
			if (col >= 0 && row >= 0)
			{
				const GridPos to{static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row)};
				moveItem(m_dragOrigin, to);
			}
			m_dragging = false;
			syncNodeState();
		}
	}

	/// @brief Called when pointer right-clicks on the grid.
	/// @param localX X in local grid space.
	/// @param localY Y in local grid space.
	void onRightClick(float localX, float localY)
	{
		const auto [col, row] = hitTestCell(localX, localY);
		if (col < 0 || row < 0) { return; }

		const auto& cell = m_cells[cellIndex(
			static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row))];
		if (cell.has_value() && m_onItemRightClicked)
		{
			m_onItemRightClicked(*cell);
		}
	}

	/// @brief Cancel an in-progress drag operation.
	void cancelDrag()
	{
		m_dragging = false;
		syncNodeState();
	}

	/// @brief Extract the item being dragged (for cross-grid transfer).
	/// @return The dragged item, or nullopt if not dragging.
	std::optional<UIInventoryItem> extractDraggedItem()
	{
		if (!m_dragging) { return std::nullopt; }
		m_dragging = false;
		auto item = removeItem(m_dragOrigin.col, m_dragOrigin.row);
		syncNodeState();
		return item;
	}

private:
	/// @brief Convert (col, row) to flat index.
	[[nodiscard]] std::size_t cellIndex(std::uint32_t col, std::uint32_t row) const noexcept
	{
		return static_cast<std::size_t>(row) * m_config.columns + col;
	}

	/// @brief Hit-test a local point against cells.
	/// @return (col, row) or (-1, -1) if no cell hit.
	struct CellHit { std::int32_t col; std::int32_t row; };
	[[nodiscard]] CellHit hitTestCell(float lx, float ly) const noexcept
	{
		for (std::uint32_t r = 0; r < m_config.rows; ++r)
		{
			for (std::uint32_t c = 0; c < m_config.columns; ++c)
			{
				const auto b = cellBounds(c, r);
				if (lx >= b.x && lx <= b.x + b.w && ly >= b.y && ly <= b.y + b.h)
				{
					return {static_cast<std::int32_t>(c), static_cast<std::int32_t>(r)};
				}
			}
		}
		return {-1, -1};
	}

	/// @brief Sync a single cell's data to node properties.
	void syncCellProperty(std::uint32_t col, std::uint32_t row)
	{
		const auto prefix = "cell_" + std::to_string(col) + "_" + std::to_string(row) + "_";
		const auto& cell = m_cells[cellIndex(col, row)];
		if (cell.has_value())
		{
			m_node->setProperty(prefix + "icon", cell->iconImageKey);
			m_node->setProperty(prefix + "name", cell->name);
			m_node->setProperty(prefix + "stack", std::to_string(cell->stackCount));
			m_node->setProperty(prefix + "rarity", rarityToString(cell->rarity));
			m_node->setProperty(prefix + "occupied", "true");
		}
		else
		{
			m_node->setProperty(prefix + "occupied", "false");
			m_node->setProperty(prefix + "icon", "");
			m_node->setProperty(prefix + "name", "");
			m_node->setProperty(prefix + "stack", "0");
			m_node->setProperty(prefix + "rarity", "common");
		}
	}

	/// @brief Convert rarity enum to string.
	[[nodiscard]] static const char* rarityToString(ItemRarity r) noexcept
	{
		switch (r)
		{
		case ItemRarity::Uncommon:  return "uncommon";
		case ItemRarity::Rare:      return "rare";
		case ItemRarity::Epic:      return "epic";
		case ItemRarity::Legendary: return "legendary";
		default:                    return "common";
		}
	}

	/// @brief Synchronize overall state to the UINode.
	void syncNodeState()
	{
		m_node->setProperty("hovered_col", std::to_string(m_hoveredCol));
		m_node->setProperty("hovered_row", std::to_string(m_hoveredRow));
		m_node->setProperty("selected_col", std::to_string(m_selectedCol));
		m_node->setProperty("selected_row", std::to_string(m_selectedRow));
		m_node->setProperty("dragging", m_dragging ? "true" : "false");
	}
};

} // namespace mitiru::ui
