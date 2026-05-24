#pragma once

/// @file Factory.hpp
/// @brief 型登録ファクトリパターン — キーに基づくオブジェクト生成
///
/// 文字列やenum等のキーで型を登録し、実行時にオブジェクトを生成する。
/// エンティティ生成・UI部品の動的構築・プラグインシステム等に使用する。
///
/// @code
/// struct Enemy { virtual ~Enemy() = default; };
/// struct Goblin : Enemy {};
/// struct Dragon : Enemy {};
///
/// sgc::Factory<Enemy> factory;
/// factory.registerType("goblin", []() { return std::make_unique<Goblin>(); });
/// factory.registerType("dragon", []() { return std::make_unique<Dragon>(); });
///
/// auto enemy = factory.create("goblin");
/// @endcode

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc
{

/// @brief 型登録ファクトリパターン
/// @tparam Base 生成する基底クラス型
/// @tparam Key キーの型（デフォルト: std::string）
/// @tparam Args createに渡す追加引数の型
template <typename Base, typename Key = std::string, typename... Args>
class Factory
{
public:
	/// @brief 生成関数の型
	using Creator = std::function<std::unique_ptr<Base>(Args...)>;

	/// @brief 型を登録する
	/// @param key 登録キー
	/// @param creator 生成関数
	/// @throw std::runtime_error 同一キーが既に登録されている場合
	void registerType(const Key& key, Creator creator)
	{
		if (m_creators.contains(key))
		{
			throw std::runtime_error("Factory: key already registered");
		}
		m_creators.emplace(key, std::move(creator));
	}

	/// @brief オブジェクトを生成する
	/// @param key 登録キー
	/// @param args 生成関数に渡す引数
	/// @return 生成されたオブジェクト
	/// @throw std::runtime_error キーが未登録の場合
	[[nodiscard]] std::unique_ptr<Base> create(const Key& key, Args... args) const
	{
		const auto it = m_creators.find(key);
		if (it == m_creators.end())
		{
			throw std::runtime_error("Factory: unknown key");
		}
		return it->second(std::forward<Args>(args)...);
	}

	/// @brief キーが登録済みか確認する
	/// @param key 確認するキー
	/// @return 登録されていればtrue
	[[nodiscard]] bool isRegistered(const Key& key) const noexcept
	{
		return m_creators.contains(key);
	}

	/// @brief 登録済みキーの一覧を返す
	/// @return キーのvector
	[[nodiscard]] std::vector<Key> registeredKeys() const
	{
		std::vector<Key> keys;
		keys.reserve(m_creators.size());
		for (const auto& [k, v] : m_creators)
		{
			keys.push_back(k);
		}
		return keys;
	}

	/// @brief 登録を全てクリアする
	void clear() noexcept
	{
		m_creators.clear();
	}

	/// @brief 登録済み型の数を返す
	/// @return 登録数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_creators.size();
	}

private:
	std::unordered_map<Key, Creator> m_creators;  ///< キーと生成関数のマップ
};

} // namespace sgc
