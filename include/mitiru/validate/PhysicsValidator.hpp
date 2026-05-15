#pragma once

/// @file PhysicsValidator.hpp
/// @brief 物理状態の健全性チェッカー
/// @details GameWorld内のエンティティの物理状態（位置、速度等）を検証し、
///          NaN、範囲外、テレポート等の異常を検出する。
///
/// @code
/// mitiru::validate::PhysicsValidator validator;
/// auto anomalies = validator.validate(world);
/// for (const auto& a : anomalies) {
///     log(a.description);
/// }
/// @endcode

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/scene/GameWorld.hpp>

namespace mitiru::validate
{

/// @brief 物理異常の情報
struct PhysicsAnomaly
{
	/// @brief 異常の種類
	enum Type
	{
		OutOfBounds,        ///< ワールド範囲外
		WallPenetration,    ///< 壁貫通
		ExcessiveVelocity,  ///< 過剰な速度
		NaNPosition,        ///< 位置がNaN
		TeleportDetected,   ///< テレポート検出
		OverlapDetected     ///< オーバーラップ検出
	};

	Type type;                      ///< 異常タイプ
	scene::EntityId entityId = 0;   ///< エンティティID
	std::string entityName;         ///< エンティティ名
	sgc::Vec3f position;            ///< 検出時の位置
	float value = 0.0f;             ///< 関連する値（速度、距離等）
	float threshold = 0.0f;         ///< 閾値
	std::string description;        ///< 説明

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"type\":" + std::to_string(static_cast<int>(type)) + ",";
		json += "\"entityId\":" + std::to_string(entityId) + ",";
		json += "\"entityName\":\"" + observe::jsonEscape(entityName) + "\",";
		json += "\"position\":{";
		json += "\"x\":" + std::to_string(position.x) + ",";
		json += "\"y\":" + std::to_string(position.y) + ",";
		json += "\"z\":" + std::to_string(position.z);
		json += "},";
		json += "\"value\":" + std::to_string(value) + ",";
		json += "\"threshold\":" + std::to_string(threshold) + ",";
		json += "\"description\":\"" + observe::jsonEscape(description) + "\"";
		json += "}";
		return json;
	}
};

/// @brief 物理バリデータの設定
struct PhysicsValidatorConfig
{
	sgc::Vec3f worldMin{-10000.0f, -10000.0f, -10000.0f};  ///< ワールド下限
	sgc::Vec3f worldMax{10000.0f, 10000.0f, 10000.0f};     ///< ワールド上限
	float maxVelocity = 1000.0f;                             ///< 最大速度（単位/秒）
	float maxTeleportDistance = 100.0f;                      ///< テレポート判定距離
	float overlapThreshold = 0.01f;                          ///< オーバーラップ判定閾値
};

/// @brief 物理状態バリデータ
/// @details GameWorld内の全TransformComponentを検証し、物理的に異常な状態を検出する。
class PhysicsValidator
{
public:
	/// @brief コンストラクタ
	/// @param config バリデータ設定
	explicit PhysicsValidator(PhysicsValidatorConfig config = {})
		: m_config(config)
	{
	}

	/// @brief 設定を更新する
	/// @param config 新しい設定
	void setConfig(const PhysicsValidatorConfig& config)
	{
		m_config = config;
	}

	/// @brief TransformComponentを持つ全エンティティを検証する
	/// @param world 検証対象のゲームワールド
	/// @return 検出された異常のリスト
	std::vector<PhysicsAnomaly> validate(scene::GameWorld& world)
	{
		std::vector<PhysicsAnomaly> anomalies;

		world.forEach<scene::TransformComponent>(
			[this, &world, &anomalies](scene::EntityId id, scene::TransformComponent& tc)
			{
				const auto& pos = tc.position;
				const std::string name = world.entityName(id);

				// NaN検出
				if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z))
				{
					PhysicsAnomaly a;
					a.type = PhysicsAnomaly::NaNPosition;
					a.entityId = id;
					a.entityName = name;
					a.position = pos;
					a.value = 0.0f;
					a.threshold = 0.0f;
					a.description = "Entity '" + name + "' has NaN position";
					anomalies.push_back(std::move(a));
					return; // NaNの場合は他のチェックをスキップ
				}

				// 範囲外検出
				if (pos.x < m_config.worldMin.x || pos.x > m_config.worldMax.x
					|| pos.y < m_config.worldMin.y || pos.y > m_config.worldMax.y
					|| pos.z < m_config.worldMin.z || pos.z > m_config.worldMax.z)
				{
					PhysicsAnomaly a;
					a.type = PhysicsAnomaly::OutOfBounds;
					a.entityId = id;
					a.entityName = name;
					a.position = pos;
					a.value = 0.0f;
					a.threshold = 0.0f;
					a.description = "Entity '" + name + "' is out of world bounds";
					anomalies.push_back(std::move(a));
				}

				// テレポート検出（前フレームの位置と比較）
				auto prevIt = m_previousPositions.find(id);
				if (prevIt != m_previousPositions.end())
				{
					const float dx = pos.x - prevIt->second.x;
					const float dy = pos.y - prevIt->second.y;
					const float dz = pos.z - prevIt->second.z;
					const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

					if (distance > m_config.maxTeleportDistance)
					{
						PhysicsAnomaly a;
						a.type = PhysicsAnomaly::TeleportDetected;
						a.entityId = id;
						a.entityName = name;
						a.position = pos;
						a.value = distance;
						a.threshold = m_config.maxTeleportDistance;
						a.description = "Entity '" + name + "' teleported "
							+ std::to_string(distance) + " units (max: "
							+ std::to_string(m_config.maxTeleportDistance) + ")";
						anomalies.push_back(std::move(a));
					}
				}

				// 位置履歴を更新
				m_previousPositions[id] = pos;
			});

		return anomalies;
	}

	/// @brief 位置履歴をリセットする
	void reset()
	{
		m_previousPositions.clear();
	}

	/// @brief 異常リストをJSON配列に変換する
	/// @param anomalies 異常リスト
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson(const std::vector<PhysicsAnomaly>& anomalies) const
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < anomalies.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += anomalies[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	PhysicsValidatorConfig m_config;                            ///< バリデータ設定
	std::map<scene::EntityId, sgc::Vec3f> m_previousPositions;  ///< 前フレームの位置履歴
};

} // namespace mitiru::validate
