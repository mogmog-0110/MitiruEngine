#pragma once

/// @file Win32Platform.hpp
/// @brief Win32プラットフォーム実装
/// @details Windows環境でのウィンドウ生成を担当するIPlatform実装。

#ifdef _WIN32

#include <memory>
#include <string_view>

#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>

namespace mitiru
{

/// @brief Win32プラットフォーム実装
/// @details Win32Windowを生成するプラットフォーム。
///          Windows環境でのみ使用可能。
class Win32Platform final : public IPlatform
{
public:
	/// @brief ウィンドウを生成する
	/// @param title ウィンドウタイトル（UTF-8）
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	/// @return Win32Windowインスタンス
	[[nodiscard]] std::unique_ptr<IWindow> createWindow(
		std::string_view title, int width, int height) override
	{
		auto window = std::make_unique<Win32Window>(title, width, height);
		window->show();
		return window;
	}

	/// @brief プラットフォーム種別を取得する
	/// @return PlatformType::Windows
	[[nodiscard]] PlatformType type() const noexcept override
	{
		return PlatformType::Windows;
	}
};

} // namespace mitiru

#endif // _WIN32
