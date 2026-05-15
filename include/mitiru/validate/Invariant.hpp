#pragma once

/// @file Invariant.hpp
/// @brief 不変条件チェックシステム
/// @details ゲームロジックの不変条件を登録し、フレーム毎に検証する。
///          AIエージェントがゲーム状態の健全性を確認するために使用する。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::validate
{

/// @brief 検証結果
struct ValidationResult
{
	bool passed = false;          ///< 検証に合格したか
	std::string name;             ///< 不変条件の名前
	std::string message;          ///< 失敗時のメッセージ
	std::uint64_t frame = 0;      ///< 検証を実行したフレーム番号

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"passed\":" + std::string(passed ? "true" : "false") + ",";
		json += "\"name\":\"" + name + "\",";
		json += "\"message\":\"" + message + "\",";
		json += "\"frame\":" + std::to_string(frame);
		json += "}";
		return json;
	}
};

/// @brief 不変条件レジストリ
/// @details 名前付きの述語関数を登録し、一括検証を行う。
///
/// @code
/// mitiru::validate::InvariantRegistry registry;
/// registry.add("player_health_positive", []() { return player.health > 0; });
/// auto results = registry.checkAll(frameNumber);
/// @endcode
class InvariantRegistry
{
public:
	/// @brief 述語関数型
	using Predicate = std::function<bool()>;

	/// @brief 不変条件を登録する
	/// @param name 不変条件の名前（一意であること）
	/// @param pred 検証述語（trueで合格）
	void add(std::string name, Predicate pred)
	{
		m_entries.push_back(Entry{std::move(name), std::move(pred)});
	}

	/// @brief 不変条件を削除する
	/// @param name 削除する不変条件の名前
	void remove(const std::string& name)
	{
		m_entries.erase(
			std::remove_if(m_entries.begin(), m_entries.end(),
				[&name](const Entry& e) { return e.name == name; }),
			m_entries.end());
	}

	/// @brief 全ての不変条件を検証する
	/// @param frame 現在のフレーム番号
	/// @return 検証結果のリスト
	[[nodiscard]] std::vector<ValidationResult> checkAll(std::uint64_t frame) const
	{
		std::vector<ValidationResult> results;
		results.reserve(m_entries.size());

		for (const auto& entry : m_entries)
		{
			ValidationResult result;
			result.name = entry.name;
			result.frame = frame;
			result.passed = entry.predicate();
			result.message = result.passed ? "OK" : "FAILED: " + entry.name;
			results.push_back(std::move(result));
		}

		return results;
	}

	/// @brief 全検証結果をJSON文字列として返す
	/// @param frame 現在のフレーム番号
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson(std::uint64_t frame) const
	{
		const auto results = checkAll(frame);
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += results[i].toJson();
		}
		json += "]";
		return json;
	}

	/// @brief 登録されている不変条件の数を取得する
	/// @return 不変条件の数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 全不変条件をクリアする
	void clear() noexcept
	{
		m_entries.clear();
	}

private:
	/// @brief 内部エントリ
	struct Entry
	{
		std::string name;        ///< 不変条件の名前
		Predicate predicate;     ///< 検証述語
	};

	std::vector<Entry> m_entries;  ///< 登録済み不変条件
};

} // namespace mitiru::validate
