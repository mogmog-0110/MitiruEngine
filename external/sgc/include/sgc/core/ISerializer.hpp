#pragma once

/// @file ISerializer.hpp
/// @brief 統一シリアライズ/デシリアライズインターフェースとJSON実装
///
/// キー・バリュー形式のシリアライゼーションを抽象化する。
/// JsonSerializer / JsonDeserializer はインライン実装で外部依存なし。
///
/// @code
/// sgc::JsonSerializer ser;
/// ser.writeString("name", "Hero");
/// ser.writeInt("level", 42);
/// ser.writeBool("active", true);
/// ser.beginObject("stats");
/// ser.writeFloat("hp", 100.5f);
/// ser.endObject();
/// std::string json = ser.finalize();
///
/// sgc::JsonDeserializer des(json);
/// auto name = des.readString("name");  // "Hero"
/// auto level = des.readInt("level");    // 42
/// @endcode

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sgc
{

/// @brief シリアライザの抽象インターフェース
///
/// キー・バリュー形式でデータを書き出す。
/// オブジェクトや配列のネストにも対応。
class ISerializer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISerializer() = default;

	/// @brief 整数値を書き込む
	/// @param key キー名
	/// @param value 書き込む値
	virtual void writeInt(std::string_view key, int32_t value) = 0;

	/// @brief 浮動小数点値を書き込む
	/// @param key キー名
	/// @param value 書き込む値
	virtual void writeFloat(std::string_view key, float value) = 0;

	/// @brief 文字列値を書き込む
	/// @param key キー名
	/// @param value 書き込む値
	virtual void writeString(std::string_view key, std::string_view value) = 0;

	/// @brief 真偽値を書き込む
	/// @param key キー名
	/// @param value 書き込む値
	virtual void writeBool(std::string_view key, bool value) = 0;

	/// @brief オブジェクトの開始を書き込む
	/// @param key オブジェクトのキー名
	virtual void beginObject(std::string_view key) = 0;

	/// @brief オブジェクトの終了を書き込む
	virtual void endObject() = 0;

	/// @brief 配列の開始を書き込む
	/// @param key 配列のキー名
	virtual void beginArray(std::string_view key) = 0;

	/// @brief 配列の終了を書き込む
	virtual void endArray() = 0;

	/// @brief シリアライズ結果を文字列として取得する
	/// @return シリアライズされた文字列
	[[nodiscard]] virtual std::string finalize() const = 0;
};

/// @brief デシリアライザの抽象インターフェース
///
/// キー・バリュー形式でデータを読み出す。
class IDeserializer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IDeserializer() = default;

	/// @brief 整数値を読み出す
	/// @param key キー名
	/// @return 読み出した値
	[[nodiscard]] virtual int32_t readInt(std::string_view key) const = 0;

	/// @brief 浮動小数点値を読み出す
	/// @param key キー名
	/// @return 読み出した値
	[[nodiscard]] virtual float readFloat(std::string_view key) const = 0;

	/// @brief 文字列値を読み出す
	/// @param key キー名
	/// @return 読み出した文字列
	[[nodiscard]] virtual std::string readString(std::string_view key) const = 0;

	/// @brief 真偽値を読み出す
	/// @param key キー名
	/// @return 読み出した値
	[[nodiscard]] virtual bool readBool(std::string_view key) const = 0;

	/// @brief 指定キーが存在するか判定する
	/// @param key キー名
	/// @return 存在すればtrue
	[[nodiscard]] virtual bool hasKey(std::string_view key) const = 0;
};

// ── JSON実装 ─────────────────────────────────────────────

/// @brief JSON形式のシリアライザ（簡易文字列構築、外部依存なし）
///
/// writeXxx() で追加した値を finalize() でJSON文字列として取得する。
class JsonSerializer final : public ISerializer
{
public:
	/// @brief デフォルトコンストラクタ
	JsonSerializer()
	{
		m_stream << "{";
		m_needsComma.push_back(false);
	}

	/// @brief 整数値を書き込む
	void writeInt(std::string_view key, int32_t value) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":" << value;
	}

	/// @brief 浮動小数点値を書き込む
	void writeFloat(std::string_view key, float value) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":" << value;
	}

	/// @brief 文字列値を書き込む
	void writeString(std::string_view key, std::string_view value) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":\"" << escapeString(value) << "\"";
	}

	/// @brief 真偽値を書き込む
	void writeBool(std::string_view key, bool value) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":" << (value ? "true" : "false");
	}

	/// @brief オブジェクトの開始を書き込む
	void beginObject(std::string_view key) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":{";
		m_needsComma.push_back(false);
	}

	/// @brief オブジェクトの終了を書き込む
	void endObject() override
	{
		m_stream << "}";
		if (!m_needsComma.empty()) m_needsComma.pop_back();
		if (!m_needsComma.empty()) m_needsComma.back() = true;
	}

	/// @brief 配列の開始を書き込む
	void beginArray(std::string_view key) override
	{
		writeCommaIfNeeded();
		m_stream << "\"" << key << "\":[";
		m_needsComma.push_back(false);
	}

	/// @brief 配列の終了を書き込む
	void endArray() override
	{
		m_stream << "]";
		if (!m_needsComma.empty()) m_needsComma.pop_back();
		if (!m_needsComma.empty()) m_needsComma.back() = true;
	}

	/// @brief JSON文字列を取得する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string finalize() const override
	{
		return m_stream.str() + "}";
	}

