#pragma once

/// @file Config.hpp
/// @brief エンジン設定構造体
/// @details Mitiruエンジンの初期化パラメータを保持する。

#include <cstdint>
#include <string>

namespace mitiru
{

/// @brief グラフィックスバックエンド列挙
namespace gfx
{

/// @brief GPUバックエンドの種別
enum class Backend
{
	Auto,    ///< 環境に応じて自動選択
	Dx11,    ///< DirectX 11
	Dx12,    ///< DirectX 12
	Vulkan,  ///< Vulkan
	OpenGL,  ///< OpenGL
	WebGL,   ///< WebGL2（Emscripten/WASM環境用）
	Null     ///< ヌルデバイス（ヘッドレスモード用）
};

} // namespace gfx

/// @brief エンジン設定
/// @details エンジン起動時に渡す全パラメータを集約する。
///          デフォルト値が設定されているため、必要なフィールドだけ変更すればよい。
struct EngineConfig
{
	std::string title = "Mitiru Game";     ///< ウィンドウタイトル
	int windowWidth = 1280;                ///< ウィンドウ幅（ピクセル）
	int windowHeight = 720;                ///< ウィンドウ高さ（ピクセル）
	bool headless = false;                 ///< ヘッドレスモード（ウィンドウなし）
	bool deterministic = true;             ///< 決定論的モード（固定dt）
	std::uint64_t randomSeed = 42;         ///< 乱数シード
	float targetTps = 60.0f;               ///< 目標TPS（tick/秒）
	gfx::Backend gfxBackend = gfx::Backend::Auto;  ///< グラフィックスバックエンド
	bool enableObserver = true;            ///< オブザーバー機能の有効化
	int observePort = 0;                   ///< オブザーバーポート（0=自動割当）
};

} // namespace mitiru
