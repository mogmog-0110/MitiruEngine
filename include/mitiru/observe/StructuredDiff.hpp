#pragma once

/// @file StructuredDiff.hpp
/// @brief 型付きエンティティ・コンポーネント差分追跡
/// @details GameWorldのエンティティとコンポーネントの変化を構造化された形式で
///          追跡する。登録されたコンポーネントシリアライザーを用いて、
///          フレーム間のエンティティ生成・破棄・コンポーネント変更を検出する。
///
/// @code
/// mitiru::observe::StructuredDiff diff;
/// diff.registerSerializer<mitiru::scene::TransformComponent>("TransformComponent",
///     [](const auto& t) {
///         return "{\"x\":" + std::to_string(t.position.x) + "}";
///     });
/// auto changes = diff.diff(world, frameNumber);
/// std::cout << StructuredDiff::toJson(changes);
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/scene/GameWorld.hpp>

namespace mitiru::observe
{

/// @brief 構造化変更の種類
enum class StructuredChangeType : std::uint8_t
{
	EntityCreated,       ///< エンティティが新規作成された
	EntityDestroyed,     ///< エンティティが破棄された
	ComponentAdded,      ///< コンポーネントが追加された
	ComponentRemoved,    ///< コンポーネントが削除された
	ComponentChanged,    ///< コンポーネントの値が変更された
	ParentChanged        ///< 親エンティティが変更された
};

/// @brief 構造化された変更情報
/// @details 1つのエンティティ/コンポーネントの変化を表す。
struct StructuredChange
{
	StructuredChangeType type;             ///< 変更の種類
	scene::EntityId entityId = 0;          ///< 対象エンティティID
	std::string entityName;                ///< エンティティ名
	std::string componentType;             ///< コンポーネント型名（例: "TransformComponent"）
	std::string oldValue;                  ///< 変更前の値（JSON）
	std::string newValue;                  ///< 変更後の値（JSON）
	std::uint64_t frame = 0;               ///< 変化が発生したフレーム番号

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"type\":";
		json += std::to_string(static_cast<int>(type));
		json += ",\"entityId\":";
		json += std::to_string(entityId);
		json += ",\"entityName\":\"";
		json += jsonEscape(entityName);
		json += "\",\"componentType\":\"";
		json += jsonEscape(componentType);
		json += "\",\"oldValue\":\"";
		json += jsonEscape(oldValue);
		json += "\",\"newValue\":\"";
		json += jsonEscape(newValue);
		json += "\",\"frame\":";
		json += std::to_string(frame);
		json += "}";
		return json;
	}
};

/// @brief 型付きエンティティ・コンポーネント差分トラッカー
/// @details GameWorldの状態をフレームごとにスナップショットし、
///          前フレームとの差分を構造化された変更情報として出力する。
///          コンポーネントのシリアライズには登録されたシリアライザーを使用する。
class StructuredDiff
{
public:
	/// @brief 現在のワールド状態と前回の状態の差分を計算する
	/// @param world 現在のGameWorldへの参照
	/// @param frame 現在のフレーム番号
	/// @return 前回からの変更情報一覧
	/// @details 処理手順:
	///          1. 登録済みシリアライザーでワールドをスナップショット
	///          2. 前回のスナップショットと比較
	///          3. 新規エンティティ・削除エンティティ・変更コンポーネントを検出
	///          4. 現在のスナップショットを保存
	///          5. 変更情報を返す
	[[nodiscard]] std::vector<StructuredChange> diff(scene::GameWorld& world, std::uint64_t frame)
	{
		std::vector<StructuredChange> changes;

		/// 現在の状態をスナップショット
		std::map<scene::EntityId, EntitySnapshot> currentState;
		for (const auto& serializer : m_serializers)
		{
			serializer.collect(world, currentState);
		}

		/// エンティティ名を収集
		for (auto& [entityId, snapshot] : currentState)
		{
			snapshot.name = world.entityName(entityId);
		}

		/// 新規エンティティの検出
		for (const auto& [entityId, snapshot] : currentState)
		{
			const auto prevIt = m_previousState.find(entityId);
			if (prevIt == m_previousState.end())
			{
				/// 新規エンティティ
				changes.push_back(StructuredChange{
					.type = StructuredChangeType::EntityCreated,
					.entityId = entityId,
					.entityName = snapshot.name,
					.componentType = {},
					.oldValue = {},
					.newValue = {},
					.frame = frame
				});

				/// 新規エンティティのコンポーネントは ComponentAdded として記録
				for (const auto& compType : snapshot.componentTypes)
				{
					const auto valIt = snapshot.componentValues.find(compType);
					const auto newVal = (valIt != snapshot.componentValues.end())
						? valIt->second : std::string{};

					changes.push_back(StructuredChange{
						.type = StructuredChangeType::ComponentAdded,
						.entityId = entityId,
						.entityName = snapshot.name,
						.componentType = compType,
						.oldValue = {},
						.newValue = newVal,
						.frame = frame
					});
				}
			}
			else
			{
				/// 既存エンティティのコンポーネント変更を検出
				const auto& prevSnapshot = prevIt->second;
				detectComponentChanges(
					entityId, snapshot.name, prevSnapshot, snapshot, frame, changes);
			}
		}

		/// 削除されたエンティティの検出
		for (const auto& [entityId, prevSnapshot] : m_previousState)
		{
			if (currentState.find(entityId) == currentState.end())
			{
				changes.push_back(StructuredChange{
					.type = StructuredChangeType::EntityDestroyed,
					.entityId = entityId,
					.entityName = prevSnapshot.name,
					.componentType = {},
					.oldValue = {},
					.newValue = {},
					.frame = frame
				});
			}
		}

		/// 現在の状態を保存
		m_previousState = std::move(currentState);
		m_currentFrame = frame;

		return changes;
	}

