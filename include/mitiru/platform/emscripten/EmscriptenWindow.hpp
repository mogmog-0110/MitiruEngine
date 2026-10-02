#pragma once

/// @file EmscriptenWindow.hpp
/// @brief Emscripten/WASM 用ウィンドウ実装
/// @details HTML キャンバスをウィンドウとして扱う IWindow 実装。
///          Emscripten 環境でのみコンパイルされる。
///          キーボード・マウス・ホイールのコールバックを登録し、
///          InputState へ入力イベントを反映する。

#ifdef __EMSCRIPTEN__

#include <string>
#include <string_view>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <mitiru/platform/IWindow.hpp>
#include <mitiru/input/InputState.hpp>

namespace mitiru
{

/// @brief Emscripten 環境用のウィンドウ実装
/// @details HTML キャンバス要素をウィンドウとして抽象化する。
///          emscripten_set_canvas_element_size でサイズを管理し、
///          Emscripten のコールバック機構でキーボード・マウス入力を処理する。
///
/// @code
/// EmscriptenWindow window("#canvas", 1280, 720);
/// InputState input;
/// window.setInputState(&input);
/// while (!window.shouldClose())
/// {
///     input.beginFrame();
///     window.pollEvents();
///     // 描画処理...
/// }
/// @endcode
class EmscriptenWindow final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param canvasSelector キャンバス要素の CSS セレクタ（例: "#canvas"）
	/// @param width 初期幅（ピクセル）
	/// @param height 初期高さ（ピクセル）
	explicit EmscriptenWindow(
		std::string_view canvasSelector, int width, int height)
		: m_canvasSelector(canvasSelector)
		, m_width(width)
		, m_height(height)
	{
		emscripten_set_canvas_element_size(
			m_canvasSelector.c_str(), m_width, m_height);
	}

