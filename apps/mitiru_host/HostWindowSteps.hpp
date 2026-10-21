#pragma once

/// @file HostWindowSteps.hpp
/// @brief --window-shot の最中に、窓の寸法・最小化・全画面を台本どおりに変え、そのたびの絵を撮る
/// @details 環境変数 MITIRU_WINDOW_SHOT_STEPS に「resize:960x540,minimize,restore,fullscreen,windowed」のように並べる。
///          1 つ目を 20 フレーム目に行い、以後 30 フレームおき。絵は --window-shot で渡した PNG の名前に
///          _<番号>_<手順>.png を付けて、行ってから 8 フレーム後に撮る (minimize は撮らず、窓の状態だけ記録する)。
///          窓は --window-shot と同じく前に出さない。人の画面に触れない手順だけを持つ。

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/platform/win32/WindowShot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

namespace mitiru::host
{

class HostWindowSteps
{
public:
	explicit HostWindowSteps(const std::string& shotPath)
	{
		const char* env = std::getenv("MITIRU_WINDOW_SHOT_STEPS");
		if (env == nullptr || env[0] == '\0') { return; }
		m_stem = std::filesystem::path(shotPath).replace_extension().string();
		const std::string text = env;
		std::size_t from = 0;
		while (from <= text.size())
		{
			const std::size_t comma = text.find(',', from);
			const std::string item = text.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
			if (!item.empty()) { m_steps.push_back(item); }
			if (comma == std::string::npos) { break; }
			from = comma + 1;
		}
	}

	[[nodiscard]] bool active() const noexcept { return !m_steps.empty(); }

	/// @brief 台本の最後の手順の絵まで撮り終えるフレーム番号。--max-frames が足りないとき、呼び手が伸ばす
	[[nodiscard]] int framesNeeded() const noexcept
	{
		return m_steps.empty() ? 0 : kFirstFrame + kInterval * static_cast<int>(m_steps.size()) + kShotDelay + 2;
	}

	/// @brief 失敗した手順の数 (絵が撮れなかった、最小化から戻らなかったなど)
	[[nodiscard]] int failures() const noexcept { return m_failures; }

	void onFrame(Engine& engine, int totalFrame)
	{
#ifdef _WIN32
		if (m_steps.empty()) { return; }
		const int rel = totalFrame - kFirstFrame;
		if (rel < 0) { return; }
		const int index = rel / kInterval;
		if (index >= static_cast<int>(m_steps.size())) { return; }
		const int phase = rel % kInterval;
		const std::string& step = m_steps[static_cast<std::size_t>(index)];
		if (phase == 0) { perform(engine, step); }
		else if (phase == kShotDelay) { verify(engine, index, step); }
#else
		(void)engine; (void)totalFrame;
#endif
	}

private:
	static constexpr int kFirstFrame = 20;
	static constexpr int kInterval = 30;
	static constexpr int kShotDelay = 8;

#ifdef _WIN32
	[[nodiscard]] static HWND hwndOf(Engine& engine)
	{
		IWindow* win = engine.window();
		return win == nullptr ? nullptr : reinterpret_cast<HWND>(win->nativeHandle());
	}

	void perform(Engine& engine, const std::string& step)
	{
		const HWND hwnd = hwndOf(engine);
		if (hwnd == nullptr) { return; }
		if (step.rfind("resize:", 0) == 0)
		{
			int w = 0, h = 0;
			if (std::sscanf(step.c_str() + 7, "%dx%d", &w, &h) == 2) { engine.resizeWindowClient(w, h); }
		}
		else if (step == "minimize") { ShowWindow(hwnd, SW_SHOWMINNOACTIVE); }
		else if (step == "restore") { ShowWindow(hwnd, SW_SHOWNOACTIVATE); }
		else if (step == "fullscreen") { engine.setFullscreen(true); }
		else if (step == "windowed") { engine.setFullscreen(false); }
		else { console::noticef("MITIRU_WINDOW_SHOT_STEPS の %s は知らない手順です", step.c_str()); }
		console::noticef("[窓の手順] %s", step.c_str());
	}

	void verify(Engine& engine, int index, const std::string& step)
	{
		const HWND hwnd = hwndOf(engine);
		if (hwnd == nullptr) { return; }
		if (step == "minimize")
		{
			const bool iconic = IsIconic(hwnd) != FALSE;
			console::noticef("[窓の手順] minimize 後: 最小化=%s", iconic ? "はい" : "いいえ");
			if (!iconic) { ++m_failures; }
			return;
		}
		RECT rc{};
		GetClientRect(hwnd, &rc);
		const platform::WindowShot shot = platform::captureWindowClient(hwnd);
		const std::string path = m_stem + "_" + std::to_string(index + 1) + "_" + sanitize(step) + ".png";
		const bool saved = !shot.empty() && render::savePixelsToPng(shot.rgba.data(), shot.width, shot.height, path);
		console::noticef("[窓の手順] %s 後: 窓の中 %ldx%ld、エンジンの窓 %dx%d、最小化=%s、絵=%s", step.c_str(),
		                 rc.right - rc.left, rc.bottom - rc.top, engine.window()->width(), engine.window()->height(),
		                 IsIconic(hwnd) != FALSE ? "はい" : "いいえ", saved ? path.c_str() : "撮れず");
		if (!saved || (step == "restore" && IsIconic(hwnd) != FALSE)) { ++m_failures; }
	}

	[[nodiscard]] static std::string sanitize(std::string s)
	{
		for (char& c : s) { if (c == ':') { c = '_'; } }
		return s;
	}
#endif

	std::string m_stem;
	std::vector<std::string> m_steps;
	int m_failures = 0;
};

}  // namespace mitiru::host
