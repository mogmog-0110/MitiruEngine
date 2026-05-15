#pragma once

/// @file NetworkReplication.hpp
/// @brief ネットワークレプリケーション基盤
/// @details Scene Nodeの状態をクライアント-サーバー間で同期する。
///          デッドレコニング、ラグ補償の基本フレームワーク。

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::network
{

/// @brief レプリケーション対象プロパティ
struct ReplicatedProperty
{
	std::string name;      ///< プロパティ名 (e.g. "position", "rotation")
	float values[4] = {};  ///< 最大4要素 (x,y,z,w)
	int numValues = 3;     ///< 要素数
	uint32_t lastUpdateFrame = 0;
};

/// @brief レプリケーション対象エンティティ
struct ReplicatedEntity
{
	int nodeId = -1;
	uint32_t networkId = 0;         ///< ネットワーク上のユニークID
	uint8_t ownerPeerId = 0;        ///< 所有者のPeer ID
	std::vector<ReplicatedProperty> properties;
	bool dirty = false;             ///< 変更があったか
};

/// @brief デッドレコニング状態
struct DeadReckoningState
{
	float position[3] = {};
	float velocity[3] = {};
	float acceleration[3] = {};
	double timestamp = 0.0;

	/// @brief 指定時刻の位置を予測する
	void predict(double t, float outPos[3]) const noexcept
	{
		const float dt = static_cast<float>(t - timestamp);
		for (int i = 0; i < 3; ++i)
		{
			outPos[i] = position[i] + velocity[i] * dt + 0.5f * acceleration[i] * dt * dt;
		}
	}
};

/// @brief レプリケーションマネージャー
class ReplicationManager
{
public:
	/// @brief エンティティを登録する
	void registerEntity(int nodeId, uint8_t ownerPeerId)
	{
		ReplicatedEntity entity;
		entity.nodeId = nodeId;
		entity.networkId = m_nextNetworkId++;
		entity.ownerPeerId = ownerPeerId;
		m_entities[entity.networkId] = entity;
	}

	/// @brief プロパティを更新する（ローカル変更）
	void updateProperty(uint32_t networkId, const std::string& propName,
	                    const float* values, int numValues)
	{
		auto it = m_entities.find(networkId);
		if (it == m_entities.end()) { return; }
		for (auto& prop : it->second.properties)
		{
			if (prop.name == propName)
			{
				for (int i = 0; i < numValues && i < 4; ++i) { prop.values[i] = values[i]; }
				prop.numValues = numValues;
				it->second.dirty = true;
				return;
			}
		}
		// 新規プロパティ
		ReplicatedProperty prop;
		prop.name = propName;
		for (int i = 0; i < numValues && i < 4; ++i) { prop.values[i] = values[i]; }
		prop.numValues = numValues;
		it->second.properties.push_back(prop);
		it->second.dirty = true;
	}

	/// @brief ダーティなエンティティをシリアライズする（送信用）
	[[nodiscard]] std::vector<uint8_t> serializeDirty()
	{
		std::vector<uint8_t> data;
		for (auto& [id, entity] : m_entities)
		{
			if (!entity.dirty) { continue; }
			// Header: networkId(4) + propCount(2)
			pushU32(data, entity.networkId);
			pushU16(data, static_cast<uint16_t>(entity.properties.size()));
			for (const auto& prop : entity.properties)
			{
				// propNameLen(1) + propName + numValues(1) + values(4*numValues)
				pushU8(data, static_cast<uint8_t>(std::min(prop.name.size(), size_t{255})));
				data.insert(data.end(), prop.name.begin(),
					prop.name.begin() + static_cast<ptrdiff_t>(std::min(prop.name.size(), size_t{255})));
				pushU8(data, static_cast<uint8_t>(prop.numValues));
				for (int i = 0; i < prop.numValues; ++i) { pushF32(data, prop.values[i]); }
			}
			entity.dirty = false;
		}
		return data;
	}

	/// @brief 全エンティティ数
	[[nodiscard]] size_t entityCount() const noexcept { return m_entities.size(); }

private:
	std::unordered_map<uint32_t, ReplicatedEntity> m_entities;
	uint32_t m_nextNetworkId = 1;

	static void pushU8(std::vector<uint8_t>& d, uint8_t v) { d.push_back(v); }
	static void pushU16(std::vector<uint8_t>& d, uint16_t v)
	{
		d.push_back(static_cast<uint8_t>(v & 0xFF));
		d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	}
	static void pushU32(std::vector<uint8_t>& d, uint32_t v)
	{
		for (int i = 0; i < 4; ++i) { d.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); }
	}
	static void pushF32(std::vector<uint8_t>& d, float v)
	{
		uint32_t bits;
		std::memcpy(&bits, &v, 4);
		pushU32(d, bits);
	}
};

} // namespace mitiru::network
