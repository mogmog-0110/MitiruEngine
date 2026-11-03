#pragma once

/// @file HostAccessibility.hpp
/// @brief 読み上げソフト (Narrator など) へ UI を見せる host の仕事。
/// @details 窓に UI Automation の提供者を付ける。提供者は窓に 1 つしか付けられないので、次のどちらかを選ぶ (ADR 0117)。
///          - AccessKit (MITIRU_WITH_ACCESSKIT で組み、accesskit.dll が読めるとき): RML の木 (role、name、フォーカス、操作) を見せる。
///            data-a11y の通知は、その要素を live region にして木の中で伝える。
///          - 段階 1 (UiaRoot): 木は持たず、data-a11y を付けた要素の文を UiaRaiseNotificationEvent で渡す。
///          どちらも、読み上げソフトが動いている間だけ文書をたどる。ゲームの DLL には何も見せず、出力は録画にも再生にも入らない。
///          読み上げソフトからの操作 (押す、フォーカス、値の変更) は UI の event として届き、InputSnapshot には載らない。
///          窓の無い実行 (headless、台本、撮影) では何もしない。

#include <string>

#include <mitiru/core/Engine.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/AccessKitBridge.hpp>
#include <mitiru/platform/win32/OsAccessibility.hpp>
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
		if (hwnd != m_hwnd && hwnd != m_failedWindow) { attach(*win, hwnd); }
		if (m_mode == Mode::None) { return; }

		const bool wanted = readerWanted();
		if (m_mode == Mode::AccessKit) { onFrameAccessKit(engine, wanted); }
		else { onFrameNotifications(engine, wanted); }
#else
		(void)engine;
#endif
	}

private:
#ifdef _WIN32
	enum class Mode { None, Notifications, AccessKit };

	// 読み上げソフトが動いているか。OS の設定と UIA の客の有無を、毎フレームでなく 15 フレームに 1 回だけ問い合わせる
	bool readerWanted()
	{
		if (m_pollCountdown-- <= 0)
		{
			m_pollCountdown = 14;
			m_wanted = platform::readScreenReaderOn() || platform::UiaRoot::clientsListening();
			wchar_t title[256] = {};
			::GetWindowTextW(m_hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, title, -1, nullptr, 0, nullptr, nullptr);
			m_title.assign(n > 1 ? static_cast<std::size_t>(n - 1) : 0, '\0');
			if (n > 1) { ::WideCharToMultiByte(CP_UTF8, 0, title, -1, m_title.data(), n, nullptr, nullptr); }
		}
		return m_wanted;
	}

	void onFrameNotifications(Engine& engine, bool listening)
	{
		engine.uiHost().setAnnouncementsEnabled(listening);
		if (!listening) { return; }
		for (const ui_rml::UiAnnouncement& a : engine.uiHost().takeAnnouncements())
		{
			m_root.announce(a.text, a.assertive ? platform::UiaPoliteness::Assertive : platform::UiaPoliteness::Polite,
			                a.activity);
		}
	}

	void onFrameAccessKit([[maybe_unused]] Engine& engine, [[maybe_unused]] bool wanted)
	{
#ifdef MITIRU_HAS_ACCESSKIT
		ui_rml::RmlUiHost& ui = engine.uiHost();
		ui.setAnnouncementsEnabled(false);   // 通知は木の live region が運ぶ
		if (!wanted)   // 読み上げソフトが止まったら、一度つながっていても文書をたどるのをやめる
		{
			if (m_treeSent) { ui.resetAccessTree(); m_treeSent = false; }
			return;
		}
		const bool foreground = ::GetForegroundWindow() == m_hwnd;
		if (foreground != m_foreground)
		{
			m_foreground = foreground;
			m_kit.setWindowFocused(foreground);
		}
		RECT client{};
		if (::GetClientRect(m_hwnd, &client)) { m_kit.setClientSize(client.right - client.left, client.bottom - client.top); }
		const std::vector<ui_rml::AccessRequest> requests = m_kit.takeRequests();
		if (!requests.empty()) { (void)ui.performAccess(requests); }
		if (auto tree = ui.takeAccessTree(m_title))
		{
			(void)m_kit.push(*tree);
			m_treeSent = true;
		}
#endif
	}

	void attach(Win32Window& win, HWND hwnd)
	{
		// 窓が作り直された時は、古い窓の提供者を外してから新しい窓へ付ける
		m_root.detach();
		m_mode = Mode::None;
#ifdef MITIRU_HAS_ACCESSKIT
		m_kit.detach();
		m_treeSent = false;
		m_foreground = ::GetForegroundWindow() == hwnd;
		if (m_kit.attach(hwnd, m_foreground))
		{
			m_hwnd = hwnd;
			m_mode = Mode::AccessKit;
			win.setAccessibilityHandler([this](HWND h, WPARAM w, LPARAM l) { return m_kit.handleGetObject(h, w, l); });
			return;
		}
#endif
		wchar_t title[256] = {};
		::GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
		if (!m_root.attach(hwnd, title))
		{
			m_failedWindow = hwnd;   // UI Automation が使えない OS で、毎フレーム作り直そうとしない
			return;
		}
		m_hwnd = hwnd;
		m_mode = Mode::Notifications;
		win.setAccessibilityHandler([this](HWND h, WPARAM w, LPARAM l) { return m_root.handleGetObject(h, w, l); });
	}

	platform::UiaRoot m_root;
#ifdef MITIRU_HAS_ACCESSKIT
	platform::AccessKitBridge m_kit;
#endif
	Mode m_mode = Mode::None;
	HWND m_hwnd = nullptr;
	HWND m_failedWindow = nullptr;
	int m_pollCountdown = 0;
	bool m_wanted = false;
	bool m_foreground = false;
	bool m_treeSent = false;
	std::string m_title;
#endif
};

}  // namespace mitiru::host
