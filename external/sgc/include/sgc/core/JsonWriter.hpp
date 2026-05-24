#pragma once

/// @file JsonWriter.hpp
/// @brief JSONシリアライザ（ビジター実装）
///
/// JsonArrayWriter と JsonWriter を提供する。
/// Visitableコンセプトに基づくビジター方式のシリアライズ。
///
/// @code
/// sgc::JsonWriter w;
/// w.write("name", std::string("player"));
/// w.write("x", 1.5f);
/// std::string json = w.toString();
/// @endcode

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "sgc/core/SerializeConcepts.hpp"

namespace sgc
{

// ── 内部ユーティリティ ────────────────────────────────────────

namespace detail
{

/// @brief 浮動小数点を文字列化する（ロケール非依存）
/// @param v 値
/// @return 文字列表現
[[nodiscard]] inline std::string formatFloat(float v)
{
	if (std::isnan(v)) return "null";
	if (std::isinf(v)) return "null";
	std::array<char, 64> buf{};
	auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
	if (ec != std::errc{}) return "0";
	return std::string(buf.data(), ptr);
}

/// @brief 倍精度浮動小数点を文字列化する（ロケール非依存）
/// @param v 値
/// @return 文字列表現
[[nodiscard]] inline std::string formatDouble(double v)
{
	if (std::isnan(v)) return "null";
	if (std::isinf(v)) return "null";
	std::array<char, 64> buf{};
	auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
	if (ec != std::errc{}) return "0";
	return std::string(buf.data(), ptr);
}

/// @brief 文字列をJSONエスケープする
/// @param s 入力文字列
/// @return エスケープ済み文字列
[[nodiscard]] inline std::string escapeString(const std::string& s)
{
	std::string result;
	result.reserve(s.size());
	for (const char c : s)
	{
		switch (c)
		{
		case '"': result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default: result += c; break;
		}
	}
	return result;
}

} // namespace detail

// ── JsonArrayWriter ────────────────────────────────────────────

/// @brief JSON配列シリアライザ
///
/// @code
/// sgc::JsonArrayWriter arr;
/// arr.push(1);
/// arr.push(2);
/// arr.push(3);
/// std::string json = arr.toString(); // "[1,2,3]"
/// @endcode
class JsonArrayWriter
{
public:
	/// @brief 整数値を追加する
	/// @param value 値
	void push(int value)
	{
		m_elements.push_back(std::to_string(value));
	}

	/// @brief 浮動小数点値を追加する
	/// @param value 値
	void push(float value)
	{
		m_elements.push_back(detail::formatFloat(value));
	}

	/// @brief 倍精度浮動小数点値を追加する
	/// @param value 値
	void push(double value)
	{
		m_elements.push_back(detail::formatDouble(value));
	}

	/// @brief 真偽値を追加する
	/// @param value 値
	void push(bool value)
	{
		m_elements.push_back(value ? "true" : "false");
	}

	/// @brief 文字列を追加する
	/// @param value 値
	void push(const std::string& value)
	{
		m_elements.push_back("\"" + detail::escapeString(value) + "\"");
	}

	/// @brief ネストされたオブジェクトを追加する
	/// @param nested ネストされたJsonWriter
	void push(const JsonWriter& nested);

	/// @brief JSON配列文字列を生成する
	/// @return JSON配列文字列（例: "[1,2,3]"）
	[[nodiscard]] std::string toString() const
	{
		std::string result = "[";
		for (std::size_t i = 0; i < m_elements.size(); ++i)
		{
			if (i > 0) result += ",";
			result += m_elements[i];
		}
		result += "]";
		return result;
	}

private:
	std::vector<std::string> m_elements;
};

// ── JsonWriter ──────────────────────────────────────────────────

/// @brief JSONシリアライザ（ビジター実装）
///
/// @code
/// sgc::JsonWriter w;
/// w.write("name", std::string("player"));
/// w.write("x", 1.5f);
/// std::string json = w.toString();
/// @endcode
class JsonWriter
{
public:
	/// @brief 整数値を書き込む
	/// @param name キー名
	/// @param value 値
	void write(const std::string& name, int value)
	{
		m_fields.push_back({name, std::to_string(value)});
	}

