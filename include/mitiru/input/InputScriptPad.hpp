#pragma once

/// @file InputScriptPad.hpp
/// @brief --input-script のパッド行。`<frame> pad[N] <BUTTON> down|up`、`<frame> axis[N] <AXIS> <値>`、
///        `<frame> gyro[N] x y z`、`<frame> accel[N] x y z`、`<frame> touch[N] <指 1|2> <x> <y>|up`
/// @details 実機もドライバも通さず、InputSnapshot のパッド欄 (台ごとの gamepads[4] と gamepadsExt[4]、全台を合成した
///          gamepad*) を台本で埋める。パッドを持っていない人と AI が同じ入力を再現できるようにするため。
///          N は 1〜4 (省略は 1)。台本に一度でも出てきたパッドは、frame 0 から接続済みとして扱う
///          (起動前に挿してあったパッドと同じ見え方にする)。軸・ジャイロ・加速度・指の位置は次に書くまで保つ。
///          合成の規則は host と同じ (ボタンは OR、軸は接続している最初の 1 台)。
///          タッチパッドの押し込みは `pad TOUCHPAD down|up` で書く。

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace mitiru::input
{

/// @brief PadScriptEvent::ext の値 (0 はボタンか軸)
inline constexpr std::uint8_t kPadScriptGyro     = 1;
inline constexpr std::uint8_t kPadScriptAccel    = 2;
inline constexpr std::uint8_t kPadScriptTouch    = 3;   ///< value 1 = 置く (v = x, y)、0 = 離す
inline constexpr std::uint8_t kPadScriptTouchpad = 4;   ///< タッチパッドの押し込み。value 1 = down

struct PadScriptEvent
{
	int           frame = 0;
	int           pad = 0;       ///< 0..3
	bool          isAxis = false;
	std::uint32_t button = 0;    ///< ModuleApi の gamepad:: ビット (isAxis = false)
	int           axis = 0;      ///< gamepad::Axis の添字 (isAxis = true) / 指の番号 (kPadScriptTouch)
	float         value = 0.0f;  ///< isAxis: 軸の値 / ボタン: 1 = down, 0 = up
	std::uint8_t  ext = 0;       ///< kPadScript* (0 = ボタンか軸)
	float         v[3] = {};     ///< ジャイロ (rad/s)・加速度 (m/s^2)・指の x, y (0..1)
};

namespace detail
{
	[[nodiscard]] inline std::string upperAscii(std::string s)
	{
		for (auto& c : s) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
		return s;
	}

	/// "pad" / "pad2" のような語から 0 始まりの台番号を返す。prefix で始まらない・番号が範囲外なら -1。
	[[nodiscard]] inline int padIndexOf(const std::string& word, const char* prefix)
	{
		const std::string w = upperAscii(word);
		const std::string p = upperAscii(prefix);
		if (w.rfind(p, 0) != 0) { return -1; }
		const std::string num = w.substr(p.size());
		if (num.empty()) { return 0; }
		if (num.size() != 1 || num[0] < '1' || num[0] > '4') { return -1; }
		return num[0] - '1';
	}

	[[nodiscard]] inline bool parseFloat(const std::string& s, float& out)
	{
		try { std::size_t used = 0; out = std::stof(s, &used); return used == s.size(); }
		catch (...) { return false; }
	}
}  // namespace detail

/// @brief ボタン名 → gamepad:: ビット (A B X Y LB RB Start Back LS RS Up Down Left Right)。不明なら 0
[[nodiscard]] inline std::uint32_t padButtonBit(const std::string& name)
{
	struct Entry { const char* name; std::uint32_t bit; };
	static constexpr Entry kTable[] = {
		{"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008},
		{"START", 0x0010}, {"BACK", 0x0020}, {"LS", 0x0040}, {"RS", 0x0080},
		{"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000},
	};
	const std::string u = detail::upperAscii(name);
	for (const auto& e : kTable)
	{
		if (u == e.name) { return e.bit; }
	}
	return 0;
}

/// @brief 軸名 → gamepad::Axis の添字 (LX LY RX RY LT RT)。不明なら -1
[[nodiscard]] inline int padAxisIndex(const std::string& name)
{
	static constexpr const char* kNames[] = {"LX", "LY", "RX", "RY", "LT", "RT"};
	const std::string u = detail::upperAscii(name);
	for (int i = 0; i < 6; ++i)
	{
		if (u == kNames[i]) { return i; }
	}
	return -1;
}

namespace detail
{
	inline bool parseButtonLine(int frame, int pad, const std::string& arg, std::istream& rest,
	                            std::vector<PadScriptEvent>& out)
	{
		std::string act;
		const bool touchpad = upperAscii(arg) == "TOUCHPAD";
		const std::uint32_t bit = padButtonBit(arg);
		if ((bit == 0 && !touchpad) || !(rest >> act)) { return false; }
		const std::string a = upperAscii(act);
		if (a != "DOWN" && a != "UP") { return false; }
		PadScriptEvent e{frame, pad, false, bit, 0, a == "DOWN" ? 1.0f : 0.0f};
		e.ext = touchpad ? kPadScriptTouchpad : std::uint8_t{0};
		out.push_back(e);
		return true;
	}

	inline bool parseVectorLine(int frame, int pad, std::uint8_t ext, const std::string& arg, std::istream& rest,
	                            std::vector<PadScriptEvent>& out)
	{
		PadScriptEvent e{frame, pad};
		e.ext = ext;
		if (!parseFloat(arg, e.v[0]) || !(rest >> e.v[1] >> e.v[2])) { return false; }
		out.push_back(e);
		return true;
	}

	inline bool parseTouchLine(int frame, int pad, const std::string& arg, std::istream& rest,
	                           std::vector<PadScriptEvent>& out)
	{
		if (arg != "1" && arg != "2") { return false; }
		PadScriptEvent e{frame, pad};
		e.ext = kPadScriptTouch;
		e.axis = arg[0] - '1';
		std::string x;
		if (!(rest >> x)) { return false; }
		if (upperAscii(x) == "UP") { out.push_back(e); return true; }
		if (!parseFloat(x, e.v[0]) || !(rest >> e.v[1])) { return false; }
		e.v[0] = std::clamp(e.v[0], 0.0f, 1.0f);
		e.v[1] = std::clamp(e.v[1], 0.0f, 1.0f);
		e.value = 1.0f;
		out.push_back(e);
		return true;
	}
}  // namespace detail

/// @brief verb が pad / axis / gyro / accel / touch の行を読む。読めたら out に足して true
/// @details それ以外の行は false を返し、呼び出し側はキーの行として読み続ける。
[[nodiscard]] inline bool parsePadScriptLine(int frame, const std::string& verb, const std::string& arg,
                                             std::istream& rest, std::vector<PadScriptEvent>& out)
{
	if (const int pad = detail::padIndexOf(verb, "pad"); pad >= 0) { return detail::parseButtonLine(frame, pad, arg, rest, out); }
	if (const int pad = detail::padIndexOf(verb, "axis"); pad >= 0)
	{
		float value = 0.0f;
		const int axis = padAxisIndex(arg);
		if (axis < 0 || !(rest >> value)) { return false; }
		const float lo = (axis >= 4) ? 0.0f : -1.0f;  // trigger は 0..1、stick は -1..1
		out.push_back({frame, pad, true, 0, axis, std::clamp(value, lo, 1.0f)});
		return true;
	}
	if (const int pad = detail::padIndexOf(verb, "gyro"); pad >= 0) { return detail::parseVectorLine(frame, pad, kPadScriptGyro, arg, rest, out); }
	if (const int pad = detail::padIndexOf(verb, "accel"); pad >= 0) { return detail::parseVectorLine(frame, pad, kPadScriptAccel, arg, rest, out); }
	if (const int pad = detail::padIndexOf(verb, "touch"); pad >= 0) { return detail::parseTouchLine(frame, pad, arg, rest, out); }
	return false;
}

/// @brief パッド行を毎フレーム InputSnapshot へ当てる
struct PadScriptPlayer
{
	struct Touch { bool down = false; float x = 0.0f; float y = 0.0f; };

	std::vector<PadScriptEvent> events;
	std::size_t   cursor = 0;
	bool          used[4] = {};
	std::uint32_t held[4] = {};
	float         axes[4][6] = {};
	bool          motion[4] = {};    ///< 台本に gyro / accel が出てきた台
	bool          touchUsed[4] = {};
	float         gyro[4][3] = {};
	float         accel[4][3] = {};
	Touch         touch[4][2] = {};
	bool          touchpadHeld[4] = {};

	/// @brief 読み込み後に 1 回呼ぶ (frame 順に並べ、出てきたパッドを接続済みにする)
	void finalize()
	{
		std::stable_sort(events.begin(), events.end(),
			[](const PadScriptEvent& a, const PadScriptEvent& b) { return a.frame < b.frame; });
		for (const auto& e : events)
		{
			used[e.pad] = true;
			motion[e.pad] = motion[e.pad] || e.ext == kPadScriptGyro || e.ext == kPadScriptAccel;
			touchUsed[e.pad] = touchUsed[e.pad] || e.ext == kPadScriptTouch || e.ext == kPadScriptTouchpad;
		}
	}

	[[nodiscard]] bool active() const noexcept { return !events.empty(); }

	template <typename Snapshot>
	void apply(int frame, Snapshot& snap)
	{
		std::uint32_t prev[4];
		bool prevTouchpad[4];
		std::copy(std::begin(held), std::end(held), prev);
		std::copy(std::begin(touchpadHeld), std::end(touchpadHeld), prevTouchpad);
		while (cursor < events.size() && events[cursor].frame <= frame) { step(events[cursor++]); }
		writePads(snap, prev);
		writePadsExt(snap, prevTouchpad);
		writeComposite(snap);
	}

private:
	void step(const PadScriptEvent& e)
	{
		auto& t = touch[e.pad][e.axis & 1];
		switch (e.ext)
		{
		case kPadScriptGyro:     std::copy(e.v, e.v + 3, gyro[e.pad]); break;
		case kPadScriptAccel:    std::copy(e.v, e.v + 3, accel[e.pad]); break;
		case kPadScriptTouch:    t = Touch{e.value != 0.0f, e.v[0], e.v[1]}; break;
		case kPadScriptTouchpad: touchpadHeld[e.pad] = e.value != 0.0f; break;
		default:
			if (e.isAxis) { axes[e.pad][e.axis] = e.value; }
			else if (e.value != 0.0f) { held[e.pad] |= e.button; }
			else { held[e.pad] &= ~e.button; }
			break;
		}
	}

	template <typename Snapshot>
	void writePads(Snapshot& snap, const std::uint32_t* prev) const
	{
		for (int p = 0; p < 4; ++p)
		{
			auto& g = snap.gamepads[p];
			g.connected           = used[p] ? 1 : 0;
			g.buttonsDown         = held[p];
			g.buttonsJustPressed  = held[p] & ~prev[p];
			g.buttonsJustReleased = prev[p] & ~held[p];
			for (int a = 0; a < 6; ++a) { g.axes[a] = axes[p][a]; }
		}
	}

	/// 拡張の欄も台本だけで決める (実機のジャイロや電池が混ざると再現しない)。機種は Standard、電池は不明
	template <typename Snapshot>
	void writePadsExt(Snapshot& snap, const bool* prevTouchpad) const
	{
		for (int p = 0; p < 4; ++p)
		{
			auto& e = snap.gamepadsExt[p];
			e = {};
			if (!used[p]) { continue; }
			e.kind = 1;
			e.battery = -1;
			e.caps = static_cast<std::uint8_t>((motion[p] ? 0x03 : 0) | (touchUsed[p] ? 0x04 : 0));
			e.motionActive = motion[p] ? 1 : 0;
			e.extraDown = touchpadHeld[p] ? 1 : 0;
			e.extraPressed = (touchpadHeld[p] && !prevTouchpad[p]) ? 1 : 0;
			e.extraReleased = (!touchpadHeld[p] && prevTouchpad[p]) ? 1 : 0;
			std::copy(gyro[p], gyro[p] + 3, e.gyro);
			std::copy(accel[p], accel[p] + 3, e.accel);
			for (int f = 0; f < 2; ++f)
			{
				e.touch[f].down = touch[p][f].down ? 1 : 0;
				e.touch[f].x = touch[p][f].x;
				e.touch[f].y = touch[p][f].y;
				e.touch[f].pressure = touch[p][f].down ? 1.0f : 0.0f;
			}
		}
	}

	template <typename Snapshot>
	void writeComposite(Snapshot& snap) const
	{
		snap.gamepadConnected = 0;
		snap.gamepadButtonsDown = snap.gamepadButtonsJustPressed = snap.gamepadButtonsJustReleased = 0;
		for (int a = 0; a < 6; ++a) { snap.gamepadAxes[a] = 0.0f; }
		for (int p = 0; p < 4; ++p)
		{
			const auto& g = snap.gamepads[p];
			if (g.connected == 0) { continue; }
			if (snap.gamepadConnected == 0)
			{
				for (int a = 0; a < 6; ++a) { snap.gamepadAxes[a] = g.axes[a]; }
			}
			snap.gamepadConnected = 1;
			snap.gamepadButtonsDown         |= g.buttonsDown;
			snap.gamepadButtonsJustPressed  |= g.buttonsJustPressed;
			snap.gamepadButtonsJustReleased |= g.buttonsJustReleased;
		}
	}
};

}  // namespace mitiru::input
