#pragma once
/// @file EnttBridge.hpp
/// @brief entt ECS ブリッジ
/// @details entt::registry をラップして MitiruEngine のゲームコードで使用可能にする。

#include <entt/entt.hpp>
#include <string>
#include <unordered_map>

namespace mitiru::ecs {

/// @brief entt ベースのワールド
/// @details entt::registry のシンプルなラッパー。
///          既存のMitiruWorldと並行して使用可能。
class EnttWorld {
public:
    using Entity = entt::entity;

    /// @brief エンティティを作成する
    [[nodiscard]] Entity createEntity(const std::string& name = "") {
        auto entity = m_registry.create();
        if (!name.empty()) m_names[entity] = name;
        return entity;
    }

    /// @brief エンティティを破棄する
    void destroyEntity(Entity entity) {
        m_names.erase(entity);
        m_registry.destroy(entity);
    }

    /// @brief コンポーネントを追加する
    template<typename T, typename... Args>
    T& addComponent(Entity entity, Args&&... args) {
        return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }

    /// @brief コンポーネントを取得する
    template<typename T>
    T* getComponent(Entity entity) {
        return m_registry.try_get<T>(entity);
    }

    /// @brief コンポーネントの有無を確認する
    template<typename T>
    bool hasComponent(Entity entity) const {
        return m_registry.all_of<T>(entity);
    }

    /// @brief ビューを取得する（イテレーション用）
    template<typename... Components>
    auto view() { return m_registry.view<Components...>(); }

    /// @brief エンティティ数を取得する
    [[nodiscard]] std::size_t entityCount() const noexcept {
        return static_cast<std::size_t>(m_registry.storage<entt::entity>()->in_use());
    }

    /// @brief レジストリへの直接アクセス
    [[nodiscard]] entt::registry& registry() noexcept { return m_registry; }
    [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }

    /// @brief エンティティ名を取得する
    [[nodiscard]] std::string entityName(Entity entity) const {
        auto it = m_names.find(entity);
        return (it != m_names.end()) ? it->second : "";
    }

private:
    entt::registry m_registry;
    std::unordered_map<entt::entity, std::string> m_names;
};

} // namespace mitiru::ecs
