#pragma once

/// @file ComponentRegistry.hpp
/// @brief ランタイムコンポーネント型レジストリ
/// @details コンポーネント型の名前とシリアライザを実行時に登録し、
///          型消去されたコンポーネントデータをJSON等にシリアライズする。

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/core/TypeId.hpp>

namespace mitiru::ecs
{

/// @brief コンポーネントシリアライザ型
/// @details void* からコンポーネントデータを受け取り、文字列に変換する関数。
using ComponentSerializer = std::function<std::string(const void*)>;

/// @brief ランタイムコンポーネント型レジストリ
/// @details コンポーネント型を名前とシリアライザ付きで登録し、
///          動的なイントロスペクションを可能にする。
///
/// @code
/// mitiru::ecs::ComponentRegistry registry;
/// registry.registerType<Position>("Position", [](const void* p) {
///     const auto& pos = *static_cast<const Position*>(p);
///     return "{\"x\":" + std::to_string(pos.x) + "}";
/// });
/// @endcode
class ComponentRegistry
{
public:
	/// @brief コンポーネント型を登録する
	/// @tparam T コンポーネント型
	/// @param name 型の表示名
	/// @param serializer シリアライズ関数
	template <typename T>
	void registerType(std::string name, ComponentSerializer serializer)
	{
		const auto typeId = sgc::typeId<T>();
		m_entries[typeId] = Entry{std::move(name), std::move(serializer)};
	}

	/// @brief 指定型IDのコンポーネントデータをシリアライズする
	/// @param typeId コンポーネントの型ID
	/// @param data コンポーネントデータへのポインタ
	/// @return シリアライズされた文字列（未登録の場合は空文字列）
	[[nodiscard]] std::string serialize(sgc::TypeIdValue typeId, const void* data) const
	{
		const auto it = m_entries.find(typeId);
		if (it == m_entries.end() || !data)
		{
			return {};
		}
		return it->second.serializer(data);
	}

	/// @brief 登録済み型名の一覧を取得する
	/// @return 型名のベクタ
	[[nodiscard]] std::vector<std::string> registeredNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_entries.size());
		for (const auto& [id, entry] : m_entries)
		{
			names.push_back(entry.name);
		}
		return names;
	}

	/// @brief 指定型が登録済みか判定する
	/// @tparam T コンポーネント型
	/// @return 登録済みなら true
	template <typename T>
	[[nodiscard]] bool isRegistered() const
	{
		return m_entries.find(sgc::typeId<T>()) != m_entries.end();
	}

	/// @brief 登録数を取得する
	/// @return 登録されている型の数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_entries.size();
	}

	/// @brief JSON文字列に変換する
	/// @return 登録型名の一覧をJSON配列で返す
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "[";
		bool first = true;
		for (const auto& [id, entry] : m_entries)
		{
			if (!first)
			{
				json += ",";
			}
			json += "\"" + entry.name + "\"";
			first = false;
		}
		json += "]";
		return json;
	}

private:
	/// @brief 内部エントリ
	struct Entry
	{
		std::string name;                 ///< 型の表示名
		ComponentSerializer serializer;   ///< シリアライズ関数
	};

	/// @brief 型ID → エントリ
	std::unordered_map<sgc::TypeIdValue, Entry> m_entries;
};

} // namespace mitiru::ecs
