#pragma once

/// @file IPlatform.hpp
/// @brief プラットフォーム抽象インターフェース
/// @details ウィンドウ生成などプラットフォーム固有の操作を抽象化する。

#include <memory>
#include <string_view>

#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief プラットフォーム種別
enum class PlatformType
{
	Headless,    ///< ヘッドレス（ウィンドウなし）
	Windows,     ///< Windows
	Linux,       ///< Linux
	MacOS,       ///< macOS
	Emscripten   ///< Emscripten/WASM（ブラウザ環境）
};

/// @brief プラットフォームの抽象インターフェース
/// @details ウィンドウ生成など、OS固有の機能を抽象化する。
class IPlatform
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IPlatform() = default;

	/// @brief ウィンドウを生成する
	/// @param title ウィンドウタイトル
	/// @param width ウィンドウ幅
	/// @param height ウィンドウ高さ
	/// @return 生成されたウィンドウ
	[[nodiscard]] virtual std::unique_ptr<IWindow> createWindow(
		std::string_view title, int width, int height) = 0;

	/// @brief プラットフォーム種別を取得する
	[[nodiscard]] virtual PlatformType type() const noexcept = 0;
};

} // namespace mitiru
