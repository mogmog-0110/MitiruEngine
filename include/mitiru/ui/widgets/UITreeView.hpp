#pragma once

/// @file UITreeView.hpp
/// @brief Expandable hierarchical tree view widget with keyboard navigation.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Node data for a tree view hierarchy.
struct UITreeNode
{
	std::string label;                         ///< Display label.
	std::string iconImageKey;                  ///< Icon when collapsed or leaf.
	std::string expandedIconImageKey;          ///< Icon when expanded.
	std::vector<UITreeNode> children;          ///< Child nodes.
	bool expanded = false;                     ///< Whether children are visible.
	bool selected = false;                     ///< Selection state.
	bool enabled = true;                       ///< Whether the node is interactive.
	std::any data;                             ///< Arbitrary user data.
};

/// @brief Configuration for creating a UITreeView.
struct UITreeViewConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float indentWidth = 20.0f;                 ///< Pixels per indent level.
	float nodeHeight = 24.0f;                  ///< Height of each tree node row.
	std::string expandIconImageKey;            ///< Image for expand indicator.
	std::string collapseIconImageKey;          ///< Image for collapse indicator.
	std::string nodeBackgroundImageKey;        ///< Default node background image.
	std::string nodeHoverImageKey;             ///< Hovered node background image.
	std::string nodeSelectedImageKey;          ///< Selected node background image.
	std::string connectionLineColor;           ///< Color string for connection lines.
	float connectionLineWidth = 1.0f;          ///< Width of connection lines.
	float fontSize = 13.0f;                    ///< Node label font size.
	bool multiSelect = false;                  ///< Allow multiple selection.
};

/// @brief Path to a node in the tree, as a sequence of child indices.
using TreeNodePath = std::vector<int>;

/// @brief Expandable hierarchical tree view widget.
///
/// Supports expand/collapse, keyboard navigation, single/multi selection,
/// and connection lines between parent and child nodes.
///
/// @code
///   UITreeViewConfig cfg;
///   cfg.id = 130;
///   cfg.indentWidth = 20.0f;
///   UITreeView tree(cfg);
///
///   UITreeNode root;
///   root.label = "Root";
///   root.children = {{"Child A"}, {"Child B"}};
///   tree.setRoot({root});
///
///   tree.setOnNodeSelected([](const TreeNodePath& path) { /* handle */ });
///   tree.expandAll();
/// @endcode
class UITreeView
{
	std::shared_ptr<UINode> m_node;
	std::vector<UITreeNode> m_roots;
	std::vector<TreeNodePath> m_selectedPaths;
	TreeNodePath m_focusedPath;
	float m_indentWidth;
	float m_nodeHeight;
	bool m_multiSelect;

	// Flattened visible list for navigation.
	struct FlatEntry
	{
		TreeNodePath path;
		int depth = 0;
		bool hasChildren = false;
		bool expanded = false;
		bool selected = false;
		bool enabled = true;
		std::string label;
		std::string iconImageKey;
	};
	std::vector<FlatEntry> m_flatList;
	int m_focusedFlatIndex = -1;

	std::function<void(const TreeNodePath&)> m_onNodeSelected;
	std::function<void(const TreeNodePath&)> m_onNodeExpanded;
	std::function<void(const TreeNodePath&)> m_onNodeCollapsed;

public:
	/// @brief Construct a tree view from configuration.
	/// @param config Tree view configuration.
	explicit UITreeView(const UITreeViewConfig& config)
		: m_indentWidth(config.indentWidth)
		, m_nodeHeight(config.nodeHeight)
		, m_multiSelect(config.multiSelect)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.properties["widget_type"] = "tree_view";
		data.properties["indent_width"] = std::to_string(config.indentWidth);
		data.properties["node_height"] = std::to_string(config.nodeHeight);
		data.properties["expand_icon_image"] = config.expandIconImageKey;
		data.properties["collapse_icon_image"] = config.collapseIconImageKey;
		data.properties["node_bg_image"] = config.nodeBackgroundImageKey;
		data.properties["node_hover_image"] = config.nodeHoverImageKey;
		data.properties["node_selected_image"] = config.nodeSelectedImageKey;
		data.properties["connection_line_color"] = config.connectionLineColor;
		data.properties["connection_line_width"] = std::to_string(config.connectionLineWidth);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["multi_select"] = config.multiSelect ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the root nodes.
	[[nodiscard]] const std::vector<UITreeNode>& roots() const noexcept { return m_roots; }

	/// @brief Get the flattened visible list (for rendering).
	[[nodiscard]] const std::vector<FlatEntry>& flatList() const noexcept { return m_flatList; }

	/// @brief Get the selected node paths.
	[[nodiscard]] const std::vector<TreeNodePath>& getSelectedNodes() const noexcept { return m_selectedPaths; }

