#pragma once

/// @file Win32Window.hpp
/// @brief Win32 ウィンドウ実装
/// @details Windows API を使用した実ウィンドウの作成・管理を行う。
///          PeekMessageW によるノンブロッキングメッセージループを提供する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

#include <windowsx.h>

#include <mitiru/core/Env.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/platform/win32/ImeComposition.hpp>
#include <mitiru/platform/win32/ImeVirtualKey.hpp>
#include <mitiru/platform/win32/Win32KeyMessage.hpp>

namespace mitiru
{

/// @brief Win32 ウィンドウ実装
/// @details HWND をラップし、Win32 メッセージキューの処理を行う。
///          DX11 スワップチェーン生成用に HWND ハンドルを公開する。
class Win32Window final : public IWindow
{
public:
	/// @brief このプロセスが作る窓を「画面に出さない・フォーカスを取らない」へ切り替える
	/// @details テスト・スクリプト実行がユーザーの操作を奪わないための唯一の入口。窓を作る
	///          場所はテストにもツールにも散っているので、呼び出し側ごとの引数ではなく
	///          プロセス単位の状態にしてある (新しいテストが指定を忘れても後退しない)。
	///          立っている間は WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW で生成し、座標指定が
	///          無ければ画面外へ置き、SW_SHOWNOACTIVATE でだけ見せる (DXGI の Present は
	///          可視の窓を必要とするので SW_HIDE にはしない)。
	///          環境変数 `MITIRU_NO_ACTIVATE` (0 と空以外) でも立つ。子プロセスへ継承される
	///          ので、host が spawn するツール窓にもそのまま適用される。ゲーム窓にドックする
	///          ツール窓 (mitiru_tool) は、開いた時もクリックされた時もゲームのフォーカスを
	///          奪わないよう、自分でもこれを立てる。
	static void setProcessNoActivate(bool on) noexcept { noActivateState() = on; }

	[[nodiscard]] static bool processNoActivate() noexcept { return noActivateState(); }

