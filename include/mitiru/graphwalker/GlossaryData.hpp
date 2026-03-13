#pragma once

/// @file GlossaryData.hpp
/// @brief GraphWalker数学用語辞典データモデル
///
/// 数学用語のエントリを保持し、検索・カテゴリフィルタ・JSON読み込みを提供する。
/// 基本的な演算子・三角関数・指数・定数のビルトインエントリを含む。
///
/// @code
/// mitiru::gw::GlossaryDatabase db;
/// db.loadBuiltinEntries();
/// auto entry = db.search("sin");
/// if (entry) { /* entry->definition を表示 */ }
/// @endcode

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::gw
{

/// @brief 用語辞典の1エントリ
struct GlossaryEntry
{
	std::string term;       ///< 用語名
	std::string definition; ///< 定義
	std::string example;    ///< 使用例
	std::string category;   ///< カテゴリ
};

/// @brief 数学用語辞典データベース
///
/// エントリの追加・検索・カテゴリ別取得・JSON読み込みを行う。
/// loadBuiltinEntries()で基本的な数学用語をプリセットできる。
class GlossaryDatabase
{
public:
	/// @brief エントリを追加する
	/// @param entry 追加するエントリ
	void addEntry(GlossaryEntry entry)
	{
		m_entries.push_back(std::move(entry));
	}

	/// @brief 用語名で完全一致検索する
	/// @param term 検索する用語名
	/// @return 見つかった場合はエントリ、見つからない場合はnullopt
	[[nodiscard]] std::optional<GlossaryEntry> search(const std::string& term) const
	{
		for (const auto& entry : m_entries)
		{
			if (entry.term == term)
			{
				return entry;
			}
		}
		return std::nullopt;
	}

	/// @brief カテゴリで絞り込んだエントリ一覧を取得する
	/// @param category カテゴリ名
	/// @return 該当するエントリ一覧
	[[nodiscard]] std::vector<GlossaryEntry> getByCategory(const std::string& category) const
	{
		std::vector<GlossaryEntry> result;
		for (const auto& entry : m_entries)
		{
			if (entry.category == category)
			{
				result.push_back(entry);
			}
		}
		return result;
	}

	/// @brief 全エントリを取得する
	/// @return エントリ一覧のconst参照
	[[nodiscard]] const std::vector<GlossaryEntry>& getAll() const { return m_entries; }

	/// @brief エントリ数を取得する
	/// @return 登録数
	[[nodiscard]] std::size_t size() const { return m_entries.size(); }

	/// @brief 全エントリをクリアする
	void clear() { m_entries.clear(); }

	/// @brief 簡易JSON文字列からエントリを読み込む
	///
	/// 期待フォーマット（1行1エントリ、タブ区切り）:
	/// term\tdefinition\texample\tcategory
	///
	/// @param tsvData タブ区切りデータ
	/// @return 1件以上読み込めればtrue
	bool loadFromJson(const std::string& tsvData)
	{
		if (tsvData.empty())
		{
			return false;
		}

		int loaded = 0;
		std::size_t pos = 0;

		while (pos < tsvData.size())
		{
			// 行末を探す
			auto lineEnd = tsvData.find('\n', pos);
			if (lineEnd == std::string::npos)
			{
				lineEnd = tsvData.size();
			}

			const std::string line = tsvData.substr(pos, lineEnd - pos);
			pos = lineEnd + 1;

			if (line.empty())
			{
				continue;
			}

			// タブで分割
			auto fields = splitByTab(line);
			if (fields.size() >= 4)
			{
				GlossaryEntry entry;
				entry.term = fields[0];
				entry.definition = fields[1];
				entry.example = fields[2];
				entry.category = fields[3];
				m_entries.push_back(std::move(entry));
				++loaded;
			}
		}

		return loaded > 0;
	}

	/// @brief ビルトインの数学用語エントリをロードする
	void loadBuiltinEntries()
	{
		// 基本演算
		addEntry({"+", "addition operator", "2 + 3 = 5", "basic"});
		addEntry({"-", "subtraction operator", "5 - 3 = 2", "basic"});
		addEntry({"*", "multiplication operator", "2 * 3 = 6", "basic"});
		addEntry({"/", "division operator", "6 / 3 = 2", "basic"});

		// 三角関数
		addEntry({"sin", "sine function", "sin(pi/2) = 1", "trigonometry"});
		addEntry({"cos", "cosine function", "cos(0) = 1", "trigonometry"});
		addEntry({"tan", "tangent function", "tan(pi/4) = 1", "trigonometry"});

		// 指数・対数
		addEntry({"exp", "exponential function", "exp(1) = e", "exponential"});
		addEntry({"log", "natural logarithm", "log(e) = 1", "exponential"});

		// 定数
		addEntry({"pi", "ratio of circumference to diameter", "pi = 3.14159...", "constant"});
		addEntry({"e", "Euler's number", "e = 2.71828...", "constant"});

		// 冪乗・絶対値
		addEntry({"^", "exponentiation operator", "2^3 = 8", "basic"});
		addEntry({"abs", "absolute value function", "abs(-5) = 5", "basic"});
	}

private:
	/// @brief タブ区切り文字列を分割する
	/// @param s 入力文字列
	/// @return 分割結果
	[[nodiscard]] static std::vector<std::string> splitByTab(const std::string& s)
	{
		std::vector<std::string> result;
		std::size_t start = 0;

		while (start <= s.size())
		{
			auto tabPos = s.find('\t', start);
			if (tabPos == std::string::npos)
			{
				result.push_back(s.substr(start));
				break;
			}
			result.push_back(s.substr(start, tabPos - start));
			start = tabPos + 1;
		}

		return result;
	}

	std::vector<GlossaryEntry> m_entries; ///< エントリ一覧
};

} // namespace mitiru::gw
