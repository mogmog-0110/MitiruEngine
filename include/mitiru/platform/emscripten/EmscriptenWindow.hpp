#pragma once

/// @file EmscriptenWindow.hpp
/// @brief Emscripten/WASM用ウィンドウ実装
/// @details HTMLキャンバスをウィンドウとして扱うIWindow実装。
///          Emscripten環境でのみコンパイルされる。

#ifdef __EMSCRIPTEN__

#include <string>
#include <string_view>

#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief Emscripten環境用のウィンドウ実装
/// @details HTMLキャンバス要素をウィンドウとして抽象化する。
///          emscripten_set_canvas_element_size でサイズを管理し、
///          イベントポーリングはEmscriptenのコールバック機構に委譲する。
///
/// @code
/// EmscriptenWindow window("#canvas", 1280, 720);
/// while (!window.shouldClose())
/// {
///     window.pollEvents();
///     // 描画処理...
/// }
/// @endcode
class EmscriptenWindow final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param canvasSelector キャンバス要素のCSSセレクタ（例: "#canvas"）
	/// @param width 初期幅（ピクセル）
	/// @param height 初期高さ（ピクセル）
	explicit EmscriptenWindow(
		std::string_view canvasSelector, int width, int height)
		: m_canvasSelector(canvasSelector)
		, m_width(width)
		, m_height(height)
	{
		/// @note 実際のEmscripten APIコール:
		/// emscripten_set_canvas_element_size(
		///     m_canvasSelector.c_str(), m_width, m_height);
		/// スタブ実装のため、サイズ値の保持のみ行う
	}

	/// @brief ウィンドウが閉じるべきかどうか
	/// @return requestClose()が呼ばれていればtrue
	/// @note ブラウザ環境ではページ遷移やタブクローズで閉じ要求が発生する
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief イベントをポーリングする（Emscriptenではノーオペレーション）
	/// @details Emscriptenはコールバック駆動のイベントモデルを使用するため、
	///          明示的なポーリングは不要。
	void pollEvents() override
	{
		/// Emscriptenのイベントループがコールバック経由でイベントを配信する
	}

	/// @brief ウィンドウ幅を取得する
	/// @return キャンバスの幅（ピクセル）
	[[nodiscard]] int width() const override
	{
		return m_width;
	}

	/// @brief ウィンドウ高さを取得する
	/// @return キャンバスの高さ（ピクセル）
	[[nodiscard]] int height() const override
	{
		return m_height;
	}

	/// @brief ウィンドウタイトルを設定する
	/// @param title 新しいタイトル文字列
	/// @note ブラウザ環境ではdocument.titleに反映される想定
	void setTitle(std::string_view title) override
	{
		m_title = title;
		/// @note 実際の実装:
		/// emscripten_run_script(
		///     ("document.title='" + m_title + "'").c_str());
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

	/// @brief キャンバスセレクタを取得する
	/// @return CSSセレクタ文字列
	[[nodiscard]] const std::string& canvasSelector() const noexcept
	{
		return m_canvasSelector;
	}

private:
	std::string m_canvasSelector; ///< キャンバス要素のCSSセレクタ
	std::string m_title;          ///< ウィンドウタイトル
	int m_width;                  ///< キャンバス幅
	int m_height;                 ///< キャンバス高さ
	bool m_shouldClose = false;   ///< 閉じ要求フラグ
};

} // namespace mitiru

#endif // __EMSCRIPTEN__
