#pragma once

/// @file FontLoader.hpp
/// @brief ビットマップフォントメトリクスローダー
/// @details フォントメトリクス情報をファイルまたは埋め込みデータからロードする。
///          シンプルなキー=値形式のテキストファイルに対応する。

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace mitiru::resource
{

/// @brief フォントメトリクス情報
/// @details ビットマップフォントのグリフサイズや文字範囲を保持する。
struct FontMetrics
{
	int glyphWidth = 8;     ///< グリフ幅（ピクセル）
	int glyphHeight = 8;    ///< グリフ高さ（ピクセル）
	int firstChar = 32;     ///< 最初の文字コード（ASCII）
	int lastChar = 126;     ///< 最後の文字コード（ASCII）
	std::string name;       ///< フォント名

	/// @brief グリフ数を取得する
	/// @return 格納されているグリフの総数
	[[nodiscard]] constexpr int glyphCount() const noexcept
	{
		return (lastChar >= firstChar) ? (lastChar - firstChar + 1) : 0;
	}

	/// @brief 指定文字がフォントに含まれるか判定する
	/// @param ch 文字コード
	/// @return 範囲内なら true
	[[nodiscard]] constexpr bool contains(char ch) const noexcept
	{
		const int code = static_cast<int>(ch);
		return code >= firstChar && code <= lastChar;
	}

	/// @brief テキストの描画幅を計算する
	/// @param text テキスト
	/// @param scale 拡大率
	/// @return 描画幅（ピクセル）
	[[nodiscard]] constexpr int textWidth(std::string_view text, int scale = 1) const noexcept
	{
		return static_cast<int>(text.size()) * glyphWidth * scale;
	}

	/// @brief テキストの描画高さを計算する
	/// @param scale 拡大率
	/// @return 描画高さ（ピクセル）
	[[nodiscard]] constexpr int textHeight(int scale = 1) const noexcept
	{
		return glyphHeight * scale;
	}
};

/// @brief フォントメトリクスローダー
/// @details フォントメトリクス情報をファイルまたは埋め込みデータからロードする。
///
///          ファイル形式（キー=値のシンプルなテキスト形式）:
///          @code
///          name=DefaultFont
///          glyphWidth=8
///          glyphHeight=8
///          firstChar=32
///          lastChar=126
///          @endcode
///
/// @code
/// mitiru::resource::FontLoader loader;
///
/// // ファイルから読み込む
/// auto metrics = loader.loadFromFile("fonts/default.fnt");
///
/// // 埋め込みデータから生成する
/// auto embedded = FontLoader::createDefault();
/// @endcode
class FontLoader
{
public:
	/// @brief デフォルトコンストラクタ
	FontLoader() noexcept = default;

	/// @brief ファイルからフォントメトリクスを読み込む
	/// @param path ファイルパス（キー=値形式のテキストファイル）
	/// @return フォントメトリクス（読み込み失敗時はnullopt）
	[[nodiscard]] std::optional<FontMetrics> loadFromFile(std::string_view path) const
	{
		std::ifstream file(std::string(path));
		if (!file.is_open())
		{
			return std::nullopt;
		}

		std::string content(
			(std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());

		return parseMetrics(content);
	}

	/// @brief 文字列からフォントメトリクスをパースする
	/// @param content キー=値形式の文字列
	/// @return フォントメトリクス（パース失敗時はnullopt）
	[[nodiscard]] static std::optional<FontMetrics> parseMetrics(std::string_view content)
	{
		FontMetrics metrics;
		bool hasAnyField = false;

		std::istringstream stream(std::string(content));
		std::string line;

		while (std::getline(stream, line))
		{
			/// 空行とコメント行をスキップする
			if (line.empty() || line[0] == '#')
			{
				continue;
			}

			/// '='でキーと値を分割する
			const auto eqPos = line.find('=');
			if (eqPos == std::string::npos)
			{
				continue;
			}

			const auto key = trimWhitespace(line.substr(0, eqPos));
			const auto value = trimWhitespace(line.substr(eqPos + 1));

			if (key == "name")
			{
				metrics.name = value;
				hasAnyField = true;
			}
			else if (key == "glyphWidth")
			{
				metrics.glyphWidth = parseIntOr(value, metrics.glyphWidth);
				hasAnyField = true;
			}
			else if (key == "glyphHeight")
			{
				metrics.glyphHeight = parseIntOr(value, metrics.glyphHeight);
				hasAnyField = true;
			}
			else if (key == "firstChar")
			{
				metrics.firstChar = parseIntOr(value, metrics.firstChar);
				hasAnyField = true;
			}
			else if (key == "lastChar")
			{
				metrics.lastChar = parseIntOr(value, metrics.lastChar);
				hasAnyField = true;
			}
		}

		if (!hasAnyField)
		{
			return std::nullopt;
		}

		return metrics;
	}

	/// @brief デフォルトのフォントメトリクスを生成する
	/// @details BitmapFont互換の8x8 ASCII（32-126）メトリクスを返す。
	/// @return デフォルトメトリクス
	[[nodiscard]] static constexpr FontMetrics createDefault() noexcept
	{
		FontMetrics metrics;
		metrics.glyphWidth = 8;
		metrics.glyphHeight = 8;
		metrics.firstChar = 32;
		metrics.lastChar = 126;
		return metrics;
	}

	/// @brief カスタムメトリクスを生成する
	/// @param fontName フォント名
	/// @param glyphW グリフ幅
	/// @param glyphH グリフ高さ
	/// @param first 最初の文字コード
	/// @param last 最後の文字コード
	/// @return カスタムメトリクス
	[[nodiscard]] static FontMetrics createCustom(
		std::string_view fontName,
		int glyphW, int glyphH,
		int first = 32, int last = 126)
	{
		FontMetrics metrics;
		metrics.name = std::string(fontName);
		metrics.glyphWidth = glyphW;
		metrics.glyphHeight = glyphH;
		metrics.firstChar = first;
		metrics.lastChar = last;
		return metrics;
	}

private:
	/// @brief 前後の空白を除去する
	/// @param str 対象文字列
	/// @return トリム済み文字列
	[[nodiscard]] static std::string trimWhitespace(const std::string& str)
	{
		const auto start = str.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
		{
			return "";
		}
		const auto end = str.find_last_not_of(" \t\r\n");
		return str.substr(start, end - start + 1);
	}

	/// @brief 文字列を整数にパースする（失敗時はデフォルト値）
	/// @param str 数値文字列
	/// @param defaultValue パース失敗時のデフォルト値
	/// @return パース結果
	[[nodiscard]] static int parseIntOr(const std::string& str, int defaultValue) noexcept
	{
		try
		{
			return std::stoi(str);
		}
		catch (...)
		{
			return defaultValue;
		}
	}
};

} // namespace mitiru::resource
