#pragma once

/// @file UIInventoryGrid.hpp
/// @brief inventory system 向けの drag & drop item grid widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief item の rarity (枠の見た目スタイルに影響する)。
enum class ItemRarity : std::uint8_t
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary
};

/// @brief grid 座標 (column, row)。
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

/// @brief inventory grid に配置できる item のデータ。
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

/// @brief UIInventoryGrid 生成用の設定。
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

/// @brief drag & drop 対応の inventory grid widget。
///
/// item cell の grid を管理し、drag & drop、stack の統合、hover tooltip、
/// 複数 grid 間の移動に対応する。
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

	/// @brief row-major 順で格納された cell 群。
	std::vector<std::optional<UIInventoryItem>> m_cells;

	std::int32_t m_hoveredCol = -1;
	std::int32_t m_hoveredRow = -1;
	std::int32_t m_selectedCol = -1;
	std::int32_t m_selectedRow = -1;

	// drag 状態
	bool m_dragging = false;
	GridPos m_dragOrigin{};
	float m_dragGhostX = 0.0f;
	float m_dragGhostY = 0.0f;

	std::function<void(GridPos, GridPos)> m_onItemMoved;
	std::function<void(const UIInventoryItem&, GridPos)> m_onItemDropped;
	std::function<void(const UIInventoryItem&)> m_onItemRightClicked;
	std::function<void(const UIInventoryItem&, GridPos)> m_onItemHovered;

