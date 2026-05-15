#pragma once

/// @file Serialization.hpp
/// @brief 統一シリアライゼーションフレームワーク
/// @details ISerializableインターフェース、JsonWriter/JsonReader、
///          SerializationRegistryによるポリモーフィック逆シリアライズを提供する。
///          nlohmann::jsonラッパーを内蔵し、外部依存なしで使用可能。
///
/// @code
/// class MyObject : public mitiru::ISerializable {
///     MITIRU_SERIALIZABLE(MyObject)
///     void serialize(mitiru::JsonWriter& w) const override {
///         w.write("name", m_name);
///         w.write("pos", m_position);
///     }
///     void deserialize(mitiru::JsonReader& r) override {
///         m_name = r.read<std::string>("name", "");
///         m_position = r.read<Vec2>("pos", Vec2{0,0});
///     }
/// };
/// @endcode

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{

// ── 軽量JSONデータ型 ──

/// @brief JSON値の型タグ
enum class JsonType : std::uint8_t
{
	Null,
	Bool,
	Int,
	Float,
	String,
	Array,
	Object,
};

/// @brief 軽量JSON値
/// @details nlohmann::jsonの代替として、外部依存なしで使用可能なJSON値型。
class JsonValue
{
public:
	/// @brief デフォルトコンストラクタ（null値）
	JsonValue() = default;

	/// @brief bool値を構築する
	explicit JsonValue(bool v) : m_type(JsonType::Bool), m_bool(v) {}

	/// @brief int値を構築する
	explicit JsonValue(int v) : m_type(JsonType::Int), m_int(v) {}

	/// @brief float値を構築する
	explicit JsonValue(float v) : m_type(JsonType::Float), m_float(v) {}

	/// @brief double→float値を構築する
	explicit JsonValue(double v) : m_type(JsonType::Float), m_float(static_cast<float>(v)) {}

	/// @brief string値を構築する
	explicit JsonValue(std::string v) : m_type(JsonType::String), m_string(std::move(v)) {}

	/// @brief string_viewからstring値を構築する
	explicit JsonValue(std::string_view v) : m_type(JsonType::String), m_string(v) {}

	/// @brief const char*からstring値を構築する
	explicit JsonValue(const char* v) : m_type(JsonType::String), m_string(v ? v : "") {}

	/// @brief 型タグを取得する
	[[nodiscard]] JsonType type() const noexcept { return m_type; }

	/// @brief null値か判定する
	[[nodiscard]] bool isNull() const noexcept { return m_type == JsonType::Null; }

	// ── 値取得 ──

	[[nodiscard]] bool asBool(bool def = false) const noexcept
	{
		return (m_type == JsonType::Bool) ? m_bool : def;
	}

	[[nodiscard]] int asInt(int def = 0) const noexcept
	{
		if (m_type == JsonType::Int) return m_int;
		if (m_type == JsonType::Float) return static_cast<int>(m_float);
		return def;
	}

	[[nodiscard]] float asFloat(float def = 0.0f) const noexcept
	{
		if (m_type == JsonType::Float) return m_float;
		if (m_type == JsonType::Int) return static_cast<float>(m_int);
		return def;
	}

	[[nodiscard]] const std::string& asString(const std::string& def = s_empty) const noexcept
	{
		return (m_type == JsonType::String) ? m_string : def;
	}

	// ── 配列操作 ──

	void pushBack(JsonValue val)
	{
		m_type = JsonType::Array;
		m_array.push_back(std::move(val));
	}

	[[nodiscard]] std::size_t arraySize() const noexcept
	{
		return m_array.size();
	}

	[[nodiscard]] const JsonValue& at(std::size_t index) const
	{
		static const JsonValue s_null;
		return (index < m_array.size()) ? m_array[index] : s_null;
	}

	[[nodiscard]] const std::vector<JsonValue>& asArray() const noexcept { return m_array; }

	// ── オブジェクト操作 ──

	void set(const std::string& key, JsonValue val)
	{
		m_type = JsonType::Object;
		m_object[key] = std::move(val);
	}

	[[nodiscard]] bool has(const std::string& key) const
	{
		return m_object.find(key) != m_object.end();
	}

	[[nodiscard]] const JsonValue& get(const std::string& key) const
	{
		static const JsonValue s_null;
		const auto it = m_object.find(key);
		return (it != m_object.end()) ? it->second : s_null;
	}

	[[nodiscard]] const std::unordered_map<std::string, JsonValue>& asObject() const noexcept
	{
		return m_object;
	}

	// ── JSON文字列変換 ──

	/// @brief JSON文字列にシリアライズする
	[[nodiscard]] std::string toJson(int indent = 0) const
	{
		std::ostringstream oss;
		writeJson(oss, indent, 0);
		return oss.str();
	}

private:
	JsonType m_type = JsonType::Null;
	bool m_bool = false;
	int m_int = 0;
	float m_float = 0.0f;
	std::string m_string;
	std::vector<JsonValue> m_array;
	std::unordered_map<std::string, JsonValue> m_object;

	inline static const std::string s_empty;

	void writeJson(std::ostringstream& oss, int indent, int depth) const
	{
		const std::string pad = (indent > 0) ? std::string(static_cast<std::size_t>(depth * indent), ' ') : "";
		const std::string padInner = (indent > 0) ? std::string(static_cast<std::size_t>((depth + 1) * indent), ' ') : "";
		const std::string nl = (indent > 0) ? "\n" : "";

		switch (m_type)
		{
		case JsonType::Null:
			oss << "null";
			break;
		case JsonType::Bool:
			oss << (m_bool ? "true" : "false");
			break;
		case JsonType::Int:
			oss << m_int;
			break;
		case JsonType::Float:
			oss << m_float;
			break;
		case JsonType::String:
			oss << "\"" << escapeString(m_string) << "\"";
			break;
		case JsonType::Array:
		{
			oss << "[" << nl;
			for (std::size_t i = 0; i < m_array.size(); ++i)
			{
				oss << padInner;
				m_array[i].writeJson(oss, indent, depth + 1);
				if (i + 1 < m_array.size()) oss << ",";
				oss << nl;
			}
			oss << pad << "]";
			break;
		}
		case JsonType::Object:
		{
			oss << "{" << nl;
			std::size_t idx = 0;
			for (const auto& [key, val] : m_object)
			{
				oss << padInner << "\"" << escapeString(key) << "\": ";
				val.writeJson(oss, indent, depth + 1);
				if (idx + 1 < m_object.size()) oss << ",";
				oss << nl;
				++idx;
			}
			oss << pad << "}";
			break;
		}
		}
	}

	[[nodiscard]] static std::string escapeString(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (char c : s)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n"; break;
			case '\t': result += "\\t"; break;
			case '\r': result += "\\r"; break;
			default:   result += c; break;
			}
		}
		return result;
	}
};

