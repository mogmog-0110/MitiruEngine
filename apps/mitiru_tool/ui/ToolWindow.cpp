#include "ToolWindow.hpp"

#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/observe/DockChannel.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>

namespace mitiru::tool
{

namespace
{

bool hostAlive(int pid)
{
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (h == nullptr)
	{
		// 開けない理由が access-denied なら (プロセスは在る) 生きているとみなす。
		return GetLastError() == ERROR_ACCESS_DENIED;
	}
	const DWORD r = WaitForSingleObject(h, 0);
	CloseHandle(h);
	return r == WAIT_TIMEOUT;
}

float dpOf(const Win32Window& window)
{
	const UINT dpi = GetDpiForWindow(window.getHandle());
	return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

int scaled(int v, float dp)
{
	return static_cast<int>(std::lround(static_cast<float>(v) * dp));
}

/// ゲーム窓の矩形を読み、その下辺か右脇に自窓を合わせる。毎フレームゲーム窓の z-order の直後へ
/// 差し込むので、間に他の窓が割り込まない。
class Dock
{
public:
	Dock(std::optional<int> pid, int mode) : m_mode(pid ? mode : 0)
	{
		if (m_mode != 0) { m_reader.emplace(*pid); }
	}

	void update(Win32Window& window)
	{
		if (m_mode == 0) { return; }
		if (auto j = m_reader->poll())
		{
			m_hwnd = static_cast<std::uintptr_t>(j->value("hwnd", 0LL));
			m_x = j->value("x", 0); m_y = j->value("y", 0);
			m_w = j->value("w", 0); m_h = j->value("h", 0);
		}
		if (m_w <= 0 || m_h <= 0) { return; }
		int mx = 0, my = 0, mw = 0, mh = 0;
		window.getWindowRect(mx, my, mw, mh);
		if (m_mode == 2) { window.dockBelow(m_hwnd, m_x + m_w, m_y, mw > 0 ? mw : 400, mh > 0 ? mh : 620); }
		else             { window.dockBelow(m_hwnd, m_x, m_y + m_h, m_w, mh > 0 ? mh : 56); }
	}

	[[nodiscard]] HWND gameWindow() const noexcept { return reinterpret_cast<HWND>(m_hwnd); }

private:
	int m_mode;
	std::optional<observe::DockReader> m_reader;
	std::uintptr_t m_hwnd = 0;
	int m_x = 0, m_y = 0, m_w = 0, m_h = 0;
};

/// ドックした窓は WS_EX_NOACTIVATE なので、クリックされても OS はフォーカスを移さない。キーを受けたいページに
/// 頼まれた時だけ自分から前面へ出る。頼まれるのは人がこの窓を押した直後なので、前面を変えてよいのは最後の
/// 入力を受けたプロセスだけ、という OS の制限には掛からない。
class WindowKeyboard final : public ToolKeyboard
{
public:
	WindowKeyboard(const Win32Window& window, const Dock& dock) : m_window(window), m_dock(dock) {}

	void take() override { SetForegroundWindow(m_window.getHandle()); }

	void giveBack() override
	{
		const HWND game = m_dock.gameWindow();
		if (game != nullptr && IsWindow(game) && GetForegroundWindow() == m_window.getHandle()) { SetForegroundWindow(game); }
	}

private:
	const Win32Window& m_window;
	const Dock& m_dock;
};

// 窓はマウスを capture しないので、窓の外で離したボタンの WM_*BUTTONUP は届かず InputState は押したままになる。
// 指が本当に離れていたら離したことにして、ドラッグ (rewind のつまみ、scene_view の枠) を終わらせる。
bool stillHeld(int logicalVk)
{
	int vk = logicalVk;
	if (GetSystemMetrics(SM_SWAPBUTTON) != 0 && (vk == VK_LBUTTON || vk == VK_RBUTTON))
	{
		vk = vk == VK_LBUTTON ? VK_RBUTTON : VK_LBUTTON;   // GetAsyncKeyState は物理ボタンを見る
	}
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

ToolPointer pointerFrom(const InputState& input)
{
	ToolPointer p;
	const auto [x, y] = input.mousePosition();
	p.x = x;
	p.y = y;
	p.buttons[0] = input.isMouseButtonDown(MouseButton::Left) && stillHeld(VK_LBUTTON);
	p.buttons[1] = input.isMouseButtonDown(MouseButton::Right) && stillHeld(VK_RBUTTON);
	p.buttons[2] = input.isMouseButtonDown(MouseButton::Middle) && stillHeld(VK_MBUTTON);
	p.wheel = input.mouseWheelDelta();
	p.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	p.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	return p;
}

/// 窓 1 枚分の部品と、大きさ・DPI の変化の追従。
class ToolWindowLoop
{
public:
	ToolWindowLoop(const WindowSpec& spec, int posX, int posY, float dp0)
		: m_window(spec.title, scaled(spec.width, dp0), scaled(spec.height, dp0), DisplayMode::Windowed, true,
		           posX == INT_MIN ? CW_USEDEFAULT : posX, posY == INT_MIN ? CW_USEDEFAULT : posY)
		, m_device(&m_window)
	{
		m_window.setInputState(&m_input);
		m_window.setMinClientSize(scaled(spec.minWidth, dp0), scaled(spec.minHeight, dp0));
	}

	bool start(const ToolOptions& options, std::string& error, ToolKeyboard* keyboard)
	{
		const auto bg = pageBackground(options.page);
		m_device.setClearColor(bg[0], bg[1], bg[2], bg[3]);
		m_dp = dpOf(m_window);
		m_w = m_window.width();
		m_h = m_window.height();
		return m_session.start(m_device.nativeDevice(), m_device.commandQueue(), options, m_w, m_h, m_dp, error, keyboard);
	}

	[[nodiscard]] const Win32Window& window() const noexcept { return m_window; }

	/// @return false なら窓が閉じられた
	bool pump()
	{
		m_input.beginFrame();
		m_window.pollEvents();
		return !m_window.shouldClose();
	}

	void dock(Dock& d) { d.update(m_window); }

	/// @return 描いたら true (最小化中は描かない)
	bool frame(double now)
	{
		followSize();
		if (m_w <= 0 || m_h <= 0)
		{
			m_session.keepWatching();
			return false;
		}
		m_session.frame(now, pointerFrom(m_input), m_window.takeKeyMessages());
		m_device.beginFrame();
		if (auto* swap = m_device.getSwapChain())
		{
			m_session.render(swap->getBackBufferResource(swap->currentBackBufferIndex()), m_w, m_h);
		}
		m_device.endFrame();
		return true;
	}

	[[nodiscard]] bool documentLoaded() { return m_session.ui().document() != nullptr; }

private:
	void followSize()
	{
		const float dp = dpOf(m_window);
		if (m_window.width() == m_w && m_window.height() == m_h && dp == m_dp) { return; }
		m_w = m_window.width();
		m_h = m_window.height();
		m_dp = dp;
		if (m_w > 0 && m_h > 0) { m_device.onResize(m_w, m_h); }
		m_session.resize(m_w, m_h, m_dp);
	}

	InputState m_input;
	Win32Window m_window;
	gfx::Dx12Device m_device;
	ToolSession m_session;
	float m_dp = 1.0f;
	int m_w = 0;
	int m_h = 0;
};

} // namespace

WindowSpec windowSpecFor(const std::string& page)
{
	if (page == "rewind")     { return { 1280, 56, 480, 48, 1, "rewind" }; }
	if (page == "scene_view") { return { 900, 700, 480, 360, 0, "MitiruEngine \xE2\x80\x94 scene_view" }; }
	if (page == "frame_view") { return { 640, 820, 480, 480, 0, "MitiruEngine \xE2\x80\x94 frame_view" }; }
	if (page == "nav")        { return { 560, 760, 400, 420, 2, "MitiruEngine \xE2\x80\x94 nav" }; }
	if (page == "ai")         { return { 440, 760, 340, 420, 2, "MitiruEngine \xE2\x80\x94 ai" }; }
	WindowSpec spec;
	spec.dockMode = page == "why_view" ? 0 : 2;
	spec.title = "MitiruEngine \xE2\x80\x94 " + page;
	return spec;
}

std::array<float, 4> pageBackground(const std::string& page)
{
	if (page == "rewind") { return { 0.933f, 0.941f, 0.953f, 1.0f }; }   // #eef0f3
	return { 0.988f, 0.988f, 0.984f, 1.0f };                             // #fcfcfb (紙地)
}

WindowRun runToolWindow(const ToolOptions& options, const WindowSpec& spec, int posX, int posY, int maxFrames)
{
	WindowRun run;
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// ゲーム窓に付く窓は、開いた時もクリックされた時もゲームのフォーカスを奪わない。
	if (spec.dockMode != 0 && options.pid) { Win32Window::setProcessNoActivate(true); }
	ToolWindowLoop loop(spec, posX, posY, static_cast<float>(GetDpiForSystem()) / 96.0f);
	Dock dock(options.pid, spec.dockMode);
	WindowKeyboard keyboard(loop.window(), dock);
	if (!loop.start(options, run.error, &keyboard)) { return run; }
	const auto t0 = std::chrono::steady_clock::now();
	while (maxFrames <= 0 || run.frames < maxFrames)
	{
		if (!loop.pump() || (options.pid && !hostAlive(*options.pid))) { break; }
		loop.dock(dock);
		if (loop.frame(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count())) { ++run.frames; }
		else { Sleep(16); }
	}
	run.documentLoaded = loop.documentLoaded();
	return run;
}

} // namespace mitiru::tool
