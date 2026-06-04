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

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

#include <windowsx.h>

#include <mitiru/core/Config.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/InputInjector.hpp>

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
	/// @param displayMode ウィンドウ表示モード
	/// @param resizable ユーザがフレームでリサイズできるか (false の場合は
	///                  WS_THICKFRAME/WS_MAXIMIZEBOX を外して固定サイズ)
	explicit Win32Window(std::string_view title, int width, int height,
		DisplayMode displayMode = DisplayMode::Windowed,
		bool resizable = true)
		: m_width(width)
		, m_height(height)
		, m_displayMode(displayMode)
		, m_resizable(resizable)
	{
		/// Per-Monitor V2 DPI awareness を有効化する
		/// → 125%/150% スケール環境でも物理ピクセル単位で 1:1 描画される
		enableDpiAwareness();

		registerWindowClass();

		/// タイトルをワイド文字に変換
		const int wideLen = MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr, 0);
		std::wstring wideTitle(static_cast<std::size_t>(wideLen), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
			wideTitle.data(), wideLen);

		if (displayMode == DisplayMode::BorderlessFullscreen)
		{
			/// ボーダーレスフルスクリーン: モニタ全域を覆うウィンドウ
			/// ALT+TAB が速く、modern game の標準
			HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO mi{};
			mi.cbSize = sizeof(mi);
			GetMonitorInfoW(monitor, &mi);
			const int x = mi.rcMonitor.left;
			const int y = mi.rcMonitor.top;
			const int w = mi.rcMonitor.right - mi.rcMonitor.left;
			const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

			m_hwnd = CreateWindowExW(
				0, CLASS_NAME, wideTitle.c_str(),
				WS_POPUP | WS_VISIBLE,
				x, y, w, h,
				nullptr, nullptr, GetModuleHandleW(nullptr), this);

			if (!m_hwnd)
			{
				throw std::runtime_error("Win32Window: CreateWindowExW (borderless) failed");
			}
			m_width = w;
			m_height = h;
		}
		else
		{
			/// Windowed: 通常のリサイズ可能ウィンドウ。`resizable=false` の
			/// 時は WS_THICKFRAME / WS_MAXIMIZEBOX を外して固定サイズに。
			const UINT dpi = systemDpi();
			// WS_VISIBLE で生成時から可視にする（Borderless が WS_POPUP|WS_VISIBLE なのと対称。#22）。
			// 以前は不可視生成 + ShowWindow も無く、host 以外の standalone 消費者で窓が出ない罠だった。
			const DWORD style = (m_resizable
				? WS_OVERLAPPEDWINDOW
				: (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX))
				| WS_VISIBLE;
			RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			adjustWindowRectForDpi(&rect, style, FALSE, 0, dpi);

			int windowWidth = rect.right - rect.left;
			int windowHeight = rect.bottom - rect.top;

			/// 作業領域に収まるようクランプ
			RECT workArea{};
			if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0))
			{
				const int workW = workArea.right - workArea.left;
				const int workH = workArea.bottom - workArea.top;
				if (windowWidth > workW || windowHeight > workH)
				{
					const int frameW = windowWidth - width;
					const int frameH = windowHeight - height;
					m_width  = (std::min)(width,  workW - frameW);
					m_height = (std::min)(height, workH - frameH);
					windowWidth  = m_width  + frameW;
					windowHeight = m_height + frameH;
				}
			}

			m_hwnd = CreateWindowExW(
				0, CLASS_NAME, wideTitle.c_str(),
				style,
				CW_USEDEFAULT, CW_USEDEFAULT,
				windowWidth, windowHeight,
				nullptr, nullptr, GetModuleHandleW(nullptr), this);

			if (!m_hwnd)
			{
				throw std::runtime_error("Win32Window: CreateWindowExW failed");
			}

			RECT actualClient{};
			if (GetClientRect(m_hwnd, &actualClient))
			{
				m_width = actualClient.right - actualClient.left;
				m_height = actualClient.bottom - actualClient.top;
			}
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
	///          ゲームループをブロックしない。毎フレーム applyCursorCapture() を呼ぶ。
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
		applyCursorCapture();
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

	/// @brief ランタイムでフルスクリーン/ウィンドウを切り替える
	/// @param enable true=ボーダーレスフルスクリーン, false=ウィンドウ
	void setFullscreen(bool enable)
	{
		const bool already = (m_displayMode == DisplayMode::BorderlessFullscreen);
		if (enable == already) return;

		if (enable)
		{
			// ウィンドウ状態を保存
			m_savedStyle = GetWindowLong(m_hwnd, GWL_STYLE);
			GetWindowRect(m_hwnd, &m_savedRect);

			// プライマリモニター全域に広げる
			HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi{}; mi.cbSize = sizeof(mi);
			GetMonitorInfoW(monitor, &mi);

			SetWindowLong(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
			SetWindowPos(m_hwnd, HWND_TOP,
				mi.rcMonitor.left,  mi.rcMonitor.top,
				mi.rcMonitor.right  - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

			m_width  = mi.rcMonitor.right  - mi.rcMonitor.left;
			m_height = mi.rcMonitor.bottom - mi.rcMonitor.top;
			m_displayMode = DisplayMode::BorderlessFullscreen;
		}
		else
		{
			// 保存されたウィンドウ状態を復元
			SetWindowLong(m_hwnd, GWL_STYLE, m_savedStyle ? m_savedStyle : WS_OVERLAPPEDWINDOW | WS_VISIBLE);
			const RECT r = m_savedRect.right > 0 ? m_savedRect : RECT{100, 100, 1920+100, 1080+100};
			SetWindowPos(m_hwnd, HWND_NOTOPMOST,
				r.left, r.top, r.right - r.left, r.bottom - r.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			ShowWindow(m_hwnd, SW_NORMAL);

			m_width  = r.right  - r.left;
			m_height = r.bottom - r.top;
			m_displayMode = DisplayMode::Windowed;
		}
	}

	/// @brief 現在フルスクリーンかどうか
	[[nodiscard]] bool isFullscreen() const noexcept
	{
		return m_displayMode == DisplayMode::BorderlessFullscreen;
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
	void setInputState(InputState* state) noexcept override
	{
		m_inputState = state;
	}

	/// @brief 入力インジェクターを設定する
	/// @param injector InputInjectorへの非所有ポインタ（Engineが所有）
	/// @details 設定後はキー/マウスイベントをInputState直接mutateではなく
	///          injector::inject() 経由で発行する。nullptr でフォールバックに戻る。
	void setInputInjector(InputInjector* injector) noexcept override
	{
		m_inputInjector = injector;
	}

	/// @brief DEBUG: InputStateポインタを取得する
	[[nodiscard]] const InputState* getInputStatePtr() const noexcept { return m_inputState; }

	/// @brief リサイズコールバックの型
	using ResizeCallback = std::function<void(int, int)>;

	/// @brief ウィンドウリサイズ時のコールバックを設定する
	/// @param cb 新しいwidth, heightを受け取るコールバック
	void setResizeCallback(std::function<void(int, int)> cb) noexcept override
	{
		m_resizeCallback = std::move(cb);
	}

	/// @brief リサイズ時の最小クライアントサイズを設定する (px、0=制限なし)
	/// @details WM_GETMINMAXINFO で client→window サイズへ変換して強制する。
	void setMinClientSize(int w, int h) noexcept override
	{
		m_minClientW = w;
		m_minClientH = h;
	}

	/// @brief Win32 modal resize loop 中も engine を tick させるための callback
	/// @details ユーザが window 枠を drag すると Windows は `DefWindowProc` 内で
	///          modal loop に入り、main thread を block する → engine main loop
	///          (`tickOneFrame`) が止まり描画/計算/CEF pump も止まる。
	///          WM_ENTERSIZEMOVE で SetTimer し WM_TIMER で本 callback を呼ぶ
	///          ことで、drag 中も ~60fps で engine が回り続ける。Direct3D SDK
	///          sample の standard pattern。
	void setTickCallback(std::function<void()> cb) noexcept
	{
		m_tickCallback = std::move(cb);
	}

	/// @brief 現在 modal resize loop (枠 drag) 中か
	/// @details Engine::onWindowResize がこれを参照して、drag 中は
	///          logical / CEF re-layout を抑止し backbuffer のみ追従させる。
	///          release (WM_EXITSIZEMOVE) で onModalResizeEnd が呼ばれた時に
	///          初めて本格 resize する。
	[[nodiscard]] bool inModalLoop() const noexcept { return m_inModalLoop; }

	/// @brief WM_EXITSIZEMOVE で 1 回だけ呼ばれる callback
	/// @details drag 完了後の最終 size で full resize を実施するために engine
	///          が登録する。
	void setModalResizeEndCallback(std::function<void()> cb) noexcept
	{
		m_modalResizeEndCallback = std::move(cb);
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

		case WM_GETMINMAXINFO:
		{
			/// リサイズの最小サイズを強制する (config.minWindowWidth/Height 由来)。
			/// client px 指定なので frame 込みの window px へ変換して ptMinTrackSize に。
			if (m_minClientW > 0 || m_minClientH > 0)
			{
				const DWORD style =
					static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
				const DWORD exStyle =
					static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
				RECT r = {0, 0, m_minClientW, m_minClientH};
				adjustWindowRectForDpi(&r, style, FALSE, exStyle, systemDpi());
				auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
				if (m_minClientW > 0) { mmi->ptMinTrackSize.x = r.right - r.left; }
				if (m_minClientH > 0) { mmi->ptMinTrackSize.y = r.bottom - r.top; }
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		case WM_SIZE:
		{
			/// クライアント領域サイズの更新
			RECT clientRect = {};
			GetClientRect(hwnd, &clientRect);
			m_width = clientRect.right - clientRect.left;
			m_height = clientRect.bottom - clientRect.top;

			/// リサイズコールバックの呼び出し（最小化時は除外）
			if (m_width > 0 && m_height > 0 && m_resizeCallback)
			{
				m_resizeCallback(m_width, m_height);
			}
			return 0;
		}

		/// --- Modal resize loop ティック維持 ---
		/// drag 中も engine main loop を回し続けるための timer-driven tick。
		/// 詳細は setTickCallback の comment 参照。
		case WM_ENTERSIZEMOVE:
			m_inModalLoop = true;
			if (m_tickCallback)
			{
				/// USER_TIMER_MINIMUM (10ms) より遅めの 8ms 指定だと
				/// 内部で 10ms にクランプされる。~60fps target で 16ms。
				SetTimer(hwnd, kModalTickTimerId, 16, nullptr);
			}
			return 0;

		case WM_EXITSIZEMOVE:
			KillTimer(hwnd, kModalTickTimerId);
			m_inModalLoop = false;
			/// modal 中に deferred されていた full resize (logical / CEF) を発火
			if (m_modalResizeEndCallback) { m_modalResizeEndCallback(); }
			/// 反映後 1 frame 引いて即座に画面更新
			if (m_tickCallback) { m_tickCallback(); }
			return 0;

		case WM_TIMER:
			if (wParam == kModalTickTimerId && m_tickCallback)
			{
				m_tickCallback();
			}
			return 0;

		/// --- キーボード入力 ---
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if (m_inputInjector)
			{
				m_inputInjector->inject(InputCommand{InputCommandType::KeyDown, mapVirtualKey(wParam)});
			}
			else if (m_inputState)
			{
				m_inputState->setKeyDown(mapVirtualKey(wParam), true);
			}
			return 0;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			if (m_inputInjector)
			{
				m_inputInjector->inject(InputCommand{InputCommandType::KeyUp, mapVirtualKey(wParam)});
			}
			else if (m_inputState)
			{
				m_inputState->setKeyDown(mapVirtualKey(wParam), false);
			}
			return 0;

		/// --- focus 喪失 --------------------------------------------------
		/// ユーザが alt-tab で離れた (または dev companion のような別 window を
		/// クリックした) 時、Windows はこの hwnd へ WM_KEYUP を配送しなくなる。
		/// その時点で押されていた key は InputState 内で永久に "down" のまま残る
		/// — 典型的な "矢印キー stuck" bug。ここでクリアし、game に正しい
		/// release edge が届くようにする。
		case WM_KILLFOCUS:
			if (m_inputState) { m_inputState->clearHeldKeys(); }
			return 0;

		/// --- マウス移動 ---
		case WM_MOUSEMOVE:
		{
			/// カーソルスナップバック後の WM_MOUSEMOVE は無視する。
			/// SetCursorPos がウィンドウに送る合成イベントをデルタ二重計上から守る。
			if (m_ignoreNextMouseMove)
			{
				m_ignoreNextMouseMove = false;
				return 0;
			}
			const float mx = static_cast<float>(GET_X_LPARAM(lParam));
			const float my = static_cast<float>(GET_Y_LPARAM(lParam));
			m_lastMouseX = mx;
			m_lastMouseY = my;
			++m_mouseMoveCount;
			if (m_inputInjector)
			{
				m_inputInjector->inject(InputCommand{InputCommandType::MouseMove, 0, 0, mx, my});
			}
			else if (m_inputState)
			{
				m_inputState->setMousePosition(mx, my);
				// DEBUG: 書き込み直後に読み返す
				auto [rx, ry] = m_inputState->mousePosition();
				m_dbgReadbackX = rx;
				m_dbgReadbackY = ry;
			}
			return 0;
		}

		/// --- マウスボタン ---
		case WM_LBUTTONDOWN:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseDown, 0, static_cast<int>(MouseButton::Left)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Left, true);
			return 0;
		case WM_LBUTTONUP:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseUp, 0, static_cast<int>(MouseButton::Left)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Left, false);
			return 0;
		case WM_RBUTTONDOWN:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseDown, 0, static_cast<int>(MouseButton::Right)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Right, true);
			return 0;
		case WM_RBUTTONUP:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseUp, 0, static_cast<int>(MouseButton::Right)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Right, false);
			return 0;
		case WM_MBUTTONDOWN:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseDown, 0, static_cast<int>(MouseButton::Middle)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Middle, true);
			return 0;
		case WM_MBUTTONUP:
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{InputCommandType::MouseUp, 0, static_cast<int>(MouseButton::Middle)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(MouseButton::Middle, false);
			return 0;

		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	/// @brief Per-Monitor V2 DPI awareness を有効化する
	/// @details Windows 10 1703+ で SetProcessDpiAwarenessContext、
	///          それ以前の Win10 で SetProcessDpiAwareness、
	///          さらに古い環境で SetProcessDPIAware を使う。
	///          user32.dll/shcore.dll を動的バインドして古い Windows でも build できる。
	static void enableDpiAwareness() noexcept
	{
		using SetCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
		if (auto* user32 = GetModuleHandleW(L"user32.dll"))
		{
			auto setCtx = reinterpret_cast<SetCtxFn>(
				GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
			if (setCtx
				&& setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
			{
				return;
			}
		}
		/// 古い Windows 用 fallback
		SetProcessDPIAware();
	}

public:
	/// @brief プライマリモニタの DPI を取得する (96 = 100%)
	/// @details Engine_Init_Lifecycle.hpp 等で useLogicalWindowSize スケーリングを
	///          計算するため public。Win32 API を直接使わず本メソッド経由で
	///          取得すれば古い Windows (GetDpiForSystem 未提供) でも安全。
	[[nodiscard]] static UINT systemDpi() noexcept
	{
		using GetDpiFn = UINT (WINAPI*)(HWND);
		if (auto* user32 = GetModuleHandleW(L"user32.dll"))
		{
			auto getDpi = reinterpret_cast<GetDpiFn>(
				GetProcAddress(user32, "GetDpiForSystem"));
			if (getDpi)
			{
				return getDpi(nullptr);
			}
		}
		return 96;
	}

private:

	/// @brief DPI 対応版 AdjustWindowRectEx (古い Win では fallback)
	static void adjustWindowRectForDpi(
		LPRECT rect, DWORD style, BOOL menu, DWORD exStyle, UINT dpi) noexcept
	{
		using AdjFn = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
		if (auto* user32 = GetModuleHandleW(L"user32.dll"))
		{
			auto adj = reinterpret_cast<AdjFn>(
				GetProcAddress(user32, "AdjustWindowRectExForDpi"));
			if (adj)
			{
				adj(rect, style, menu, exStyle, dpi);
				return;
			}
		}
		AdjustWindowRectEx(rect, style, menu, exStyle);
	}

	/// @brief カーソルキャプチャの適用・解除を毎フレーム行う
	/// @details pollEvents() から呼ばれる。InputState::isCursorCaptured() を参照し
	///          キャプチャ状態の遷移を検出してOSコールを発行する。
	///
	///          遷移パターン:
	///          - false → true  : ShowCursor(FALSE) + SetCapture + ClipCursor + スナップ
	///          - true (維持中) : センター差分 → setRawMouseDelta → SetCursorPos でスナップ
	///          - true → false  : ClipCursor(nullptr) + ReleaseCapture + ShowCursor(TRUE)
	void applyCursorCapture()
	{
		if (!m_hwnd || !m_inputState)
		{
			return;
		}

		const bool wantCapture = m_inputState->isCursorCaptured();

		if (!m_captureActive && wantCapture)
		{
			/// キャプチャ開始
			ShowCursor(FALSE);
			SetCapture(m_hwnd);

			RECT clientRect{};
			GetClientRect(m_hwnd, &clientRect);
			POINT topLeft{ clientRect.left, clientRect.top };
			POINT bottomRight{ clientRect.right, clientRect.bottom };
			ClientToScreen(m_hwnd, &topLeft);
			ClientToScreen(m_hwnd, &bottomRight);
			const RECT screenRect{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
			ClipCursor(&screenRect);

			m_captureCenterX = (topLeft.x + bottomRight.x) / 2;
			m_captureCenterY = (topLeft.y + bottomRight.y) / 2;
			SetCursorPos(m_captureCenterX, m_captureCenterY);
			m_ignoreNextMouseMove = true;

			m_captureActive = true;
		}
		else if (m_captureActive && wantCapture)
		{
			/// キャプチャ維持中: 現在カーソル位置とセンターの差をデルタとして蓄積し、
			/// スナップバックする。
			POINT cursorPos{};
			GetCursorPos(&cursorPos);

			const float dx = static_cast<float>(cursorPos.x - m_captureCenterX);
			const float dy = static_cast<float>(cursorPos.y - m_captureCenterY);

			if (dx != 0.0f || dy != 0.0f)
			{
				m_inputState->setRawMouseDelta(dx, dy);
				SetCursorPos(m_captureCenterX, m_captureCenterY);
				m_ignoreNextMouseMove = true;
			}
		}
		else if (m_captureActive && !wantCapture)
		{
			/// キャプチャ解除
			ClipCursor(nullptr);
			ReleaseCapture();
			ShowCursor(TRUE);
			m_captureActive = false;
		}
	}

	HWND m_hwnd = nullptr;            ///< ウィンドウハンドル
	int m_width = 0;                  ///< クライアント領域の幅
	int m_height = 0;                 ///< クライアント領域の高さ
	DisplayMode m_displayMode = DisplayMode::Windowed;
	bool m_resizable = true;          ///< ユーザがフレームでリサイズできるか
	int m_minClientW = 0;             ///< 最小クライアント幅 (px、0=制限なし)
	int m_minClientH = 0;             ///< 最小クライアント高さ (px、0=制限なし)
	LONG m_savedStyle = 0;            ///< フルスクリーン前のウィンドウスタイル
	RECT m_savedRect  = {};           ///< フルスクリーン前のウィンドウ矩形

	/// カーソルキャプチャ管理
	bool  m_captureActive       = false; ///< 前フレームのキャプチャ状態（遷移検出用）
	int   m_captureCenterX      = 0;     ///< スナップバック先X（スクリーン座標）
	int   m_captureCenterY      = 0;     ///< スナップバック先Y（スクリーン座標）
	bool  m_ignoreNextMouseMove = false; ///< SetCursorPos 後の合成 WM_MOUSEMOVE を読み飛ばす

public:
	float m_lastMouseX = -1;          ///< DEBUG: 最後のWM_MOUSEMOVEのX
	float m_lastMouseY = -1;          ///< DEBUG: 最後のWM_MOUSEMOVEのY
	float m_dbgReadbackX = -1;        ///< DEBUG: setMousePosition直後のreadback
	float m_dbgReadbackY = -1;        ///< DEBUG: setMousePosition直後のreadback
	int m_mouseMoveCount = 0;         ///< DEBUG: WM_MOUSEMOVE受信回数
private:
	bool m_shouldClose = false;            ///< 閉じ要求フラグ
	InputState* m_inputState = nullptr;   ///< 入力状態転送先（非所有）
	InputInjector* m_inputInjector = nullptr; ///< 入力インジェクター（非所有）。非nullの場合はinject()経由でイベント発行
	ResizeCallback m_resizeCallback;      ///< リサイズコールバック
	std::function<void()> m_tickCallback;  ///< modal-loop tick (drag-resize 中の engine 駆動)
	std::function<void()> m_modalResizeEndCallback; ///< WM_EXITSIZEMOVE で 1 回呼ぶ deferred full resize
	bool m_inModalLoop = false;            ///< WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE
	static constexpr UINT_PTR kModalTickTimerId = 0x4D54; // 'MT'
};

} // namespace mitiru

#endif // _WIN32