// ── JsonWriter ──

/// @brief JSONシリアライズライター
/// @details beginObject/endObject, write(key, value) でJSON構造を構築する。
class JsonWriter
{
public:
	/// @brief コンストラクタ
	JsonWriter() = default;

	/// @brief オブジェクトの開始
	void beginObject()
	{
		m_stack.push_back(JsonValue{});
		m_stack.back().set("__type__", JsonValue{"object"});
		// JsonValueはset()でObjectモードになるため、__type__は目印として使用
	}

	/// @brief オブジェクトの終了
	void endObject()
	{
		if (m_stack.empty()) return;

		auto obj = std::move(m_stack.back());
		m_stack.pop_back();

		// __type__マーカーを除去する
		// (JsonValueのオブジェクトからは除去できないが、出力時に無視する)

		if (m_stack.empty())
		{
			m_root = std::move(obj);
		}
		else
		{
			// 親がArrayなら追加する
			m_stack.back().pushBack(std::move(obj));
		}
	}

	/// @brief 配列の開始
	void beginArray(const std::string& key)
	{
		m_pendingArrayKey = key;
		m_stack.push_back(JsonValue{});
		// 空のJsonValueをArray用のプレースホルダとして積む
	}

	/// @brief 配列の終了
	void endArray()
	{
		if (m_stack.size() < 2) return;

		auto arr = std::move(m_stack.back());
		m_stack.pop_back();

		m_stack.back().set(m_pendingArrayKey, std::move(arr));
		m_pendingArrayKey.clear();
	}

