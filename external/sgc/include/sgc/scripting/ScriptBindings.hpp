#pragma once

/// @file ScriptBindings.hpp
/// @brief スクリプトエンジン抽象インターフェース
///
/// ゲームロジックをスクリプト言語から操作するための
/// 汎用バインディングレイヤーを提供する。
///
/// @code
/// // カスタムスクリプトエンジンの実装
/// class LuaEngine : public sgc::scripting::IScriptEngine
/// {
/// public:
///     ScriptValue eval(const std::string& code) override { /* ... */ }
///     bool registerFunction(const std::string& name, ScriptFunction fn) override { /* ... */ }
///     bool registerVariable(const std::string& name, ScriptValue val) override { /* ... */ }
/// };
/// @endcode

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace sgc::scripting
{

/// @brief スクリプト値を表す多態型
///
/// スクリプトエンジンとの値の受け渡しに使用する。
/// int, float, bool, string のいずれかを保持できる。
using ScriptValue = std::variant<int, float, bool, std::string>;

/// @brief スクリプトから呼び出し可能な関数の型
///
/// 引数としてScriptValueのベクタを受け取り、ScriptValueを返す。
using ScriptFunction = std::function<ScriptValue(const std::vector<ScriptValue>&)>;

/// @brief スクリプトエンジン抽象インターフェース
///
/// 各種スクリプト言語（Lua, Python等）のバインディングを
/// 統一的に扱うためのインターフェース。
class IScriptEngine
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IScriptEngine() = default;

	/// @brief スクリプトコードを評価する
	/// @param code 実行するスクリプトコード文字列
	/// @return 実行結果のScriptValue
	[[nodiscard]]
	virtual ScriptValue eval(const std::string& code) = 0;

	/// @brief ネイティブ関数をスクリプトに登録する
	/// @param name スクリプト側での関数名
	/// @param fn 登録する関数オブジェクト
	/// @return 登録に成功した場合true
	virtual bool registerFunction(const std::string& name, ScriptFunction fn) = 0;

	/// @brief 変数をスクリプトに登録する
	/// @param name スクリプト側での変数名
	/// @param val 変数の初期値
	/// @return 登録に成功した場合true
	virtual bool registerVariable(const std::string& name, ScriptValue val) = 0;
};

/// @brief ScriptValueの型名を取得する
/// @param val 対象のスクリプト値
/// @return 型名文字列（"int", "float", "bool", "string"のいずれか）
[[nodiscard]]
inline std::string getTypeName(const ScriptValue& val)
{
	return std::visit(
		[](const auto& v) -> std::string
		{
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, int>)
			{
				return "int";
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				return "float";
			}
			else if constexpr (std::is_same_v<T, bool>)
			{
				return "bool";
			}
			else
			{
				return "string";
			}
		},
		val
	);
}

/// @brief ScriptValueをint値として取得する
/// @param val 対象のスクリプト値
/// @return int値（型が異なる場合は0）
[[nodiscard]]
inline int getInt(const ScriptValue& val)
{
	if (const auto* p = std::get_if<int>(&val))
	{
		return *p;
	}
	return 0;
}

/// @brief ScriptValueをfloat値として取得する
/// @param val 対象のスクリプト値
/// @return float値（型が異なる場合は0.0f）
[[nodiscard]]
inline float getFloat(const ScriptValue& val)
{
	if (const auto* p = std::get_if<float>(&val))
	{
		return *p;
	}
	return 0.0f;
}

/// @brief ScriptValueをbool値として取得する
/// @param val 対象のスクリプト値
/// @return bool値（型が異なる場合はfalse）
[[nodiscard]]
inline bool getBool(const ScriptValue& val)
{
	if (const auto* p = std::get_if<bool>(&val))
	{
		return *p;
	}
	return false;
}

/// @brief ScriptValueをstring値として取得する
/// @param val 対象のスクリプト値
/// @return string値（型が異なる場合は空文字列）
[[nodiscard]]
inline std::string getString(const ScriptValue& val)
{
	if (const auto* p = std::get_if<std::string>(&val))
	{
		return *p;
	}
	return "";
}

} // namespace sgc::scripting
