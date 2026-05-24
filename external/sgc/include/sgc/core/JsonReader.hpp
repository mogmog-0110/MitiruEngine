#pragma once

/// @file JsonReader.hpp
/// @brief JSONデシリアライザ（ビジター実装）
///
/// JSON文字列を解析し、型安全な読み込みインターフェースを提供する。
/// toJson / fromJson / fromJsonResult 便利関数も含む。
///
/// @code
/// sgc::JsonReader r;
/// r.fromString("{\"x\":1.5,\"name\":\"player\"}");
/// float x = 0;
/// r.read("x", x);
/// @endcode

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "sgc/core/JsonWriter.hpp"
#include "sgc/core/SerializeConcepts.hpp"

namespace sgc
{

/// @brief JSONデシリアライザ（ビジター実装）
///
/// @code
/// sgc::JsonReader r;
/// r.fromString("{\"x\":1.5,\"name\":\"player\"}");
/// float x = 0;
/// r.read("x", x);
/// std::string name;
/// r.read("name", name);
/// @endcode
class JsonReader
{
public:
	/// @brief JSON文字列を解析する
	/// @param json JSON文字列
	void fromString(const std::string& json)
	{
		m_values.clear();
		m_rawObjects.clear();
		m_rawArrays.clear();
		m_hasError = false;
		m_errorMessage.clear();

		// 空白を除去して先頭が'{' であることを確認
		std::size_t start = 0;
		while (start < json.size()
			&& (json[start] == ' ' || json[start] == '\t'
				|| json[start] == '\n' || json[start] == '\r'))
		{
			++start;
		}

		if (start >= json.size() || json[start] != '{')
		{
			m_hasError = true;
			m_errorMessage = "Invalid JSON: expected '{' at start";
			return;
		}
		parseObject(json, 0);
	}

	/// @brief パースエラーが発生したかを返す
	/// @return エラーがあればtrue
	[[nodiscard]] bool hasError() const noexcept { return m_hasError; }

	/// @brief エラーメッセージを返す
	/// @return エラーメッセージ（エラーがなければ空文字列）
	[[nodiscard]] const std::string& errorMessage() const noexcept { return m_errorMessage; }

	/// @brief 整数値を読み込む
	/// @param name キー名
	/// @param value 読み込み先
	void read(const std::string& name, int& value) const
	{
		const auto it = m_values.find(name);
		if (it != m_values.end())
		{
			const auto& s = it->second;
			auto result = std::from_chars(s.data(), s.data() + s.size(), value);
			if (result.ec != std::errc{}) value = 0;
		}
	}

	/// @brief 浮動小数点値を読み込む（ロケール非依存）
	/// @param name キー名
	/// @param value 読み込み先
	void read(const std::string& name, float& value) const
	{
		const auto it = m_values.find(name);
		if (it != m_values.end())
		{
			const auto& s = it->second;
			if (s == "null") { value = 0.0f; return; }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L \
	&& !defined(__GNUC__)
			// MSVC / Clang (libc++) は from_chars(float) をサポート
			auto result = std::from_chars(s.data(), s.data() + s.size(), value);
			if (result.ec != std::errc{}) value = 0.0f;
#else
			// GCC の libstdc++ は from_chars(float) が未実装の場合がある
			char* end = nullptr;
			value = std::strtof(s.c_str(), &end);
			if (end == s.c_str()) value = 0.0f;
#endif
		}
	}

	/// @brief 倍精度浮動小数点値を読み込む（ロケール非依存）
	/// @param name キー名
	/// @param value 読み込み先
	void read(const std::string& name, double& value) const
	{
		const auto it = m_values.find(name);
		if (it != m_values.end())
		{
			const auto& s = it->second;
			if (s == "null") { value = 0.0; return; }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L \
	&& !defined(__GNUC__)
			auto result = std::from_chars(s.data(), s.data() + s.size(), value);
			if (result.ec != std::errc{}) value = 0.0;
#else
			char* end = nullptr;
			value = std::strtod(s.c_str(), &end);
			if (end == s.c_str()) value = 0.0;
#endif
		}
	}

