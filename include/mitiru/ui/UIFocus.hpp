#pragma once

/// @file UIFocus.hpp
/// @brief UIフォーカス管理
/// @details Tab/Shift+Tabによるフォーカス移動、フォーカス順序の自動構築、
///          フォーカス可能ロールの判定を提供する。

#include <vector>

#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

namespace detail
{

/// @brief ロールがデフォルトでフォーカス可能かどうか
[[nodiscard]] inline bool isRoleFocusable(UIRole role) noexcept
{
	switch (role)
	{
	case UIRole::Button:
	case UIRole::Slider:
	case UIRole::Toggle:
	case UIRole::TextInput:
	case UIRole::Dropdown:
	case UIRole::MenuItem:
		return true;
	default:
		return false;
	}
}

/// @brief フォーカス順序を深さ優先で構築する再帰実装
inline void buildFocusOrderImpl(UINode& node, std::vector<UINode*>& order)
{
	if (!node.visible())
	{
		return;
	}

	// このノードがフォーカス可能であれば追加
	if (node.focusable())
	{
		order.push_back(&node);
	}

	// 子ノードを順に走査（深さ優先）
	for (auto& child : node.children())
	{
		buildFocusOrderImpl(*child, order);
	}
}

} // namespace detail

/// @brief UIフォーカスマネージャ
/// @details フォーカスの設定・移動・クリアを管理する。
///          Tab/Shift+Tabでフォーカスを循環的に移動できる。
class UIFocusManager
{
	UINode* m_focusedNode = nullptr;

public:
	/// @brief フォーカスされたノードを設定する
	/// @param node フォーカスするノード（nullptrでクリア）
	void setFocusedNode(UINode* node) noexcept
	{
		m_focusedNode = node;
	}

	/// @brief フォーカスされたノードを取得する
	/// @return フォーカスされたノード（なければnullptr）
	[[nodiscard]] UINode* getFocusedNode() const noexcept
	{
		return m_focusedNode;
	}

	/// @brief フォーカスをクリアする
	void clearFocus() noexcept
	{
		m_focusedNode = nullptr;
	}

	/// @brief 指定ノードIDがフォーカスされているか
	/// @param nodeId 確認するノードID
	/// @return フォーカスされていればtrue
	[[nodiscard]] bool isFocused(UINodeId nodeId) const noexcept
	{
		return m_focusedNode != nullptr && m_focusedNode->id() == nodeId;
	}

	/// @brief フォーカス可能な全ノードをツリー順序で取得する
	/// @param root UIツリーのルートノード
	/// @return フォーカス可能なノードの深さ優先順リスト
	[[nodiscard]] std::vector<UINode*> buildFocusOrder(UINode& root) const
	{
		std::vector<UINode*> order;
		detail::buildFocusOrderImpl(root, order);
		return order;
	}

	/// @brief 次のフォーカス可能ノードにフォーカスを移動する（Tab）
	/// @param root UIツリーのルートノード
	void focusNext(UINode& root)
	{
		const auto order = buildFocusOrder(root);
		if (order.empty())
		{
			return;
		}

		if (m_focusedNode == nullptr)
		{
			m_focusedNode = order.front();
			return;
		}

		// 現在のフォーカスノードのインデックスを検索
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			if (order[i] == m_focusedNode)
			{
				// ラップアラウンド
				m_focusedNode = order[(i + 1) % order.size()];
				return;
			}
		}

		// 現在のフォーカスノードがリストにない場合は先頭に
		m_focusedNode = order.front();
	}

	/// @brief 前のフォーカス可能ノードにフォーカスを移動する（Shift+Tab）
	/// @param root UIツリーのルートノード
	void focusPrevious(UINode& root)
	{
		const auto order = buildFocusOrder(root);
		if (order.empty())
		{
			return;
		}

		if (m_focusedNode == nullptr)
		{
			m_focusedNode = order.back();
			return;
		}

		for (std::size_t i = 0; i < order.size(); ++i)
		{
			if (order[i] == m_focusedNode)
			{
				// ラップアラウンド
				m_focusedNode = order[(i + order.size() - 1) % order.size()];
				return;
			}
		}

		m_focusedNode = order.back();
	}
};

} // namespace mitiru::ui
