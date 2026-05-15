#pragma once

/// @file FlagManager.hpp
/// @brief VN用フラグ・変数マネージャ
/// @details ゲーム状態のフラグと変数をキーバリューストアで管理する。
///          bool/int/float/string型をサポートし、条件評価、スコープ管理、
///          変更通知、JSON直列化を提供する。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <mitiru/data/Json.hpp>

namespace mitiru::vn
{

/// @brief フラグ値の型（bool, int, float, string）
using FlagValue = std::variant<bool, int, float, std::string>;

/// @brief フラグ変更時のコールバック型
using FlagChangeCallback = std::function<void(const std::string& key, const FlagValue& oldValue, const FlagValue& newValue)>;

/// @brief フラグのスコープ
enum class FlagScope
{
	Global,		///< グローバル（ゲーム全体で永続）
	Chapter,	///< チャプター単位（チャプター変更時にクリア可能）
};

/// @brief VN用フラグ・変数マネージャ
/// @details ゲーム状態を管理するキーバリューストア。
///          条件式の評価やJSON直列化でセーブ/ロードと統合する。
///
/// @code
/// mitiru::vn::FlagManager flags;
/// flags.set("met_sakura", true);
/// flags.set("affection", 5);
/// flags.set("player_name", std::string("Taro"));
///
/// bool result = flags.evaluate("met_sakura == true");
/// flags.set("affection", 10);
///
/// std::string json = flags.toJson();
/// FlagManager loaded;
/// loaded.fromJson(json);
/// @endcode
class FlagManager
{
public:
	// ── 設定・取得 ─────────────────────────────────────────

	/// @brief フラグを設定する
	/// @param key キー名
	/// @param value 値
	/// @param scope スコープ（デフォルト: Global）
	void set(const std::string& key, FlagValue value, FlagScope scope = FlagScope::Global)
	{
		auto& store = (scope == FlagScope::Global) ? m_globalFlags : m_chapterFlags;

		FlagValue oldValue;
		auto it = store.find(key);
		if (it != store.end())
		{
			oldValue = it->second;
		}

		store[key] = value;
		notifyChange(key, oldValue, value);
	}