	/// @brief ウィンドウが閉じるべきかどうか
	/// @return requestClose()が呼ばれていれば true
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief イベントをポーリングする（Emscripten ではノーオペレーション）
	/// @details Emscripten はコールバック駆動のイベントモデルを使用するため、
	///          明示的なポーリングは不要。
	void pollEvents() override
	{
		// Emscripten のイベントループがコールバック経由でイベントを配信する
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
	/// @note ブラウザ環境では document.title に反映される
	void setTitle(std::string_view title) override
	{
		m_title = title;
		EM_ASM({
			document.title = UTF8ToString($0);
		}, m_title.c_str());
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

	/// @brief キャンバスセレクタを取得する
	/// @return CSS セレクタ文字列
	[[nodiscard]] const std::string& canvasSelector() const noexcept
	{
		return m_canvasSelector;
	}

	/// @brief InputState を設定し、入力コールバックを登録する
	/// @param input InputState へのポインタ（nullptr で解除）
	void setInputState(InputState* input) noexcept override
	{
		m_inputState = input;

		if (m_inputState != nullptr)
		{
			initCallbacks();
		}
	}

	/// @brief ホイールデルタを取得する
	/// @return 前回取得後に蓄積された垂直スクロールデルタ
	[[nodiscard]] float consumeWheelDelta() noexcept
	{
		const float delta = m_wheelDelta;
		m_wheelDelta = 0.0f;
		return delta;
	}

private:
	/// @brief DOM キーコードをエンジン内部キーコードに変換する
	/// @param emKey EmscriptenKeyboardEvent の keyCode
	/// @return エンジン内部キーコード（0-255）、変換不能なら-1
	[[nodiscard]] static int mapKeyCode(unsigned long emKey) noexcept
	{
		// ASCII 文字キー（A-Z, 0-9）はそのまま使える
		// DOM keyCode は大文字 ASCII と一致する
		if (emKey >= 'A' && emKey <= 'Z')
		{
			return static_cast<int>(emKey);
		}
		if (emKey >= '0' && emKey <= '9')
		{
			return static_cast<int>(emKey);
		}

		// 特殊キー
		switch (emKey)
		{
		case 32:  return 32;   // Space
		case 13:  return 13;   // Enter
		case 27:  return 27;   // Escape
		case 8:   return 8;    // Backspace
		case 9:   return 9;    // Tab
		case 37:  return 37;   // Arrow Left
		case 38:  return 38;   // Arrow Up
		case 39:  return 39;   // Arrow Right
		case 40:  return 40;   // Arrow Down
		case 16:  return 16;   // Shift
		case 17:  return 17;   // Ctrl
		case 18:  return 18;   // Alt
		case 46:  return 46;   // Delete
		case 45:  return 45;   // Insert
		case 36:  return 36;   // Home
		case 35:  return 35;   // End
		case 33:  return 33;   // Page Up
		case 34:  return 34;   // Page Down
		case 112: return 112;  // F1
		case 113: return 113;  // F2
		case 114: return 114;  // F3
		case 115: return 115;  // F4
		case 116: return 116;  // F5
		case 117: return 117;  // F6
		case 118: return 118;  // F7
		case 119: return 119;  // F8
		case 120: return 120;  // F9
		case 121: return 121;  // F10
		case 122: return 122;  // F11
		case 123: return 123;  // F12
		default:  break;
		}

		// 範囲内であればそのまま渡す
		if (emKey < 256)
		{
			return static_cast<int>(emKey);
		}

		return -1;
	}

	/// @brief DOM マウスボタン番号を MouseButton に変換する
	/// @param domButton DOM の button 値（0=左, 1=中, 2=右）
	/// @return MouseButton 列挙値
	/// @note DOM では中=1,右=2 だがエンジンでは Right=1,Middle=2
	[[nodiscard]] static MouseButton mapMouseButton(unsigned short domButton) noexcept
	{
		switch (domButton)
		{
		case 0:  return MouseButton::Left;
		case 1:  return MouseButton::Middle;
		case 2:  return MouseButton::Right;
		default: return MouseButton::Left;
		}
	}

	/// @brief キーダウンコールバック
	static EM_BOOL onKeyDown(
		int eventType, const EmscriptenKeyboardEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		const int key = mapKeyCode(event->keyCode);
		if (key >= 0)
		{
			self->m_inputState->setKeyDown(key, true);
		}

		return EM_TRUE;
	}

	/// @brief キーアップコールバック
	static EM_BOOL onKeyUp(
		int eventType, const EmscriptenKeyboardEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		const int key = mapKeyCode(event->keyCode);
		if (key >= 0)
		{
			self->m_inputState->setKeyDown(key, false);
		}

		return EM_TRUE;
	}

	/// @brief マウスダウンコールバック
	static EM_BOOL onMouseDown(
		int eventType, const EmscriptenMouseEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		self->m_inputState->setMouseButtonDown(
			mapMouseButton(event->button), true);

		return EM_TRUE;
	}

	/// @brief マウスアップコールバック
	static EM_BOOL onMouseUp(
		int eventType, const EmscriptenMouseEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		self->m_inputState->setMouseButtonDown(
			mapMouseButton(event->button), false);

		return EM_TRUE;
	}

	/// @brief マウス移動コールバック
	static EM_BOOL onMouseMove(
		int eventType, const EmscriptenMouseEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		// CSS 座標 → canvas 論理座標に変換
		// targetX/Y は canvas 要素の CSS 座標（引き伸ばし後）
		// canvas 描画解像度(m_width x m_height)に正規化する
		int cssW = 0, cssH = 0;
		emscripten_get_canvas_element_size(self->m_canvasSelector.c_str(), &cssW, &cssH);
		double cssPixelW = 0, cssPixelH = 0;
		emscripten_get_element_css_size(self->m_canvasSelector.c_str(), &cssPixelW, &cssPixelH);

		float mx = static_cast<float>(event->targetX);
		float my = static_cast<float>(event->targetY);

		if (cssPixelW > 0 && cssPixelH > 0)
		{
			mx = mx * static_cast<float>(cssW) / static_cast<float>(cssPixelW);
			my = my * static_cast<float>(cssH) / static_cast<float>(cssPixelH);
		}

		self->m_inputState->setMousePosition(mx, my);

		return EM_TRUE;
	}

	/// @brief ホイールコールバック
	static EM_BOOL onWheel(
		int eventType, const EmscriptenWheelEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		// deltaY を蓄積（DOM_DELTA_PIXEL モードで正規化）
		double delta = event->deltaY;
		if (event->deltaMode == DOM_DELTA_LINE)
		{
			delta *= 40.0;
		}
		else if (event->deltaMode == DOM_DELTA_PAGE)
		{
			delta *= 800.0;
		}
		self->m_wheelDelta += static_cast<float>(-delta / 120.0);

		return EM_TRUE;
	}

	/// @brief タッチ開始コールバック（最初のタッチをマウス左ボタン押下にマッピング）
	static EM_BOOL onTouchStart(
		int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr || event->numTouches < 1)
		{
			return EM_FALSE;
		}

		const auto& touch = event->touches[0];
		self->m_inputState->setMousePosition(
			static_cast<float>(touch.targetX),
			static_cast<float>(touch.targetY));
		self->m_inputState->setMouseButtonDown(MouseButton::Left, true);

		return EM_TRUE;
	}

	/// @brief タッチ移動コールバック（最初のタッチをマウス移動にマッピング）
	static EM_BOOL onTouchMove(
		int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr || event->numTouches < 1)
		{
			return EM_FALSE;
		}

		const auto& touch = event->touches[0];
		self->m_inputState->setMousePosition(
			static_cast<float>(touch.targetX),
			static_cast<float>(touch.targetY));

		return EM_TRUE;
	}

