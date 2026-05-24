#pragma once

/// @file NetworkEntity.hpp
/// @brief ネットワーク同期エンティティの管理
///
/// ネットワーク越しに同期するエンティティの所有権・権威を管理する。
/// エンティティの登録・検索・権威移譲などの機能を提供する。
///
/// @code
/// sgc::network::NetworkEntityManager manager;
/// auto id = manager.registerEntity(1, sgc::network::Authority::Local);
/// auto* entity = manager.find(id);
/// manager.transferAuthority(id, 2, sgc::network::Authority::Remote);
/// @endcode

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sgc::network
{

/// @brief ネットワークエンティティの所有権
enum class Authority : uint8_t
{
	Local,   ///< ローカルで制御
	Remote,  ///< リモートで制御
	Server,  ///< サーバーが権威
};

/// @brief ネットワーク同期エンティティ
///
/// ネットワーク越しに同期するエンティティの基本情報を保持する。
struct NetworkEntity
{
	uint32_t networkId = 0;      ///< ネットワークID
	uint32_t ownerId = 0;        ///< 所有者のピアID
	Authority authority = Authority::Local;  ///< 権威の種類
	uint32_t lastUpdateTick = 0; ///< 最後に更新されたティック

	/// @brief ローカルで制御されているか判定する
	/// @return ローカル制御ならtrue
	[[nodiscard]] bool isLocallyControlled() const noexcept
	{
		return authority == Authority::Local;
	}

	/// @brief リモートで制御されているか判定する
	/// @return リモート制御ならtrue
	[[nodiscard]] bool isRemotelyControlled() const noexcept
	{
		return authority == Authority::Remote;
	}
};

/// @brief ネットワークエンティティ管理
///
/// ネットワークIDによるエンティティの登録・検索・権威移譲を行う。
class NetworkEntityManager
{
public:
	/// @brief エンティティを登録する
	/// @param ownerId 所有者のピアID
	/// @param auth 権威の種類
	/// @return 割り当てられたネットワークID
	uint32_t registerEntity(uint32_t ownerId, Authority auth)
	{
		const uint32_t id = m_nextId++;
		NetworkEntity entity;
		entity.networkId = id;
		entity.ownerId = ownerId;
		entity.authority = auth;
		m_entities.emplace(id, entity);
		return id;
	}

	/// @brief エンティティを登録解除する
	/// @param networkId 解除するネットワークID
	void unregisterEntity(uint32_t networkId)
	{
		m_entities.erase(networkId);
	}

	/// @brief ネットワークIDでエンティティを検索する
	/// @param networkId 検索するネットワークID
	/// @return 見つかればポインタ、なければnullptr
	[[nodiscard]] NetworkEntity* find(uint32_t networkId)
	{
		const auto it = m_entities.find(networkId);
		if (it == m_entities.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief ネットワークIDでエンティティを検索する（const版）
	/// @param networkId 検索するネットワークID
	/// @return 見つかればポインタ、なければnullptr
	[[nodiscard]] const NetworkEntity* find(uint32_t networkId) const
	{
		const auto it = m_entities.find(networkId);
		if (it == m_entities.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief エンティティの権威を移譲する
	/// @param networkId 対象のネットワークID
	/// @param newOwnerId 新しい所有者のピアID
	/// @param newAuth 新しい権威の種類
	void transferAuthority(uint32_t networkId, uint32_t newOwnerId, Authority newAuth)
	{
		auto* entity = find(networkId);
		if (entity != nullptr)
		{
			entity->ownerId = newOwnerId;
			entity->authority = newAuth;
		}
	}

	/// @brief 指定所有者のエンティティID一覧を取得する
	/// @param ownerId 所有者のピアID
	/// @return ネットワークIDのリスト
	[[nodiscard]] std::vector<uint32_t> getEntitiesOwnedBy(uint32_t ownerId) const
	{
		std::vector<uint32_t> result;
		for (const auto& [id, entity] : m_entities)
		{
			if (entity.ownerId == ownerId)
			{
				result.push_back(id);
			}
		}
		return result;
	}

	/// @brief 登録エンティティ数を取得する
	/// @return エンティティ数
	[[nodiscard]] size_t entityCount() const noexcept
	{
		return m_entities.size();
	}

	/// @brief 全エンティティをクリアする
	void clear()
	{
		m_entities.clear();
	}

private:
	std::unordered_map<uint32_t, NetworkEntity> m_entities;  ///< エンティティマップ
	uint32_t m_nextId = 1;  ///< 次に割り当てるID
};

} // namespace sgc::network