	/// @brief フラグを取得する
	/// @param key キー名
	/// @return 値（存在しない場合はnullopt）
	[[nodiscard]] std::optional<FlagValue> get(const std::string& key) const
	{
		// チャプタースコープを先に検索
		auto it = m_chapterFlags.find(key);
		if (it != m_chapterFlags.end())
		{
			return it->second;
		}
		// グローバルスコープを検索
		it = m_globalFlags.find(key);
		if (it != m_globalFlags.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief bool値として取得する
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return bool値
	[[nodiscard]] bool getBool(const std::string& key, bool defaultValue = false) const
	{
		auto val = get(key);
		if (!val.has_value()) return defaultValue;
		if (auto* b = std::get_if<bool>(&*val)) return *b;
		if (auto* i = std::get_if<int>(&*val)) return *i != 0;
		if (auto* f = std::get_if<float>(&*val)) return *f != 0.0f;
		return defaultValue;
	}

	/// @brief int値として取得する
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return int値
	[[nodiscard]] int getInt(const std::string& key, int defaultValue = 0) const
	{
		auto val = get(key);
		if (!val.has_value()) return defaultValue;
		if (auto* i = std::get_if<int>(&*val)) return *i;
		if (auto* b = std::get_if<bool>(&*val)) return *b ? 1 : 0;
		if (auto* f = std::get_if<float>(&*val)) return static_cast<int>(*f);
		return defaultValue;
	}

	/// @brief float値として取得する
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return float値
	[[nodiscard]] float getFloat(const std::string& key, float defaultValue = 0.0f) const
	{
		auto val = get(key);
		if (!val.has_value()) return defaultValue;
		if (auto* f = std::get_if<float>(&*val)) return *f;
		if (auto* i = std::get_if<int>(&*val)) return static_cast<float>(*i);
		if (auto* b = std::get_if<bool>(&*val)) return *b ? 1.0f : 0.0f;
		return defaultValue;
	}

	/// @brief string値として取得する
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return string値
	[[nodiscard]] std::string getString(const std::string& key, const std::string& defaultValue = "") const
	{
		auto val = get(key);
		if (!val.has_value()) return defaultValue;
		if (auto* s = std::get_if<std::string>(&*val)) return *s;
		return flagValueToString(*val);
	}

	/// @brief フラグが存在するか確認する
	/// @param key キー名
	/// @return 存在すればtrue
	[[nodiscard]] bool has(const std::string& key) const noexcept
	{
		return m_chapterFlags.count(key) > 0 || m_globalFlags.count(key) > 0;
	}

	/// @brief フラグを削除する
	/// @param key キー名
	void remove(const std::string& key)
	{
		m_chapterFlags.erase(key);
		m_globalFlags.erase(key);
	}

	// ── バルク操作 ─────────────────────────────────────────

	/// @brief 複数のフラグを一括設定する
	/// @param flags キーと値のペア
	/// @param scope スコープ
	void setMultiple(const std::vector<std::pair<std::string, FlagValue>>& flags,
		FlagScope scope = FlagScope::Global)
	{
		for (const auto& [key, value] : flags)
		{
			set(key, value, scope);
		}
	}

	/// @brief 全フラグを取得する
	/// @return 全フラグのマップ（チャプタースコープがグローバルを上書き）
	[[nodiscard]] std::unordered_map<std::string, FlagValue> getAll() const
	{
		auto result = m_globalFlags;
		for (const auto& [key, value] : m_chapterFlags)
		{
			result[key] = value;
		}
		return result;
	}

	/// @brief 全フラグをクリアする
	/// @param scope クリアするスコープ（省略時は両方）
	void clear(std::optional<FlagScope> scope = std::nullopt)
	{
		if (!scope.has_value() || *scope == FlagScope::Global)
		{
			m_globalFlags.clear();
		}
		if (!scope.has_value() || *scope == FlagScope::Chapter)
		{
			m_chapterFlags.clear();
		}
	}

	/// @brief チャプタースコープのフラグのみクリアする
	void clearChapterFlags()
	{
		m_chapterFlags.clear();
	}

	/// @brief フラグ数を取得する
	/// @return フラグ数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_globalFlags.size() + m_chapterFlags.size();
	}

	// ── 条件評価 ───────────────────────────────────────────

	/// @brief 条件式を評価する
	/// @param expression 条件式（例: "flag_agreed == true", "affection >= 5"）
	/// @return 評価結果
	/// @details サポートする演算子: ==, !=, <, <=, >, >=, &&, ||
	[[nodiscard]] bool evaluate(std::string_view expression) const
	{
		auto tokens = tokenizeExpression(expression);
		std::size_t pos = 0;
		return parseOrExpr(tokens, pos);
	}

	// ── オブザーバ ─────────────────────────────────────────

	/// @brief フラグ変更コールバックを登録する
	/// @param callback コールバック関数
	/// @return コールバックID（解除用）
	std::size_t onChange(FlagChangeCallback callback)
	{
		std::size_t id = m_nextCallbackId++;
		m_callbacks.push_back({id, std::move(callback)});
		return id;
	}

	/// @brief コールバックを解除する
	/// @param id コールバックID
	void removeCallback(std::size_t id)
	{
		m_callbacks.erase(
			std::remove_if(m_callbacks.begin(), m_callbacks.end(),
				[id](const CallbackEntry& e) { return e.id == id; }),
			m_callbacks.end());
	}

	// ── 直列化 ─────────────────────────────────────────────

	/// @brief JSON文字列として出力する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;
		j["global"] = serializeStoreToJson(m_globalFlags);
		j["chapter"] = serializeStoreToJson(m_chapterFlags);
		return j.dump();
	}

	/// @brief JSON文字列から復元する
	/// @param json JSON形式の文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		m_globalFlags.clear();
		m_chapterFlags.clear();

		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return false;

		if (j.contains("global") && j["global"].is_object())
		{
			deserializeStoreFromJson(j["global"], m_globalFlags);
		}
		if (j.contains("chapter") && j["chapter"].is_object())
		{
			deserializeStoreFromJson(j["chapter"], m_chapterFlags);
		}

