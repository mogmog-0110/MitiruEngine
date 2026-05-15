#pragma once

/// @file Main.hpp
/// @brief プラットフォーム非依存エントリーポイント
/// @details ユーザーコードからプラットフォーム依存を完全に隠蔽する。
///          MITIRU_MAIN マクロを使用して、どのプラットフォームでも
///          同じコードでゲームを起動できる。
///
/// @code
/// #include <mitiru/Mitiru.hpp>
///
/// MITIRU_MAIN
/// {
///     mitiru::EngineConfig config;
///     config.title = "My Game";
///     config.windowWidth = 1280;
///     config.windowHeight = 720;
///
///     MyGame game;
///     mitiru::Run(game, config);
/// }
/// @endcode
///
/// あるいはテンプレート版:
/// @code
/// #include <mitiru/Mitiru.hpp>
///
/// MITIRU_RUN(MyGame, "My Game", 1280, 720)
/// @endcode

#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Game.hpp>

namespace mitiru
{

/// @brief ゲームを実行する（1行で完了する便利関数）
/// @param game ゲームインスタンス
/// @param config エンジン設定
inline void Run(Game& game, const EngineConfig& config = {})
{
	Engine engine;
	engine.run(game, config);
}

/// @brief ゲームをデフォルト設定で実行する
/// @tparam T ゲームクラス（mitiru::Game を継承）
/// @param title ウィンドウタイトル
/// @param width ウィンドウ幅
/// @param height ウィンドウ高さ
template <typename T>
void Run(const char* title = "Mitiru Game", int width = 1280, int height = 720)
{
	EngineConfig config;
	config.title = title;
	config.windowWidth = width;
	config.windowHeight = height;

	T game;
	Run(game, config);
}

} // namespace mitiru

// ── プラットフォーム別エントリーポイントマクロ ──

/// @brief プラットフォーム非依存のメイン関数マクロ
/// @details MITIRU_MAIN { ... } と書くだけで、Windows/Linux/macOS/Web全てで動く。
///          内部でプラットフォーム固有のエントリーポイントを自動生成する。
///
/// Windows: WinMain + main の両方を生成
/// Linux/macOS: main のみ
/// Emscripten: main (emscripten_set_main_loop対応)

#if defined(_WIN32) && !defined(MITIRU_CONSOLE_APP)

// Windows: コンソール窓を出さないGUIアプリケーション
// WinMainとmainの両方を定義して、どちらのサブシステムでも動くようにする

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#define MITIRU_MAIN                                                      \
	static void mitiru_user_main();                                      \
	int main() { mitiru_user_main(); return 0; }                         \
	int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)                 \
	{ mitiru_user_main(); return 0; }                                    \
	static void mitiru_user_main()

#else

// Linux / macOS / Emscripten / コンソールモード
#define MITIRU_MAIN                                                      \
	static void mitiru_user_main();                                      \
	int main() { mitiru_user_main(); return 0; }                         \
	static void mitiru_user_main()

#endif

/// @brief 1行でゲームを起動するマクロ
/// @param GameClass ゲームクラス名
/// @param title ウィンドウタイトル
/// @param width ウィンドウ幅
/// @param height ウィンドウ高さ
#define MITIRU_RUN(GameClass, title, width, height)                       \
	MITIRU_MAIN                                                          \
	{                                                                    \
		mitiru::Run<GameClass>(title, width, height);                     \
	}