	/// @brief コンポーネントシリアライザーを登録する
	/// @tparam T コンポーネント型
	/// @param typeName コンポーネントの型名文字列
	/// @param serializer コンポーネントをJSON文字列に変換する関数
	/// @details 型消去ラッパーを介して、GameWorld::forEach<T> でエンティティを走査し、
	///          シリアライザーでJSON化した結果をスナップショットに格納する。
	template <typename T>
	void registerSerializer(const std::string& typeName,
	                        std::function<std::string(const T&)> serializer)
	{
		m_serializers.push_back(SerializerEntry{
			.typeName = typeName,
			.collect = [typeName, ser = std::move(serializer)](
				scene::GameWorld& world,
				std::map<scene::EntityId, EntitySnapshot>& state)
			{
				world.forEach<T>([&](scene::EntityId id, T& component)
				{
					auto& snapshot = state[id];
					snapshot.componentTypes.insert(typeName);
					snapshot.componentValues[typeName] = ser(component);
				});
			}
		});
	}

	/// @brief 内部状態をリセットする
	void reset()
	{
		m_previousState.clear();
		m_currentFrame = 0;
	}

	/// @brief 追跡中のエンティティ数を取得する
	/// @return 前回のスナップショットに含まれるエンティティ数
	[[nodiscard]] std::size_t trackedEntityCount() const noexcept
	{
		return m_previousState.size();
	}

	/// @brief 変更情報一覧をJSON配列文字列に変換する
	/// @param changes 変更情報の配列
	/// @return JSON配列形式の文字列
	[[nodiscard]] static std::string toJson(const std::vector<StructuredChange>& changes)
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < changes.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += changes[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief エンティティの状態スナップショット
	struct EntitySnapshot
	{
		std::string name;                                    ///< エンティティ名
		std::set<std::string> componentTypes;                ///< 保持するコンポーネント型名の集合
		std::map<std::string, std::string> componentValues;  ///< 型名 → JSON値のマップ
	};

	/// @brief 型消去されたシリアライザーエントリ
	struct SerializerEntry
	{
		std::string typeName;   ///< コンポーネント型名

		/// @brief ワールドを走査してスナップショットに収集する関数
		std::function<void(scene::GameWorld&,
		                   std::map<scene::EntityId, EntitySnapshot>&)> collect;
	};

	/// @brief 2つのスナップショット間のコンポーネント変更を検出する
	/// @param entityId 対象エンティティID
	/// @param entityName エンティティ名
	/// @param prev 前回のスナップショット
	/// @param curr 現在のスナップショット
	/// @param frame 現在のフレーム番号
	/// @param changes 変更情報の出力先
	static void detectComponentChanges(
		scene::EntityId entityId,
		const std::string& entityName,
		const EntitySnapshot& prev,
		const EntitySnapshot& curr,
		std::uint64_t frame,
		std::vector<StructuredChange>& changes)
	{
		/// 新規追加されたコンポーネント
		for (const auto& compType : curr.componentTypes)
		{
			if (prev.componentTypes.find(compType) == prev.componentTypes.end())
			{
				const auto valIt = curr.componentValues.find(compType);
				const auto newVal = (valIt != curr.componentValues.end())
					? valIt->second : std::string{};

				changes.push_back(StructuredChange{
					.type = StructuredChangeType::ComponentAdded,
					.entityId = entityId,
					.entityName = entityName,
					.componentType = compType,
					.oldValue = {},
					.newValue = newVal,
					.frame = frame
				});
			}
		}

		/// 削除されたコンポーネント
		for (const auto& compType : prev.componentTypes)
		{
			if (curr.componentTypes.find(compType) == curr.componentTypes.end())
			{
				const auto valIt = prev.componentValues.find(compType);
				const auto oldVal = (valIt != prev.componentValues.end())
					? valIt->second : std::string{};

				changes.push_back(StructuredChange{
					.type = StructuredChangeType::ComponentRemoved,
					.entityId = entityId,
					.entityName = entityName,
					.componentType = compType,
					.oldValue = oldVal,
					.newValue = {},
					.frame = frame
				});
			}
		}

		/// 値が変更されたコンポーネント
		for (const auto& compType : curr.componentTypes)
		{
			if (prev.componentTypes.find(compType) != prev.componentTypes.end())
			{
				const auto prevValIt = prev.componentValues.find(compType);
				const auto currValIt = curr.componentValues.find(compType);
				const auto prevVal = (prevValIt != prev.componentValues.end())
					? prevValIt->second : std::string{};
				const auto currVal = (currValIt != curr.componentValues.end())
					? currValIt->second : std::string{};

				if (prevVal != currVal)
				{
					changes.push_back(StructuredChange{
						.type = StructuredChangeType::ComponentChanged,
						.entityId = entityId,
						.entityName = entityName,
						.componentType = compType,
						.oldValue = prevVal,
						.newValue = currVal,
						.frame = frame
					});
				}
			}
		}
	}

	std::map<scene::EntityId, EntitySnapshot> m_previousState;   ///< 前フレームのエンティティ状態
	std::uint64_t m_currentFrame = 0;                            ///< 現在のフレーム番号
	std::vector<SerializerEntry> m_serializers;                  ///< 登録済みシリアライザー
};

} // namespace mitiru::observe
