#pragma once

/// @file GameWorld.hpp
/// @brief 高レベルなゲームワールド（コンポーネント管理）
///
/// 型消去コンポーネントストレージとエンティティ管理を統合し、
/// シンプルなゲームオブジェクト管理を提供する。
///
/// @code
/// mitiru::scene::GameWorld world;
/// auto id = world.createEntity("Player");
/// world.addComponent(id, TransformComponent{{0,0,0},{0,0,0},{1,1,1}});
/// world.addComponent(id, MeshComponent{"player_mesh"});
/// world.forEach<TransformComponent>([](auto id, auto& t) {
///     t.position.y += 0.1f;
/// });
/// @endcode

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sgc/math/Vec3.hpp"

namespace mitiru::scene
{

/// @brief エンティティ識別子
using EntityId = uint32_t;

/// @brief 無効なエンティティID
inline constexpr EntityId INVALID_ENTITY = 0;

// ── 標準コンポーネント ──────────────────────────────

/// @brief トランスフォームコンポーネント
struct TransformComponent
{
	sgc::Vec3f position{0.0f, 0.0f, 0.0f};  ///< 位置
	sgc::Vec3f rotation{0.0f, 0.0f, 0.0f};  ///< 回転（オイラー角、ラジアン）
	sgc::Vec3f scale{1.0f, 1.0f, 1.0f};     ///< 拡大率
};

/// @brief メッシュコンポーネント
struct MeshComponent
{
	std::string meshId;  ///< メッシュアセットID
};

/// @brief ライトの種類
enum class LightType
{
	Directional,  ///< 平行光源
	Point,        ///< 点光源
	Spot          ///< スポットライト
};

/// @brief ライトコンポーネント
struct LightComponent
{
	LightType type{LightType::Point};    ///< ライト種類
	sgc::Vec3f color{1.0f, 1.0f, 1.0f}; ///< ライト色
	float intensity{1.0f};               ///< 強度
	float range{10.0f};                  ///< 影響範囲
};

/// @brief カメラコンポーネント
struct CameraComponent
{
	float fov{60.0f};       ///< 視野角（度）
	float nearClip{0.1f};   ///< ニアクリップ
	float farClip{1000.0f}; ///< ファークリップ
	bool active{false};     ///< アクティブカメラか
};

/// @brief 音源コンポーネント
struct AudioSourceComponent
{
	std::string soundId;  ///< サウンドアセットID
	float volume{1.0f};  ///< 音量
	bool loop{false};     ///< ループ再生か
};

/// @brief コンポーネントストレージ（型消去）
class ComponentStore
{
public:
	/// @brief コンポーネントを追加する
	template <typename T>
	void add(EntityId entityId, T component)
	{
		auto& storage = getOrCreateStorage<T>();
		storage[entityId] = std::move(component);
	}

	/// @brief コンポーネントを取得する
	template <typename T>
	[[nodiscard]] T* get(EntityId entityId) noexcept
	{
		auto it = m_storages.find(std::type_index(typeid(T)));
		if (it == m_storages.end()) return nullptr;
		auto& storage = std::any_cast<Storage<T>&>(it->second);
		auto compIt = storage.find(entityId);
		if (compIt == storage.end()) return nullptr;
		return &compIt->second;
	}

	/// @brief コンポーネントを取得する（const版）
	template <typename T>
	[[nodiscard]] const T* get(EntityId entityId) const noexcept
	{
		auto it = m_storages.find(std::type_index(typeid(T)));
		if (it == m_storages.end()) return nullptr;
		const auto& storage = std::any_cast<const Storage<T>&>(it->second);
		auto compIt = storage.find(entityId);
		if (compIt == storage.end()) return nullptr;
		return &compIt->second;
	}

	/// @brief コンポーネントの有無を確認する
	template <typename T>
	[[nodiscard]] bool has(EntityId entityId) const noexcept
	{
		auto it = m_storages.find(std::type_index(typeid(T)));
		if (it == m_storages.end()) return false;
		const auto& storage = std::any_cast<const Storage<T>&>(it->second);
		return storage.count(entityId) > 0;
	}

