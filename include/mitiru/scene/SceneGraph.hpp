#pragma once

/// @file SceneGraph.hpp
/// @brief 階層的トランスフォームグラフ
/// @details エンティティの親子関係を管理し、ワールド変換行列を
///          親チェーンから計算する。世代付きNodeIdで安全なハンドルを提供。

#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <sgc/math/Mat4.hpp>

namespace mitiru::scene
{

/// @brief ノード識別子（インデックス＋世代で安全性を確保）
struct NodeId
{
	uint32_t value{0xFFFFFFFF};  ///< パック値（上位16bit: 世代、下位16bit: インデックス）

	/// @brief インデックスを取得する
	[[nodiscard]] uint16_t index() const noexcept { return static_cast<uint16_t>(value & 0xFFFF); }

	/// @brief 世代を取得する
	[[nodiscard]] uint16_t generation() const noexcept { return static_cast<uint16_t>(value >> 16); }

	/// @brief 有効なIDか
	[[nodiscard]] bool isValid() const noexcept { return value != 0xFFFFFFFF; }

	/// @brief 等値比較
	bool operator==(const NodeId& other) const noexcept { return value == other.value; }
	bool operator!=(const NodeId& other) const noexcept { return value != other.value; }

	/// @brief インデックスと世代からNodeIdを作成する
	[[nodiscard]] static NodeId make(uint16_t idx, uint16_t gen) noexcept
	{
		return NodeId{static_cast<uint32_t>(gen) << 16 | idx};
	}

	/// @brief 無効なNodeId
	[[nodiscard]] static NodeId invalid() noexcept { return NodeId{0xFFFFFFFF}; }
};

/// @brief シーンノード
struct SceneNode
{
	std::string name;                        ///< ノード名
	sgc::Mat4f localTransform = sgc::Mat4f::identity(); ///< ローカル変換行列
	NodeId parent = NodeId::invalid();       ///< 親ノード
	std::vector<NodeId> children;            ///< 子ノードリスト
};

/// @brief 走査順序
enum class TraversalOrder
{
	DepthFirst,    ///< 深さ優先
	BreadthFirst   ///< 幅優先
};

/// @brief シーングラフ（階層的トランスフォーム）
class SceneGraph
{
public:
	/// @brief ノードを作成する
	/// @param name ノード名
	/// @return 新規ノードID
	[[nodiscard]] NodeId createNode(const std::string& name = "")
	{
		uint16_t idx;
		if (!m_freeList.empty())
		{
			idx = m_freeList.back();
			m_freeList.pop_back();
		}
		else
		{
			idx = static_cast<uint16_t>(m_nodes.size());
			m_nodes.emplace_back();
			m_generations.push_back(0);
			m_alive.push_back(false);
		}

		m_generations[idx]++;
		m_alive[idx] = true;
		m_nodes[idx] = SceneNode{};
		m_nodes[idx].name = name;

		return NodeId::make(idx, m_generations[idx]);
	}

	/// @brief ノードを破棄する（子ノードも再帰的に破棄）
	/// @param id 破棄するノードID
	void destroyNode(NodeId id)
	{
		if (!isValid(id)) return;
		const auto idx = id.index();

		/// 子ノードを先に再帰削除
		auto childrenCopy = m_nodes[idx].children;
		for (const auto& childId : childrenCopy)
		{
			destroyNode(childId);
		}

		/// 親の子リストから自分を除去
		if (m_nodes[idx].parent.isValid())
		{
			const auto parentIdx = m_nodes[idx].parent.index();
			if (parentIdx < m_alive.size() && m_alive[parentIdx])
			{
				auto& siblings = m_nodes[parentIdx].children;
				siblings.erase(
					std::remove(siblings.begin(), siblings.end(), id),
					siblings.end());
			}
		}

		m_alive[idx] = false;
		m_freeList.push_back(idx);
	}

	/// @brief ノードが有効か
	[[nodiscard]] bool isValid(NodeId id) const noexcept
	{
		if (!id.isValid()) return false;
		const auto idx = id.index();
		if (idx >= m_nodes.size()) return false;
		return m_alive[idx] && m_generations[idx] == id.generation();
	}

	/// @brief ノード数を返す
	[[nodiscard]] size_t nodeCount() const noexcept
	{
		size_t count = 0;
		for (bool a : m_alive) { if (a) ++count; }
		return count;
	}

