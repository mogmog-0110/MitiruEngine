#pragma once

/// @file UITreeView.hpp
/// @brief 展開可能な階層 tree view widget。keyboard navigation 対応。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief tree view 階層の node データ。
struct UITreeNode
{
	std::string label;                         ///< 表示 label。
	std::string iconImageKey;                  ///< 折りたたみ時 / leaf の icon。
	std::string expandedIconImageKey;          ///< 展開時の icon。
	std::vector<UITreeNode> children;          ///< 子 node。
	bool expanded = false;                     ///< 子を表示中か。
	bool selected = false;                     ///< 選択状態。
	bool enabled = true;                       ///< 操作可能か。
	std::any data;                             ///< 任意のユーザーデータ。
};

/// @brief UITreeView 生成用の設定。
struct UITreeViewConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float indentWidth = 20.0f;                 ///< indent 1 段あたりの px。
	float nodeHeight = 24.0f;                  ///< tree node 1 行の高さ。
	std::string expandIconImageKey;            ///< 展開インジケータの画像。
	std::string collapseIconImageKey;          ///< 折りたたみインジケータの画像。
	std::string nodeBackgroundImageKey;        ///< 既定の node 背景画像。
	std::string nodeHoverImageKey;             ///< hover 時の node 背景画像。
	std::string nodeSelectedImageKey;          ///< 選択時の node 背景画像。
	std::string connectionLineColor;           ///< 接続線の色文字列。
	float connectionLineWidth = 1.0f;          ///< 接続線の幅。
	float fontSize = 13.0f;                    ///< node label の font size。
	bool multiSelect = false;                  ///< 複数選択を許可する。
};

/// @brief tree 内の node への path。child index の列で表す。
using TreeNodePath = std::vector<int>;

/// @brief 展開可能な階層 tree view widget。
///
/// expand/collapse、keyboard navigation、単一 / 複数選択、
/// 親子間の接続線をサポートする。
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

	// navigation 用に平坦化した visible リスト。
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
	/// @brief 設定から tree view を構築する。
	/// @param config tree view 設定。
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

	/// @brief 基となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief root node 群を取得する。
	[[nodiscard]] const std::vector<UITreeNode>& roots() const noexcept { return m_roots; }

	/// @brief 平坦化した visible リストを取得する (描画用)。
	[[nodiscard]] const std::vector<FlatEntry>& flatList() const noexcept { return m_flatList; }

	/// @brief 選択中の node path 群を取得する。
	[[nodiscard]] const std::vector<TreeNodePath>& getSelectedNodes() const noexcept { return m_selectedPaths; }

	/// @brief focus 中の path を取得する。
	[[nodiscard]] const TreeNodePath& focusedPath() const noexcept { return m_focusedPath; }

	// ── Configuration ────────────────────────────────────────

	/// @brief node 選択時の callback を設定する。
	void setOnNodeSelected(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeSelected = std::move(callback);
	}

	/// @brief node 展開時の callback を設定する。
	void setOnNodeExpanded(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeExpanded = std::move(callback);
	}

	/// @brief node 折りたたみ時の callback を設定する。
	void setOnNodeCollapsed(std::function<void(const TreeNodePath&)> callback)
	{
		m_onNodeCollapsed = std::move(callback);
	}

	// ── Data ─────────────────────────────────────────────────

	/// @brief tree の root node 群を設定する。
	/// @param roots root node の vector。
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

	/// @brief 指定 path の node を展開する。
	/// @param path node への path。
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

	/// @brief 指定 path の node を折りたたむ。
	/// @param path node への path。
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

	/// @brief tree 内の全 node を展開する。
	void expandAll()
	{
		expandAllRecursive(m_roots);
		rebuildFlatList();
		syncNodeState();
	}

	/// @brief tree 内の全 node を折りたたむ。
	void collapseAll()
	{
		collapseAllRecursive(m_roots);
		rebuildFlatList();
		syncNodeState();
	}

	/// @brief node の展開 / 折りたたみを切り替える。
	/// @param path node への path。
	void toggle(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode || treeNode->children.empty()) { return; }

		if (treeNode->expanded) { collapse(path); }
		else { expand(path); }
	}

	// ── Selection ────────────────────────────────────────────

	/// @brief 指定 path の node を選択する。
	/// @param path node への path。
	void selectNode(const TreeNodePath& path)
	{
		UITreeNode* treeNode = findNode(path);
		if (!treeNode || !treeNode->enabled) { return; }

		if (!m_multiSelect)
		{
			// 以前の選択をクリアする。
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

	/// @brief 全選択をクリアする。
	void clearSelection()
	{
		clearSelectionRecursive(m_roots);
		m_selectedPaths.clear();
		rebuildFlatList();
		syncNodeState();
	}

	// ── Keyboard Navigation ──────────────────────────────────

	/// @brief focus を上へ移動する (Up key)。
	void focusPrevious()
	{
		if (m_flatList.empty()) { return; }
		m_focusedFlatIndex = (m_focusedFlatIndex <= 0)
			? static_cast<int>(m_flatList.size()) - 1
			: m_focusedFlatIndex - 1;
		m_focusedPath = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path;
		syncNodeState();
	}

	/// @brief focus を下へ移動する (Down key)。
	void focusNext()
	{
		if (m_flatList.empty()) { return; }
		m_focusedFlatIndex = (m_focusedFlatIndex >= static_cast<int>(m_flatList.size()) - 1)
			? 0
			: m_focusedFlatIndex + 1;
		m_focusedPath = m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path;
		syncNodeState();
	}

	/// @brief focus 中の node を折りたたむ、または親へ移動する (Left key)。
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
			// 親へ移動する。
			TreeNodePath parentPath(entry.path.begin(), entry.path.end() - 1);
			m_focusedPath = parentPath;
			updateFocusedFlatIndex();
			syncNodeState();
		}
	}

	/// @brief focus 中の node を展開する、または最初の child へ移動する (Right key)。
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
			// 最初の child へ移動する。
			focusNext();
		}
	}

	/// @brief focus 中の node を選択する (Space key)。
	void confirmFocused()
	{
		if (m_focusedFlatIndex >= 0 && m_focusedFlatIndex < static_cast<int>(m_flatList.size()))
		{
			selectNode(m_flatList[static_cast<std::size_t>(m_focusedFlatIndex)].path);
		}
	}

