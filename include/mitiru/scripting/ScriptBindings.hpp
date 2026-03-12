#pragma once

/// @file ScriptBindings.hpp
/// @brief Mitiruエンジンのスクリプトバインディング定義
/// @details スクリプトエンジンに公開するAPIのカタログを定義する。
///          将来的にLua/WASM等の具象エンジンにバインドする。

#include <string>
#include <vector>

namespace mitiru::scripting
{

/// @brief スクリプトバインディング定義
/// @details エンジンがスクリプトに公開する関数・変数の一覧を管理する。
///          各バインディングは名前・カテゴリ・説明を持つ。
struct ScriptBindings
{
	/// @brief 個々のバインディング情報
	struct Binding
	{
		std::string name;         ///< バインディング名（例: "moveEntity"）
		std::string category;     ///< カテゴリ（例: "ecs", "input", "scene"）
		std::string description;  ///< 説明（日本語）
	};

	/// @brief 利用可能なバインディング一覧を取得する
	/// @return バインディングのvector
	/// @details エンジンが提供する標準バインディングの一覧を返す。
	[[nodiscard]] static std::vector<Binding> availableBindings()
	{
		return {
			{"getEntity",     "ecs",   "エンティティを取得する"},
			{"createEntity",  "ecs",   "エンティティを生成する"},
			{"destroyEntity", "ecs",   "エンティティを破棄する"},
			{"isKeyDown",     "input", "キーが押されているか判定する"},
			{"isKeyPressed",  "input", "キーが押された瞬間か判定する"},
			{"playSound",     "audio", "サウンドを再生する"},
			{"stopSound",     "audio", "サウンドを停止する"},
			{"changeScene",   "scene", "シーンを切り替える"},
			{"log",           "debug", "デバッグログを出力する"},
			{"getDeltaTime",  "core",  "前フレームからの経過時間を取得する"},
		};
	}
};

} // namespace mitiru::scripting
