#pragma once

/// @file HostAccessibility.hpp
/// @brief 読み上げソフト (Narrator) へ、UI の data-a11y を付けた要素の文を通知として渡す host の仕事。
/// @details 窓に UI Automation の根の提供者を付け、読み上げソフトが動いている間だけ UI の文書を走査する。
///          ゲームの DLL には何も見せず、通知は出力なので録画にも再生にも入らない。窓の無い実行 (headless、
///          台本、撮影) では何もしない。

#include <string>

#include <mitiru/core/Engine.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/UiaRoot.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

namespace mitiru::host
{

class HostAccessibility
{
public:
	HostAccessibility() = default;
	HostAccessibility(const HostAccessibility&) = delete;
	HostAccessibility& operator=(const HostAccessibility&) = delete;

	/// @brief onFrameStart から毎フレーム呼ぶ。
	void onFrame(Engine& engine)
	{
#ifdef _WIN32
		auto* win = dynamic_cast<Win32Window*>(engine.window());
		if (win == nullptr || Win32Window::processNoActivate())
		{
			engine.uiHost().setAnnouncementsEnabled(false);   // 窓が無い間は、通知の送り先も無い
			return;
		}
		const HWND hwnd = reinterpret_cast<HWND>(win->nativeHandle());
		if (hwnd != m_root.window() && hwnd != m_failedWindow) { attach(*win, hwnd); }
		if (!m_root.attached()) { return; }

		const bool listening = platform::UiaRoot::clientsListening();
		engine.uiHost().setAnnouncementsEnabled(listening);
		if (!listening) { return; }
		for (const ui_rml::UiAnnouncement& a : engine.uiHost().takeAnnouncements())
		{
			m_root.announce(a.text, a.assertive ? platform::UiaPoliteness::Assertive : platform::UiaPoliteness::Polite,
			                a.activity);
		}
#else
		(void)engine;
#endif
	}

private:
#ifdef _WIN32
	void attach(Win32Window& win, HWND hwnd)
	{
		// 窓が作り直された時は、古い窓の提供者を外してから新しい窓へ付ける
		wchar_t title[256] = {};
		::GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
		if (!m_root.attach(hwnd, title))
		{
			m_failedWindow = hwnd;   // UI Automation が使えない OS で、毎フレーム作り直そうとしない
			return;
		}
		win.setAccessibilityHandler([this](HWND h, WPARAM w, LPARAM l) { return m_root.handleGetObject(h, w, l); });
	}

	platform::UiaRoot m_root;
	HWND m_failedWindow = nullptr;
#endif
};

}  // namespace mitiru::host