	/// @brief 真偽値を読み込む
	/// @param name キー名
	/// @param value 読み込み先
	void read(const std::string& name, bool& value) const
	{
		const auto it = m_values.find(name);
		if (it != m_values.end())
		{
			value = (it->second == "true");
		}
	}

	/// @brief 文字列を読み込む
	/// @param name キー名
	/// @param value 読み込み先
	void read(const std::string& name, std::string& value) const
	{
		const auto it = m_values.find(name);
		if (it != m_values.end())
		{
			value = it->second;
		}
	}

	/// @brief ネストされたオブジェクトを読み込む
	/// @param name キー名
	/// @param nested 読み込み先のJsonReader
	void read(const std::string& name, JsonReader& nested) const
	{
		const auto it = m_rawObjects.find(name);
		if (it != m_rawObjects.end())
		{
			nested.fromString(it->second);
		}
	}

	/// @brief int配列を読み込む
	/// @param name キー名
	/// @param vec 読み込み先
	void read(const std::string& name, std::vector<int>& vec) const
	{
		const auto it = m_rawArrays.find(name);
		if (it == m_rawArrays.end()) return;
		vec.clear();
		auto elements = parseArrayElements(it->second);
		for (const auto& s : elements)
		{
			int v = 0;
			std::from_chars(s.data(), s.data() + s.size(), v);
			vec.push_back(v);
		}
	}

	/// @brief float配列を読み込む
	/// @param name キー名
	/// @param vec 読み込み先
	void read(const std::string& name, std::vector<float>& vec) const
	{
		const auto it = m_rawArrays.find(name);
		if (it == m_rawArrays.end()) return;
		vec.clear();
		auto elements = parseArrayElements(it->second);
		for (const auto& s : elements)
		{
			float v = 0.0f;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L \
	&& !defined(__GNUC__)
			auto result = std::from_chars(s.data(), s.data() + s.size(), v);
			if (result.ec != std::errc{}) v = 0.0f;
#else
			char* end = nullptr;
			v = std::strtof(s.c_str(), &end);
			if (end == s.c_str()) v = 0.0f;
#endif
			vec.push_back(v);
		}
	}

	/// @brief double配列を読み込む
	/// @param name キー名
	/// @param vec 読み込み先
	void read(const std::string& name, std::vector<double>& vec) const
	{
		const auto it = m_rawArrays.find(name);
		if (it == m_rawArrays.end()) return;
		vec.clear();
		auto elements = parseArrayElements(it->second);
		for (const auto& s : elements)
		{
			double v = 0.0;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L \
	&& !defined(__GNUC__)
			auto result = std::from_chars(s.data(), s.data() + s.size(), v);
			if (result.ec != std::errc{}) v = 0.0;
#else
			char* end = nullptr;
			v = std::strtod(s.c_str(), &end);
			if (end == s.c_str()) v = 0.0;
#endif
			vec.push_back(v);
		}
	}

	/// @brief Visitable型を読み込む（汎用ネスト読み込み）
	/// @tparam T visit(JsonReader&)を持つ型
	/// @param name キー名
	/// @param obj 読み込み先
	template <typename T>
		requires Visitable<T, JsonReader>
			&& (!std::same_as<T, JsonReader>)
	void read(const std::string& name, T& obj) const
	{
		const auto it = m_rawObjects.find(name);
		if (it != m_rawObjects.end())
		{
			JsonReader nested;
			nested.fromString(it->second);
			obj.visit(nested);
		}
	}

	/// @brief Visitable型のvectorを読み込む
	/// @tparam T visit(JsonReader&)を持つ型
	/// @param name キー名
	/// @param vec 読み込み先
	template <typename T>
		requires Visitable<T, JsonReader>
			&& (!std::same_as<T, int>) && (!std::same_as<T, float>) && (!std::same_as<T, double>)
	void read(const std::string& name, std::vector<T>& vec) const
	{
		const auto it = m_rawArrays.find(name);
		if (it == m_rawArrays.end()) return;
		vec.clear();
		auto elements = parseArrayElements(it->second);
		for (const auto& elem : elements)
		{
			T obj{};
			JsonReader nested;
			nested.fromString(elem);
			obj.visit(nested);
			vec.push_back(std::move(obj));
		}
	}