	/// @brief noActivate 時に座標指定が無い窓を置く場所 (どのモニタにも載らない)
	static constexpr int kOffscreenX = -32000;
	static constexpr int kOffscreenY = -32000;

	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	/// @param displayMode ウィンドウ表示モード
	/// @param resizable ユーザがフレームでリサイズできるか (false の場合は
	///                  WS_THICKFRAME/WS_MAXIMIZEBOX を外して固定サイズ)
	explicit Win32Window(std::string_view title, int width, int height,
		DisplayMode displayMode = DisplayMode::Windowed,
		bool resizable = true,
		int posX = CW_USEDEFAULT, int posY = CW_USEDEFAULT)
		: m_width(width)
		, m_height(height)
		, m_displayMode(displayMode)
		, m_resizable(resizable)
	{
		/// Per-Monitor V2 DPI awareness を有効化する
		/// → 125%/150% スケール環境でも物理ピクセル単位で 1:1 描画される
		enableDpiAwareness();

		registerWindowClass();

		// WS_EX_NOACTIVATE = 生成も表示もアクティブ化を伴わない。WS_EX_TOOLWINDOW =
		// taskbar にボタンを出さない (窓が画面外でも、点滅するボタンは視界に入る)。
		const bool noAct = processNoActivate();
		const DWORD exStyle = noAct ? (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) : 0u;

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
			// noActivate では寸法だけモニタ全域に合わせ、場所は画面外に置く。
			// 全面を覆う窓こそ「視界を奪う」ものなので、隠すのは位置の方。
			const int x = noAct ? kOffscreenX : mi.rcMonitor.left;
			const int y = noAct ? kOffscreenY : mi.rcMonitor.top;
			const int w = mi.rcMonitor.right - mi.rcMonitor.left;
			const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

			m_hwnd = CreateWindowExW(
				exStyle, CLASS_NAME, wideTitle.c_str(),
				noAct ? WS_POPUP : (WS_POPUP | WS_VISIBLE),
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
			// 不可視で生成すると、ShowWindow を呼ばない standalone 消費者で窓が出ない。
			const DWORD baseStyle = m_resizable
				? WS_OVERLAPPEDWINDOW
				: (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
			// noActivate では WS_VISIBLE を外す。生成時可視の窓はその場で前面化するので、
			// 画面外に置いてもフォーカスだけは奪われる。表示は下の SW_SHOWNOACTIVATE で行う。
			const DWORD style = noAct ? baseStyle : (baseStyle | WS_VISIBLE);
			RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			// exStyle を渡さないと TOOLWINDOW の細いキャプション分だけクライアントが狭まり、
			// 寸法で焼いた golden が合わなくなる。
			adjustWindowRectForDpi(&rect, style, FALSE, exStyle, dpi);

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

			// posX/posY が指定されていれば最初からそこに出す (実画面に一瞬も出さない)。
			// noActivate で指定が無い場合は OS 任せにせず画面外へ送る。
			const int createX = (noAct && posX == CW_USEDEFAULT) ? kOffscreenX : posX;
			const int createY = (noAct && posX == CW_USEDEFAULT) ? kOffscreenY : posY;
			m_hwnd = CreateWindowExW(
				exStyle, CLASS_NAME, wideTitle.c_str(),
				style,
				createX, createY,
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

		// DXGI の Present は可視の窓を必要とする (不可視だと flip model が前に進まない)。
		// SW_SHOWNOACTIVATE は「見せるがアクティブにしない」ので、画面外の座標と合わせて
		// 誰の視界にも入らないまま swap chain だけが回る。
		if (noAct)
		{
			ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
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
		destroyLoadedIcons();
	}

	/// コピー禁止
	Win32Window(const Win32Window&) = delete;
	Win32Window& operator=(const Win32Window&) = delete;

	/// ムーブ禁止（HWND のユーザーデータが this を指すため）
	Win32Window(Win32Window&&) = delete;
	Win32Window& operator=(Win32Window&&) = delete;

	/// @brief ウィンドウが閉じられるべきかどうか
	/// @return WM_CLOSE/WM_DESTROY を受信済みなら true
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief Win32 メッセージキューをポーリングする
	/// @details PeekMessageW を使用したノンブロッキング処理。
	///          ゲームループをブロックしない。毎フレーム applyCursorCapture() を呼ぶ。
	void pollEvents() override
	{
		/// 枠 drag / 窓移動中の tick は window procedure の中から呼ばれる。深さ 1 に制限する。
		if (m_inPollEvents) { return; }
		m_inPollEvents = true;
		m_keyMessages.beginPump();
		MSG msg = {};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				m_shouldClose = true;
				m_inPollEvents = false;
				return;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		m_inPollEvents = false;
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

	/// @brief ウィンドウアイコンを .ico ファイルで設定する
	/// @param icoPath.ico ファイルのパス (UTF-8)
	/// @details LR_LOADFROMFILE で読み、WM_SETICON (BIG/SMALL) に反映する。
	///          読めない場合は何も知らせず、既定 (WNDCLASS の hIcon) のまま継続する。
	void setIcon(std::string_view icoPath) override
	{
		if (!m_hwnd || icoPath.empty()) { return; }

		const int wideLen = MultiByteToWideChar(
			CP_UTF8, 0, icoPath.data(), static_cast<int>(icoPath.size()), nullptr, 0);
		std::wstring widePath(static_cast<std::size_t>(wideLen), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, icoPath.data(), static_cast<int>(icoPath.size()),
			widePath.data(), wideLen);

		// 変数名 small は禁止 (rpcndr.h が small を char へ #define する)
		HICON bigIcon = static_cast<HICON>(LoadImageW(
			nullptr, widePath.c_str(), IMAGE_ICON, 0, 0,
			LR_LOADFROMFILE | LR_DEFAULTSIZE));
		HICON smallIcon = static_cast<HICON>(LoadImageW(
			nullptr, widePath.c_str(), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
			LR_LOADFROMFILE));

		if (bigIcon)
		{
			SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
		}
		if (smallIcon)
		{
			SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
		}
		// 旧 HICON は WM_SETICON 反映後に解放する (window は参照を差し替え済み)。
		destroyLoadedIcons();
		m_iconBig   = bigIcon;
		m_iconSmall = smallIcon;
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

	/// @brief 閉じ要求を取り消す
	/// @details WM_CLOSE は `m_shouldClose` を立てるだけで窓を破棄しないので、閉じる前に確認を
	///          挟む host や、閉じた窓を復帰させる game がこれで無かったことにできる。
	void cancelClose() noexcept { m_shouldClose = false; }

	/// @brief ランタイムでフルスクリーン/ウィンドウを切り替える
	/// @param enable true=ボーダーレスフルスクリーン, false=ウィンドウ
	void setFullscreen(bool enable)
	{
		const bool already = (m_displayMode == DisplayMode::BorderlessFullscreen);
		if (enable == already) return;

		// SetWindowPos は既定で対象をアクティブにする。noActivate ではそこだけ止める。
		const UINT noAct = processNoActivate() ? SWP_NOACTIVATE : 0u;

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
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | noAct);

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
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | noAct);
			ShowWindow(m_hwnd, processNoActivate() ? SW_SHOWNOACTIVATE : SW_NORMAL);

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
		ShowWindow(m_hwnd, processNoActivate() ? SW_SHOWNOACTIVATE : SW_SHOW);
		UpdateWindow(m_hwnd);
	}

	/// @brief ウィンドウを非表示にする
	void hide()
	{
		ShowWindow(m_hwnd, SW_HIDE);
	}

	/// @brief ネイティブウィンドウハンドルを取得する
	/// @return HWND（DX11 スワップチェーン生成に使用）
	[[nodiscard]] HWND getHandle() const noexcept
	{
		return m_hwnd;
	}

	/// @brief 入力状態の転送先を設定する
	/// @param state InputState への非所有ポインタ（Engine が所有）
	void setInputState(InputState* state) noexcept override
	{
		m_inputState = state;
	}

	/// @brief 入力インジェクターを設定する
	/// @param injector InputInjector への非所有ポインタ（Engine が所有）
	/// @details 設定後はキー/マウスイベントを InputState を直接書き換えるのではなく
	///          injector::inject() 経由で発行する。nullptr でフォールバックに戻る。
	void setInputInjector(InputInjector* injector) noexcept override
	{
		m_inputInjector = injector;
	}

	/// @brief DEBUG: InputState ポインタを取得する
	[[nodiscard]] const InputState* getInputStatePtr() const noexcept { return m_inputState; }

	/// @brief リサイズコールバックの型
	using ResizeCallback = std::function<void(int, int)>;

	/// @brief ウィンドウリサイズ時のコールバックを設定する
	/// @param cb 新しい width, height を受け取るコールバック
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

	/// @brief 窓の画面上の矩形 (枠込みの外側) を返す。ドッキングの追従に使う。
	bool getWindowRect(int& x, int& y, int& w, int& h) const override
	{
		RECT r{};
		if (m_hwnd == nullptr || !GetWindowRect(m_hwnd, &r)) { return false; }
		x = r.left; y = r.top; w = r.right - r.left; h = r.bottom - r.top;
		return true;
	}

	/// @brief 窓を (x,y) サイズ (w,h) へ動かす。z-order・フォーカスは変えない (ドック追従用)。
	void moveWindow(int x, int y, int w, int h) override
	{
		if (m_hwnd != nullptr)
		{
			SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	/// @brief taskbar と alt-tab から外す (WS_EX_TOOLWINDOW)。会話窓のような、主窓に従属して
	///        出入りする補助窓に使う。WS_EX_APPWINDOW と排他なので同時に外す。
	void setToolWindow()
	{
		if (m_hwnd != nullptr)
		{
			const LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
			SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE,
			                  (ex | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW));
			SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
			             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
	}

	/// @brief 常に最前面に置く (WS_EX_TOPMOST)。ドックしたシークバーが背面へ潜らないように。
	void setTopmost() override
	{
		if (m_hwnd != nullptr)
		{
			SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
			             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	/// @brief HWND を整数で返す (ドック窓が「この窓の真下」に z-order 挿入するため)。
	[[nodiscard]] std::uintptr_t nativeHandle() const override
	{
		return reinterpret_cast<std::uintptr_t>(m_hwnd);
	}

	/// @brief aboveWindow の真下に (x,y,w,h) で置く。z-order で直後に差し込む (割り込み防止)。
	void dockBelow(std::uintptr_t aboveWindow, int x, int y, int w, int h) override
	{
		if (m_hwnd == nullptr) { return; }
		HWND above = reinterpret_cast<HWND>(aboveWindow);
		// aboveWindow が無効ならただ移動 (z-order 据え置き)。
		const HWND insertAfter = (above != nullptr && IsWindow(above)) ? above : HWND_TOP;
		SetWindowPos(m_hwnd, insertAfter, x, y, w, h, SWP_NOACTIVATE);
	}

	/// @brief この窓を閉じたときに WM_QUIT を投げるかを決める (既定 true)
	/// @details WM_QUIT はスレッド単位なので、複数の窓を作っては閉じるアプリでは、閉じた窓が
	///          残した WM_QUIT を次に作った窓の pollEvents が拾い、開いた直後の窓が
	///          shouldClose() を返す。窓が 1 つだけのアプリは既定のままでよい。
	void setQuitOnDestroy(bool enabled) noexcept { m_quitOnDestroy = enabled; }

	/// @brief Win32 modal resize loop 中も engine を tick させるための callback
	/// @details ユーザが window 枠を drag すると Windows は `DefWindowProc` 内で
	///          modal loop に入り、main thread を block する → engine main loop
	///          (`tickOneFrame`) が止まり描画/計算も止まる。
	///          WM_ENTERSIZEMOVE で SetTimer し WM_TIMER で本 callback を呼ぶ
	///          ことで、drag 中も ~60fps で engine が回り続ける。Direct3D SDK
	///          sample の standard pattern。
	void setTickCallback(std::function<void()> cb) noexcept
	{
		m_tickCallback = std::move(cb);
	}

	/// @brief クライアント領域のどこを掴んでもタイトルバーと同じにドラッグできるようにする
	/// @details 窓そのものを動かすことが主な用途の consumer 向け。マウスをクライアント入力に
	///          使っている consumer では、この設定でクライアント側のマウスメッセージが
	///          届かなくなるので有効にしないこと。既定は off。
	void setDragByClientArea(bool enabled) noexcept { m_dragByClientArea = enabled; }

	/// @brief リサイズ可否を生成後に切り替える
	/// @details 1 枚の窓を作り替えながら使い回す consumer 向け。WS_THICKFRAME はスタイルなので
	///          生成時に決まるが、縁の当たり判定 (borderless の hit test 含む) はこれを見る。
	void setResizable(bool resizable) noexcept
	{
		m_resizable = resizable;
		if (m_hwnd == nullptr)
		{
			return;
		}
		LONG style = GetWindowLongW(m_hwnd, GWL_STYLE);
		if (resizable)
		{
			style |= WS_THICKFRAME;
		}
		else
		{
			style &= ~WS_THICKFRAME;
		}
		SetWindowLongW(m_hwnd, GWL_STYLE, style);
		SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
		             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	/// @brief キャプションと縁の絵を消し、描画面を窓の全面にする
	/// @details スタイルは残して WM_NCCALCSIZE の計算だけ変えるので、taskbar・最小化・
	///          スナップ・OS のドラッグループは本物のまま。閉じる/最小化ボタンは consumer が
	///          描き、その位置を @ref setHitTestOverride で HTCLOSE / HTREDUCE として返せば、
	///          クリックは OS の同じ経路 (SC_CLOSE → WM_CLOSE 等) を通る。
	///          最大化だけは外す: 全面クライアントの窓を最大化すると縁の分だけ画面から
	///          はみ出すため。
	void setBorderless(bool enabled) noexcept
	{
		m_borderless = enabled;
		if (m_hwnd == nullptr)
		{
			return;
		}
		LONG style = GetWindowLongW(m_hwnd, GWL_STYLE);
		if (enabled)
		{
			style &= ~WS_MAXIMIZEBOX;
		}
		SetWindowLongW(m_hwnd, GWL_STYLE, style);
		SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
		             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		if (enabled)
		{
			extendFrameForShadow();
		}
	}

	/// @brief borderless 時の当たり判定を consumer が差し込む
	/// @param cb (窓ローカル x, y) を受け、HTCLOSE / HTREDUCE / HTCLIENT 等を返す。
	///           0 を返すと既定 (縁のリサイズ判定 → 残りは掴んでドラッグ) に任せる
	void setHitTestOverride(std::function<LRESULT(int, int)> cb) noexcept
	{
		m_hitTestOverride = std::move(cb);
	}

	/// @brief クライアント領域のカーソルを差し替える (WM_SETCURSOR、desktop_world 要望)
	/// @param cursor 差し替え先。所有権は呼び出し側のまま (LoadCursor 系はシステム共有、
	///               自前 LoadImage/CreateIconFromResource の場合は呼び出し側が解放する)。
	///               nullptr で既定 (OS 標準の矢印等) に戻す
	void setClientCursor(HCURSOR cursor) noexcept
	{
		m_clientCursor = cursor;
	}

	/// @brief このフレームに確定した UTF-8 テキストを取り出し、内部バッファを空にする (J5)
	/// @details WM_CHAR / WM_IME_COMPOSITION(GCS_RESULTSTR) で溜めたものを engine が
	///          毎フレーム 1 回取り出す (action event の drain と同じ「取ったら空」契約)。
	[[nodiscard]] std::string consumeTextInput() noexcept
	{
		std::string out = std::move(m_pendingTextInput);
		m_pendingTextInput.clear();
		return out;
	}

	/// @brief IME で変換中の文字列 (UTF-8)。変換していなければ空。
	[[nodiscard]] const platform::ImeCompositionUtf8& imeComposition() const noexcept { return m_imeComposition; }

	/// @brief ゲームがテキストを受けたいかを伝える (毎フレーム)。false の間は IME を窓から外し、遊んでいる
	///        間に変換窓が出ないようにする。true の間は IME を戻し、変換窓と候補窓を area (クライアント座標の
	///        入力欄) へ置く。状態か入力欄が変わった時だけ Imm を呼ぶ。
	void setTextInputArea(bool active, const RECT& area) noexcept
	{
		if (m_hwnd == nullptr) { return; }
		if (m_textInputState != (active ? 1 : 0))
		{
			ImmAssociateContextEx(m_hwnd, nullptr, active ? IACE_DEFAULT : 0);
			m_textInputState = active ? 1 : 0;
			m_textInputArea = RECT{};
			if (!active) { m_imeComposition = {}; }
		}
		if (!active || EqualRect(&area, &m_textInputArea)) { return; }
		m_textInputArea = area;
		placeImeWindows(area);
	}

	/// @brief このフレームに受けたキーボードのメッセージのうち、まだ受け取っていない分 (UI の入力欄向け)
	/// @details ゲーム向けの InputState / consumeTextInput とは別に、受けた順のまま残す。
	///          VK_PROCESSKEY も元のキーへ戻さずに残す (ブラウザは 229 を「IME が使ったキー」として扱う)。
	///          中身は次の pollEvents まで有効。
	[[nodiscard]] std::span<const platform::Win32KeyMessage> takeKeyMessages() noexcept
	{
		return m_keyMessages.take();
	}

	/// @brief タイトルバーの背景色を変える (Windows 11 以降)
	/// @details DWMWA_CAPTION_COLOR。対応しない OS では黙って何もしない。枠もボタンも
	///          本物のまま、色だけがゲームの画面に馴染む。
	void setCaptionColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
	{
		using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
		if (dwm == nullptr)
		{
			return;
		}
		if (auto setAttr =
		        reinterpret_cast<SetAttrFn>(GetProcAddress(dwm, "DwmSetWindowAttribute")))
		{
			constexpr DWORD kCaptionColor = 35;   // DWMWA_CAPTION_COLOR (Win11 22000+)
			const COLORREF color = RGB(r, g, b);
			setAttr(m_hwnd, kCaptionColor, &color, sizeof(color));
		}
		FreeLibrary(dwm);
	}

	/// @brief 現在 modal resize loop (枠 drag) 中か
	/// @details Engine::onWindowResize がこれを参照して、drag 中は
	///          logical re-layout を抑止し backbuffer のみ追従させる。
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
	/// @brief Win32 仮想キーコードを mitiru 内部キーコードに変換する
	/// @param vk Win32 仮想キーコード
	/// @return mitiru キーコード整数値（KeyCode の enum 値と一致）
	/// @details KeyCode は Win32 VK コードに準拠しているため、
	///          0〜255 の範囲内ならそのまま返す。
	[[nodiscard]] static int mapVirtualKey(WPARAM vk) noexcept
	{
		const auto code = static_cast<int>(vk);
		if (code >= 0 && code < InputState::MAX_KEYS)
		{
			return code;
		}
		return 0;
	}

	[[nodiscard]] static int keyCodeFromMessage(HWND hwnd, WPARAM vk) noexcept
	{
		const UINT swallowed = (vk == VK_PROCESSKEY) ? ImmGetVirtualKey(hwnd) : 0;
		return mapVirtualKey(static_cast<WPARAM>(
			platform::resolveImeVirtualKey(static_cast<unsigned>(vk), swallowed)));
	}

	static_assert(platform::kWmKeyDown == WM_KEYDOWN && platform::kWmKeyUp == WM_KEYUP
	              && platform::kWmChar == WM_CHAR && platform::kWmSysKeyDown == WM_SYSKEYDOWN
	              && platform::kWmSysKeyUp == WM_SYSKEYUP && platform::kWmSysChar == WM_SYSCHAR);

	/// GetKeyState はこのメッセージが作られた時点の状態を返す (取り出し時の状態ではない)
	[[nodiscard]] static std::uint32_t captureKeyState(UINT msg, WPARAM wParam) noexcept
	{
		using namespace platform::keystate;
		std::uint32_t s = 0;
		if (GetKeyState(VK_SHIFT) < 0)       { s |= kShift; }
		if (GetKeyState(VK_CONTROL) < 0)     { s |= kControl; }
		if (GetKeyState(VK_MENU) < 0)        { s |= kAlt; }
		if ((GetKeyState(VK_CAPITAL) & 1) != 0) { s |= kCapsLock; }
		if ((GetKeyState(VK_NUMLOCK) & 1) != 0) { s |= kNumLock; }
		const bool isChar = msg == WM_CHAR || msg == WM_SYSCHAR;
		if (isChar && GetKeyState(VK_RMENU) < 0)
		{
			// 配列上その文字に Ctrl+Alt が要るなら、右 Alt は AltGr として押されている
			constexpr int kCtrlAlt = 2 | 4;
			const SHORT scan = VkKeyScanExW(static_cast<WCHAR>(wParam), GetKeyboardLayout(0));
			if (scan != -1 && ((scan >> 8) & kCtrlAlt) == kCtrlAlt) { s |= kAltGr; }
		}
		return s;
	}

	void recordKeyMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept
	{
		m_keyMessages.push(platform::Win32KeyMessage{
			static_cast<std::uint32_t>(msg), static_cast<std::uint32_t>(wParam),
			static_cast<std::int32_t>(lParam), captureKeyState(msg, wParam)});
	}

	/// 本物の WM_KEYUP と同じ lParam (スキャンコード、拡張キー、直前は押下、離した) を組み立てる
	void recordFocusLossKeyUp(int vk) noexcept
	{
		const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
		std::uint32_t l = 1u | ((scan & 0xFFu) << 16) | (1u << 30) | (1u << 31);
		if ((scan & 0xFF00u) == 0xE000u) { l |= 1u << 24; }
		m_keyMessages.push(platform::Win32KeyMessage{
			platform::kWmKeyUp, static_cast<std::uint32_t>(vk), static_cast<std::int32_t>(l), 0});
	}

	/// @brief ウィンドウクラス名
	static constexpr const wchar_t* CLASS_NAME = L"MitiruWindowClass";

	/// @brief exe に埋まっている icon 資源のうち Explorer が選ぶのと同じ 1 つの資源名
	/// @return 資源が無ければ nullptr (呼び出し側で既定 icon を使う)
	/// @details ID を 1 と決め打たない。Explorer は RT_GROUP_ICON の最も若い名前を採るので、
	///          `IDI_APP 101 ICON "..."` のように書かれた exe でも同じものが出る。
	static LPWSTR executableIconName()
	{
		const HMODULE self = GetModuleHandleW(nullptr);
		struct Pick
		{
			LPWSTR name = nullptr;
			bool numeric = true;
		} pick;

		EnumResourceNamesW(
			self, reinterpret_cast<LPCWSTR>(RT_GROUP_ICON),
			[](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
				Pick& best = *reinterpret_cast<Pick*>(param);
				const bool numeric = IS_INTRESOURCE(name);
				// 数値名は数値名同士で小さい方、文字列名しか無ければ最初のもの。
				const bool better =
					best.name == nullptr ||
					(numeric && (!best.numeric ||
					             reinterpret_cast<ULONG_PTR>(name) <
					                 reinterpret_cast<ULONG_PTR>(best.name)));
				if (better)
				{
					best.name = name;
					best.numeric = numeric;
				}
				return TRUE;
			},
			reinterpret_cast<LONG_PTR>(&pick));

		return pick.name;
	}

	/// @brief exe の icon 資源を指定寸法で読む (0,0 は既定寸法)
	/// @details 大小を別々に読むのは、.ico に入っている 16px 用の絵を taskbar と title bar に
	///          出すため。1 つの HICON を使い回すと大きい絵を縮めたものになる。
	static HICON loadExecutableIcon(LPWSTR name, int cx, int cy)
	{
		if (name == nullptr)
		{
			return nullptr;
		}
		return static_cast<HICON>(
			LoadImageW(GetModuleHandleW(nullptr), name, IMAGE_ICON, cx, cy,
			           (cx == 0 && cy == 0) ? LR_DEFAULTSIZE : 0));
	}

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
		// exe が自前の icon 資源を持つならそれを使う。Explorer が exe に出すものと窓・taskbar の
		// ものが食い違うのを防ぐためで、.rc を足す以外に game 側の呼び出しは要らない。
		// 32512 = IDI_APPLICATION (非 UNICODE 構成でも W 版に合わせ MAKEINTRESOURCEW 直指定)
		LPWSTR iconName = executableIconName();
		HICON bigIcon = loadExecutableIcon(iconName, 0, 0);
		HICON smallIcon = loadExecutableIcon(iconName, GetSystemMetrics(SM_CXSMICON),
		                                     GetSystemMetrics(SM_CYSMICON));
		wc.hIcon   = bigIcon ? bigIcon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
		wc.hIconSm = smallIcon ? smallIcon : wc.hIcon;
		wc.lpszClassName = CLASS_NAME;

		if (!RegisterClassExW(&wc))
		{
			throw std::runtime_error("Win32Window: RegisterClassExW failed");
		}

		registered = true;
	}

	/// @brief Win32 ウィンドウプロシージャ
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
			/// ウィンドウ生成時に this ポインタを保存
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
		case WM_NCCALCSIZE:
			/// borderless: クライアント領域を窓の全面に広げる。スタイル (WS_CAPTION 等) は
			/// 残したまま計算だけを変えるので、taskbar・最小化アニメーション・スナップ・
			/// DWM の影といった「本物の窓」の挙動は全部そのまま残る。消えるのは絵としての
			/// キャプションと縁だけで、その面は consumer が描く。
			if (m_borderless && wParam == TRUE)
			{
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		case WM_NCHITTEST:
		{
			if (m_borderless)
			{
				return borderlessHitTest(hwnd, lParam);
			}
			/// クライアント領域だけをタイトルバー扱いに読み替える。縁 (HTLEFT 等) や
			/// 閉じる・最小化ボタン (HTCLOSE 等) は DefWindowProc の答えのまま残るので、
			/// リサイズもボタンも今までどおり働く。
			if (m_dragByClientArea)
			{
				const LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
				return (hit == HTCLIENT) ? HTCAPTION : hit;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		/// borderless の描いたボタン。押した時ではなく**離した時の位置**で反応させるので、
		/// 押してからボタンの外へカーソルを動かせば取り消しになる (本物のボタンと同じ作法)。
		/// DefWindowProc 自身の追跡はカーソルの実座標を読むため、ここで肩代わりする。
		case WM_NCLBUTTONDOWN:
			if (m_borderless && (wParam == HTCLOSE || wParam == HTREDUCE))
			{
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		case WM_NCLBUTTONUP:
			if (m_borderless && wParam == HTCLOSE)
			{
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
				return 0;
			}
			if (m_borderless && wParam == HTREDUCE)
			{
				ShowWindow(hwnd, SW_MINIMIZE);
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		case WM_CLOSE:
			m_shouldClose = true;
			return 0;

		case WM_DESTROY:
			m_shouldClose = true;
			if (m_quitOnDestroy)
			{
				PostQuitMessage(0);
			}
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
			/// modal 中に deferred されていた full resize (logical) を発火
			if (m_modalResizeEndCallback) { m_modalResizeEndCallback(); }
			/// 反映後 1 frame 引いて即座に画面更新
			runTickCallbackOnce();
			return 0;

		case WM_TIMER:
			if (wParam == kModalTickTimerId)
			{
				runTickCallbackOnce();
			}
			return 0;

		/// --- drag 中は「窓が動いたとき」にも描き直す ---
		/// timer だけだと 16ms に 1 回しか描かないのに、窓はマウスの報告レート (125Hz 以上) で
		/// 動き続ける。描いた時の位置と、その絵が画面に出る時の位置がずれるので、窓の中身が
		/// 世界へ固定されている consumer では中身が引きずられて見える。timer も残す。
		/// マウスを止めたまま掴んでいる間も時間は進めなければならないため。
		case WM_MOVE:
			if (m_inModalLoop)
			{
				runTickCallbackOnce();
			}
			return 0;

		/// --- キーボード入力 ---
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			recordKeyMessage(msg, wParam, lParam);
			const int kc = keyCodeFromMessage(hwnd, wParam);
			if (kc == 0)
			{
				return 0;
			}
			// hardware で押している key を覚える (focus 喪失時にまとめて release するため)。
			if (std::find(m_heldKeys.begin(), m_heldKeys.end(), kc) == m_heldKeys.end())
			{ m_heldKeys.push_back(kc); }
			if (m_inputInjector)
			{
				m_inputInjector->inject(InputCommand{InputCommandType::KeyDown, kc});
			}
			else if (m_inputState)
			{
				m_inputState->setKeyDown(kc, true);
			}
			return 0;
		}

		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			recordKeyMessage(msg, wParam, lParam);
			const int kc = keyCodeFromMessage(hwnd, wParam);
			if (kc == 0)
			{
				return 0;
			}
			m_heldKeys.erase(std::remove(m_heldKeys.begin(), m_heldKeys.end(), kc), m_heldKeys.end());
			if (m_inputInjector)
			{
				m_inputInjector->inject(InputCommand{InputCommandType::KeyUp, kc});
			}
			else if (m_inputState)
			{
				m_inputState->setKeyDown(kc, false);
			}
			return 0;
		}

		/// --- テキスト入力 (J5) ---------------------------------------------
		/// 本命は UI (RmlUi) の入力欄。ここは「プレイヤー名入力」等、ゲーム内の
		/// 簡易テキスト入力向けの最小手段。確定文字だけを拾う (composition 中の
		/// 未確定プレビューは含めない)。IME の候補ウィンドウ自体は素通しなので
		/// DefWindowProcW に必ず渡す (自前で描かない)。
		case WM_CHAR:
		{
			recordKeyMessage(msg, wParam, lParam);
			const wchar_t wc = static_cast<wchar_t>(wParam);
			if (wc >= 0x20 || wc == L'\t')  // 制御文字 (Backspace/Enter 等) は既存キー入力側で扱う
			{
				if (IS_HIGH_SURROGATE(wc))
				{
					m_pendingHighSurrogate = wc;
				}
				else if (IS_LOW_SURROGATE(wc) && m_pendingHighSurrogate != 0)
				{
					const wchar_t pair[2] = {m_pendingHighSurrogate, wc};
					appendTextInputUtf16(pair, 2);
					m_pendingHighSurrogate = 0;
				}
				else
				{
					m_pendingHighSurrogate = 0;
					appendTextInputUtf16(&wc, 1);
				}
			}
			return 0;
		}

		/// IME が確定した文字列 (GCS_RESULTSTR) は textInput へ、変換中の文字列 (GCS_COMPSTR) は
		/// imeComposition へ (後者は確定するまで入力として扱わない)。
		case WM_IME_COMPOSITION:
			if ((lParam & GCS_RESULTSTR) != 0)
			{
				if (HIMC himc = ImmGetContext(hwnd))
				{
					const LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
					if (bytes > 0)
					{
						std::wstring wbuf(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
						ImmGetCompositionStringW(himc, GCS_RESULTSTR, wbuf.data(), static_cast<DWORD>(bytes));
						appendTextInputUtf16(wbuf.data(), static_cast<int>(wbuf.size()));
						m_keyMessages.pushImeCommit(std::span<const char16_t>(
							reinterpret_cast<const char16_t*>(wbuf.data()), wbuf.size()));
					}
					ImmReleaseContext(hwnd, himc);
				}
			}
			readImeComposition(hwnd, lParam);
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		case WM_IME_ENDCOMPOSITION:
			m_imeComposition = {};
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		/// 確定文字は上の GCS_RESULTSTR で受け取り済み。DefWindowProc に渡すと WM_CHAR になって
		/// 同じ文字がもう一度届く。変換窓の表示のために WM_IME_COMPOSITION の方は渡している。
		case WM_IME_CHAR:
			return 0;

		case WM_SYSCHAR:
			recordKeyMessage(msg, wParam, lParam);
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		/// --- focus 喪失 --------------------------------------------------
		/// ユーザが alt-tab で離れた (または dev companion のような別 window を
		/// クリックした) 時、Windows はこの hwnd へ WM_KEYUP を配送しなくなる。
		/// その時点で押されていた key は InputState 内で永久に "down" のまま残る。
		/// 典型的な "矢印キー stuck" bug。ここでクリアし、game に正しい
		/// release edge が届くようにする。
		case WM_KILLFOCUS:
			// focus が外れると Windows は WM_KEYUP を配送しなくなる。押していた key を
			// injector 経路でも release する (注入 KeyUp)。AI 注入入力は触らない。
			if (m_inputInjector)
			{
				for (const int kc : m_heldKeys)
				{ m_inputInjector->inject(InputCommand{InputCommandType::KeyUp, kc}); }
			}
			if (m_inputState) { m_inputState->clearHeldKeys(); }
			for (const int kc : m_heldKeys) { recordFocusLossKeyUp(kc); }
			m_heldKeys.clear();
			return 0;

		/// --- カーソル差し替え (desktop_world 要望) ---
		/// クライアント領域上でのみ横取りする。枠 (HTLEFT 等のリサイズ矢印) は
		/// DefWindowProc に任せないと resize 操作の見た目がおかしくなる。
		case WM_SETCURSOR:
			if (m_clientCursor && LOWORD(lParam) == HTCLIENT)
			{
				SetCursor(m_clientCursor);
				return TRUE;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		/// --- マウス移動 ---
		case WM_MOUSEMOVE:
		{
			/// カーソルスナップバック後の WM_MOUSEMOVE は無視する。
			/// SetCursorPos がウィンドウに送る合成イベントでデルタを二重に計上しないようにする。
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
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		{
			const MouseButton b = (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) ? MouseButton::X2 : MouseButton::X1;
			const bool down = (msg == WM_XBUTTONDOWN);
			if (m_inputInjector)
				m_inputInjector->inject(InputCommand{down ? InputCommandType::MouseDown : InputCommandType::MouseUp,
				                                     0, static_cast<int>(b)});
			else if (m_inputState)
				m_inputState->setMouseButtonDown(b, down);
			return TRUE;  // X ボタンだけは処理したら TRUE を返す約束 (WM_XBUTTONDOWN の文書)
		}
		case WM_MOUSEWHEEL:
			// ホイールの回転量 (符号付き、120 = 1 ノッチ) を今フレームのデルタとして積む。
			if (m_inputState)
				m_inputState->addMouseWheelDelta(static_cast<float>(static_cast<short>(HIWORD(wParam))));
			return 0;
		case WM_MOUSEHWHEEL:
			if (m_inputState)
				m_inputState->addMouseWheelHDelta(static_cast<float>(static_cast<short>(HIWORD(wParam))));
			return 0;

		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	/// @brief noActivate 状態の実体 (ヘッダオンリーなので関数内 static で 1 個にまとめる)
	static bool& noActivateState() noexcept
	{
		static bool state = env::flag("MITIRU_NO_ACTIVATE");
		return state;
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

		// 前面でない間は適用しない (alt-tab 中に他アプリのカーソルを掴まない)
		const bool focused = (GetForegroundWindow() == m_hwnd);
		const bool wantCapture = focused && m_inputState->isCursorCaptured();

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

	/// @brief setIcon で LoadImageW した HICON を解放する (未設定なら no-op)
	void destroyLoadedIcons() noexcept
	{
		if (m_iconBig)   { DestroyIcon(m_iconBig);   m_iconBig = nullptr; }
		if (m_iconSmall) { DestroyIcon(m_iconSmall); m_iconSmall = nullptr; }
	}

	HWND m_hwnd = nullptr;            ///< ウィンドウハンドル
	HICON m_iconBig = nullptr;        ///< setIcon で読んだ ICON_BIG (所有)
	HICON m_iconSmall = nullptr;      ///< setIcon で読んだ ICON_SMALL (所有)
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
	bool m_quitOnDestroy = true;           ///< 破棄時に WM_QUIT を投げるか (setQuitOnDestroy)
	InputState* m_inputState = nullptr;   ///< 入力状態転送先（非所有）
	InputInjector* m_inputInjector = nullptr; ///< 入力インジェクター（非所有）。非nullの場合はinject()経由でイベント発行
	std::vector<int> m_heldKeys;              ///< hardware で今押している key (focus 喪失時に release してstuckを防ぐ)
	ResizeCallback m_resizeCallback;      ///< リサイズコールバック
	std::function<void()> m_tickCallback;  ///< modal-loop tick (drag-resize 中の engine 駆動)
	std::function<void()> m_modalResizeEndCallback; ///< WM_EXITSIZEMOVE で 1 回呼ぶ deferred full resize
	bool m_inModalLoop = false;            ///< WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE
	bool m_dragByClientArea = false;       ///< クライアント領域掴みドラッグ (setDragByClientArea)
	bool m_borderless = false;             ///< 全面クライアント (setBorderless)
	std::function<LRESULT(int, int)> m_hitTestOverride; ///< borderless 時の consumer 当たり判定
	HCURSOR m_clientCursor = nullptr; ///< setClientCursor() で差し込まれたカーソル (nullptr=既定)

	std::string m_pendingTextInput;       ///< consumeTextInput() が吸い出すまでの UTF-8 蓄積 (J5)
	wchar_t     m_pendingHighSurrogate = 0; ///< WM_CHAR のサロゲートペア上位が来た時の一時保持
	platform::Win32KeyMessageQueue m_keyMessages;
	platform::ImeCompositionUtf8   m_imeComposition;     ///< 変換中の文字列 (GCS_COMPSTR)
	int  m_textInputState = -1;   ///< setTextInputArea の最後の値 (-1 = まだ一度も呼ばれていない)
	RECT m_textInputArea{};       ///< 変換窓を置いた入力欄 (クライアント座標)

	/// @brief WM_IME_COMPOSITION の変換中の文字列とキャレットを読む。確定だけのメッセージなら空にする。
	void readImeComposition(HWND hwnd, LPARAM lParam)
	{
		if ((lParam & GCS_COMPSTR) == 0)
		{
			if ((lParam & GCS_RESULTSTR) != 0) { m_imeComposition = {}; }
			return;
		}
		HIMC himc = ImmGetContext(hwnd);
		if (himc == nullptr) { return; }
		const LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
		std::u16string text((bytes > 0) ? static_cast<std::size_t>(bytes) / sizeof(char16_t) : 0u, u'\0');
		if (!text.empty()) { ImmGetCompositionStringW(himc, GCS_COMPSTR, text.data(), static_cast<DWORD>(bytes)); }
		const LONG cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
		ImmReleaseContext(hwnd, himc);
		m_imeComposition = platform::toUtf8Composition(text, (cursor >= 0) ? static_cast<std::size_t>(cursor) : text.size());
	}

	/// @brief 変換窓を入力欄の左上、候補窓を入力欄の下 (入力欄に重ねない) へ置く。
	void placeImeWindows(const RECT& area) noexcept
	{
		HIMC himc = ImmGetContext(m_hwnd);
		if (himc == nullptr) { return; }
		COMPOSITIONFORM comp{};
		comp.dwStyle = CFS_POINT;
		comp.ptCurrentPos = POINT{area.left, area.top};
		ImmSetCompositionWindow(himc, &comp);
		CANDIDATEFORM cand{};
		cand.dwIndex = 0;
		cand.dwStyle = CFS_EXCLUDE;
		cand.ptCurrentPos = POINT{area.left, area.bottom};
		cand.rcArea = area;
		ImmSetCandidateWindow(himc, &cand);
		ImmReleaseContext(m_hwnd, himc);
	}

	/// @brief UTF-16 文字列を UTF-8 へ変換して m_pendingTextInput に追記する
	void appendTextInputUtf16(const wchar_t* wtext, int wlen) noexcept
	{
		if (wtext == nullptr || wlen <= 0) { return; }
		const int needed = WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, nullptr, 0, nullptr, nullptr);
		if (needed <= 0) { return; }
		const std::size_t oldSize = m_pendingTextInput.size();
		m_pendingTextInput.resize(oldSize + static_cast<std::size_t>(needed));
		WideCharToMultiByte(CP_UTF8, 0, wtext, wlen,
			m_pendingTextInput.data() + oldSize, needed, nullptr, nullptr);
	}

	/// @brief borderless の窓に DWM の影を残す
	/// @details WM_NCCALCSIZE を 0 にすると影も消えるが、フレームを 1px だけクライアントへ
	///          張り出させると戻る。張り出した分は不透明な描画の下に隠れて見えない。
	void extendFrameForShadow() noexcept
	{
		using ExtendFn = HRESULT(WINAPI*)(HWND, const void*);
		const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
		if (dwm == nullptr)
		{
			return;
		}
		if (auto extend =
		        reinterpret_cast<ExtendFn>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea")))
		{
			struct { int l; int r; int t; int b; } margins{0, 0, 1, 0};
			extend(m_hwnd, &margins);
		}
		FreeLibrary(dwm);
	}

	/// @brief borderless 時の WM_NCHITTEST
	/// @details 優先順: consumer の override (描いたボタン等) → リサイズの縁 → 掴んでドラッグ。
	///          縁の判定はスタイルに WS_THICKFRAME がある窓 (resizable) だけ。
	[[nodiscard]] LRESULT borderlessHitTest(HWND hwnd, LPARAM lParam) const noexcept
	{
		POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		RECT r{};
		GetWindowRect(hwnd, &r);
		const int lx = p.x - r.left;
		const int ly = p.y - r.top;

		if (m_hitTestOverride)
		{
			const LRESULT hit = m_hitTestOverride(lx, ly);
			if (hit != 0)
			{
				return hit;
			}
		}

		if ((GetWindowLongW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0)
		{
			constexpr int kGrip = 8;
			const int w = r.right - r.left;
			const int h = r.bottom - r.top;
			const bool left = lx < kGrip;
			const bool right = lx >= w - kGrip;
			const bool top = ly < kGrip;
			const bool bottom = ly >= h - kGrip;
			if (top && left) { return HTTOPLEFT; }
			if (top && right) { return HTTOPRIGHT; }
			if (bottom && left) { return HTBOTTOMLEFT; }
			if (bottom && right) { return HTBOTTOMRIGHT; }
			if (left) { return HTLEFT; }
			if (right) { return HTRIGHT; }
			if (top) { return HTTOP; }
			if (bottom) { return HTBOTTOM; }
		}

		return m_dragByClientArea ? HTCAPTION : HTCLIENT;
	}
	/// tick callback は内部で pollEvents() を呼ぶ。そこから WM_TIMER が再配送されると
	/// frame が入れ子になり、DX12 の command list / fence がおかしくなる。
	bool m_inTickCallback = false;
	bool m_inPollEvents = false;
	static constexpr UINT_PTR kModalTickTimerId = 0x4D54; // 'MT'

	/// @brief tick callback を再入なしで 1 回だけ呼ぶ
	void runTickCallbackOnce()
	{
		if (m_inTickCallback || !m_tickCallback) { return; }
		m_inTickCallback = true;
		m_tickCallback();
		m_inTickCallback = false;
	}
};

} // namespace mitiru

#endif // _WIN32