private:
	/// @brief path から node を検索する。見つからなければ nullptr を返す。
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

	/// @brief tree から平坦化した visible リストを再構築する。
	void rebuildFlatList()
	{
		m_flatList.clear();
		TreeNodePath path;
		flattenRecursive(m_roots, path, 0);
		updateFocusedFlatIndex();
	}

	/// @brief visible な node を再帰的に平坦化する。
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

	/// @brief m_focusedPath から focus 中の flat index を更新する。
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

	/// @brief 全 node を再帰的に展開する。
	static void expandAllRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			if (!treeNode.children.empty()) { treeNode.expanded = true; }
			expandAllRecursive(treeNode.children);
		}
	}

	/// @brief 全 node を再帰的に折りたたむ。
	static void collapseAllRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			treeNode.expanded = false;
			collapseAllRecursive(treeNode.children);
		}
	}

	/// @brief 全 node の選択を再帰的にクリアする。
	static void clearSelectionRecursive(std::vector<UITreeNode>& nodes)
	{
		for (auto& treeNode : nodes)
		{
			treeNode.selected = false;
			clearSelectionRecursive(treeNode.children);
		}
	}

	/// @brief path を文字列に encode する (例: "0.2.1")。
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

	/// @brief 状態を UINode へ同期する。
	void syncNodeState()
	{
		m_node->setProperty("node_count", std::to_string(m_flatList.size()));
		m_node->setProperty("focused_index", std::to_string(m_focusedFlatIndex));
		m_node->setProperty("focused_path", pathToString(m_focusedPath));

		// 選択中の path を encode する。
		std::string selStr;
		for (const auto& path : m_selectedPaths)
		{
			if (!selStr.empty()) { selStr += ";"; }
			selStr += pathToString(path);
		}
		m_node->setProperty("selected_paths", selStr);

		// renderer 向けに visible な flat entry を encode する。
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
