#pragma once

/// @file Win32Window.hpp
/// @brief Win32ウィンドウ実装
/// @details Windows APIを使用した実ウィンドウの作成・管理を行う。
///          PeekMessageWによるノンブロッキングメッセージループを提供する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

#include <windowsx.h>

#include <mitiru/platform/IWindow.hpp>
#include <mitiru/input/InputState.hpp>

namespace mitiru
{

/// @brief Win32ウィンドウ実装
/// @details HWNDをラップし、Win32メッセージキューの処理を行う。
///          DX11スワップチェーン生成用にHWNDハンドルを公開する。
class Win32Window final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	explicit Win32Window(std::string_view title, int width, int height)
		: m_width(width)
		, m_height(height)
	{
		registerWindowClass();

		/// クライアント領域サイズからウィンドウサイズを計算
		RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
		AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

		const int windowWidth = rect.right - rect.left;
		const int windowHeight = rect.bottom - rect.top;

		/// タイトルをワイド文字に変換
		const int wideLen = MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr, 0);
		std::wstring wideTitle(static_cast<std::size_t>(wideLen), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
			wideTitle.data(), wideLen);

		/// ウィンドウ生成
		m_hwnd = CreateWindowExW(
			0,
			CLASS_NAME,
			wideTitle.c_str(),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			windowWidth, windowHeight,
			nullptr,
			nullptr,
			GetModuleHandleW(nullptr),
			this
		);

