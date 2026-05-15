#pragma once

/// @file GamePluginMacros.hpp
/// @brief ゲームDLLエクスポートマクロ
/// @details ゲームプロジェクトの1つの.cppファイルに MITIRU_GAME_PLUGIN(MyGame) を
///          記述することで、MitiruHubから動的ロード可能なDLLを生成できる。
///
/// @code
/// // MyGame.cpp
/// #include "MyGame.hpp"
/// #include <mitiru/core/GamePluginMacros.hpp>
///
/// MITIRU_GAME_PLUGIN(MyGame)
/// @endcode

#include <mitiru/core/Game.hpp>

/// @brief DLLエクスポート属性
/// @details Windows: __declspec(dllexport)、その他: visibility("default")
#ifdef _WIN32
#define MITIRU_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define MITIRU_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// @brief ゲームDLLプラグインエクスポートマクロ
/// @param GameClass mitiru::Game を継承したゲームクラス名
/// @details 以下の3関数をDLLからエクスポートする:
///          - createGame()   : ゲームインスタンスを new して返す
///          - destroyGame()  : ゲームインスタンスを delete する
///          - getGameName()  : ゲームクラス名の文字列を返す
#define MITIRU_GAME_PLUGIN(GameClass)                                     \
	MITIRU_PLUGIN_EXPORT mitiru::Game* createGame()                       \
	{                                                                     \
		return new GameClass();                                           \
	}                                                                     \
	MITIRU_PLUGIN_EXPORT void destroyGame(mitiru::Game* game)             \
	{                                                                     \
		delete game;                                                      \
	}                                                                     \
	MITIRU_PLUGIN_EXPORT const char* getGameName()                        \
	{                                                                     \
		return #GameClass;                                                \
	}