		return true;
	}

private:
	// ── 内部型 ─────────────────────────────────────────────

	struct CallbackEntry
	{
		std::size_t id;
		FlagChangeCallback callback;
	};

	/// @brief 条件式のトークン型
	enum class ExprTokenType
	{
		Identifier,		///< 変数名
		BoolLiteral,	///< true/false
		IntLiteral,		///< 整数
		FloatLiteral,	///< 浮動小数点
		StringLiteral,	///< 文字列
		EqEq,			///< ==
		NotEq,			///< !=
		Less,			///< <
		LessEq,			///< <=
		Greater,		///< >
		GreaterEq,		///< >=
		And,			///< &&
		Or,				///< ||
		Not,			///< !
		LParen,			///< (
		RParen,			///< )
		Eof,			///< 終端
	};

	struct ExprToken
	{
		ExprTokenType type = ExprTokenType::Eof;
		std::string text;
	};

	// ── 通知 ───────────────────────────────────────────────

	void notifyChange(const std::string& key, const FlagValue& oldValue, const FlagValue& newValue)
	{
		for (const auto& entry : m_callbacks)
		{
			entry.callback(key, oldValue, newValue);
		}
	}

	// ── 条件式の字句解析 ───────────────────────────────────

	[[nodiscard]] static std::vector<ExprToken> tokenizeExpression(std::string_view src)
	{
		std::vector<ExprToken> tokens;
		std::size_t i = 0;

		while (i < src.size())
		{
			if (std::isspace(static_cast<unsigned char>(src[i])))
			{
				++i;
				continue;
			}

			// 文字列リテラル
			if (src[i] == '"')
			{
				++i;
				std::size_t start = i;
				while (i < src.size() && src[i] != '"') ++i;
				tokens.push_back({ExprTokenType::StringLiteral, std::string(src.substr(start, i - start))});
				if (i < src.size()) ++i;
				continue;
			}

			// 2文字演算子
			if (i + 1 < src.size())
			{
				auto two = src.substr(i, 2);
				if (two == "==") { tokens.push_back({ExprTokenType::EqEq, "=="}); i += 2; continue; }
				if (two == "!=") { tokens.push_back({ExprTokenType::NotEq, "!="}); i += 2; continue; }
				if (two == "<=") { tokens.push_back({ExprTokenType::LessEq, "<="}); i += 2; continue; }
				if (two == ">=") { tokens.push_back({ExprTokenType::GreaterEq, ">="}); i += 2; continue; }
				if (two == "&&") { tokens.push_back({ExprTokenType::And, "&&"}); i += 2; continue; }
				if (two == "||") { tokens.push_back({ExprTokenType::Or, "||"}); i += 2; continue; }
			}

			// 1文字演算子
			if (src[i] == '<') { tokens.push_back({ExprTokenType::Less, "<"}); ++i; continue; }
			if (src[i] == '>') { tokens.push_back({ExprTokenType::Greater, ">"}); ++i; continue; }
			if (src[i] == '!') { tokens.push_back({ExprTokenType::Not, "!"}); ++i; continue; }
			if (src[i] == '(') { tokens.push_back({ExprTokenType::LParen, "("}); ++i; continue; }
			if (src[i] == ')') { tokens.push_back({ExprTokenType::RParen, ")"}); ++i; continue; }

			// 数値リテラル
			if (std::isdigit(static_cast<unsigned char>(src[i])) ||
				(src[i] == '-' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1]))))
			{
				std::size_t start = i;
				if (src[i] == '-') ++i;
				bool hasDot = false;
				while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.'))
				{
					if (src[i] == '.') hasDot = true;
					++i;
				}
				auto text = std::string(src.substr(start, i - start));
				tokens.push_back({hasDot ? ExprTokenType::FloatLiteral : ExprTokenType::IntLiteral, text});
				continue;
			}

			// 識別子 / true / false
			if (std::isalpha(static_cast<unsigned char>(src[i])) || src[i] == '_')
			{
				std::size_t start = i;
				while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_'))
				{
					++i;
				}
				auto text = std::string(src.substr(start, i - start));
				if (text == "true" || text == "false")
				{
					tokens.push_back({ExprTokenType::BoolLiteral, text});
				}
				else
				{
					tokens.push_back({ExprTokenType::Identifier, text});
				}
				continue;
			}

			// 不明な文字はスキップ
			++i;
		}

		tokens.push_back({ExprTokenType::Eof, ""});
		return tokens;
	}

	// ── 条件式の再帰下降パーサー ───────────────────────────

	/// @brief OR式: and_expr (|| and_expr)*
	[[nodiscard]] bool parseOrExpr(const std::vector<ExprToken>& tokens, std::size_t& pos) const
	{
		bool result = parseAndExpr(tokens, pos);
		while (pos < tokens.size() && tokens[pos].type == ExprTokenType::Or)
		{
			++pos;
			bool rhs = parseAndExpr(tokens, pos);
			result = result || rhs;
		}
		return result;
	}

	/// @brief AND式: comparison (&& comparison)*
	[[nodiscard]] bool parseAndExpr(const std::vector<ExprToken>& tokens, std::size_t& pos) const
	{
		bool result = parseComparison(tokens, pos);
		while (pos < tokens.size() && tokens[pos].type == ExprTokenType::And)
		{
			++pos;
			bool rhs = parseComparison(tokens, pos);
			result = result && rhs;
		}
		return result;
	}

	/// @brief 比較式: primary (op primary)?
	[[nodiscard]] bool parseComparison(const std::vector<ExprToken>& tokens, std::size_t& pos) const
	{
		// NOT演算子
		if (pos < tokens.size() && tokens[pos].type == ExprTokenType::Not)
		{
			++pos;
			return !parseComparison(tokens, pos);
		}

		// 括弧
		if (pos < tokens.size() && tokens[pos].type == ExprTokenType::LParen)
		{
			++pos;
			bool result = parseOrExpr(tokens, pos);
			if (pos < tokens.size() && tokens[pos].type == ExprTokenType::RParen)
			{
				++pos;
			}
			return result;
		}

		FlagValue lhs = parseValue(tokens, pos);

		if (pos >= tokens.size() || tokens[pos].type == ExprTokenType::Eof ||
			tokens[pos].type == ExprTokenType::And || tokens[pos].type == ExprTokenType::Or ||
			tokens[pos].type == ExprTokenType::RParen)
		{
			// 単独の値をboolとして評価
			return valueToBool(lhs);
		}

		auto op = tokens[pos].type;
		++pos;

		FlagValue rhs = parseValue(tokens, pos);
		return compareValues(lhs, op, rhs);
	}

	/// @brief 値をパースする（変数参照またはリテラル）
	[[nodiscard]] FlagValue parseValue(const std::vector<ExprToken>& tokens, std::size_t& pos) const
	{
		if (pos >= tokens.size())
		{
			return false;
		}

		const auto& tok = tokens[pos];
		++pos;

		switch (tok.type)
		{
		case ExprTokenType::BoolLiteral:
			return tok.text == "true";
		case ExprTokenType::IntLiteral:
			return std::stoi(tok.text);
		case ExprTokenType::FloatLiteral:
			return std::stof(tok.text);
		case ExprTokenType::StringLiteral:
			return tok.text;
		case ExprTokenType::Identifier:
		{
			auto val = get(tok.text);
			if (val.has_value()) return *val;
			return false;
		}
		default:
			return false;
		}
	}

	/// @brief FlagValueをboolに変換する
	[[nodiscard]] static bool valueToBool(const FlagValue& val)
	{
		if (auto* b = std::get_if<bool>(&val)) return *b;
		if (auto* i = std::get_if<int>(&val)) return *i != 0;
		if (auto* f = std::get_if<float>(&val)) return *f != 0.0f;
		if (auto* s = std::get_if<std::string>(&val)) return !s->empty();
		return false;
	}

	/// @brief 2つのFlagValueを比較する
	[[nodiscard]] static bool compareValues(const FlagValue& lhs, ExprTokenType op, const FlagValue& rhs)
	{
		// 文字列同士
		if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs))
		{
			const auto& l = std::get<std::string>(lhs);
			const auto& r = std::get<std::string>(rhs);
			switch (op)
			{
			case ExprTokenType::EqEq:    return l == r;
			case ExprTokenType::NotEq:   return l != r;
			case ExprTokenType::Less:    return l < r;
			case ExprTokenType::LessEq:  return l <= r;
			case ExprTokenType::Greater: return l > r;
			case ExprTokenType::GreaterEq: return l >= r;
			default: return false;
			}
		}

		// bool同士
		if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs))
		{
			bool l = std::get<bool>(lhs);
			bool r = std::get<bool>(rhs);
			switch (op)
			{
			case ExprTokenType::EqEq:  return l == r;
			case ExprTokenType::NotEq: return l != r;
			default: return false;
			}
		}

		// 数値比較（int/float混在対応）
		float l = toFloat(lhs);
		float r = toFloat(rhs);
		switch (op)
		{
		case ExprTokenType::EqEq:    return l == r;
		case ExprTokenType::NotEq:   return l != r;
		case ExprTokenType::Less:    return l < r;
		case ExprTokenType::LessEq:  return l <= r;
		case ExprTokenType::Greater: return l > r;
		case ExprTokenType::GreaterEq: return l >= r;
		default: return false;
		}
	}

	/// @brief FlagValueをfloatに変換する
	[[nodiscard]] static float toFloat(const FlagValue& val)
	{
		if (auto* f = std::get_if<float>(&val)) return *f;
		if (auto* i = std::get_if<int>(&val)) return static_cast<float>(*i);
		if (auto* b = std::get_if<bool>(&val)) return *b ? 1.0f : 0.0f;
		return 0.0f;
	}

	// ── 直列化ヘルパー ─────────────────────────────────────

	[[nodiscard]] static std::string flagValueToString(const FlagValue& val)
	{
		if (auto* b = std::get_if<bool>(&val)) return *b ? "true" : "false";
		if (auto* i = std::get_if<int>(&val)) return std::to_string(*i);
		if (auto* f = std::get_if<float>(&val))
		{
			std::ostringstream oss;
			oss << *f;
			return oss.str();
		}
		if (auto* s = std::get_if<std::string>(&val)) return *s;
		return "";
	}

	/// @brief フラグストアをnlohmann::jsonオブジェクトに変換する
	[[nodiscard]] static mitiru::data::Json serializeStoreToJson(
		const std::unordered_map<std::string, FlagValue>& store)
	{
		mitiru::data::Json j = mitiru::data::Json::object();
		for (const auto& [key, value] : store)
		{
			if (auto* b = std::get_if<bool>(&value)) { j[key] = *b; }
			else if (auto* i = std::get_if<int>(&value)) { j[key] = *i; }
			else if (auto* f = std::get_if<float>(&value)) { j[key] = *f; }
			else if (auto* s = std::get_if<std::string>(&value)) { j[key] = *s; }
		}
		return j;
	}

	/// @brief nlohmann::jsonオブジェクトからフラグストアを復元する
	static void deserializeStoreFromJson(const mitiru::data::Json& j,
		std::unordered_map<std::string, FlagValue>& store)
	{
		for (auto it = j.begin(); it != j.end(); ++it)
		{
			const auto& key = it.key();
			const auto& val = it.value();

			if (val.is_boolean())
			{
				store[key] = val.get<bool>();
			}
			else if (val.is_number_integer())
			{
				store[key] = val.get<int>();
			}
			else if (val.is_number_float())
			{
				store[key] = val.get<float>();
			}
			else if (val.is_string())
			{
				store[key] = val.get<std::string>();
			}
		}
	}

	// ── メンバ ─────────────────────────────────────────────

	std::unordered_map<std::string, FlagValue> m_globalFlags;		///< グローバルフラグ
	std::unordered_map<std::string, FlagValue> m_chapterFlags;		///< チャプタースコープフラグ
	std::vector<CallbackEntry> m_callbacks;							///< 変更通知コールバック
	std::size_t m_nextCallbackId = 0;								///< 次のコールバックID
};

} // namespace mitiru::vn
