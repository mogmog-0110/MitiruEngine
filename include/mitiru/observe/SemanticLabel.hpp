#pragma once

/// @file SemanticLabel.hpp
/// @brief エンティティ用セマンティックラベル
/// @details AIエージェントが読み取れるタグ情報をエンティティに付与する。
///          例: "controllable", "physics", "enemy" など。

#include <algorithm>
#include <string>
#include <vector>

namespace mitiru::observe
{

/// @brief エンティティに付与するセマンティックラベル群
/// @details AIエージェントがエンティティの役割を識別するために使用する。
///          ラベルは重複なしで管理される。
struct SemanticLabel
{
	std::vector<std::string> labels;  ///< ラベル一覧

	/// @brief ラベルを追加する
	/// @param label 追加するラベル文字列
	/// @note 既に存在する場合は追加しない
	void addLabel(const std::string& label)
	{
		if (!hasLabel(label))
		{
			labels.push_back(label);
		}
	}

	/// @brief 指定ラベルが存在するか判定する
	/// @param label 検索するラベル文字列
	/// @return 存在すれば true
	[[nodiscard]] bool hasLabel(const std::string& label) const
	{
		return std::find(labels.begin(), labels.end(), label) != labels.end();
	}

	/// @brief ラベルを削除する
	/// @param label 削除するラベル文字列
	void removeLabel(const std::string& label)
	{
		const auto it = std::find(labels.begin(), labels.end(), label);
		if (it != labels.end())
		{
			labels.erase(it);
		}
	}

	/// @brief ラベル数を取得する
	/// @return ラベル数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return labels.size();
	}

	/// @brief ラベルが空か判定する
	/// @return 空なら true
	[[nodiscard]] bool empty() const noexcept
	{
		return labels.empty();
	}

	/// @brief JSON配列文字列に変換する
	/// @return JSON配列形式の文字列（例: ["controllable","physics"]）
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < labels.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += "\"" + labels[i] + "\"";
		}
		json += "]";
		return json;
	}
};

} // namespace mitiru::observe
