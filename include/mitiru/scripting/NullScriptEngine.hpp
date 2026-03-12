#pragma once

/// @file NullScriptEngine.hpp
/// @brief Nullスクリプトエンジン（No-op実装）
/// @details スクリプト機能を無効化したい場合に使用するダミー実装。
///          全操作が成功し、デフォルト値を返す。

#include <mitiru/scripting/IScriptEngine.hpp>

namespace mitiru::scripting
{

/// @brief Nullスクリプトエンジン
/// @details IScriptEngineのNo-op実装。テストやスクリプト非使用時に利用する。
///          execute()は常にtrue、getGlobal系は0/空文字列を返す。
class NullScriptEngine final : public IScriptEngine
{
public:
	/// @brief スクリプトを実行する（常に成功）
	/// @param script スクリプト文字列（無視される）
	/// @return 常に true
	bool execute(std::string_view /*script*/) override
	{
		return true;
	}

	/// @brief ファイルからスクリプトを実行する（常に成功）
	/// @param path ファイルパス（無視される）
	/// @return 常に true
	bool executeFile(std::string_view /*path*/) override
	{
		return true;
	}

	/// @brief グローバル数値変数を設定する（何もしない）
	/// @param name 変数名（無視される）
	/// @param value 値（無視される）
	void setGlobal(std::string_view /*name*/, double /*value*/) override
	{
	}

	/// @brief グローバル文字列変数を設定する（何もしない）
	/// @param name 変数名（無視される）
	/// @param value 値（無視される）
	void setGlobal(std::string_view /*name*/, std::string_view /*value*/) override
	{
	}

	/// @brief グローバル数値変数を取得する（常に0.0）
	/// @param name 変数名（無視される）
	/// @return 常に 0.0
	[[nodiscard]] double getGlobalNumber(std::string_view /*name*/) const override
	{
		return 0.0;
	}

	/// @brief グローバル文字列変数を取得する（常に空文字列）
	/// @param name 変数名（無視される）
	/// @return 常に空文字列
	[[nodiscard]] std::string getGlobalString(std::string_view /*name*/) const override
	{
		return {};
	}

	/// @brief グローバル関数を呼び出す（常に成功）
	/// @param name 関数名（無視される）
	/// @return 常に true
	bool callFunction(std::string_view /*name*/) override
	{
		return true;
	}

	/// @brief 最後のエラーメッセージを取得する（常に空）
	/// @return 常に空文字列
	[[nodiscard]] std::string lastError() const override
	{
		return {};
	}
};

} // namespace mitiru::scripting