		if (!m_hwnd)
		{
			throw std::runtime_error("Win32Window: CreateWindowExW failed");
		}
	}

	/// @brief デストラクタ
	~Win32Window() override
	{
		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
	}

	/// コピー禁止
	Win32Window(const Win32Window&) = delete;
	Win32Window& operator=(const Win32Window&) = delete;

	/// ムーブ禁止（HWNDのユーザーデータがthisを指すため）
	Win32Window(Win32Window&&) = delete;
	Win32Window& operator=(Win32Window&&) = delete;

	/// @brief ウィンドウが閉じられるべきかどうか
	/// @return WM_CLOSE/WM_DESTROYを受信済みなら true
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief Win32メッセージキューをポーリングする
	/// @details PeekMessageWを使用したノンブロッキング処理。
	///          ゲームループをブロックしない。
	void pollEvents() override
	{
		MSG msg = {};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				m_shouldClose = true;
				return;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	/// @brief クライアント領域の幅を取得する
	[[nodiscard]] int width() const override
	{
		return m_width;
	}

	/// @brief クライアント領域の高さを取得する
	[[nodiscard]] int height() const override
	{
		return m_height;
	}

	/// @brief ウィンドウタイトルを設定する
	/// @param title 新しいタイトル文字列（UTF-8）
	void setTitle(std::string_view title) override
	{
		const int wideLen = MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr, 0);
		std::wstring wideTitle(static_cast<std::size_t>(wideLen), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
			wideTitle.data(), wideLen);
		SetWindowTextW(m_hwnd, wideTitle.c_str());
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

	/// @brief ウィンドウを表示する
	void show()
	{
		ShowWindow(m_hwnd, SW_SHOW);
		UpdateWindow(m_hwnd);
	}

	/// @brief ウィンドウを非表示にする
	void hide()
	{
		ShowWindow(m_hwnd, SW_HIDE);
	}

	/// @brief ネイティブウィンドウハンドルを取得する
	/// @return HWND（DX11スワップチェーン生成に使用）
	[[nodiscard]] HWND getHandle() const noexcept
	{
		return m_hwnd;
	}

	/// @brief 入力状態の転送先を設定する
	/// @param state InputStateへの非所有ポインタ（Engineが所有）
	void setInputState(InputState* state) noexcept
	{
		m_inputState = state;
	}

private:
	/// @brief Win32仮想キーコードをmitiru内部キーコードに変換する
	/// @param vk Win32仮想キーコード
	/// @return mitiruキーコード整数値（KeyCodeのenum値と一致）
	/// @details KeyCodeはWin32 VKコードに準拠しているため、
	///          0〜255の範囲内ならそのまま返す。
	[[nodiscard]] static int mapVirtualKey(WPARAM vk) noexcept
	{
		const auto code = static_cast<int>(vk);
		if (code >= 0 && code < InputState::MAX_KEYS)
		{
			return code;
		}
		return 0;
	}

	/// @brief ウィンドウクラス名
	static constexpr const wchar_t* CLASS_NAME = L"MitiruWindowClass";

	/// @brief ウィンドウクラスを登録する（一度だけ）
	static void registerWindowClass()
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}

		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(WNDCLASSEXW);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = windowProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
		wc.lpszClassName = CLASS_NAME;

		if (!RegisterClassExW(&wc))
		{
			throw std::runtime_error("Win32Window: RegisterClassExW failed");
		}

		registered = true;
	}

	/// @brief Win32ウィンドウプロシージャ
	/// @param hwnd ウィンドウハンドル
	/// @param msg メッセージ
	/// @param wParam WPARAM
	/// @param lParam LPARAM
	/// @return メッセージ処理結果
	static LRESULT CALLBACK windowProc(
		HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		Win32Window* self = nullptr;

		if (msg == WM_NCCREATE)
		{
			/// ウィンドウ生成時にthisポインタを保存
			auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = static_cast<Win32Window*>(createStruct->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<Win32Window*>(
				GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (self)
		{
			return self->handleMessage(hwnd, msg, wParam, lParam);
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	/// @brief インスタンスメッセージハンドラ
	/// @param hwnd ウィンドウハンドル
	/// @param msg メッセージ
	/// @param wParam WPARAM
	/// @param lParam LPARAM
	/// @return メッセージ処理結果
	LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CLOSE:
			m_shouldClose = true;
			return 0;

		case WM_DESTROY:
			m_shouldClose = true;
			PostQuitMessage(0);
			return 0;

		case WM_SIZE:
		{
			/// クライアント領域サイズの更新
			RECT clientRect = {};
			GetClientRect(hwnd, &clientRect);
			m_width = clientRect.right - clientRect.left;
			m_height = clientRect.bottom - clientRect.top;
			return 0;
		}

		/// --- キーボード入力 ---
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if (m_inputState)
			{
				m_inputState->setKeyDown(mapVirtualKey(wParam), true);
			}
			return 0;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			if (m_inputState)
			{
				m_inputState->setKeyDown(mapVirtualKey(wParam), false);
			}
			return 0;

		/// --- マウス移動 ---
		case WM_MOUSEMOVE:
			if (m_inputState)
			{
				m_inputState->setMousePosition(
					static_cast<float>(GET_X_LPARAM(lParam)),
					static_cast<float>(GET_Y_LPARAM(lParam)));
			}
			return 0;

		/// --- マウスボタン ---
		case WM_LBUTTONDOWN:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Left, true);
			}
			return 0;
		case WM_LBUTTONUP:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Left, false);
			}
			return 0;
		case WM_RBUTTONDOWN:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Right, true);
			}
			return 0;
		case WM_RBUTTONUP:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Right, false);
			}
			return 0;
		case WM_MBUTTONDOWN:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Middle, true);
			}
			return 0;
		case WM_MBUTTONUP:
			if (m_inputState)
			{
				m_inputState->setMouseButtonDown(MouseButton::Middle, false);
			}
			return 0;

		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	HWND m_hwnd = nullptr;            ///< ウィンドウハンドル
	int m_width = 0;                  ///< クライアント領域の幅
	int m_height = 0;                 ///< クライアント領域の高さ
	bool m_shouldClose = false;       ///< 閉じ要求フラグ
	InputState* m_inputState = nullptr;  ///< 入力状態転送先（非所有）
};

} // namespace mitiru

#endif // _WIN32