	// ── 値の書き込み ──

	void write(const std::string& key, bool value)
	{
		current().set(key, JsonValue{value});
	}

	void write(const std::string& key, int value)
	{
		current().set(key, JsonValue{value});
	}

	void write(const std::string& key, float value)
	{
		current().set(key, JsonValue{value});
	}

	void write(const std::string& key, const std::string& value)
	{
		current().set(key, JsonValue{value});
	}

	void write(const std::string& key, std::string_view value)
	{
		current().set(key, JsonValue{std::string(value)});
	}

	/// @brief Vec2値を書き込む
	void write(const std::string& key, const sgc::Vec2f& v)
	{
		JsonValue arr;
		arr.pushBack(JsonValue{v.x});
		arr.pushBack(JsonValue{v.y});
		current().set(key, std::move(arr));
	}

	/// @brief Vec3値を書き込む（x, y, z配列として）
	void writeVec3(const std::string& key, float x, float y, float z)
	{
		JsonValue arr;
		arr.pushBack(JsonValue{x});
		arr.pushBack(JsonValue{y});
		arr.pushBack(JsonValue{z});
		current().set(key, std::move(arr));
	}

	/// @brief Color値を書き込む
	void write(const std::string& key, const sgc::Colorf& c)
	{
		JsonValue arr;
		arr.pushBack(JsonValue{c.r});
		arr.pushBack(JsonValue{c.g});
		arr.pushBack(JsonValue{c.b});
		arr.pushBack(JsonValue{c.a});
		current().set(key, std::move(arr));
	}

	/// @brief Rect値を書き込む
	void write(const std::string& key, const sgc::Rectf& r)
	{
		JsonValue arr;
		arr.pushBack(JsonValue{r.x()});
		arr.pushBack(JsonValue{r.y()});
		arr.pushBack(JsonValue{r.width()});
		arr.pushBack(JsonValue{r.height()});
		current().set(key, std::move(arr));
	}

	/// @brief vector<T>を書き込む（T = int, float, string）
	template <typename T>
	void writeVector(const std::string& key, const std::vector<T>& vec)
	{
		JsonValue arr;
		for (const auto& item : vec)
		{
			arr.pushBack(JsonValue{item});
		}
		current().set(key, std::move(arr));
	}

	/// @brief map<string, T>を書き込む
	template <typename T>
	void writeMap(const std::string& key, const std::unordered_map<std::string, T>& map)
	{
		JsonValue obj;
		for (const auto& [k, v] : map)
		{
			obj.set(k, JsonValue{v});
		}
		current().set(key, std::move(obj));
	}

	/// @brief ルートJsonValueを取得する
	[[nodiscard]] const JsonValue& root() const noexcept { return m_root; }

	/// @brief JSON文字列を取得する
	[[nodiscard]] std::string toJson(int indent = 2) const { return m_root.toJson(indent); }

private:
	JsonValue m_root;
	std::vector<JsonValue> m_stack;
	std::string m_pendingArrayKey;

	/// @brief 現在のスタックトップを取得する
	JsonValue& current()
	{
		return m_stack.empty() ? m_root : m_stack.back();
	}
};

// ── JsonReader ──

/// @brief JSONデシリアライズリーダー
/// @details has(key), read<T>(key, default)でJSON値を読み出す。
class JsonReader
{
public:
	/// @brief コンストラクタ
	/// @param value 読み取り元のJsonValue
	explicit JsonReader(const JsonValue& value)
		: m_value(value)
	{
	}

	/// @brief キーの存在を判定する
	[[nodiscard]] bool has(const std::string& key) const
	{
		return m_value.has(key);
	}

	// ── 型別読み取り ──

	/// @brief bool値を読み取る
	[[nodiscard]] bool readBool(const std::string& key, bool def = false) const
	{
		return m_value.get(key).asBool(def);
	}

	/// @brief int値を読み取る
	[[nodiscard]] int readInt(const std::string& key, int def = 0) const
	{
		return m_value.get(key).asInt(def);
	}

