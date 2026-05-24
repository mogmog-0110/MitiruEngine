#pragma once

/// @file StringTable.hpp
/// @brief ローカライズ文字列テーブル
///
/// キーと翻訳テキストの対応を管理する。
/// DataNodeからの一括読み込み、プレースホルダー置換に対応。
///
/// @code
/// sgc::i18n::StringTable table;
/// table.addEntry("greeting", "Hello, {0}!");
/// table.addEntry("score", "Score: {0} / {1}");
///
/// auto msg = table.format("greeting", {"Alice"});
/// // => "Hello, Alice!"
///
/// auto score = table.format("score", {"100", "200"});
/// // => "Score: 100 / 200"
/// @endcode

#include <initializer_list>
#include <string>
#include <unordered_map>

#include "sgc/data/DataNode.hpp"

namespace sgc::i18n
{

/// @brief ローカライズ用文字列テーブル
///
/// キーから翻訳テキストを検索する。{0}, {1}, ... 形式のプレースホルダーを
/// 引数で置換するformat機能を提供する。
class StringTable
{
public:
	/// @brief DataNodeからエントリを一括読み込みする
	///
	/// DataNodeはオブジェクト型で、キー→文字列値の対応を持つことを期待する。
	///
	/// @param node データノード（オブジェクト型）
	///
	/// @code
	/// auto node = sgc::data::DataNode::parse(R"({
	///     "title": "My Game",
	///     "start": "Start Game"
	/// })");
	/// table.load(node);
	/// @endcode
	void load(const sgc::data::DataNode& node)
	{
		if (!node.isObject()) return;
		for (const auto& key : node.keys())
		{
			const auto& child = node[key];
			if (child.isString())
			{
				m_entries[key] = child.asString();
			}
		}
	}

	/// @brief エントリを追加する
	/// @param key キー文字列
	/// @param value 翻訳テキスト
	void addEntry(const std::string& key, const std::string& value)
	{
		m_entries[key] = value;
	}

	/// @brief キーに対応するテキストを取得する
	/// @param key キー文字列
	/// @return 翻訳テキスト（見つからなければ"[MISSING]"）
	[[nodiscard]] const std::string& get(const std::string& key) const
	{
		const auto it = m_entries.find(key);
		if (it != m_entries.end())
		{
			return it->second;
		}
		return MISSING_KEY;
	}

	/// @brief プレースホルダーを置換してテキストを取得する
	///
	/// {0}, {1}, {2} ... の形式のプレースホルダーを引数で順に置換する。
	///
	/// @param key キー文字列
	/// @param args 置換引数のリスト
	/// @return 置換済みテキスト
	[[nodiscard]] std::string format(const std::string& key,
	                                 std::initializer_list<std::string> args) const
	{
		std::string result = get(key);
		size_t argIndex = 0;
		for (const auto& arg : args)
		{
			const std::string placeholder = "{" + std::to_string(argIndex) + "}";
			size_t pos = 0;
			while ((pos = result.find(placeholder, pos)) != std::string::npos)
			{
				result.replace(pos, placeholder.size(), arg);
				pos += arg.size();
			}
			++argIndex;
		}
		return result;
	}

	/// @brief キーが存在するか判定する
	/// @param key キー文字列
	/// @return 存在すればtrue
	[[nodiscard]] bool hasKey(const std::string& key) const
	{
		return m_entries.find(key) != m_entries.end();
	}

	/// @brief 登録エントリ数を取得する
	/// @return エントリ数
	[[nodiscard]] size_t size() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 全エントリをクリアする
	void clear()
	{
		m_entries.clear();
	}

private:
	std::unordered_map<std::string, std::string> m_entries;

	/// @brief キーが見つからない場合の定数文字列
	static inline const std::string MISSING_KEY = "[MISSING]";
};

} // namespace sgc::i18n
