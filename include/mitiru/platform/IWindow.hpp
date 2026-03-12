#pragma once

/// @file IWindow.hpp
/// @brief ウィンドウ抽象インターフェース
/// @details プラットフォーム固有のウィンドウ操作を抽象化する。

#include <string_view>

namespace mitiru
{

/// @brief ウィンドウの抽象インターフェース
/// @details ヘッドレス実装やOS固有実装がこのインターフェースを実装する。
class IWindow
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IWindow() = default;

	/// @brief ウィンドウが閉じられるべきかどうか
	/// @return 閉じるべき場合 true
	[[nodiscard]] virtual bool shouldClose() const = 0;

	/// @brief イベントをポーリングする
	virtual void pollEvents() = 0;

	/// @brief ウィンドウ幅を取得する
	[[nodiscard]] virtual int width() const = 0;

	/// @brief ウィンドウ高さを取得する
	[[nodiscard]] virtual int height() const = 0;

	/// @brief ウィンドウタイトルを設定する
	/// @param title 新しいタイトル文字列
	virtual void setTitle(std::string_view title) = 0;

	/// @brief ウィンドウの閉じ要求を設定する
	virtual void requestClose() = 0;
};

} // namespace mitiru
