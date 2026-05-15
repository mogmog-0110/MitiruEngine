#pragma once

/// @file WindowFactory.hpp
/// @brief ウィンドウファクトリ
/// @details プラットフォームに応じたIWindow実装を生成するファクトリ関数を提供する。
///          利用可能なバックエンドに応じて適切なウィンドウ実装を自動選択する。

#include <memory>
#include <stdexcept>
#include <string_view>

#include <mitiru/platform/IWindow.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

#ifdef MITIRU_HAS_SDL2
#include <mitiru/platform/sdl2/Sdl2Window.hpp>
#endif

#ifdef MITIRU_HAS_GLFW
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif

namespace mitiru
{

/// @brief ウィンドウバックエンド種別
enum class WindowBackend
{
	Auto,    ///< 自動選択（Win32 > SDL2 > GLFW > エラー）
	Win32,   ///< Win32 API（Windows専用）
	Sdl2,    ///< SDL2（クロスプラットフォーム）
	Glfw,    ///< GLFW（クロスプラットフォーム、Vulkan向け）
};

/// @brief プラットフォームに応じたウィンドウを生成する
/// @param backend ウィンドウバックエンド種別
/// @param title ウィンドウタイトル
/// @param width ウィンドウ幅
/// @param height ウィンドウ高さ
/// @return 生成されたウィンドウのユニークポインタ
/// @throw std::runtime_error 未対応バックエンドの場合
///
/// @code
/// auto window = mitiru::createWindow(
///     mitiru::WindowBackend::Auto, "My Game", 1280, 720);
/// while (!window->shouldClose())
/// {
///     window->pollEvents();
/// }
/// @endcode
[[nodiscard]] inline std::unique_ptr<IWindow> createWindow(
	WindowBackend backend,
	std::string_view title,
	int width,
	int height)
{
	switch (backend)
	{
	case WindowBackend::Auto:
#ifdef _WIN32
		return std::make_unique<Win32Window>(title, width, height);
#elif defined(MITIRU_HAS_SDL2)
		return std::make_unique<Sdl2Window>(title, width, height);
#elif defined(MITIRU_HAS_GLFW)
		return std::make_unique<GlfwWindow>(title, width, height);
#else
		throw std::runtime_error(
			"No window backend available on this platform");
#endif

	case WindowBackend::Win32:
#ifdef _WIN32
		return std::make_unique<Win32Window>(title, width, height);
#else
		throw std::runtime_error(
			"Win32 window backend is only available on Windows");
#endif

	case WindowBackend::Sdl2:
#ifdef MITIRU_HAS_SDL2
		return std::make_unique<Sdl2Window>(title, width, height);
#else
		throw std::runtime_error(
			"SDL2 window backend requires MITIRU_HAS_SDL2 to be defined");
#endif

	case WindowBackend::Glfw:
#ifdef MITIRU_HAS_GLFW
		return std::make_unique<GlfwWindow>(title, width, height);
#else
		throw std::runtime_error(
			"GLFW window backend requires MITIRU_HAS_GLFW to be defined");
#endif
	}

	throw std::runtime_error("Unknown window backend");
}

} // namespace mitiru