private:
	std::ostringstream m_stream;         ///< 出力ストリーム
	std::vector<bool> m_needsComma;      ///< ネストレベルごとのカンマ要否

	/// @brief 必要に応じてカンマを書き込む
	void writeCommaIfNeeded()
	{
		if (!m_needsComma.empty() && m_needsComma.back())
		{
			m_stream << ",";
		}
		if (!m_needsComma.empty())
		{
			m_needsComma.back() = true;
		}
	}

	/// @brief 文字列中の特殊文字をエスケープする
	/// @param s エスケープ対象文字列
	/// @return エスケープ済み文字列
	[[nodiscard]] static std::string escapeString(std::string_view s)
	{
		std::string result;
		result.reserve(s.size());
		for (const char c : s)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n";  break;
			case '\r': result += "\\r";  break;
			case '\t': result += "\\t";  break;
			default:   result += c;      break;
			}
		}
		return result;
	}
};

/// @brief JSON形式のデシリアライザ（簡易パーサー、外部依存なし）
///
/// JSON文字列からキー・バリューペアを読み出す。
/// トップレベルのフラットなオブジェクトを対象とする。
class JsonDeserializer final : public IDeserializer
{
public:
	/// @brief JSON文字列から構築する
	/// @param json JSON形式の文字列
	explicit JsonDeserializer(const std::string& json)
	{
		parse(json);
	}

	/// @brief 整数値を読み出す
	[[nodiscard]] int32_t readInt(std::string_view key) const override
	{
		const auto it = m_values.find(std::string(key));
		if (it == m_values.end()) return 0;
		return std::stoi(it->second);
	}

	/// @brief 浮動小数点値を読み出す
	[[nodiscard]] float readFloat(std::string_view key) const override
	{
		const auto it = m_values.find(std::string(key));
		if (it == m_values.end()) return 0.0f;
		return std::stof(it->second);
	}

	/// @brief 文字列値を読み出す
	[[nodiscard]] std::string readString(std::string_view key) const override
	{
		const auto it = m_values.find(std::string(key));
		if (it == m_values.end()) return {};
		return it->second;
	}

	/// @brief 真偽値を読み出す
	[[nodiscard]] bool readBool(std::string_view key) const override
	{
		const auto it = m_values.find(std::string(key));
		if (it == m_values.end()) return false;
		return it->second == "true";
	}

	/// @brief 指定キーが存在するか判定する
	[[nodiscard]] bool hasKey(std::string_view key) const override
	{
		return m_values.find(std::string(key)) != m_values.end();
	}

private:
	std::unordered_map<std::string, std::string> m_values;  ///< キー・バリュー格納

	/// @brief 空白をスキップする
	/// @param json JSON文字列
	/// @param pos 現在位置（更新される）
	static void skipWhitespace(const std::string& json, std::size_t& pos)
	{
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
			json[pos] == '\r' || json[pos] == '\t'))
		{
			++pos;
		}
	}

	/// @brief クォート付き文字列をパースする
	/// @param json JSON文字列
	/// @param pos 現在位置（更新される）
	/// @return パースした文字列
	static std::string parseString(const std::string& json, std::size_t& pos)
	{
		if (pos >= json.size() || json[pos] != '"') return {};
		++pos;  // 開始クォートをスキップ
		std::string result;
		while (pos < json.size() && json[pos] != '"')
		{
			if (json[pos] == '\\' && pos + 1 < json.size())
			{
				++pos;
				switch (json[pos])
				{
				case '"':  result += '"';  break;
				case '\\': result += '\\'; break;
				case 'n':  result += '\n'; break;
				case 'r':  result += '\r'; break;
				case 't':  result += '\t'; break;
				default:   result += json[pos]; break;
				}
			}
			else
			{
				result += json[pos];
			}
			++pos;
		}
		if (pos < json.size()) ++pos;  // 終了クォートをスキップ
		return result;
	}

	/// @brief 値（文字列・数値・真偽値・ネストオブジェクト/配列）をパースする
	/// @param json JSON文字列
	/// @param pos 現在位置（更新される）
	/// @return パースした値の文字列表現
	static std::string parseValue(const std::string& json, std::size_t& pos)
	{
		skipWhitespace(json, pos);
		if (pos >= json.size()) return {};

		// 文字列
		if (json[pos] == '"')
		{
			return parseString(json, pos);
		}

		// ネストオブジェクトまたは配列はスキップ
		if (json[pos] == '{' || json[pos] == '[')
		{
			const char open = json[pos];
			const char close = (open == '{') ? '}' : ']';
			int depth = 1;
			std::size_t start = pos;
			++pos;
			while (pos < json.size() && depth > 0)
			{
				if (json[pos] == open) ++depth;
				else if (json[pos] == close) --depth;
				else if (json[pos] == '"')
				{
					// 文字列内のブレースは無視
					++pos;
					while (pos < json.size() && json[pos] != '"')
					{
						if (json[pos] == '\\') ++pos;
						++pos;
					}
				}
				++pos;
			}
			return json.substr(start, pos - start);
		}

		// 数値・真偽値・null
		std::size_t start = pos;
		while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
			json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n')
		{
			++pos;
		}
		return json.substr(start, pos - start);
	}

	/// @brief トップレベルのJSONオブジェクトをパースする
	/// @param json JSON文字列
	void parse(const std::string& json)
	{
		std::size_t pos = 0;
		skipWhitespace(json, pos);
		if (pos >= json.size() || json[pos] != '{') return;
		++pos;  // '{' をスキップ

		while (pos < json.size())
		{
			skipWhitespace(json, pos);
			if (pos >= json.size() || json[pos] == '}') break;

			if (json[pos] == ',') { ++pos; continue; }

			// キーをパース
			std::string key = parseString(json, pos);
			skipWhitespace(json, pos);
			if (pos < json.size() && json[pos] == ':') ++pos;

			// 値をパース
			std::string value = parseValue(json, pos);
			m_values[key] = value;
		}
	}
};

} // namespace sgc