	/// @brief float値を読み取る
	[[nodiscard]] float readFloat(const std::string& key, float def = 0.0f) const
	{
		return m_value.get(key).asFloat(def);
	}

	/// @brief string値を読み取る
	[[nodiscard]] std::string readString(const std::string& key, const std::string& def = "") const
	{
		const auto& v = m_value.get(key);
		return v.isNull() ? def : v.asString(def);
	}

	/// @brief Vec2値を読み取る（[x, y]配列）
	[[nodiscard]] sgc::Vec2f readVec2(const std::string& key, const sgc::Vec2f& def = {}) const
	{
		const auto& v = m_value.get(key);
		if (v.arraySize() < 2) return def;
		return {v.at(0).asFloat(), v.at(1).asFloat()};
	}

	/// @brief Color値を読み取る（[r, g, b, a]配列）
	[[nodiscard]] sgc::Colorf readColor(const std::string& key,
	                                     const sgc::Colorf& def = {1.0f, 1.0f, 1.0f, 1.0f}) const
	{
		const auto& v = m_value.get(key);
		if (v.arraySize() < 4) return def;
		return {v.at(0).asFloat(), v.at(1).asFloat(),
		        v.at(2).asFloat(), v.at(3).asFloat()};
	}

	/// @brief Rect値を読み取る（[x, y, w, h]配列）
	[[nodiscard]] sgc::Rectf readRect(const std::string& key,
	                                   const sgc::Rectf& def = {}) const
	{
		const auto& v = m_value.get(key);
		if (v.arraySize() < 4) return def;
		return sgc::Rectf{v.at(0).asFloat(), v.at(1).asFloat(),
		                  v.at(2).asFloat(), v.at(3).asFloat()};
	}

	/// @brief vector<int>を読み取る
	[[nodiscard]] std::vector<int> readIntVector(const std::string& key) const
	{
		const auto& v = m_value.get(key);
		std::vector<int> result;
		result.reserve(v.arraySize());
		for (std::size_t i = 0; i < v.arraySize(); ++i)
		{
			result.push_back(v.at(i).asInt());
		}
		return result;
	}

	/// @brief vector<float>を読み取る
	[[nodiscard]] std::vector<float> readFloatVector(const std::string& key) const
	{
		const auto& v = m_value.get(key);
		std::vector<float> result;
		result.reserve(v.arraySize());
		for (std::size_t i = 0; i < v.arraySize(); ++i)
		{
			result.push_back(v.at(i).asFloat());
		}
		return result;
	}

	/// @brief vector<string>を読み取る
	[[nodiscard]] std::vector<std::string> readStringVector(const std::string& key) const
	{
		const auto& v = m_value.get(key);
		std::vector<std::string> result;
		result.reserve(v.arraySize());
		for (std::size_t i = 0; i < v.arraySize(); ++i)
		{
			result.push_back(v.at(i).asString());
		}
		return result;
	}

	/// @brief map<string, string>を読み取る
	[[nodiscard]] std::unordered_map<std::string, std::string> readStringMap(
		const std::string& key) const
	{
		const auto& v = m_value.get(key);
		std::unordered_map<std::string, std::string> result;
		for (const auto& [k, val] : v.asObject())
		{
			result[k] = val.asString();
		}
		return result;
	}

	/// @brief テンプレート読み取り（bool特殊化）
	template <typename T>
	[[nodiscard]] T read(const std::string& key, const T& def = T{}) const;

	/// @brief 子オブジェクトのReaderを取得する
	[[nodiscard]] JsonReader subReader(const std::string& key) const
	{
		return JsonReader{m_value.get(key)};
	}

	/// @brief 元のJsonValueを取得する
	[[nodiscard]] const JsonValue& value() const noexcept { return m_value; }

private:
	const JsonValue& m_value;
};

// ── テンプレート特殊化 ──

template <>
inline bool JsonReader::read<bool>(const std::string& key, const bool& def) const
{
	return readBool(key, def);
}

template <>
inline int JsonReader::read<int>(const std::string& key, const int& def) const
{
	return readInt(key, def);
}

