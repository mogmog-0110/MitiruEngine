#pragma once

/// @file InputScript.hpp
/// @brief --input-script が読む決定論的入力台本のパーサ (G1)。
/// @details フレーム番号指定 (`<frame> <down|up> <KEY>` / `<frame> move <dx> <dy> [frames]`)
///          に加え、秒指定 (`t=<sec> down|up|press <KEY>` / `t=<sec> move <dx> <dy> [dur_sec]`)
///          を読める。秒指定は `--capture-dir` 撮影中に描画が疎になっても
///          「その時刻に何が起きるべきか」を表せる（フレーム番号は描画回数に依存するため
///          撮影の重さでゲーム内時刻とずれる。詳細は core/Config.hpp の captureActive）。

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace mitiru::input
{

/// @brief 台本上の 1 イベント。frame は読み込み時に (秒指定なら) 確定済み。
struct InputScriptEvent
{
	enum Kind { Key, MouseBtn, MouseMove };
	int  frame = 0;
	Kind kind = Key;
	int  a = 0;  ///< Key: vk / MouseBtn: 0=L 1=R 2=M / MouseMove: dx
	int  b = 0;  ///< Key・MouseBtn: down=1 up=0 / MouseMove: dy
};

/// @brief キー名 (英数字1文字 / LEFT・SPACE 等の名前 / 数値) を仮想キーコードへ変換する。
/// @return 不明な名前は -1
[[nodiscard]] inline int keyNameToVk(const std::string& name) noexcept
{
	if (name.empty()) { return -1; }
	if (name.size() == 1)
	{
		char c = name[0];
		if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		{
			return static_cast<int>(static_cast<unsigned char>(c));
		}
	}
	std::string u = name;
	for (auto& c : u) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
	if (u == "LEFT")  { return 0x25; }
	if (u == "UP")    { return 0x26; }
	if (u == "RIGHT") { return 0x27; }
	if (u == "DOWN")  { return 0x28; }
	if (u == "SPACE") { return 0x20; }
	if (u == "ENTER" || u == "RETURN") { return 0x0D; }
	if (u == "ESCAPE" || u == "ESC")   { return 0x1B; }
	if (u == "SHIFT") { return 0x10; }
	if (u == "TAB")   { return 0x09; }
	if (u == "CTRL" || u == "CONTROL") { return 0x11; }
	if (u == "ALT")   { return 0x12; }
	if (u == "BACK" || u == "BACKSPACE") { return 0x08; }
	try { return std::stoi(name, nullptr, 0); } catch (...) { return -1; }
}

/// @brief 台本を毎フレーム適用し InputSnapshot 相当のキー・マウス状態を上書きするプレイヤ。
/// @details テンプレート化して mitiru::module::InputSnapshot を直接持たず、
///          `apply(snap)` は snap の `keysDown/keysJustPressed/keysJustReleased[256]`・
///          `mouseButtonsDown/JustPressed/JustReleased[3]`・`mouseDeltaX/Y` を触る
///          任意の型を受ける (host 側の InputSnapshot と mock テストの両方から使える)。
struct InputScriptPlayer
{
	std::vector<InputScriptEvent> events;  ///< frame 昇順
	std::size_t cursor = 0;
	int frame = 0;
	bool held[256] = {};
	bool heldBtn[3] = {};

	template <typename Snapshot>
	void apply(Snapshot& snap)
	{
		bool prev[256];
		bool prevBtn[3];
		std::memcpy(prev, held, sizeof(prev));
		std::memcpy(prevBtn, heldBtn, sizeof(prevBtn));
		float moveX = 0.0f, moveY = 0.0f;
		while (cursor < events.size() && events[cursor].frame <= frame)
		{
			const auto& e = events[cursor++];
			switch (e.kind)
			{
			case InputScriptEvent::Key:
				if (e.a >= 0 && e.a < 256) { held[e.a] = (e.b != 0); }
				break;
			case InputScriptEvent::MouseBtn:
				if (e.a >= 0 && e.a < 3) { heldBtn[e.a] = (e.b != 0); }
				break;
			case InputScriptEvent::MouseMove:
				moveX += static_cast<float>(e.a);
				moveY += static_cast<float>(e.b);
				break;
			}
		}
		for (int v = 0; v < 256; ++v)
		{
			snap.keysDown[v]         = held[v] ? 1 : 0;
			snap.keysJustPressed[v]  = (held[v] && !prev[v]) ? 1 : 0;
			snap.keysJustReleased[v] = (!held[v] && prev[v]) ? 1 : 0;
		}
		for (int i = 0; i < 3; ++i)
		{
			snap.mouseButtonsDown[i]         = heldBtn[i] ? 1 : 0;
			snap.mouseButtonsJustPressed[i]  = (heldBtn[i] && !prevBtn[i]) ? 1 : 0;
			snap.mouseButtonsJustReleased[i] = (!heldBtn[i] && prevBtn[i]) ? 1 : 0;
		}
		snap.mouseDeltaX = moveX;
		snap.mouseDeltaY = moveY;
		++frame;
	}
};

namespace detail
{
	[[nodiscard]] inline std::string toLower(std::string s)
	{
		for (auto& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		return s;
	}

	[[nodiscard]] inline bool isActWord(const std::string& s)
	{
		return s == "down" || s == "d" || s == "DOWN" || s == "up" || s == "u" || s == "UP";
	}

	/// @brief 秒 → フレーム番号 (targetTps 換算、四捨五入)。
	[[nodiscard]] inline int secToFrame(double sec, double targetTps) noexcept
	{
		return static_cast<int>(std::lround(sec * targetTps));
	}
}  // namespace detail

/// @brief '<frame> <down|up> <KEY>' / '<frame> move <dx> <dy> [frames]' に加え、
///        't=<sec> down|up|press <KEY>' / 't=<sec> move <dx> <dy> [dur_sec]' を読む
///        (# 以降はコメント)。press は 1 フレームだけ押して離すタップ。
///        KEY には MouseL / MouseR / MouseM も使える。
/// @param targetTps 秒指定行のフレーム換算に使う TPS (通常 EngineConfig::targetTps と同値)
/// @return 失敗時 false (ファイルが開けない)
[[nodiscard]] inline bool loadInputScript(const std::string& path, InputScriptPlayer& out,
	double targetTps = 60.0)
{
	std::ifstream f(path);
	if (!f) { return false; }
	std::string line;
	while (std::getline(f, line))
	{
		const auto h = line.find('#');
		if (h != std::string::npos) { line = line.substr(0, h); }
		std::istringstream is(line);
		std::string t1;
		if (!(is >> t1)) { continue; }

		bool isSeconds = false;
		int frame = 0;
		if (t1.rfind("t=", 0) == 0)
		{
			try { frame = detail::secToFrame(std::stod(t1.substr(2)), targetTps); }
			catch (...) { continue; }
			isSeconds = true;
		}
		else
		{
			try { frame = std::stoi(t1); } catch (...) { continue; }
		}

		std::string t2, t3;
		if (!(is >> t2 >> t3)) { continue; }
		const std::string t2lower = detail::toLower(t2);

		if (t2lower == "move")
		{
			int dx = 0, dy = 0;
			try { dx = std::stoi(t3); } catch (...) { continue; }
			if (!(is >> dy)) { continue; }
			int count = 1;
			if (isSeconds)
			{
				double durSec = 0.0;
				if (is >> durSec) { count = std::max(1, detail::secToFrame(durSec, targetTps)); }
			}
			else if (int c; is >> c) { count = std::max(1, c); }
			for (int k = 0; k < count; ++k)
			{
				out.events.push_back({frame + k, InputScriptEvent::MouseMove, dx, dy});
			}
			continue;
		}

		// press (秒指定のみ): その場でタップ (down → 次フレーム up)。
		if (isSeconds && t2lower == "press")
		{
			const std::string mk = detail::toLower(t3);
			if (mk == "mousel" || mk == "mouser" || mk == "mousem")
			{
				const int idx = (mk == "mousel") ? 0 : (mk == "mouser") ? 1 : 2;
				out.events.push_back({frame,     InputScriptEvent::MouseBtn, idx, 1});
				out.events.push_back({frame + 1, InputScriptEvent::MouseBtn, idx, 0});
				continue;
			}
			const int vk = keyNameToVk(t3);
			if (vk < 0) { continue; }
			out.events.push_back({frame,     InputScriptEvent::Key, vk, 1});
			out.events.push_back({frame + 1, InputScriptEvent::Key, vk, 0});
			continue;
		}

		// 両形式を許す: "<frame> <down|up> <KEY>" と "<frame> <KEY> <down|up>"。
		std::string act, key;
		if (detail::isActWord(t2)) { act = t2; key = t3; }
		else                       { key = t2; act = t3; }
		const bool down = (act == "down" || act == "d" || act == "DOWN");
		const bool up   = (act == "up" || act == "u" || act == "UP");
		if (!down && !up) { continue; }
		const std::string mk = detail::toLower(key);
		if (mk == "mousel" || mk == "mouser" || mk == "mousem")
		{
			const int idx = (mk == "mousel") ? 0 : (mk == "mouser") ? 1 : 2;
			out.events.push_back({frame, InputScriptEvent::MouseBtn, idx, down ? 1 : 0});
			continue;
		}
		const int vk = keyNameToVk(key);
		if (vk < 0) { continue; }
		out.events.push_back({frame, InputScriptEvent::Key, vk, down ? 1 : 0});
	}
	std::stable_sort(out.events.begin(), out.events.end(),
		[](const InputScriptEvent& a, const InputScriptEvent& b) { return a.frame < b.frame; });
	return true;
}

}  // namespace mitiru::input
