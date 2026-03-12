#pragma once

/// @file SceneGraph.hpp
/// @brief 階層的トランスフォームグラフ
/// @details エンティティの親子関係を管理し、ワールド変換行列を
///          親チェーンから計算する。

#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/ecs/Entity.hpp>
#include <sgc/math/Mat4.hpp>

namespace mitiru::scene
{

/// @brief シーンノード
/// @details エンティティに対応するトランスフォームノード。
///          ローカル変換行列と親子関係を保持する。
struct SceneNode
{
	sgc::ecs::Entity entity;                          ///< 対応エンティティ
	sgc::Mat4f localTransform = sgc::Mat4f::identity();  ///< ローカル変換行列
	sgc::ecs::EntityId parentId = sgc::ecs::INVALID_ENTITY_ID;  ///< 親ノードのエンティティID
	std::vector<sgc::ecs::EntityId> childIds;         ///< 子ノードのエンティティID群
};

/// @brief シーングラフ（階層的トランスフォーム）
/// @details エンティティの親子関係を管理し、
///          ワールド変換行列を親チェーンから再帰的に計算する。
///
/// @code
/// mitiru::scene::SceneGraph graph;
/// auto parent = world.createEntity();
/// auto child = world.createEntity();
/// graph.addNode(parent);
/// graph.addNode(child, parent);
/// graph.setLocalTransform(child, Mat4f::translation({10, 0, 0}));
/// auto worldMat = graph.worldTransform(child);
/// @endcode
class SceneGraph
{
public:
	/// @brief ノードを追加する
	/// @param entity 対象エンティティ
	/// @param parent 親エンティティ（ルートノードの場合は無効Entity）
	void addNode(sgc::ecs::Entity entity,
	             sgc::ecs::Entity parent = sgc::ecs::Entity{})
	{
		SceneNode node;
		node.entity = entity;
		node.localTransform = sgc::Mat4f::identity();

		if (parent.isValid())
		{
			node.parentId = parent.id;
			/// 親ノードの子リストに追加
			auto parentIt = m_nodes.find(parent.id);
			if (parentIt != m_nodes.end())
			{
				parentIt->second.childIds.push_back(entity.id);
			}
		}

		m_nodes[entity.id] = std::move(node);
	}

	/// @brief ノードを削除する（子ノードも再帰的に削除）
	/// @param entity 削除するエンティティ
	void removeNode(sgc::ecs::Entity entity)
	{
		const auto it = m_nodes.find(entity.id);
		if (it == m_nodes.end())
		{
			return;
		}

		/// 親の子リストから削除
		if (it->second.parentId != sgc::ecs::INVALID_ENTITY_ID)
		{
			auto parentIt = m_nodes.find(it->second.parentId);
			if (parentIt != m_nodes.end())
			{
				auto& siblings = parentIt->second.childIds;
				siblings.erase(
					std::remove(siblings.begin(), siblings.end(), entity.id),
					siblings.end());
			}
		}

		/// 子ノードを再帰的に削除
		auto childIds = it->second.childIds;
		for (const auto childId : childIds)
		{
			const auto childIt = m_nodes.find(childId);
			if (childIt != m_nodes.end())
			{
				removeNode(childIt->second.entity);
			}
		}

		m_nodes.erase(entity.id);
	}

	/// @brief ローカル変換行列を設定する
	/// @param entity 対象エンティティ
	/// @param transform ローカル変換行列
	void setLocalTransform(sgc::ecs::Entity entity, const sgc::Mat4f& transform)
	{
		const auto it = m_nodes.find(entity.id);
		if (it != m_nodes.end())
		{
			it->second.localTransform = transform;
		}
	}

	/// @brief ローカル変換行列を取得する
	/// @param entity 対象エンティティ
	/// @return ローカル変換行列（未登録の場合は単位行列）
	[[nodiscard]] sgc::Mat4f localTransform(sgc::ecs::Entity entity) const
	{
		const auto it = m_nodes.find(entity.id);
		if (it == m_nodes.end())
		{
			return sgc::Mat4f::identity();
		}
		return it->second.localTransform;
	}

	/// @brief ワールド変換行列を計算する（親チェーンをたどる）
	/// @param entity 対象エンティティ
	/// @return ワールド変換行列
	[[nodiscard]] sgc::Mat4f worldTransform(sgc::ecs::Entity entity) const
	{
		const auto it = m_nodes.find(entity.id);
		if (it == m_nodes.end())
		{
			return sgc::Mat4f::identity();
		}

		sgc::Mat4f result = it->second.localTransform;
		auto parentId = it->second.parentId;

		/// 親チェーンをたどってワールド変換を累積
		while (parentId != sgc::ecs::INVALID_ENTITY_ID)
		{
			const auto parentIt = m_nodes.find(parentId);
			if (parentIt == m_nodes.end())
			{
				break;
			}
			result = parentIt->second.localTransform * result;
			parentId = parentIt->second.parentId;
		}

		return result;
	}

	/// @brief 子エンティティ一覧を取得する
	/// @param entity 親エンティティ
	/// @return 子エンティティIDのベクタ
	[[nodiscard]] std::vector<sgc::ecs::EntityId> children(
		sgc::ecs::Entity entity) const
	{
		const auto it = m_nodes.find(entity.id);
		if (it == m_nodes.end())
		{
			return {};
		}
		return it->second.childIds;
	}

	/// @brief ルートノード一覧を取得する
	/// @return ルートノードのエンティティIDベクタ
	[[nodiscard]] std::vector<sgc::ecs::EntityId> rootNodes() const
	{
		std::vector<sgc::ecs::EntityId> roots;
		for (const auto& [id, node] : m_nodes)
		{
			if (node.parentId == sgc::ecs::INVALID_ENTITY_ID)
			{
				roots.push_back(id);
			}
		}
		return roots;
	}

	/// @brief ノード数を取得する
	/// @return 登録ノード数
	[[nodiscard]] std::size_t nodeCount() const noexcept
	{
		return m_nodes.size();
	}

	/// @brief グラフ情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"nodeCount\":" + std::to_string(m_nodes.size()) + ",";
		json += "\"roots\":[";
		const auto roots = rootNodes();
		for (std::size_t i = 0; i < roots.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += std::to_string(roots[i]);
		}
		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief エンティティID → シーンノード
	std::unordered_map<sgc::ecs::EntityId, SceneNode> m_nodes;
};

} // namespace mitiru::scene
