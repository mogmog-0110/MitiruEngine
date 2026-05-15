#pragma once

/// @file Inspector.hpp
/// @brief ランタイム状態クエリAPI
/// @details AIエージェントがゲーム状態を問い合わせるための軽量API。
///          key-value形式の状態ストアを内部に保持し、
///          状態の登録・検索・JSON出力を提供する。
///          スナップショットリングヒストリー機能付き。

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::observe
{

/// @brief ランタイム状態インスペクター
/// @details ゲーム内の状態をkey-value形式で登録し、
///          AIエージェントからの問い合わせに応答する。
///          フルスナップショットの軽量な代替手段として機能する。
///          オプションでスナップショットリングヒストリーを保持できる。
class Inspector
{
public:
	/// @brief デフォルトコンストラクタ（ヒストリー無効）
	Inspector() = default;

	/// @brief ヒストリー付きコンストラクタ
	/// @param historyCapacity 保持するスナップショットの最大数（0=無効）
	explicit Inspector(std::size_t historyCapacity)
		: m_historyCapacity(historyCapacity)
	{
	}

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

	/// @brief 全状態をクリアする（ヒストリーは触らない）
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

	/// @brief 現在の m_states をリングに積む
	/// @details capacity を超えたら最古エントリを破棄する。
	/// @return コミットできた場合 true。historyCapacity == 0 の場合は false。
	bool commitSnapshot()
	{
		if (m_historyCapacity == 0)
		{
			return false;
		}
		m_history.push_back(m_states);
		if (m_history.size() > m_historyCapacity)
		{
			m_history.pop_front();
		}
		return true;
	}

	/// @brief 過去スナップショットから指定キーの値を取得する
	/// @param framesBack 0=最新コミット, 1=その前, ...
	/// @param key 検索キー
	/// @return 値が存在すれば文字列、範囲外またはキー不在なら nullopt
	[[nodiscard]] std::optional<std::string> queryAt(
		std::size_t framesBack, const std::string& key) const
	{
		if (framesBack >= m_history.size())
		{
			return std::nullopt;
		}
		const auto& snap = m_history[m_history.size() - 1 - framesBack];
		const auto it = snap.find(key);
		if (it == snap.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief 過去スナップショットをJSON文字列として返す
	/// @param framesBack 0=最新コミット, 1=その前, ...
	/// @return JSON文字列。範囲外なら "{}"
	[[nodiscard]] std::string queryAllAt(std::size_t framesBack) const
	{
		if (framesBack >= m_history.size())
		{
			return "{}";
		}
		return toJson(m_history[m_history.size() - 1 - framesBack]);
	}

	/// @brief 現在保持しているスナップショット数を返す
	/// @return スナップショット数（0〜capacity）
	[[nodiscard]] std::size_t historyDepth() const noexcept
	{
		return m_history.size();
	}

	/// @brief スナップショットの最大保持数を返す
	/// @return コンストラクタで指定した容量（0=無効）
	[[nodiscard]] std::size_t historyCapacity() const noexcept
	{
		return m_historyCapacity;
	}

	/// @brief リングヒストリーのみをクリアする（現在の m_states は触らない）
	void clearHistory() noexcept
	{
		m_history.clear();
	}

private:
	std::map<std::string, std::string> m_states;  ///< 状態ストア

	/// @brief スナップショットリング（front=最古、back=最新）
	std::deque<std::map<std::string, std::string>> m_history;

	/// @brief 最大保持スナップショット数（0=ヒストリー無効）
	std::size_t m_historyCapacity = 0;

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
			json += "\"" + jsonEscape(key) + "\":\"" + jsonEscape(value) + "\"";
			first = false;
		}
		json += "}";
		return json;
	}
};

} // namespace mitiru::observe
