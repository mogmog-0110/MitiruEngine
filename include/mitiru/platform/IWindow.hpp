#pragma once

/// @file IWindow.hpp
/// @brief ウィンドウ抽象インターフェース
/// @details プラットフォーム固有のウィンドウ操作を抽象化する。

#include <cstdint>
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

	/// @brief ウィンドウアイコンを .ico ファイルで設定する
	/// @param icoPath .ico ファイルのパス (UTF-8)
	/// @details デフォルトは no-op（headless / アイコン概念のない実装向け）。
	virtual void setIcon(std::string_view /*icoPath*/) {}

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

	/// @brief ウィンドウの画面上の矩形 (枠込みの外側) を取得する。
	/// @details 別のツール窓をこの窓に吸着・追従させる (ドッキング) ために、画面位置を知るのに使う。
	///          取得できたら true。デフォルトは非対応で false。
	virtual bool getWindowRect(int& /*x*/, int& /*y*/, int& /*w*/, int& /*h*/) const { return false; }

	/// @brief ウィンドウを画面上の (x,y) サイズ (w,h) へ動かす。ドックしたツール窓の追従に使う。
	/// @details フォーカスや z-order は変えない。デフォルトは no-op。
	virtual void moveWindow(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}

	/// @brief クリックしてもキーボードフォーカスを奪わない窓にする (WS_EX_NOACTIVATE 相当)。
	/// @details ドックしたツール窓 (シークバー等) をクリックしてもゲームが操作を保つ。デフォルトは no-op。
	virtual void setNoActivate() {}

	/// @brief 常に最前面に置く (WS_EX_TOPMOST 相当)。ドックしたシークバーが背面に潜らないように。
	/// @details デフォルトは no-op。
	virtual void setTopmost() {}

	/// @brief OS ネイティブのウィンドウハンドル (Win32 は HWND)。無ければ 0。
	/// @details ドックしたツール窓を「この窓の真下」に z-order 挿入するために使う。
	[[nodiscard]] virtual std::uintptr_t nativeHandle() const { return 0; }

	/// @brief 指定ウィンドウ (aboveWindow) の真下に、位置 (x,y) サイズ (w,h) で置く。
	/// @details z-order で aboveWindow の直後に差し込むので、間に他の窓が割り込まない。
	///          aboveWindow が 0 なら z-order を変えずに移動のみ。デフォルトは no-op。
	virtual void dockBelow(std::uintptr_t /*aboveWindow*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
};

} // namespace mitiru
