#pragma once

/// @file EmscriptenPlatform.hpp
/// @brief Emscripten/WASM 用プラットフォーム実装
/// @details EmscriptenWindow を生成する IPlatform 実装。
///          Emscripten 環境でのみコンパイルされる。

#ifdef __EMSCRIPTEN__

#include <memory>
#include <string_view>

#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/emscripten/EmscriptenWindow.hpp>

namespace mitiru
{

/// @brief Emscripten 用プラットフォーム実装
/// @details ブラウザ上の HTML キャンバスをターゲットとするプラットフォーム。
///          createWindow()で EmscriptenWindow を生成する。
///
/// @code
/// EmscriptenPlatform platform;
/// auto window = platform.createWindow("My Game", 1280, 720);
/// @endcode
class EmscriptenPlatform final : public IPlatform
{
public:
	/// @brief ウィンドウを生成する
	/// @param title ウィンドウタイトル（ブラウザの document.title に反映）
	/// @param width キャンバス幅（ピクセル）
	/// @param height キャンバス高さ（ピクセル）
	/// @return EmscriptenWindow インスタンス
	[[nodiscard]] std::unique_ptr<IWindow> createWindow(
		std::string_view title, int width, int height) override
	{
		auto window = std::make_unique<EmscriptenWindow>(
			"#canvas", width, height);
		window->setTitle(title);
		return window;
	}

	/// @brief プラットフォーム種別を取得する
	/// @return PlatformType::Emscripten
	[[nodiscard]] PlatformType type() const noexcept override
	{
		return PlatformType::Emscripten;
	}
};

} // namespace mitiru

#endif // __EMSCRIPTEN__
