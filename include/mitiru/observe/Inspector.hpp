#pragma once

/// @file Inspector.hpp
/// @brief ランタイム状態クエリAPI
/// @details AIエージェントがゲーム状態を問い合わせるための軽量API。
///          key-value形式の状態ストアを内部に保持し、
///          状態の登録・検索・JSON出力を提供する。

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::observe
{

/// @brief ランタイム状態インスペクター
/// @details ゲーム内の状態をkey-value形式で登録し、
///          AIエージェントからの問い合わせに応答する。
///          フルスナップショットの軽量な代替手段として機能する。
class Inspector
{
public:
	/// @brief 観測可能な状態を登録（または更新）する
	/// @param key 状態のキー
	/// @param value 状態の値
	void registerState(const std::string& key, const std::string& value)
	{
		m_states[key] = value;
	}

	/// @brief 指定キーの状態を問い合わせる
	/// @param key 検索キー
	/// @return 値が存在すればその文字列、なければ nullopt
	[[nodiscard]] std::optional<std::string> queryState(const std::string& key) const
	{
		const auto it = m_states.find(key);
		if (it != m_states.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief 全状態をJSON文字列として返す
	/// @return JSON オブジェクト形式の文字列
	[[nodiscard]] std::string queryAll() const
	{
		return toJson(m_states);
	}

	/// @brief 指定プレフィックスに一致する状態をフィルタして返す
	/// @param prefix キーのプレフィックス
	/// @return フィルタ結果のJSON文字列
	[[nodiscard]] std::string queryByPrefix(std::string_view prefix) const
	{
		std::map<std::string, std::string> filtered;
		for (const auto& [key, value] : m_states)
		{
			if (key.size() >= prefix.size() &&
				key.compare(0, prefix.size(), prefix) == 0)
			{
				filtered[key] = value;
			}
		}
		return toJson(filtered);
	}

	/// @brief 指定キーの状態を削除する
	/// @param key 削除するキー
	/// @return 削除できた場合 true
	bool removeState(const std::string& key)
	{
		return m_states.erase(key) > 0;
	}

	/// @brief 全状態をクリアする
	void clear() noexcept
	{
		m_states.clear();
	}

	/// @brief 登録されている状態の数を返す
	/// @return 状態の数
	[[nodiscard]] std::size_t stateCount() const noexcept
	{
		return m_states.size();
	}

private:
	std::map<std::string, std::string> m_states;  ///< 状態ストア

	/// @brief map を JSON オブジェクト文字列に変換する
	/// @param data 変換対象のmap
	/// @return JSON文字列
	[[nodiscard]] static std::string toJson(const std::map<std::string, std::string>& data)
	{
		std::string json;
		json += "{";
		bool first = true;
		for (const auto& [key, value] : data)
		{
			if (!first)
			{
				json += ",";
			}
			json += "\"" + key + "\":\"" + value + "\"";
			first = false;
		}
		json += "}";
		return json;
	}
};

} // namespace mitiru::observe