	/// @brief 指定キーが存在するか確認する
	/// @param name キー名
	/// @return 存在すればtrue
	[[nodiscard]] bool has(const std::string& name) const
	{
		return m_values.contains(name)
			|| m_rawObjects.contains(name)
			|| m_rawArrays.contains(name);
	}

private:
	std::unordered_map<std::string, std::string> m_values;      ///< プリミティブ値
	std::unordered_map<std::string, std::string> m_rawObjects;  ///< ネストオブジェクト
	std::unordered_map<std::string, std::string> m_rawArrays;   ///< 配列
	bool m_hasError{false};               ///< パースエラーフラグ
	std::string m_errorMessage;           ///< エラーメッセージ

	/// @brief 空白をスキップする
	[[nodiscard]] static std::size_t skipWhitespace(const std::string& s, std::size_t pos)
	{
		while (pos < s.size()
			&& (s[pos] == ' ' || s[pos] == '\t'
				|| s[pos] == '\n' || s[pos] == '\r'))
		{
			++pos;
		}
		return pos;
	}

	/// @brief 文字列リテラルを解析する（開始引用符の位置から）
	/// @return {解析された文字列, 終了位置（閉じ引用符の次）}
	[[nodiscard]] static std::pair<std::string, std::size_t>
	parseString(const std::string& s, std::size_t pos)
	{
		if (pos >= s.size() || s[pos] != '"') return {"", pos};
		++pos;

		std::string result;
		while (pos < s.size() && s[pos] != '"')
		{
			if (s[pos] == '\\' && pos + 1 < s.size())
			{
				++pos;
				switch (s[pos])
				{
				case '"': result += '"'; break;
				case '\\': result += '\\'; break;
				case 'n': result += '\n'; break;
				case 'r': result += '\r'; break;
				case 't': result += '\t'; break;
				default: result += s[pos]; break;
				}
			}
			else
			{
				result += s[pos];
			}
			++pos;
		}
		if (pos < s.size()) ++pos; // 閉じ引用符をスキップ
		return {result, pos};
	}

	/// @brief 値の種別
	enum class ValueKind { Primitive, Object, Array };

	/// @brief 値を解析する（位置を返す）
	struct ParsedValue
	{
		std::string value;
		std::size_t endPos;
		ValueKind kind;
	};

	[[nodiscard]] static ParsedValue parseValue(const std::string& s, std::size_t pos)
	{
		pos = skipWhitespace(s, pos);
		if (pos >= s.size()) return {"", pos, ValueKind::Primitive};

		// 文字列
		if (s[pos] == '"')
		{
			auto [str, end] = parseString(s, pos);
			return {str, end, ValueKind::Primitive};
		}

		// ネストされたオブジェクト
		if (s[pos] == '{')
		{
			int depth = 0;
			std::size_t start = pos;
			while (pos < s.size())
			{
				if (s[pos] == '{') ++depth;
				else if (s[pos] == '}')
				{
					--depth;
					if (depth == 0) { ++pos; break; }
				}
				else if (s[pos] == '"')
				{
					// 文字列内のブレースをスキップ
					++pos;
					while (pos < s.size() && s[pos] != '"')
					{
						if (s[pos] == '\\') ++pos;
						++pos;
					}
				}
				++pos;
			}
			return {s.substr(start, pos - start), pos, ValueKind::Object};
		}

		// 配列
		if (s[pos] == '[')
		{
			int depth = 0;
			std::size_t start = pos;
			while (pos < s.size())
			{
				if (s[pos] == '[') ++depth;
				else if (s[pos] == ']')
				{
					--depth;
					if (depth == 0) { ++pos; break; }
				}
				else if (s[pos] == '"')
				{
					++pos;
					while (pos < s.size() && s[pos] != '"')
					{
						if (s[pos] == '\\') ++pos;
						++pos;
					}
				}
				++pos;
			}
			return {s.substr(start, pos - start), pos, ValueKind::Array};
		}

		// 数値、true、false、null
		std::size_t start = pos;
		while (pos < s.size() && s[pos] != ',' && s[pos] != '}'
			&& s[pos] != ']' && s[pos] != ' ')
		{
			++pos;
		}
		return {s.substr(start, pos - start), pos, ValueKind::Primitive};
	}

