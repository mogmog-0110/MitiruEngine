#pragma once

/// @file CsvParser.hpp
/// @brief CSVパーサー（Siv3D CSV風）
/// @details カンマ区切りテキストを2次元文字列テーブルとして読み込む。
///          ダブルクォート内のカンマ・改行もサポートする。
///
/// @code
/// mitiru::data::CsvParser csv;
/// csv.parse("name,score\nAlice,100\nBob,200");
/// auto name = csv.get(1, 0);  // "Alice"
/// auto score = csv.getInt(1, 1);  // 100
/// @endcode

#include <sstream>
#include <string>
#include <vector>

namespace mitiru::data
{

/// @brief CSVパーサー
/// @details RFC 4180準拠のCSV解析を行う。
///          ダブルクォートで囲まれたフィールド内のカンマ・改行に対応する。
class CsvParser
{
public:
	/// @brief CSV文字列を解析する
	/// @param text CSV形式のテキスト
	void parse(const std::string& text)
	{
		m_rows.clear();
		std::vector<std::string> currentRow;
		std::string field;
		bool inQuotes = false;
		std::size_t i = 0;

		while (i < text.size())
		{
			const char ch = text[i];

			if (inQuotes)
			{
				if (ch == '"')
				{
					// エスケープされたダブルクォート
					if (i + 1 < text.size() && text[i + 1] == '"')
					{
						field += '"';
						i += 2;
					}
					else
					{
						inQuotes = false;
						++i;
					}
				}
				else
				{
					field += ch;
					++i;
				}
			}
			else
			{
				if (ch == '"')
				{
					inQuotes = true;
					++i;
				}
				else if (ch == ',')
				{
					currentRow.push_back(field);
					field.clear();
					++i;
				}
				else if (ch == '\n')
				{
					currentRow.push_back(field);
					field.clear();
					m_rows.push_back(std::move(currentRow));
					currentRow.clear();
					++i;
					// \r\nの\rは無視する
				}
				else if (ch == '\r')
				{
					++i;
				}
				else
				{
					field += ch;
					++i;
				}
			}
		}

		// 最後のフィールド・行を追加する
		if (!field.empty() || !currentRow.empty())
		{
			currentRow.push_back(field);
			m_rows.push_back(std::move(currentRow));
		}
	}

	/// @brief 行数を取得する
	[[nodiscard]] int rowCount() const noexcept
	{
		return static_cast<int>(m_rows.size());
	}

	/// @brief 指定行の列数を取得する
	/// @param row 行番号（0始まり）
	[[nodiscard]] int columnCount(int row) const noexcept
	{
		if (row < 0 || row >= static_cast<int>(m_rows.size())) return 0;
		return static_cast<int>(m_rows[row].size());
	}

	/// @brief セルの文字列値を取得する
	/// @param row 行番号（0始まり）
	/// @param col 列番号（0始まり）
	/// @return セルの文字列（範囲外は空文字列）
	[[nodiscard]] std::string get(int row, int col) const
	{
		if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};
		if (col < 0 || col >= static_cast<int>(m_rows[row].size())) return {};
		return m_rows[row][col];
	}

	/// @brief セルの整数値を取得する
	/// @param row 行番号
	/// @param col 列番号
	/// @param defaultVal デフォルト値
	/// @return 整数値
	[[nodiscard]] int getInt(int row, int col, int defaultVal = 0) const
	{
		const auto s = get(row, col);
		if (s.empty()) return defaultVal;
		try { return std::stoi(s); }
		catch (...) { return defaultVal; }
	}

	/// @brief セルの浮動小数点値を取得する
	/// @param row 行番号
	/// @param col 列番号
	/// @param defaultVal デフォルト値
	/// @return 浮動小数点値
	[[nodiscard]] float getFloat(int row, int col, float defaultVal = 0) const
	{
		const auto s = get(row, col);
		if (s.empty()) return defaultVal;
		try { return std::stof(s); }
		catch (...) { return defaultVal; }
	}

	/// @brief 指定行を取得する
	/// @param row 行番号
	/// @return 列のベクタ（範囲外は空）
	[[nodiscard]] std::vector<std::string> getRow(int row) const
	{
		if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};
		return m_rows[row];
	}

	/// @brief 全データをクリアする
	void clear() { m_rows.clear(); }

private:
	std::vector<std::vector<std::string>> m_rows; ///< 2次元テーブル
};

} // namespace mitiru::data