	/// @brief 浮動小数点値を書き込む
	/// @param name キー名
	/// @param value 値
	void write(const std::string& name, float value)
	{
		m_fields.push_back({name, detail::formatFloat(value)});
	}

	/// @brief 倍精度浮動小数点値を書き込む
	/// @param name キー名
	/// @param value 値
	void write(const std::string& name, double value)
	{
		m_fields.push_back({name, detail::formatDouble(value)});
	}

	/// @brief 真偽値を書き込む
	/// @param name キー名
	/// @param value 値
	void write(const std::string& name, bool value)
	{
		m_fields.push_back({name, value ? "true" : "false"});
	}

	/// @brief 文字列を書き込む
	/// @param name キー名
	/// @param value 値
	void write(const std::string& name, const std::string& value)
	{
		m_fields.push_back({name, "\"" + detail::escapeString(value) + "\""});
	}

	/// @brief オブジェクトを書き込む（ネスト）
	/// @param name キー名
	/// @param nested ネストされたJsonWriter
	void write(const std::string& name, const JsonWriter& nested)
	{
		m_fields.push_back({name, nested.toString()});
	}

	/// @brief 配列を書き込む
	/// @param name キー名
	/// @param arr JsonArrayWriter
	void write(const std::string& name, const JsonArrayWriter& arr)
	{
		m_fields.push_back({name, arr.toString()});
	}

	/// @brief プリミティブ型のvectorを書き込む
	/// @tparam T int, float, double のいずれか
	/// @param name キー名
	/// @param vec ベクタ
	template <typename T>
		requires (std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, double>)
	void write(const std::string& name, const std::vector<T>& vec)
	{
		JsonArrayWriter arr;
		for (const auto& v : vec)
		{
			arr.push(v);
		}
		m_fields.push_back({name, arr.toString()});
	}

	/// @brief ConstVisitable型を書き込む（汎用ネスト書き込み）
	/// @tparam T visit(JsonWriter&)を持つ型
	/// @param name キー名
	/// @param obj オブジェクト
	template <typename T>
		requires ConstVisitable<T, JsonWriter>
			&& (!std::same_as<T, JsonWriter>)
			&& (!std::same_as<T, JsonArrayWriter>)
	void write(const std::string& name, const T& obj)
	{
		JsonWriter nested;
		obj.visit(nested);
		m_fields.push_back({name, nested.toString()});
	}

	/// @brief ConstVisitable型のvectorを書き込む
	/// @tparam T visit(JsonWriter&)を持つ型
	/// @param name キー名
	/// @param vec ベクタ
	template <typename T>
		requires ConstVisitable<T, JsonWriter>
			&& (!std::same_as<T, int>) && (!std::same_as<T, float>) && (!std::same_as<T, double>)
	void write(const std::string& name, const std::vector<T>& vec)
	{
		JsonArrayWriter arr;
		for (const auto& obj : vec)
		{
			JsonWriter nested;
			obj.visit(nested);
			arr.push(nested);
		}
		m_fields.push_back({name, arr.toString()});
	}

	/// @brief JSON文字列を生成する
	/// @return JSON文字列
	[[nodiscard]] std::string toString() const
	{
		std::string result = "{";
		for (std::size_t i = 0; i < m_fields.size(); ++i)
		{
			if (i > 0) result += ",";
			result += "\"" + m_fields[i].name + "\":" + m_fields[i].value;
		}
		result += "}";
		return result;
	}

private:
	/// @brief フィールド（キーと値のペア）
	struct Field
	{
		std::string name;   ///< キー名
		std::string value;  ///< 値（JSON文字列）
	};

	std::vector<Field> m_fields;
};

// ── JsonArrayWriter 遅延定義 ─────────────────────────────────

inline void JsonArrayWriter::push(const JsonWriter& nested)
{
	m_elements.push_back(nested.toString());
}

} // namespace sgc