	/// @brief タッチ終了コールバック（マウス左ボタン解放にマッピング）
	static EM_BOOL onTouchEnd(
		int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		self->m_inputState->setMouseButtonDown(MouseButton::Left, false);

		return EM_TRUE;
	}

	/// @brief タッチキャンセルコールバック（マウス左ボタン解放にマッピング）
	static EM_BOOL onTouchCancel(
		int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		static_cast<void>(eventType);
		auto* self = static_cast<EmscriptenWindow*>(userData);

		if (self->m_inputState == nullptr)
		{
			return EM_FALSE;
		}

		self->m_inputState->setMouseButtonDown(MouseButton::Left, false);

		return EM_TRUE;
	}

	/// @brief Emscripten コールバックを登録する
	/// @details setInputState()から呼ばれる。キャンバスセレクタに対して
	///          キーボード・マウス・ホイールイベントのコールバックを設定する。
	void initCallbacks() noexcept
	{
		const char* target = m_canvasSelector.c_str();

		// キーボードは document 全体で受け取る（キャンバスにフォーカスがなくても動作）
		emscripten_set_keydown_callback(
			EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, EM_TRUE, onKeyDown);

		emscripten_set_keyup_callback(
			EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, EM_TRUE, onKeyUp);

		// マウスはキャンバス要素に対して登録
		emscripten_set_mousedown_callback(
			target, this, EM_TRUE, onMouseDown);

		emscripten_set_mouseup_callback(
			target, this, EM_TRUE, onMouseUp);

		emscripten_set_mousemove_callback(
			target, this, EM_TRUE, onMouseMove);

		emscripten_set_wheel_callback(
			target, this, EM_TRUE, onWheel);

		// タッチイベント（マウスエミュレーション）
		emscripten_set_touchstart_callback(
			target, this, EM_TRUE, onTouchStart);

		emscripten_set_touchmove_callback(
			target, this, EM_TRUE, onTouchMove);

		emscripten_set_touchend_callback(
			target, this, EM_TRUE, onTouchEnd);

		emscripten_set_touchcancel_callback(
			target, this, EM_TRUE, onTouchCancel);
	}

	std::string m_canvasSelector;     ///< キャンバス要素のCSSセレクタ
	std::string m_title;              ///< ウィンドウタイトル
	int m_width;                      ///< キャンバス幅
	int m_height;                     ///< キャンバス高さ
	bool m_shouldClose = false;       ///< 閉じ要求フラグ
	InputState* m_inputState = nullptr; ///< 入力状態（非所有ポインタ）
	float m_wheelDelta = 0.0f;        ///< 蓄積されたホイールデルタ
};

} // namespace mitiru

#endif // __EMSCRIPTEN__