	/// @brief JSONオブジェクトを解析する
	void parseObject(const std::string& json, std::size_t pos)
	{
		pos = skipWhitespace(json, pos);
		if (pos >= json.size() || json[pos] != '{') return;
		++pos;

		while (pos < json.size())
		{
			pos = skipWhitespace(json, pos);
			if (pos >= json.size() || json[pos] == '}') break;

			// キー
			auto [key, keyEnd] = parseString(json, pos);
			pos = skipWhitespace(json, keyEnd);
			if (pos >= json.size() || json[pos] != ':') break;
			++pos;

			// 値
			auto parsed = parseValue(json, pos);
			pos = parsed.endPos;

			switch (parsed.kind)
			{
			case ValueKind::Object:
				m_rawObjects[key] = parsed.value;
				break;
			case ValueKind::Array:
				m_rawArrays[key] = parsed.value;
				break;
			default:
				m_values[key] = parsed.value;
				break;
			}

			pos = skipWhitespace(json, pos);
			if (pos < json.size() && json[pos] == ',') ++pos;
		}
	}

	/// @brief 配列文字列からトップレベル要素を分割する
	/// @param arrayStr "[1,2,3]" 形式の文字列
	/// @return 各要素の文字列
	[[nodiscard]] static std::vector<std::string>
	parseArrayElements(const std::string& arrayStr)
	{
		std::vector<std::string> elements;
		if (arrayStr.size() < 2 || arrayStr.front() != '[' || arrayStr.back() != ']')
			return elements;

		// 先頭の'['と末尾の']'を除去
		std::size_t pos = 1;
		const std::size_t end = arrayStr.size() - 1;

		while (pos < end)
		{
			pos = skipWhitespace(arrayStr, pos);
			if (pos >= end) break;

			auto parsed = parseValue(arrayStr, pos);
			if (!parsed.value.empty())
			{
				elements.push_back(parsed.value);
			}
			pos = skipWhitespace(arrayStr, parsed.endPos);
			if (pos < end && arrayStr[pos] == ',') ++pos;
		}
		return elements;
	}
};

// ── 便利関数 ────────────────────────────────────────────────────

/// @brief Visitable型をJSON文字列に変換する
/// @tparam T シリアライズする型
/// @param obj オブジェクト
/// @return JSON文字列
template <typename T>
[[nodiscard]] std::string toJson(const T& obj)
	requires ConstVisitable<T, JsonWriter>
{
	JsonWriter writer;
	obj.visit(writer);
	return writer.toString();
}

/// @brief JSON文字列からVisitable型を復元する
/// @tparam T デシリアライズする型
/// @param json JSON文字列
/// @return 復元されたオブジェクト
template <typename T>
[[nodiscard]] T fromJson(const std::string& json)
	requires std::default_initializable<T> && Visitable<T, JsonReader>
{
	JsonReader reader;
	reader.fromString(json);
	T obj{};
	obj.visit(reader);
	return obj;
}

} // namespace sgc

// Result.hppに依存するため、sgcネームスペース閉じの後にインクルード
#include "sgc/types/Result.hpp"

namespace sgc
{

/// @brief JSON文字列からVisitable型をResult<T>で復元する
///
/// パースエラー時にはエラーメッセージ付きのResult<T>を返す。
///
/// @tparam T デシリアライズする型
/// @param json JSON文字列
/// @return 成功時はT、失敗時はError
///
/// @code
/// auto result = sgc::fromJsonResult<PlayerData>(jsonString);
/// if (result)
/// {
///     auto& player = result.value();
/// }
/// else
/// {
///     // result.error().message にエラー詳細
/// }
/// @endcode
template <typename T>
[[nodiscard]] Result<T> fromJsonResult(const std::string& json)
	requires std::default_initializable<T> && Visitable<T, JsonReader>
{
	JsonReader reader;
	reader.fromString(json);
	if (reader.hasError())
	{
		return {ERROR_TAG, Error{reader.errorMessage()}};
	}
	T obj{};
	obj.visit(reader);
	return obj;
}

} // namespace sgc