public:
	/// @brief 設定から inventory grid を構築する。
	/// @param config grid の設定。
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

	/// @brief 基底の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief column 数を取得する。
	[[nodiscard]] std::uint32_t columns() const noexcept { return m_config.columns; }

	/// @brief row 数を取得する。
	[[nodiscard]] std::uint32_t rows() const noexcept { return m_config.rows; }

	/// @brief 総幅を計算する。
	[[nodiscard]] float totalWidth() const noexcept
	{
		const auto n = static_cast<float>(m_config.columns);
		return 2.0f * m_config.padding + n * m_config.cellSize + (n - 1.0f) * m_config.cellSpacing;
	}

	/// @brief 総高さを計算する。
	[[nodiscard]] float totalHeight() const noexcept
	{
		const auto n = static_cast<float>(m_config.rows);
		return 2.0f * m_config.padding + n * m_config.cellSize + (n - 1.0f) * m_config.cellSpacing;
	}

	/// @brief ローカル grid 空間での cell の bounds を取得する。
	[[nodiscard]] sgc::Rectf cellBounds(std::uint32_t col, std::uint32_t row) const noexcept
	{
		const float x = m_config.padding + static_cast<float>(col) * (m_config.cellSize + m_config.cellSpacing);
		const float y = m_config.padding + static_cast<float>(row) * (m_config.cellSize + m_config.cellSpacing);
		return sgc::Rectf(x, y, m_config.cellSize, m_config.cellSize);
	}

	/// @brief grid 位置が有効か確認する。
	[[nodiscard]] bool isValidPos(std::uint32_t col, std::uint32_t row) const noexcept
	{
		return col < m_config.columns && row < m_config.rows;
	}

	/// @brief 現在 item を drag 中か確認する。
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief drag 開始位置を取得する。
	[[nodiscard]] GridPos dragOrigin() const noexcept { return m_dragOrigin; }

	/// @brief drag ghost の画面位置を取得する。
	[[nodiscard]] float dragGhostX() const noexcept { return m_dragGhostX; }
	[[nodiscard]] float dragGhostY() const noexcept { return m_dragGhostY; }

	// ── item 管理 ──────────────────────────────────────

	/// @brief 指定位置の item を取得する。空なら nullopt。
	[[nodiscard]] std::optional<UIInventoryItem> getItem(std::uint32_t col, std::uint32_t row) const
	{
		if (!isValidPos(col, row)) { return std::nullopt; }
		return m_cells[cellIndex(col, row)];
	}

	/// @brief 指定位置に item を設定する。
	void setItem(std::uint32_t col, std::uint32_t row, const UIInventoryItem& item)
	{
		if (!isValidPos(col, row)) { return; }
		m_cells[cellIndex(col, row)] = item;
		syncCellProperty(col, row);
	}

	/// @brief 指定位置の item を取り除いて返す。
	std::optional<UIInventoryItem> removeItem(std::uint32_t col, std::uint32_t row)
	{
		if (!isValidPos(col, row)) { return std::nullopt; }
		auto& cell = m_cells[cellIndex(col, row)];
		auto result = std::move(cell);
		cell.reset();
		syncCellProperty(col, row);
		return result;
	}

	/// @brief 2 つの grid 位置の item を入れ替える。
	void swapItems(GridPos from, GridPos to)
	{
		if (!isValidPos(from.col, from.row) || !isValidPos(to.col, to.row)) { return; }
		std::swap(m_cells[cellIndex(from.col, from.row)], m_cells[cellIndex(to.col, to.row)]);
		syncCellProperty(from.col, from.row);
		syncCellProperty(to.col, to.row);
	}

	/// @brief item をある位置から別の位置へ移動する (stack 統合に対応)。
	/// @return 移動 (or 統合) が行われたら true。
	bool moveItem(GridPos from, GridPos to)
	{
		if (!isValidPos(from.col, from.row) || !isValidPos(to.col, to.row)) { return false; }
		if (from == to) { return false; }

		auto& srcCell = m_cells[cellIndex(from.col, from.row)];
		auto& dstCell = m_cells[cellIndex(to.col, to.row)];

		if (!srcCell.has_value()) { return false; }

		// stack 統合: 同じ item id かつ stack 可能
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
			// 統合する空きが無い — 代わりに入れ替える
			swapItems(from, to);
			if (m_onItemMoved) { m_onItemMoved(from, to); }
			return true;
		}

		if (dstCell.has_value())
		{
			// 異なる item — 入れ替える
			swapItems(from, to);
		}
		else
		{
			// 移動先が空 — 移動する
			dstCell = std::move(srcCell);
			srcCell.reset();
			syncCellProperty(from.col, from.row);
			syncCellProperty(to.col, to.row);
		}

		if (m_onItemMoved) { m_onItemMoved(from, to); }
		return true;
	}

	/// @brief 外部ソース (例 別の grid) から drop された item を受け取る。
	/// @param item drop される item。
	/// @param col 移動先の column。
	/// @param row 移動先の row。
	/// @return item が配置されたら true。
	bool acceptExternalDrop(const UIInventoryItem& item, std::uint32_t col, std::uint32_t row)
	{
		if (!isValidPos(col, row)) { return false; }
		auto& cell = m_cells[cellIndex(col, row)];

		// 既存の item と stack 統合
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

		if (cell.has_value()) { return false; } // 占有済み

		cell = item;
		syncCellProperty(col, row);
		if (m_onItemDropped) { m_onItemDropped(item, {col, row}); }
		return true;
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief この grid 内で item が移動したときの callback を設定する。
	void setOnItemMoved(std::function<void(GridPos, GridPos)> callback)
	{
		m_onItemMoved = std::move(callback);
	}

	/// @brief この grid に item が drop されたときの callback を設定する。
	void setOnItemDropped(std::function<void(const UIInventoryItem&, GridPos)> callback)
	{
		m_onItemDropped = std::move(callback);
	}

	/// @brief item が右クリックされたときの callback を設定する。
	void setOnItemRightClicked(std::function<void(const UIInventoryItem&)> callback)
	{
		m_onItemRightClicked = std::move(callback);
	}

	/// @brief item が hover されたときの callback を設定する (tooltip 用)。
	void setOnItemHovered(std::function<void(const UIInventoryItem&, GridPos)> callback)
	{
		m_onItemHovered = std::move(callback);
	}

	// ── 操作 (event system から呼ばれる) ─────────────────

	/// @brief pointer が grid 上を移動したとき呼ばれる。
	/// @param localX ローカル grid 空間での X。
	/// @param localY ローカル grid 空間での Y。
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

		// tooltip 通知
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

	/// @brief pointer が grid 領域から離れたとき呼ばれる。
	void onPointerLeave()
	{
		m_hoveredCol = -1;
		m_hoveredRow = -1;
		syncNodeState();
	}

	/// @brief grid 上で pointer が押下されたとき呼ばれる。
	/// @param localX ローカル grid 空間での X。
	/// @param localY ローカル grid 空間での Y。
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

	/// @brief pointer が離されたとき呼ばれる。
	/// @param localX ローカル grid 空間での X。
	/// @param localY ローカル grid 空間での Y。
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

	/// @brief grid 上で pointer が右クリックされたとき呼ばれる。
	/// @param localX ローカル grid 空間での X。
	/// @param localY ローカル grid 空間での Y。
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

	/// @brief 進行中の drag 操作を取り消す。
	void cancelDrag()
	{
		m_dragging = false;
		syncNodeState();
	}

	/// @brief drag 中の item を取り出す (grid 間移動用)。
	/// @return drag 中の item、drag 中でなければ nullopt。
	std::optional<UIInventoryItem> extractDraggedItem()
	{
		if (!m_dragging) { return std::nullopt; }
		m_dragging = false;
		auto item = removeItem(m_dragOrigin.col, m_dragOrigin.row);
		syncNodeState();
		return item;
	}

private:
	/// @brief (col, row) を平坦な index に変換する。
	[[nodiscard]] std::size_t cellIndex(std::uint32_t col, std::uint32_t row) const noexcept
	{
		return static_cast<std::size_t>(row) * m_config.columns + col;
	}

	/// @brief ローカル点を cell に対して hit-test する。
	/// @return (col, row)、どの cell にも当たらなければ (-1, -1)。
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

	/// @brief 単一 cell のデータを node properties に同期する。
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

	/// @brief rarity enum を文字列に変換する。
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

	/// @brief 全体の状態を UINode に同期する。
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