	/// @brief ノードを取得する
	/// @param id ノードID
	/// @return ノードへのポインタ（無効なら nullptr）
	[[nodiscard]] SceneNode* getNode(NodeId id) noexcept
	{
		if (!isValid(id)) return nullptr;
		return &m_nodes[id.index()];
	}

	/// @brief ノードを取得する（const版）
	[[nodiscard]] const SceneNode* getNode(NodeId id) const noexcept
	{
		if (!isValid(id)) return nullptr;
		return &m_nodes[id.index()];
	}

	/// @brief 親を変更する（循環参照チェック付き）
	/// @param child 子ノード
	/// @param newParent 新しい親ノード
	void reparent(NodeId child, NodeId newParent)
	{
		if (!isValid(child) || !isValid(newParent)) return;
		if (child == newParent) return;

		/// 循環参照チェック
		if (isAncestor(child, newParent)) return;

		auto& childNode = m_nodes[child.index()];

		/// 旧親から除去
		if (childNode.parent.isValid() && isValid(childNode.parent))
		{
			auto& siblings = m_nodes[childNode.parent.index()].children;
			siblings.erase(
				std::remove(siblings.begin(), siblings.end(), child),
				siblings.end());
		}

		/// 新親に追加
		childNode.parent = newParent;
		m_nodes[newParent.index()].children.push_back(child);
	}

	/// @brief ワールド変換行列を取得する（親チェーンを辿る）
	[[nodiscard]] sgc::Mat4f getWorldTransform(NodeId id) const
	{
		if (!isValid(id)) return sgc::Mat4f::identity();

		sgc::Mat4f result = m_nodes[id.index()].localTransform;
		auto parentId = m_nodes[id.index()].parent;

		while (isValid(parentId))
		{
			result = m_nodes[parentId.index()].localTransform * result;
			parentId = m_nodes[parentId.index()].parent;
		}

		return result;
	}

	/// @brief 名前でノードを検索する
	[[nodiscard]] std::optional<NodeId> findByName(const std::string& name) const
	{
		for (size_t i = 0; i < m_nodes.size(); ++i)
		{
			if (m_alive[i] && m_nodes[i].name == name)
			{
				return NodeId::make(static_cast<uint16_t>(i), m_generations[i]);
			}
		}
		return std::nullopt;
	}

	/// @brief ノードを走査する
	/// @param root 開始ノード
	/// @param order 走査順序
	/// @param visitor 訪問関数
	void traverse(NodeId root, TraversalOrder order,
		const std::function<void(const SceneNode&)>& visitor) const
	{
		if (!isValid(root)) return;

		if (order == TraversalOrder::DepthFirst)
		{
			traverseDepthFirst(root, visitor);
		}
		else
		{
			traverseBreadthFirst(root, visitor);
		}
	}

private:
	/// @brief nodeAがnodeBの祖先かどうか
	[[nodiscard]] bool isAncestor(NodeId ancestor, NodeId descendant) const
	{
		auto current = descendant;
		while (isValid(current))
		{
			if (current == ancestor) return true;
			current = m_nodes[current.index()].parent;
		}
		return false;
	}

	/// @brief 深さ優先走査
	void traverseDepthFirst(NodeId id,
		const std::function<void(const SceneNode&)>& visitor) const
	{
		if (!isValid(id)) return;
		visitor(m_nodes[id.index()]);
		for (const auto& childId : m_nodes[id.index()].children)
		{
			traverseDepthFirst(childId, visitor);
		}
	}

	/// @brief 幅優先走査
	void traverseBreadthFirst(NodeId id,
		const std::function<void(const SceneNode&)>& visitor) const
	{
		std::queue<NodeId> queue;
		queue.push(id);

		while (!queue.empty())
		{
			auto current = queue.front();
			queue.pop();
			if (!isValid(current)) continue;

			visitor(m_nodes[current.index()]);
			for (const auto& childId : m_nodes[current.index()].children)
			{
				queue.push(childId);
			}
		}
	}

	std::vector<SceneNode> m_nodes;         ///< ノード配列
	std::vector<uint16_t> m_generations;    ///< 世代カウンタ
	std::vector<bool> m_alive;              ///< 生存フラグ
	std::vector<uint16_t> m_freeList;       ///< 再利用可能インデックス
};

} // namespace mitiru::scene
