#pragma once

/// @file WorldManager.hpp
/// @brief ゲームワールドのオーケストレーター
///
/// ワールド内の全ゲームオブジェクトの生成・更新・検索・状態管理を担う。
/// SimpleMapDataからオブジェクトを構築し、一括updateで管理する。
///
/// @code
/// mitiru::gw::WorldManager world;
/// world.buildWorld(mapData, sharedData);
/// world.update(dt);
/// auto* npc = world.findNearbyNpc(playerPos, 100.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Rect.hpp"
#include "mitiru/graphwalker/Common.hpp"
#include "mitiru/graphwalker/ObjectFactory.hpp"

namespace mitiru::gw
{

/// @brief 簡易ワールドマップデータ
///
/// WorldManager::buildWorldの入力として使用する。
/// 詳細なマップデータはWorldMap.hpp の WorldMapData を参照。
struct SimpleMapData
{
	sgc::Vec2f playerSpawn{0.0f, 0.0f}; ///< プレイヤー初期位置
	std::vector<sgc::Rectf> platforms;   ///< 静的プラットフォーム配列
	std::vector<sgc::Vec2f> npcPositions; ///< NPC配置位置
	std::vector<sgc::Vec2f> checkpointPositions; ///< チェックポイント配置位置
};

/// @brief ワールドマネージャー
///
/// ゲームワールド内の全オブジェクトを管理する。
/// マップデータからオブジェクトを生成し、検索・状態変更APIを提供する。
class WorldManager
{
public:
	/// @brief デフォルトコンストラクタ
	WorldManager() = default;

	/// @brief マップデータからワールドを構築する
	/// @param mapData 簡易マップデータ
	/// @param shared 共有データ（チェックポイント等の初期化用）
	void buildWorld(const SimpleMapData& mapData, const SharedData& /*shared*/)
	{
		clearWorld();

		// プレイヤー生成
		m_objects.push_back(ObjectFactory::createPlayer(mapData.playerSpawn));

		// 静的プラットフォーム生成
		for (const auto& plat : mapData.platforms)
		{
			m_objects.push_back(
				ObjectFactory::createStaticPlatform(plat.position, plat.size));
		}

		// NPC生成
		int npcIdx = 0;
		for (const auto& pos : mapData.npcPositions)
		{
			m_objects.push_back(ObjectFactory::createNpc(
				pos,
				"npc_" + std::to_string(npcIdx++),
				""
			));
		}

		// チェックポイント生成
		int cpIdx = 0;
		for (const auto& pos : mapData.checkpointPositions)
		{
			m_objects.push_back(ObjectFactory::createCheckpoint(
				pos,
				"checkpoint_" + std::to_string(cpIdx++)
			));
		}

		m_spawnPos = mapData.playerSpawn;
	}

	/// @brief 全オブジェクトを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& obj : m_objects)
		{
			if (obj && obj->isActive())
			{
				obj->update(dt);
			}
		}
	}

	/// @brief プレイヤーオブジェクトを取得する
	/// @return プレイヤーへのポインタ（存在しない場合nullptr）
	[[nodiscard]] IGameObject* getPlayer()
	{
		for (auto& obj : m_objects)
		{
			if (obj && obj->getType() == ObjectType::Player)
			{
				return obj.get();
			}
		}
		return nullptr;
	}

	/// @brief 全オブジェクトを取得する
	/// @return オブジェクト配列への参照
	[[nodiscard]] const std::vector<std::unique_ptr<IGameObject>>& getObjects() const noexcept
	{
		return m_objects;
	}

	/// @brief 指定位置付近のNPCを検索する
	/// @param pos 検索中心位置
	/// @param radius 検索半径
	/// @return 見つかったNPCへのポインタ（なければnullptr）
	[[nodiscard]] IGameObject* findNearbyNpc(sgc::Vec2f pos, float radius)
	{
		return findNearbyByType(pos, radius, ObjectType::NPC);
	}

	/// @brief 指定位置付近のチェックポイントを検索する
	/// @param pos 検索中心位置
	/// @param radius 検索半径
	/// @return 見つかったチェックポイントへのポインタ（なければnullptr）
	[[nodiscard]] IGameObject* findNearbyCheckpoint(sgc::Vec2f pos, float radius)
	{
		return findNearbyByType(pos, radius, ObjectType::Checkpoint);
	}

	/// @brief アイテムを収集する
	/// @param buttonId 収集するアイテムのID
	/// @param shared 共有データ（収集済みリストに追加）
	void collectItem(const std::string& buttonId, SharedData& shared)
	{
		shared.collectedItems.insert(buttonId);
	}

	/// @brief チェックポイントをアクティブ化する
	/// @param id チェックポイントID
	/// @param shared 共有データ
	void activateCheckpoint(const std::string& id, SharedData& shared)
	{
		shared.visitedCheckpoints.insert(id);
		shared.currentCheckpointId = id;

		// チェックポイント位置をリスポーン地点に設定
		for (const auto& obj : m_objects)
		{
			if (obj && obj->getType() == ObjectType::Checkpoint)
			{
				shared.playerSpawnPos = obj->getPosition();
				break;
			}
		}
	}

	/// @brief ゲートを開く
	/// @param gateId ゲートID
	/// @param shared 共有データ
	void openGate(const std::string& gateId, SharedData& shared)
	{
		shared.unlockedGates.insert(gateId);

		for (auto& obj : m_objects)
		{
			if (obj && obj->getType() == ObjectType::Gate)
			{
				obj->setActive(false);
				break;
			}
		}
	}

	/// @brief プレイヤーをリスポーンする
	/// @param shared 共有データ（最後のチェックポイント情報を使用）
	void respawnPlayer(const SharedData& shared)
	{
		auto* player = getPlayer();
		if (player)
		{
			const auto respawnPos = (!shared.currentCheckpointId.empty())
				? shared.playerSpawnPos
				: m_spawnPos;
			player->setPosition(respawnPos);
		}
	}

	/// @brief オブジェクト数を取得する
	/// @return 管理中のオブジェクト数
	[[nodiscard]] std::size_t objectCount() const noexcept
	{
		return m_objects.size();
	}

	/// @brief ワールドをクリアする
	void clearWorld()
	{
		m_objects.clear();
	}

private:
	/// @brief 指定種別のオブジェクトを近傍検索する
	/// @param pos 検索中心
	/// @param radius 検索半径
	/// @param type 検索対象の種別
	/// @return 見つかったオブジェクトへのポインタ
	[[nodiscard]] IGameObject* findNearbyByType(
		sgc::Vec2f pos, float radius, ObjectType type)
	{
		const float radiusSq = radius * radius;
		IGameObject* nearest = nullptr;
		float bestDistSq = radiusSq;

		for (auto& obj : m_objects)
		{
			if (!obj || !obj->isActive() || obj->getType() != type)
			{
				continue;
			}

			const auto diff = obj->getPosition() - pos;
			const float distSq = diff.x * diff.x + diff.y * diff.y;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				nearest = obj.get();
			}
		}
		return nearest;
	}

	std::vector<std::unique_ptr<IGameObject>> m_objects; ///< 全オブジェクト
	sgc::Vec2f m_spawnPos{0.0f, 0.0f}; ///< 初期スポーン位置
};

} // namespace mitiru::gw