	/// @brief Get the focused path.
	[[nodiscard]] const TreeNodePath& focusedPath() const noexcept { return m_focusedPath; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the node-selected callback.
	void setOnNodeSelected(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeSelected = std::move(callback);
	}

	/// @brief Set the node-expanded callback.
	void setOnNodeExpanded(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeExpanded = std::move(callback);
	}

	/// @brief Set the node-collapsed callback.
	void setOnNodeCollapsed(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeCollapsed = std::move(callback);
	}

	// ── Data ─────────────────────────────────────────────────

	/// @brief Set the root nodes of the tree.
	/// @param roots Root node vector.
	void setRoot(std::vector<UITreeNode> roots)
	{
		m_roots = std::move(roots);
		m_selectedPaths.clear();
		m_focusedPath.clear();
		m_focusedFlatIndex = -1;
		rebuildFlatList();
		syncNodeState();
	}

	// ── Expand/Collapse ──────────────────────────────────────

	/// @brief Expand a node at the given path.
	/// @param path Path to the node.
	void expand(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode || treeNode->children.empty()) { return; }
		if (treeNode->expanded) { return; }

		treeNode->expanded = true;
		rebuildFlatList();
		syncNodeState();
		if (m_onNodeExpanded) { m_onNodeExpanded(path); }
	}

	/// @brief Collapse a node at the given path.
	/// @param path Path to the node.
	void collapse(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode) { return; }
		if (!treeNode->expanded) { return; }

		treeNode->expanded = false;
		rebuildFlatList();
		syncNodeState();
		if (m_onNodeCollapsed) { m_onNodeCollapsed(path); }
	}

	/// @brief Expand all nodes in the tree.
	void expandAll()
	{
		expandAllRecursive(m_roots);
		rebuildFlatList();
		syncNodeState();
	}

	/// @brief Collapse all nodes in the tree.
	void collapseAll()
	{
		collapseAllRecursive(m_roots);
		rebuildFlatList();
		syncNodeState();
	}

	/// @brief Toggle expand/collapse of a node.
	/// @param path Path to the node.
	void toggle(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode || treeNode->children.empty()) { return; }

		if (treeNode->expanded) { collapse(path); }
		else { expand(path); }
	}

	// ── Selection ────────────────────────────────────────────

	/// @brief Select a node at the given path.
	/// @param path Path to the node.
	void selectNode(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode || !treeNode->enabled) { return; }

		if (!m_multiSelect)
		{
			// Clear previous selections.
			clearSelectionRecursive(m_roots);
			m_selectedPaths.clear();
		}

		treeNode->selected = true;
		m_selectedPaths.push_back(path);
		m_focusedPath = path;
		updateFocusedFlatIndex();
		rebuildFlatList();
		syncNodeState();
		if (m_onNodeSelected) { m_onNodeSelected(path); }
	}

	/// @brief Clear all selections.
	void clearSelection()
	{
		clearSelectionRecursive(m_roots);
		m_selectedPaths.clear();
		rebuildFlatList();
		syncNodeState();
	}

	// ── Keyboard Navigation ──────────────────────────────────

	/// @brief Move focus up (Up key).
	void focusPrevious()
	{
		if (m_flatList.empty()) { return; }
		m_focusedFlatIndex = (m_focusedFlatIndex <= 0)
			? static_cast<int>(m_flatList.size()) - 1
			: m_focusedFlatIndex - 1;
		m_focusedPath = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path;
		syncNodeState();
	}

	/// @brief Move focus down (Down key).
	void focusNext()
	{
		if (m_flatList.empty()) { return; }
		m_focusedFlatIndex = (m_focusedFlatIndex >= static_cast<int>(m_flatList.size()) - 1)
			? 0
			: m_focusedFlatIndex + 1;
		m_focusedPath = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path;
		syncNodeState();
	}

	/// @brief Collapse focused node or move to parent (Left key).
	void focusLeft()
	{
		if (m_focusedFlatIndex < 0 || m_focusedFlatIndex >= static_cast<int>(m_flatList.size())) { return; }

		const auto& entry = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)];
		if (entry.hasChildren && entry.expanded)
		{
			collapse(entry.path);
		}
		else if (entry.path.size() > 1)
		{
			// Move to parent.
			TreeNodePath parentPath(entry.path.begin(), entry.path.end() - 1);
			m_focusedPath = parentPath;
			updateFocusedFlatIndex();
			syncNodeState();
		}
	}

	/// @brief Expand focused node or move to first child (Right key).
	void focusRight()
	{
		if (m_focusedFlatIndex < 0 || m_focusedFlatIndex >= static_cast<int>(m_flatList.size())) { return; }

		const auto& entry = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)];
		if (entry.hasChildren && !entry.expanded)
		{
			expand(entry.path);
		}
		else if (entry.hasChildren && entry.expanded)
		{
			// Move to first child.
			focusNext();
		}
	}

	/// @brief Select the focused node (Space key).
	void confirmFocused()
	{
		if (m_focusedFlatIndex >= 0 && m_focusedFlatIndex < static_cast<int>(m_flatList.size()))
		{
			selectNode(m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path);
		}
	}

