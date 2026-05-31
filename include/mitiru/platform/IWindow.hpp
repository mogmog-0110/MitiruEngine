#pragma once

/// @file IWindow.hpp
/// @brief ウィンドウ抽象インターフェース
/// @details プラットフォーム固有のウィンドウ操作を抽象化する。

#include <functional>
#include <string_view>

namespace mitiru
{

// 前方宣言
class InputState;
class InputInjector;

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

	/// @brief 入力状態の転送先を設定する
	/// @param state InputStateへの非所有ポインタ（Engineが所有）
	/// @details プラットフォーム固有のイベントからInputStateに入力を転送する。
	///          デフォルトはno-op（ヘッドレス等の入力不要な実装向け）。
	virtual void setInputState(InputState* /*state*/) {}

	/// @brief 入力インジェクターを設定する
	/// @param injector InputInjectorへの非所有ポインタ（Engineが所有）
	/// @details 設定されると、キー/マウスイベントをInputState直接mutateではなく
	///          InputInjector::inject() 経由で発行する。RECORDモードでhuman playを
	///          キャプチャするために使用する。デフォルトはno-op。
	///
	/// 例:
	/// @code
	/// engine.window()->setInputInjector(&myInjector);
	/// @endcode
	virtual void setInputInjector(InputInjector* /*injector*/) noexcept {}

	/// @brief ウィンドウリサイズ時のコールバックを設定する
	/// @param cb 新しいwidth, heightを受け取るコールバック
	/// @details デフォルトはno-op（リサイズイベントを内部処理するウィンドウ向け）。
	virtual void setResizeCallback(std::function<void(int, int)> /*cb*/) {}

	/// @brief リサイズ時の最小クライアントサイズを設定する (px、0=制限なし)
	/// @details 枠 drag でこの client サイズ未満へ縮められないようにする。
	///          デフォルトはno-op（最小サイズを強制しない実装向け）。
	virtual void setMinClientSize(int /*w*/, int /*h*/) {}
};

} // namespace mitiru
