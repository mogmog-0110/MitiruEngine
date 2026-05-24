#pragma once

/// @file DataNode.hpp
/// @brief JSON互換のデータノード（ゲームパラメータ・マップデータ用）
///
/// ゲームの設定パラメータやマップデータを階層的に管理するDOM型データ構造。
/// JSON形式の読み書きを内蔵し、外部ライブラリ不要で動作する。
///
/// @code
/// using namespace sgc::data;
/// // JSONから構築
/// auto config = DataNode::parse(R"({
///     "player": {
///         "speed": 5200.0,
///         "jumpForce": 12.5,
///         "radius": 15.0
///     }
/// })");
///
/// // ドットパスでアクセス
/// double speed = config.at("player.speed").asFloat(0.0);
///
/// // プログラムから構築
/// DataNode node = DataNode::object();
/// node["name"] = DataNode("Linear Plains");
/// node["bounds"] = DataNode::array({DataNode(-15.0), DataNode(-10.0), DataNode(30.0), DataNode(25.0)});
///
/// // JSONに書き出し
/// std::string json = node.toJson(true);  // pretty print
/// @endcode

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace sgc::data
{

/// @brief データノードの型
enum class NodeType
{
	Null,    ///< null値
	Bool,    ///< 真偽値
	Int,     ///< 整数（int64_t）
	Float,   ///< 浮動小数点（double）
	String,  ///< 文字列
	Array,   ///< 配列
	Object,  ///< オブジェクト（キーバリュー）
};

/// @brief JSON互換データノード
///
/// JSONのすべての型（null, bool, number, string, array, object）を
/// 単一のクラスで表現する。ゲームパラメータやマップデータの管理に使用。
class DataNode
{
public:
	// ─── コンストラクタ ───

	/// @brief Null値を構築する
	DataNode() noexcept : m_type(NodeType::Null) {}

	/// @brief 真偽値を構築する
	explicit DataNode(bool v) noexcept : m_type(NodeType::Bool), m_bool(v) {}

	/// @brief 整数を構築する
	explicit DataNode(int v) noexcept : m_type(NodeType::Int), m_int(v) {}

	/// @brief 64bit整数を構築する
	explicit DataNode(int64_t v) noexcept : m_type(NodeType::Int), m_int(v) {}

	/// @brief 倍精度浮動小数点を構築する
	explicit DataNode(double v) noexcept : m_type(NodeType::Float), m_float(v) {}

	/// @brief 単精度浮動小数点を構築する
	explicit DataNode(float v) noexcept : m_type(NodeType::Float), m_float(static_cast<double>(v)) {}

	/// @brief 文字列を構築する（const参照）
	explicit DataNode(const std::string& v) : m_type(NodeType::String), m_string(v) {}

	/// @brief 文字列を構築する（ムーブ）
	explicit DataNode(std::string&& v) noexcept : m_type(NodeType::String), m_string(std::move(v)) {}

	/// @brief 文字列を構築する（Cスタイル文字列）
	explicit DataNode(const char* v) : m_type(NodeType::String), m_string(v) {}

	// コピー・ムーブ
	DataNode(const DataNode& other) = default;
	DataNode(DataNode&& other) noexcept = default;
	DataNode& operator=(const DataNode& other) = default;
	DataNode& operator=(DataNode&& other) noexcept = default;

	// ─── ファクトリ ───

	/// @brief 空の配列ノードを生成する
	/// @return 配列型のDataNode
	[[nodiscard]] static DataNode array()
	{
		DataNode n;
		n.m_type = NodeType::Array;
		return n;
	}

	/// @brief 初期値付き配列ノードを生成する
	/// @param items 初期要素のリスト
	/// @return 配列型のDataNode
	[[nodiscard]] static DataNode array(std::initializer_list<DataNode> items)
	{
		DataNode n;
		n.m_type = NodeType::Array;
		n.m_array = items;
		return n;
	}

	/// @brief 空のオブジェクトノードを生成する
	/// @return オブジェクト型のDataNode
	[[nodiscard]] static DataNode object()
	{
		DataNode n;
		n.m_type = NodeType::Object;
		return n;
	}

	// ─── 型情報 ───

	/// @brief ノードの型を取得する
	/// @return ノードの型
	[[nodiscard]] NodeType type() const noexcept { return m_type; }

	/// @brief Null型か判定する
	[[nodiscard]] bool isNull() const noexcept { return m_type == NodeType::Null; }

	/// @brief Bool型か判定する
	[[nodiscard]] bool isBool() const noexcept { return m_type == NodeType::Bool; }

	/// @brief Int型か判定する
	[[nodiscard]] bool isInt() const noexcept { return m_type == NodeType::Int; }

	/// @brief Float型か判定する
	[[nodiscard]] bool isFloat() const noexcept { return m_type == NodeType::Float; }

	/// @brief 数値型（IntまたはFloat）か判定する
	[[nodiscard]] bool isNumber() const noexcept { return m_type == NodeType::Int || m_type == NodeType::Float; }

	/// @brief String型か判定する
	[[nodiscard]] bool isString() const noexcept { return m_type == NodeType::String; }

	/// @brief Array型か判定する
	[[nodiscard]] bool isArray() const noexcept { return m_type == NodeType::Array; }

	/// @brief Object型か判定する
	[[nodiscard]] bool isObject() const noexcept { return m_type == NodeType::Object; }

	// ─── 値アクセス ───

	/// @brief 真偽値として取得する
	/// @param defaultVal 型が異なる場合のデフォルト値
	/// @return 真偽値
	[[nodiscard]] bool asBool(bool defaultVal = false) const noexcept
	{
		if (m_type == NodeType::Bool) return m_bool;
		return defaultVal;
	}

	/// @brief 整数として取得する
	/// @param defaultVal 型が異なる場合のデフォルト値
	/// @return 整数値
	[[nodiscard]] int64_t asInt(int64_t defaultVal = 0) const noexcept
	{
		if (m_type == NodeType::Int) return m_int;
		if (m_type == NodeType::Float) return static_cast<int64_t>(m_float);
		return defaultVal;
	}

	/// @brief 浮動小数点として取得する
	/// @param defaultVal 型が異なる場合のデフォルト値
	/// @return 浮動小数点値
	[[nodiscard]] double asFloat(double defaultVal = 0.0) const noexcept
	{
		if (m_type == NodeType::Float) return m_float;
		if (m_type == NodeType::Int) return static_cast<double>(m_int);
		return defaultVal;
	}

	/// @brief 文字列として取得する
	/// @return 文字列への参照（String型でなければ空文字列）
	[[nodiscard]] const std::string& asString() const
	{
		static const std::string empty;
		if (m_type == NodeType::String) return m_string;
		return empty;
	}

	/// @brief 文字列として取得する（デフォルト値付き）
	/// @param defaultVal 型が異なる場合のデフォルト値
	/// @return 文字列への参照
	[[nodiscard]] const std::string& asString(const std::string& defaultVal) const
	{
		if (m_type == NodeType::String) return m_string;
		return defaultVal;
	}

	// ─── 配列アクセス ───

	/// @brief 要素数を取得する（配列またはオブジェクト）
	/// @return 要素数
	[[nodiscard]] size_t size() const noexcept
	{
		if (m_type == NodeType::Array) return m_array.size();
		if (m_type == NodeType::Object) return m_objectOrder.size();
		return 0;
	}

	/// @brief 配列要素にアクセスする（範囲外はNull）
	/// @param index 配列インデックス
	/// @return 要素への参照
	[[nodiscard]] const DataNode& operator[](size_t index) const
	{
		static const DataNode null_node;
		if (m_type != NodeType::Array || index >= m_array.size()) return null_node;
		return m_array[index];
	}

	/// @brief 配列に要素を追加する（コピー）
	/// @param node 追加するノード
	void push_back(const DataNode& node)
	{
		if (m_type != NodeType::Array) m_type = NodeType::Array;
		m_array.push_back(node);
	}

	/// @brief 配列に要素を追加する（ムーブ）
	/// @param node 追加するノード
	void push_back(DataNode&& node)
	{
		if (m_type != NodeType::Array) m_type = NodeType::Array;
		m_array.push_back(std::move(node));
	}

	// ─── オブジェクトアクセス ───

	/// @brief キーで子ノードにアクセスする（存在しなければNull）
	/// @param key キー文字列
	/// @return 子ノードへのconst参照
	[[nodiscard]] const DataNode& operator[](const std::string& key) const
	{
		static const DataNode null_node;
		if (m_type != NodeType::Object) return null_node;
		auto it = m_object.find(key);
		if (it == m_object.end()) return null_node;
		return it->second;
	}

	/// @brief キーで子ノードにアクセスする（なければ作成）
	/// @param key キー文字列
	/// @return 子ノードへの参照
	DataNode& operator[](const std::string& key)
	{
		if (m_type != NodeType::Object)
		{
			m_type = NodeType::Object;
			m_object.clear();
			m_objectOrder.clear();
		}
		if (m_object.find(key) == m_object.end())
		{
			m_objectOrder.push_back(key);
		}
		return m_object[key];
	}

	/// @brief キーが存在するか判定する
	/// @param key キー文字列
	/// @return 存在すればtrue
	[[nodiscard]] bool hasKey(const std::string& key) const noexcept
	{
		if (m_type != NodeType::Object) return false;
		return m_object.find(key) != m_object.end();
	}

	/// @brief オブジェクトのキー一覧を取得する（挿入順序）
	/// @return キーのベクター
	[[nodiscard]] const std::vector<std::string>& keys() const noexcept
	{
		return m_objectOrder;
	}

	// ─── ドットパスアクセス ───

	/// @brief ドット区切りパスで深くアクセスする
	///
	/// @param dotPath ドット区切りのパス（例: "player.physics.speed"）
	/// @return 対象ノードへのconst参照（見つからなければNull）
	///
	/// @code
	/// double speed = config.at("player.physics.speed").asFloat(0.0);
	/// @endcode
	[[nodiscard]] const DataNode& at(const std::string& dotPath) const
	{
		static const DataNode null_node;
		const DataNode* current = this;
		size_t start = 0;

		while (start < dotPath.size())
		{
			size_t dot = dotPath.find('.', start);
			if (dot == std::string::npos) dot = dotPath.size();

			const std::string key = dotPath.substr(start, dot - start);
			if (current->m_type != NodeType::Object) return null_node;

			auto it = current->m_object.find(key);
			if (it == current->m_object.end()) return null_node;

			current = &it->second;
			start = dot + 1;
		}

		return *current;
	}

	// ─── JSON パース ───

	/// @brief JSON文字列をパースしてDataNodeを構築する
	/// @param json JSON形式の文字列
	/// @return パース結果のDataNode
	[[nodiscard]] static DataNode parse(const std::string& json)
	{
		size_t pos = 0;
		return parseValue(json, pos);
	}

	// ─── JSON 出力 ───

	/// @brief JSONフォーマットの文字列に変換する
	/// @param pretty 整形出力するかどうか
	/// @param indent 現在のインデントレベル
	/// @return JSON文字列
	[[nodiscard]] std::string toJson(bool pretty = false, int indent = 0) const
	{
		std::ostringstream ss;
		writeJson(ss, pretty, indent);
		return ss.str();
	}

	/// @brief ファイルからJSONを読み込んでDataNodeを構築する
	/// @param filePath ファイルパス
	/// @return 読み込み成功時はパース済みDataNode、失敗時はNullノード
	[[nodiscard]] static DataNode loadFromFile(const std::string& filePath)
	{
		std::ifstream ifs(filePath, std::ios::binary);
		if (!ifs.is_open()) return DataNode();

		std::string content((std::istreambuf_iterator<char>(ifs)),
		                     std::istreambuf_iterator<char>());

		// BOMスキップ
		if (content.size() >= 3 &&
		    static_cast<unsigned char>(content[0]) == 0xEF &&
		    static_cast<unsigned char>(content[1]) == 0xBB &&
		    static_cast<unsigned char>(content[2]) == 0xBF)
		{
			content = content.substr(3);
		}

		if (content.empty()) return DataNode();
		return parse(content);
	}

	/// @brief DataNodeをJSONファイルに書き出す
	/// @param filePath ファイルパス
	/// @param pretty 整形出力するかどうか（デフォルト: true）
	/// @return 書き込み成功時true
	bool saveToFile(const std::string& filePath, bool pretty = true) const
	{
		std::ofstream ofs(filePath, std::ios::binary);
		if (!ofs.is_open()) return false;

		const std::string json = toJson(pretty);
		ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
		return ofs.good();
	}

private:
	NodeType m_type = NodeType::Null;
	bool m_bool = false;
	int64_t m_int = 0;
	double m_float = 0.0;
	std::string m_string;
	std::vector<DataNode> m_array;
	std::unordered_map<std::string, DataNode> m_object;
	std::vector<std::string> m_objectOrder;  ///< キーの挿入順序を保持

	// ─── JSON パーサー実装 ───

	/// @brief 空白文字を読み飛ばす
	static void skipWhitespace(const std::string& s, size_t& pos)
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
		{
			++pos;
		}
	}

	/// @brief JSON値をパースする
	static DataNode parseValue(const std::string& s, size_t& pos)
	{
		skipWhitespace(s, pos);
		if (pos >= s.size()) return DataNode();

		switch (s[pos])
		{
		case '"': return parseString(s, pos);
		case '{': return parseObject(s, pos);
		case '[': return parseArray(s, pos);
		case 't': case 'f': return parseBool(s, pos);
		case 'n': return parseNull(s, pos);
		default:
			if (s[pos] == '-' || (s[pos] >= '0' && s[pos] <= '9'))
			{
				return parseNumber(s, pos);
			}
			return DataNode();
		}
	}

	/// @brief JSON文字列をパースする
	static DataNode parseString(const std::string& s, size_t& pos)
	{
		if (pos >= s.size() || s[pos] != '"') return DataNode();
		++pos;  // 開始引用符をスキップ

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
				case '/': result += '/'; break;
				case 'n': result += '\n'; break;
				case 't': result += '\t'; break;
				case 'r': result += '\r'; break;
				case 'b': result += '\b'; break;
				case 'f': result += '\f'; break;
				case 'u':
				{
					// Unicodeエスケープ: \uXXXX — そのまま通す
					result += "\\u";
					break;
				}
				default: result += s[pos]; break;
				}
			}
			else
			{
				result += s[pos];
			}
			++pos;
		}
		if (pos < s.size()) ++pos;  // 終了引用符をスキップ

		return DataNode(std::move(result));
	}

	/// @brief JSON数値をパースする
	static DataNode parseNumber(const std::string& s, size_t& pos)
	{
		size_t start = pos;
		bool isFloatVal = false;

		if (pos < s.size() && s[pos] == '-') ++pos;
		while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;

		if (pos < s.size() && s[pos] == '.')
		{
			isFloatVal = true;
			++pos;
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
		}
		if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E'))
		{
			isFloatVal = true;
			++pos;
			if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
		}

		const std::string numStr = s.substr(start, pos - start);

		if (isFloatVal)
		{
			try { return DataNode(std::stod(numStr)); }
			catch (...) { return DataNode(0.0); }
		}
		else
		{
			try { return DataNode(static_cast<int64_t>(std::stoll(numStr))); }
			catch (...) { return DataNode(static_cast<int64_t>(0)); }
		}
	}

	/// @brief JSON真偽値をパースする
	static DataNode parseBool(const std::string& s, size_t& pos)
	{
		if (s.compare(pos, 4, "true") == 0) { pos += 4; return DataNode(true); }
		if (s.compare(pos, 5, "false") == 0) { pos += 5; return DataNode(false); }
		return DataNode();
	}

	/// @brief JSON null値をパースする
	static DataNode parseNull(const std::string& s, size_t& pos)
	{
		if (s.compare(pos, 4, "null") == 0) { pos += 4; }
		return DataNode();
	}

	/// @brief JSON配列をパースする
	static DataNode parseArray(const std::string& s, size_t& pos)
	{
		if (pos >= s.size() || s[pos] != '[') return DataNode();
		++pos;

		DataNode result = DataNode::array();
		skipWhitespace(s, pos);

		if (pos < s.size() && s[pos] == ']')
		{
			++pos;
			return result;
		}

		while (pos < s.size())
		{
			result.push_back(parseValue(s, pos));
			skipWhitespace(s, pos);

			if (pos < s.size() && s[pos] == ',')
			{
				++pos;
				continue;
			}
			break;
		}

		skipWhitespace(s, pos);
		if (pos < s.size() && s[pos] == ']') ++pos;
		return result;
	}

	/// @brief JSONオブジェクトをパースする
	static DataNode parseObject(const std::string& s, size_t& pos)
	{
		if (pos >= s.size() || s[pos] != '{') return DataNode();
		++pos;

		DataNode result = DataNode::object();
		skipWhitespace(s, pos);

		if (pos < s.size() && s[pos] == '}')
		{
			++pos;
			return result;
		}

		while (pos < s.size())
		{
			skipWhitespace(s, pos);

			// キーをパース
			DataNode keyNode = parseString(s, pos);
			if (!keyNode.isString()) break;
			const std::string key = keyNode.asString();

			skipWhitespace(s, pos);
			if (pos < s.size() && s[pos] == ':') ++pos;

			// 値をパース
			result[key] = parseValue(s, pos);
			skipWhitespace(s, pos);

			if (pos < s.size() && s[pos] == ',')
			{
				++pos;
				continue;
			}
			break;
		}

		skipWhitespace(s, pos);
		if (pos < s.size() && s[pos] == '}') ++pos;
		return result;
	}

	// ─── JSON 出力実装 ───

	/// @brief JSONとしてストリームに書き出す
	void writeJson(std::ostringstream& ss, bool pretty, int indent) const
	{
		const std::string ind(pretty ? indent * 2 : 0, ' ');
		const std::string ind1(pretty ? (indent + 1) * 2 : 0, ' ');
		const std::string nl = pretty ? "\n" : "";
		const std::string sp = pretty ? " " : "";

		switch (m_type)
		{
		case NodeType::Null:
			ss << "null";
			break;
		case NodeType::Bool:
			ss << (m_bool ? "true" : "false");
			break;
		case NodeType::Int:
			ss << m_int;
			break;
		case NodeType::Float:
		{
			// 整数値の場合は .0 を付ける
			if (m_float == std::floor(m_float) && std::abs(m_float) < 1e15)
			{
				ss << std::fixed;
				ss.precision(1);
				ss << m_float;
				ss << std::defaultfloat;
			}
			else
			{
				ss << m_float;
			}
			break;
		}
		case NodeType::String:
			writeString(ss, m_string);
			break;
		case NodeType::Array:
		{
			ss << "[" << nl;
			for (size_t i = 0; i < m_array.size(); ++i)
			{
				if (pretty) ss << ind1;
				m_array[i].writeJson(ss, pretty, indent + 1);
				if (i + 1 < m_array.size()) ss << ",";
				ss << nl;
			}
			if (pretty) ss << ind;
			ss << "]";
			break;
		}
		case NodeType::Object:
		{
			ss << "{" << nl;
			for (size_t i = 0; i < m_objectOrder.size(); ++i)
			{
				const auto& key = m_objectOrder[i];
				auto it = m_object.find(key);
				if (it == m_object.end()) continue;

				if (pretty) ss << ind1;
				writeString(ss, key);
				ss << ":" << sp;
				it->second.writeJson(ss, pretty, indent + 1);
				if (i + 1 < m_objectOrder.size()) ss << ",";
				ss << nl;
			}
			if (pretty) ss << ind;
			ss << "}";
			break;
		}
		}
	}

	/// @brief 文字列をJSONエスケープして書き出す
	static void writeString(std::ostringstream& ss, const std::string& s)
	{
		ss << '"';
		for (const char c : s)
		{
			switch (c)
			{
			case '"': ss << "\\\""; break;
			case '\\': ss << "\\\\"; break;
			case '\n': ss << "\\n"; break;
			case '\t': ss << "\\t"; break;
			case '\r': ss << "\\r"; break;
			case '\b': ss << "\\b"; break;
			case '\f': ss << "\\f"; break;
			default: ss << c; break;
			}
		}
		ss << '"';
	}
};

} // namespace sgc::data
