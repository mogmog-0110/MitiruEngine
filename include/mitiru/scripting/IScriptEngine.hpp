#pragma once

/// @file IScriptEngine.hpp
/// @brief スクリプトエンジンインターフェース
/// @details Lua/WASM等のスクリプトエンジンを抽象化するインターフェース。
///          変数の設定・取得、スクリプトの実行、関数呼び出しを統一的に扱う。

#include <string>
#include <string_view>

namespace mitiru::scripting
{

/// @brief スクリプトエンジン抽象インターフェース
/// @details 各スクリプト言語の具象実装はこのインターフェースを継承する。
class IScriptEngine
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IScriptEngine() = default;

	/// @brief スクリプト文字列を実行する
	/// @param script 実行するスクリプト文字列
	/// @return 成功なら true
	virtual bool execute(std::string_view script) = 0;

	/// @brief ファイルからスクリプトを実行する
	/// @param path スクリプトファイルのパス
	/// @return 成功なら true
	virtual bool executeFile(std::string_view path) = 0;

	/// @brief グローバル数値変数を設定する
	/// @param name 変数名
	/// @param value 値
	virtual void setGlobal(std::string_view name, double value) = 0;

	/// @brief グローバル文字列変数を設定する
	/// @param name 変数名
	/// @param value 値
	virtual void setGlobal(std::string_view name, std::string_view value) = 0;

	/// @brief グローバル数値変数を取得する
	/// @param name 変数名
	/// @return 変数の値（未定義の場合は 0.0）
	[[nodiscard]] virtual double getGlobalNumber(std::string_view name) const = 0;

	/// @brief グローバル文字列変数を取得する
	/// @param name 変数名
	/// @return 変数の値（未定義の場合は空文字列）
	[[nodiscard]] virtual std::string getGlobalString(std::string_view name) const = 0;

	/// @brief グローバル関数を呼び出す
	/// @param name 関数名
	/// @return 成功なら true
	virtual bool callFunction(std::string_view name) = 0;

	/// @brief 最後のエラーメッセージを取得する
	/// @return エラーメッセージ（エラーがなければ空文字列）
	[[nodiscard]] virtual std::string lastError() const = 0;
};

} // namespace mitiru::scripting
