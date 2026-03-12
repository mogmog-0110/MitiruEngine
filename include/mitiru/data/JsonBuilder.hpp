#pragma once

/// @file JsonBuilder.hpp
/// @brief 軽量JSONビルダー・リーダーユーティリティ
///
/// 外部依存なしでJSON文字列の構築・簡易パースを行う。
///
/// @code
/// mitiru::data::JsonBuilder builder;
/// auto json = builder.beginObject()
///     .key("name").value("test")
///     .key("score").value(100)
///     .endObject().build();
///
/// mitiru::data::JsonReader reader;
/// reader.parse(json);
/// auto name = reader.getString("name"); // "test"
/// @endcode

#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::data
{

/// @brief 流暢なAPIでJSON文字列を構築するビルダー
class JsonBuilder
{
public:
	/// @brief オブジェクトの開始
	/// @return 自身への参照（メソッドチェーン用）
	JsonBuilder& beginObject()
	{
		appendSeparator();
		m_buffer += '{';
		m_stack.push_back(State::Object);
		m_needSep.push_back(false);
		return *this;
	}

	/// @brief オブジェクトの終了
	/// @return 自身への参照
	JsonBuilder& endObject()
	{
		m_buffer += '}';
		if (!m_stack.empty()) m_stack.pop_back();
		if (!m_needSep.empty()) m_needSep.pop_back();
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 配列の開始
	/// @return 自身への参照
	JsonBuilder& beginArray()
	{
		appendSeparator();
		m_buffer += '[';
		m_stack.push_back(State::Array);
		m_needSep.push_back(false);
		return *this;
	}

	/// @brief 配列の終了
	/// @return 自身への参照
	JsonBuilder& endArray()
	{
		m_buffer += ']';
		if (!m_stack.empty()) m_stack.pop_back();
		if (!m_needSep.empty()) m_needSep.pop_back();
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief キーを出力する
	/// @param k キー名
	/// @return 自身への参照
	JsonBuilder& key(const std::string& k)
	{
		appendSeparator();
		m_buffer += '"';
		m_buffer += escapeString(k);
		m_buffer += "\":";
		m_keyPending = true;
		return *this;
	}

	/// @brief 文字列値を出力する
	/// @param v 文字列値
	/// @return 自身への参照
	JsonBuilder& value(const std::string& v)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += '"';
		m_buffer += escapeString(v);
		m_buffer += '"';
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 文字列リテラル値を出力する
	/// @param v C文字列
	/// @return 自身への参照
	JsonBuilder& value(const char* v)
	{
		return value(std::string(v));
	}

	/// @brief 整数値を出力する
	/// @param v 整数値
	/// @return 自身への参照
	JsonBuilder& value(int v)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += std::to_string(v);
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 浮動小数点値を出力する
	/// @param v 浮動小数点値
	/// @return 自身への参照
	JsonBuilder& value(float v)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += formatFloat(v);
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief double値を出力する
	/// @param v double値
	/// @return 自身への参照
	JsonBuilder& value(double v)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += formatDouble(v);
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 真偽値を出力する
	/// @param v 真偽値
	/// @return 自身への参照
	JsonBuilder& value(bool v)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += (v ? "true" : "false");
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief float配列をショートハンドで出力する
	/// @param k キー名
	/// @param values float値のベクタ
	/// @return 自身への参照
	JsonBuilder& array(const std::string& k, const std::vector<float>& values)
	{
		key(k);
		m_keyPending = false;
		m_buffer += '[';
		for (size_t i = 0; i < values.size(); ++i)
		{
			if (i > 0) m_buffer += ',';
			m_buffer += formatFloat(values[i]);
		}
		m_buffer += ']';
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 生のJSON文字列を値として出力する
	/// @param raw 生JSON文字列
	/// @return 自身への参照
	JsonBuilder& rawValue(const std::string& raw)
	{
		if (!m_keyPending) appendSeparator();
		m_keyPending = false;
		m_buffer += raw;
		if (!m_needSep.empty()) m_needSep.back() = true;
		return *this;
	}

	/// @brief 構築されたJSON文字列を返す
	/// @return JSON文字列
	[[nodiscard]] std::string build() const
	{
		return m_buffer;
	}

private:
	enum class State { Object, Array };

	std::string m_buffer;
	std::vector<State> m_stack;
	std::vector<bool> m_needSep;
	bool m_keyPending{false};

	/// @brief セパレータ（カンマ）が必要であれば追加する
	void appendSeparator()
	{
		if (!m_needSep.empty() && m_needSep.back())
		{
			m_buffer += ',';
			m_needSep.back() = false;
		}
	}

	/// @brief 文字列をJSONエスケープする
	[[nodiscard]] static std::string escapeString(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (char c : s)
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

	/// @brief floatを簡潔な文字列に変換する
	[[nodiscard]] static std::string formatFloat(float v)
	{
		if (std::floor(v) == v && std::abs(v) < 1e7f)
		{
			/// 整数値の場合は小数点以下を1桁だけ
			std::ostringstream oss;
			oss << static_cast<long long>(v) << ".0";
			return oss.str();
		}
		std::ostringstream oss;
		oss << v;
		return oss.str();
	}

	/// @brief doubleを簡潔な文字列に変換する
	[[nodiscard]] static std::string formatDouble(double v)
	{
		if (std::floor(v) == v && std::abs(v) < 1e15)
		{
			std::ostringstream oss;
			oss << static_cast<long long>(v) << ".0";
			return oss.str();
		}
		std::ostringstream oss;
		oss << v;
		return oss.str();
	}
};

/// @brief 簡易JSONリーダー
/// @details フラットおよびネストされたJSONオブジェクトの値を取得する。
class JsonReader
{
public:
	/// @brief JSON文字列をパースする
	/// @param json パース対象のJSON文字列
	/// @return パース成功時true
	bool parse(const std::string& json)
	{
		m_source = json;
		m_values.clear();

		/// 先頭の空白をスキップ
		size_t pos = skipWhitespace(json, 0);
		if (pos >= json.size() || json[pos] != '{') return false;

		return parseObject(json, pos, "");
	}

	/// @brief 文字列値を取得する
	/// @param k キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<std::string> getString(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		const auto& v = it->second;
		/// 文字列値はクォートを外す
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
		{
			return unescapeString(v.substr(1, v.size() - 2));
		}
		return v;
	}

	/// @brief float値を取得する
	/// @param k キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<float> getFloat(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		try
		{
			return std::stof(it->second);
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	/// @brief 整数値を取得する
	/// @param k キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<int> getInt(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		try
		{
			return std::stoi(it->second);
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	/// @brief 真偽値を取得する
	/// @param k キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<bool> getBool(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		if (it->second == "true") return true;
		if (it->second == "false") return false;
		return std::nullopt;
	}

	/// @brief 配列を生の文字列ベクタで取得する
	/// @param k キー名
	/// @return 値ベクタ（存在しない場合nullopt）
	[[nodiscard]] std::optional<std::vector<std::string>> getArray(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		const auto& raw = it->second;
		if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') return std::nullopt;

		std::vector<std::string> result;
		size_t pos = 1;
		while (pos < raw.size() - 1)
		{
			pos = skipWhitespace(raw, pos);
			if (pos >= raw.size() - 1) break;

			auto [val, endPos] = extractValue(raw, pos);
			if (val.empty()) break;

			/// 文字列値のクォートを外す
			if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
			{
				result.push_back(unescapeString(val.substr(1, val.size() - 2)));
			}
			else
			{
				result.push_back(val);
			}

			pos = skipWhitespace(raw, endPos);
			if (pos < raw.size() && raw[pos] == ',') ++pos;
		}
		return result;
	}

	/// @brief ネストされたオブジェクトをJsonReaderとして取得する
	/// @param k キー名
	/// @return JsonReader（存在しない場合nullopt）
	[[nodiscard]] std::optional<JsonReader> getObject(const std::string& k) const
	{
		auto it = m_values.find(k);
		if (it == m_values.end()) return std::nullopt;
		const auto& raw = it->second;
		if (raw.empty() || raw.front() != '{') return std::nullopt;

		JsonReader nested;
		if (nested.parse(raw))
		{
			return nested;
		}
		return std::nullopt;
	}

	/// @brief 元のJSON文字列を返す
	[[nodiscard]] const std::string& source() const noexcept { return m_source; }

private:
	std::string m_source;
	std::unordered_map<std::string, std::string> m_values;

	/// @brief 空白をスキップする
	[[nodiscard]] static size_t skipWhitespace(const std::string& s, size_t pos)
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
		{
			++pos;
		}
		return pos;
	}

	/// @brief オブジェクトをパースする
	/// @param json JSON文字列
	/// @param pos '{' の位置
	/// @param prefix ネストキーのプレフィックス
	/// @return 成功時true
	bool parseObject(const std::string& json, size_t pos, const std::string& prefix)
	{
		if (pos >= json.size() || json[pos] != '{') return false;
		++pos; /// '{' をスキップ

		while (pos < json.size())
		{
			pos = skipWhitespace(json, pos);
			if (pos >= json.size()) return false;
			if (json[pos] == '}') return true;

			/// キーをパース
			if (json[pos] != '"') return false;
			++pos;
			size_t keyEnd = json.find('"', pos);
			if (keyEnd == std::string::npos) return false;
			std::string keyName = json.substr(pos, keyEnd - pos);
			pos = keyEnd + 1;

			pos = skipWhitespace(json, pos);
			if (pos >= json.size() || json[pos] != ':') return false;
			++pos;
			pos = skipWhitespace(json, pos);

			/// 値を抽出
			auto [val, endPos] = extractValue(json, pos);
			std::string fullKey = prefix.empty() ? keyName : (prefix + "." + keyName);
			m_values[fullKey] = val;
			pos = endPos;

			pos = skipWhitespace(json, pos);
			if (pos < json.size() && json[pos] == ',') ++pos;
		}
		return false;
	}

	/// @brief JSON値を抽出する
	/// @param json JSON文字列
	/// @param pos 値の開始位置
	/// @return {値文字列, 終了位置}
	[[nodiscard]] static std::pair<std::string, size_t> extractValue(
		const std::string& json, size_t pos)
	{
		if (pos >= json.size()) return {"", pos};

		const char ch = json[pos];

		/// 文字列
		if (ch == '"')
		{
			size_t start = pos;
			++pos;
			while (pos < json.size())
			{
				if (json[pos] == '\\')
				{
					pos += 2;
					continue;
				}
				if (json[pos] == '"')
				{
					++pos;
					return {json.substr(start, pos - start), pos};
				}
				++pos;
			}
			return {json.substr(start), pos};
		}

		/// オブジェクトまたは配列
		if (ch == '{' || ch == '[')
		{
			const char open = ch;
			const char close = (ch == '{') ? '}' : ']';
			int depth = 0;
			size_t start = pos;
			bool inString = false;
			while (pos < json.size())
			{
				if (json[pos] == '\\' && inString)
				{
					pos += 2;
					continue;
				}
				if (json[pos] == '"') inString = !inString;
				if (!inString)
				{
					if (json[pos] == open) ++depth;
					if (json[pos] == close)
					{
						--depth;
						if (depth == 0)
						{
							++pos;
							return {json.substr(start, pos - start), pos};
						}
					}
				}
				++pos;
			}
			return {json.substr(start), pos};
		}

		/// 数値・true・false・null
		{
			size_t start = pos;
			while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
				json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n' &&
				json[pos] != '\r' && json[pos] != '\t')
			{
				++pos;
			}
			return {json.substr(start, pos - start), pos};
		}
	}

	/// @brief エスケープ文字列を復元する
	[[nodiscard]] static std::string unescapeString(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '\\' && i + 1 < s.size())
			{
				switch (s[i + 1])
				{
				case '"': result += '"'; ++i; break;
				case '\\': result += '\\'; ++i; break;
				case 'n': result += '\n'; ++i; break;
				case 'r': result += '\r'; ++i; break;
				case 't': result += '\t'; ++i; break;
				default: result += s[i]; break;
				}
			}
			else
			{
				result += s[i];
			}
		}
		return result;
	}
};

} // namespace mitiru::data
