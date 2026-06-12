#pragma once

/// @file EngineCommands.hpp
/// @brief エンジン操作コマンド一括登録 (集約ヘッダ)
/// @details CommandSystemにエンジンの全操作をカテゴリ別に登録する。
///          scene / render / audio / physics / ui / vn / system / editor / asset / input
///          実装は detail/EngineCommands_*.hpp に分割。

#include <mitiru/core/CommandSystem.hpp>
#include <mitiru/core/detail/EngineCommands_Args.hpp>
#include <mitiru/core/detail/EngineCommands_Control.hpp>
#include <mitiru/core/detail/EngineCommands_World.hpp>

namespace mitiru
{

/// @brief エンジンの全操作をCommandSystemに登録するクラス
/// @details Engineへの参照を保持し、各コマンドのラムダから操作を実行する。
///          Engineが生存している間だけ有効。
///
/// @code
/// mitiru::CommandSystem cmd;
/// mitiru::Engine engine;
/// mitiru::EngineCommands::registerAll(cmd, engine);
///
/// cmd.executeString("system.fps");
/// cmd.executeString("scene.list");
/// @endcode
class EngineCommands
{
public:
	/// @brief 全カテゴリのコマンドを登録する
	/// @param cmd コマンドシステム
	/// @param engine エンジンインスタンス
	static void registerAll(CommandSystem& cmd, Engine& engine)
	{
		detail::registerSceneCommands(cmd, engine);
		detail::registerRenderCommands(cmd, engine);
		detail::registerAudioCommands(cmd, engine);
		detail::registerPhysicsCommands(cmd, engine);
		detail::registerUICommands(cmd, engine);
		detail::registerVNCommands(cmd, engine);
		detail::registerSystemCommands(cmd, engine);
		detail::registerEditorCommands(cmd, engine);
		detail::registerAssetCommands(cmd, engine);
		detail::registerInputCommands(cmd, engine);
	}
};

} // namespace mitiru