	/// @brief 特定エンティティの全コンポーネントを削除する
	void removeAll(EntityId entityId)
	{
		for (auto& [typeIdx, anyStorage] : m_storages)
		{
			if (auto it = m_erasers.find(typeIdx); it != m_erasers.end())
			{
				it->second(anyStorage, entityId);
			}
		}
	}

	/// @brief 特定のコンポーネント型を持つ全エンティティに対して処理を行う
	template <typename T>
	void forEach(const std::function<void(EntityId, T&)>& callback)
	{
		auto it = m_storages.find(std::type_index(typeid(T)));
		if (it == m_storages.end()) return;
		auto& storage = std::any_cast<Storage<T>&>(it->second);
		for (auto& [id, component] : storage)
		{
			callback(id, component);
		}
	}

private:
	template <typename T>
	using Storage = std::unordered_map<EntityId, T>;

	using EraseFn = std::function<void(std::any&, EntityId)>;

	template <typename T>
	Storage<T>& getOrCreateStorage()
	{
		auto key = std::type_index(typeid(T));
		auto it = m_storages.find(key);
		if (it == m_storages.end())
		{
			m_storages[key] = Storage<T>{};
			m_erasers[key] = [](std::any& anyStorage, EntityId eid)
			{
				auto& storage = std::any_cast<Storage<T>&>(anyStorage);
				storage.erase(eid);
			};
			it = m_storages.find(key);
		}
		return std::any_cast<Storage<T>&>(it->second);
	}

	std::unordered_map<std::type_index, std::any> m_storages;
	std::unordered_map<std::type_index, EraseFn> m_erasers;
};

/// @brief ゲームワールド
class GameWorld
{
public:
	/// @brief エンティティを作成する
	/// @param name エンティティ名
	/// @return 新規エンティティID
	[[nodiscard]] EntityId createEntity(const std::string& name = "")
	{
		const EntityId id = ++m_nextId;
		m_entities.insert(id);
		if (!name.empty())
		{
			m_names[id] = name;
		}
		return id;
	}

	/// @brief エンティティを破棄する
	void removeEntity(EntityId entityId)
	{
		if (m_entities.count(entityId) == 0) return;
		m_components.removeAll(entityId);
		m_entities.erase(entityId);
		m_names.erase(entityId);
	}

	/// @brief エンティティが有効か
	[[nodiscard]] bool isValid(EntityId entityId) const noexcept
	{
		return m_entities.count(entityId) > 0;
	}

	/// @brief エンティティ数を返す
	[[nodiscard]] size_t entityCount() const noexcept
	{
		return m_entities.size();
	}

	/// @brief エンティティ名を返す
	[[nodiscard]] std::string entityName(EntityId entityId) const
	{
		auto it = m_names.find(entityId);
		if (it == m_names.end()) return "";
		return it->second;
	}

	/// @brief コンポーネントを追加する
	template <typename T>
	void addComponent(EntityId entityId, T component)
	{
		if (!isValid(entityId)) return;
		m_components.add<T>(entityId, std::move(component));
	}

	/// @brief コンポーネントを取得する
	template <typename T>
	[[nodiscard]] T* getComponent(EntityId entityId) noexcept
	{
		return m_components.get<T>(entityId);
	}

	/// @brief コンポーネントを取得する（const版）
	template <typename T>
	[[nodiscard]] const T* getComponent(EntityId entityId) const noexcept
	{
		return m_components.get<T>(entityId);
	}

	/// @brief コンポーネントの有無を確認する
	template <typename T>
	[[nodiscard]] bool hasComponent(EntityId entityId) const noexcept
	{
		return m_components.has<T>(entityId);
	}

	/// @brief 特定のコンポーネント型を持つ全エンティティに対して処理を行う
	template <typename T>
	void forEach(const std::function<void(EntityId, T&)>& callback)
	{
		m_components.forEach<T>(callback);
	}

private:
	EntityId m_nextId{0};
	std::unordered_set<EntityId> m_entities;
	std::unordered_map<EntityId, std::string> m_names;
	ComponentStore m_components;
};

} // namespace mitiru::scene
