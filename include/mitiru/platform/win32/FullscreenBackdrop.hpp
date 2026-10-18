#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace mitiru
{

class FullscreenBackdrop final
{
public:
	FullscreenBackdrop() = default;
	~FullscreenBackdrop() { hide(); }

	FullscreenBackdrop(const FullscreenBackdrop&) = delete;
	FullscreenBackdrop& operator=(const FullscreenBackdrop&) = delete;
	FullscreenBackdrop(FullscreenBackdrop&&) = delete;
	FullscreenBackdrop& operator=(FullscreenBackdrop&&) = delete;

	void show(const RECT& monitor, HWND placeBelow)
	{
		registerWindowClass();
		hide();
		m_hwnd = CreateWindowExW(
			WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
			kClassName, L"", WS_POPUP,
			monitor.left, monitor.top,
			monitor.right - monitor.left, monitor.bottom - monitor.top,
			nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (m_hwnd == nullptr)
		{
			return;
		}
		ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
		SetWindowPos(m_hwnd, placeBelow, 0, 0, 0, 0,
		             SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
	}

	void raiseBelow(HWND above) noexcept
	{
		if (m_hwnd == nullptr || above == nullptr)
		{
			return;
		}
		constexpr UINT flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE;
		SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, flags);
		SetWindowPos(above, HWND_TOP, 0, 0, 0, 0, flags);
	}

	void hide() noexcept
	{
		if (m_hwnd != nullptr)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
	}

	[[nodiscard]] bool visible() const noexcept { return m_hwnd != nullptr; }

private:
	static constexpr const wchar_t* kClassName = L"MitiruFullscreenBackdropClass";

	static LRESULT CALLBACK windowProc(
		HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_MOUSEACTIVATE)
		{
			return MA_NOACTIVATE;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	static void registerWindowClass()
	{
		static const bool registered = [] {
			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = windowProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
			wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
			wc.lpszClassName = kClassName;
			return RegisterClassExW(&wc) != 0
				|| GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
		}();
		(void)registered;
	}

	HWND m_hwnd = nullptr;
};

} // namespace mitiru

#endif // _WIN32