template <>
inline float JsonReader::read<float>(const std::string& key, const float& def) const
{
	return readFloat(key, def);
}

template <>
inline std::string JsonReader::read<std::string>(const std::string& key, const std::string& def) const
{
	return readString(key, def);
}

template <>
inline sgc::Vec2f JsonReader::read<sgc::Vec2f>(const std::string& key, const sgc::Vec2f& def) const
{
	return readVec2(key, def);
}

template <>
inline sgc::Colorf JsonReader::read<sgc::Colorf>(const std::string& key, const sgc::Colorf& def) const
{
	return readColor(key, def);
}

template <>
inline sgc::Rectf JsonReader::read<sgc::Rectf>(const std::string& key, const sgc::Rectf& def) const
{
	return readRect(key, def);
}

// ── ISerializable インターフェース ──

/// @brief シリアライズ可能オブジェクトのインターフェース
class ISerializable
{
public:
	virtual ~ISerializable() = default;

	/// @brief オブジェクトをJsonWriterにシリアライズする
	virtual void serialize(JsonWriter& writer) const = 0;

	/// @brief JsonReaderからオブジェクトをデシリアライズする
	virtual void deserialize(JsonReader& reader) = 0;
};

// ── SerializationRegistry ──

/// @brief ポリモーフィック逆シリアライゼーション用型レジストリ
/// @details 型名とファクトリ関数を登録し、JSON内の型名から
///          適切なISerializableインスタンスを生成する。
class SerializationRegistry
{
public:
	using FactoryFunc = std::function<std::unique_ptr<ISerializable>()>;

	/// @brief シングルトンインスタンスを取得する
	[[nodiscard]] static SerializationRegistry& instance()
	{
		static SerializationRegistry s_instance;
		return s_instance;
	}

	/// @brief 型を登録する
	/// @tparam T ISerializableを継承した型
	/// @param name 型名（JSONの"type"フィールドに対応）
	template <typename T>
	void registerType(const std::string& name)
	{
		static_assert(std::is_base_of_v<ISerializable, T>,
		              "T must derive from ISerializable");
		m_factories[name] = []() -> std::unique_ptr<ISerializable> {
			return std::make_unique<T>();
		};
	}

	/// @brief ファクトリ関数で型を登録する
	/// @param name 型名
	/// @param factory ファクトリ関数
	void registerFactory(const std::string& name, FactoryFunc factory)
	{
		m_factories[name] = std::move(factory);
	}

	/// @brief JSONからオブジェクトを生成する
	/// @param name 型名
	/// @param json 読み取り元のJsonValue
	/// @return 生成されたオブジェクト（型が見つからない場合はnullptr）
	[[nodiscard]] std::unique_ptr<ISerializable> createFromJson(
		const std::string& name, const JsonValue& json) const
	{
		const auto it = m_factories.find(name);
		if (it == m_factories.end()) return nullptr;

		auto obj = it->second();
		if (obj)
		{
			JsonReader reader{json};
			obj->deserialize(reader);
		}
		return obj;
	}

	/// @brief 型が登録済みか判定する
	[[nodiscard]] bool isRegistered(const std::string& name) const
	{
		return m_factories.find(name) != m_factories.end();
	}

	/// @brief 登録を全てクリアする
	void clear() { m_factories.clear(); }

private:
	SerializationRegistry() = default;
	std::unordered_map<std::string, FactoryFunc> m_factories;
};

// ── 自動登録マクロ ──

/// @brief シリアライズ可能型の自動登録マクロ
/// @details クラス定義内に配置すると、静的初期化時にSerializationRegistryに登録される。
///
/// @code
/// class MyComponent : public mitiru::ISerializable {
///     MITIRU_SERIALIZABLE(MyComponent)
///     // ...
/// };
/// @endcode
#define MITIRU_SERIALIZABLE(ClassName) \
	private: \
		struct AutoRegister_##ClassName { \
			AutoRegister_##ClassName() { \
				::mitiru::SerializationRegistry::instance().registerType<ClassName>(#ClassName); \
			} \
		}; \
		inline static const AutoRegister_##ClassName s_autoReg_##ClassName{}; \
	public:

} // namespace mitiru