private:
	/// @brief Find a node by path. Returns nullptr if not found.
	[[nodiscard]] UITreeNode* findNode(const TreeNodePath& path)
	{
		if (path.empty()) { return nullptr; }

		std::vector<UITreeNode>* currentLevel = &m_roots;
		UITreeNode* result = nullptr;

		for (const int index : path)
		{
			if (index < 0 || index >= static_cast<int>(currentLevel->size())) { return nullptr; }
			result = &(*currentLevel)[static_cast<std::size_t>(index)];
			currentLevel = &result->children;
		}
		return result;
	}

	/// @brief Rebuild the flattened visible list from the tree.
	void rebuildFlatList()
	{
		m_flatList.clear();
		TreeNodePath path;
		flattenRecursive(m_roots, path, 0);
		updateFocusedFlatIndex();
	}

	/// @brief Recursively flatten visible nodes.
	void flattenRecursive(const std::vector<UITreeNode>& nodes, TreeNodePath& path, int depth)
	{
		for (std::size_t i = 0; i < nodes.size(); ++i)
		{
			path.push_back(static_cast<int>(i));

			const auto& treeNode = nodes[i];
			FlatEntry entry;
			entry.path = path;
			entry.depth = depth;
			entry.hasChildren = !treeNode.children.empty();
			entry.expanded = treeNode.expanded;
			entry.selected = treeNode.selected;
			entry.enabled = treeNode.enabled;
			entry.label = treeNode.label;
			entry.iconImageKey = treeNode.expanded ? treeNode.expandedIconImageKey : treeNode.iconImageKey;
			m_flatList.push_back(std::move(entry));

			if (treeNode.expanded && !treeNode.children.empty())
			{
				flattenRecursive(treeNode.children, path, depth + 1);
			}

			path.pop_back();
		}
	}

	/// @brief Update the focused flat index from m_focusedPath.
	void updateFocusedFlatIndex()
	{
		m_focusedFlatIndex = -1;
		for (std::size_t i = 0; i < m_flatList.size(); ++i)
		{
			if (m_flatList[i].path == m_focusedPath)
			{
				m_focusedFlatIndex = static_cast<int>(i);
				break;
			}
		}
	}

	/// @brief Recursively expand all nodes.
	static void expandAllRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			if (!treeNode.children.empty()) { treeNode.expanded = true; }
			expandAllRecursive(treeNode.children);
		}
	}

	/// @brief Recursively collapse all nodes.
	static void collapseAllRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			treeNode.expanded = false;
			collapseAllRecursive(treeNode.children);
		}
	}

	/// @brief Recursively clear selection on all nodes.
	static void clearSelectionRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			treeNode.selected = false;
			clearSelectionRecursive(treeNode.children);
		}
	}

	/// @brief Encode a path as a string (e.g. "0.2.1").
	[[nodiscard]] static std::string pathToString(const TreeNodePath& path)
	{
		std::string result;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			if (i > 0) { result += "."; }
			result += std::to_string(path[i]);
		}
		return result;
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setProperty("node_count", std::to_string(m_flatList.size()));
		m_node->setProperty("focused_index", std::to_string(m_focusedFlatIndex));
		m_node->setProperty("focused_path", pathToString(m_focusedPath));

		// Encode selected paths.
		std::string selStr;
		for (const auto& path : m_selectedPaths)
		{
			if (!selStr.empty()) { selStr += ";"; }
			selStr += pathToString(path);
		}
		m_node->setProperty("selected_paths", selStr);

		// Encode visible flat entries for renderer.
		for (std::size_t i = 0; i < m_flatList.size(); ++i)
		{
			const auto prefix = "node_" + std::to_string(i) + "_";
			const auto& entry = m_flatList[i];
			m_node->setProperty(prefix + "label", entry.label);
			m_node->setProperty(prefix + "depth", std::to_string(entry.depth));
			m_node->setProperty(prefix + "has_children", entry.hasChildren ? "true" : "false");
			m_node->setProperty(prefix + "expanded", entry.expanded ? "true" : "false");
			m_node->setProperty(prefix + "selected", entry.selected ? "true" : "false");
			m_node->setProperty(prefix + "enabled", entry.enabled ? "true" : "false");
			m_node->setProperty(prefix + "icon_image", entry.iconImageKey);
			m_node->setProperty(prefix + "path", pathToString(entry.path));
		}
	}
};

} // namespace mitiru::ui
