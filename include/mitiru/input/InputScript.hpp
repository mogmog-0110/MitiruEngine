#pragma once

/// @file InputScript.hpp
/// @brief --input-script が読む決定論的入力台本のパーサとプレイヤ。書式は docs/INPUT_SCRIPT.md。
/// @details 読めない行は飛ばさず `ファイル:行` 付きの誤りにする。キー名を 1 字違えた台本が
///          何も押さないまま最後まで流れると、ゲーム側の不具合に見えて原因を追えないため。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <mitiru/core/detail/ModuleTextInput.hpp>
#include <mitiru/input/InputScriptPad.hpp>
#include <mitiru/input/KeyNames.hpp>

namespace mitiru::input
{

/// @brief 台本上の 1 イベント。frame は読み込み時に (秒指定なら) 確定済み。
struct InputScriptEvent
{
	enum Kind { Key, MouseBtn, MouseMove, MousePos, Wheel, Ime };
	int  frame = 0;
	Kind kind = Key;
	int  a = 0;  ///< Key: vk / MouseBtn: 0=L 1=R 2=M 3=X1 4=X2 / MouseMove: dx / MousePos: x / Wheel: 縦ノッチ / Ime: キャレット (byte)
	int  b = 0;  ///< Key・MouseBtn: down=1 up=0 / MouseMove: dy / MousePos: y / Wheel: 横ノッチ
	std::string text{};  ///< Ime: 変換中の文字列 (UTF-8、空 = 変換していない)
};

/// @brief 台本を毎フレーム適用し、snapshot のキー・マウス・パッド・IME を台本の値で上書きする。
/// @details 実機の入力は無視される (注入だけが有効なので決定的)。`apply` は host の InputSnapshot と、
///          キーとマウスの欄だけを持つテスト用の型の両方を受ける。
struct InputScriptPlayer
{
	std::vector<InputScriptEvent> events;  ///< frame 昇順
	std::size_t cursor = 0;
	int frame = 0;
	bool held[256] = {};
	bool heldBtn[5] = {};
	bool  hasPos = false;   ///< pos で置いた座標は次の pos まで保つ
	float posX = 0.0f, posY = 0.0f;
	platform::ImeCompositionUtf8 ime;  ///< 次の ime 行まで保つ
	PadScriptPlayer pad;               ///< 台本にパッドの行があれば、パッドの欄は台本だけで決まる

	template <typename Snapshot>
	void apply(Snapshot& snap)
	{
		if constexpr (requires { snap.gamepads; }) { if (pad.active()) { pad.apply(frame, snap); } }
		bool prev[256];
		bool prevBtn[5];
		std::memcpy(prev, held, sizeof(prev));
		std::memcpy(prevBtn, heldBtn, sizeof(prevBtn));
		Motion m;
		while (cursor < events.size() && events[cursor].frame <= frame) { step(events[cursor++], m); }
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
		snap.mouseDeltaX = m.dx;
		snap.mouseDeltaY = m.dy;
		if constexpr (std::is_same_v<Snapshot, module::InputSnapshot>) { writeExtras(snap, prevBtn, m); }
		++frame;
	}

private:
	struct Motion { float dx = 0.0f, dy = 0.0f, wheelY = 0.0f, wheelX = 0.0f; };

	void step(const InputScriptEvent& e, Motion& m)
	{
		switch (e.kind)
		{
		case InputScriptEvent::Key:       if (e.a >= 0 && e.a < 256) { held[e.a] = (e.b != 0); } break;
		case InputScriptEvent::MouseBtn:  if (e.a >= 0 && e.a < 5) { heldBtn[e.a] = (e.b != 0); } break;
		case InputScriptEvent::MouseMove: m.dx += static_cast<float>(e.a); m.dy += static_cast<float>(e.b); break;
		case InputScriptEvent::Wheel:     m.wheelY += static_cast<float>(e.a); m.wheelX += static_cast<float>(e.b); break;
		case InputScriptEvent::Ime:       ime = platform::ImeCompositionUtf8{e.text, static_cast<std::size_t>(e.a)}; break;
		case InputScriptEvent::MousePos:
		{
			// 実マウスと同じく、位置が動いた分は delta にも出す (delta で視点を回す game が pos でも反応する)。
			const float nx = static_cast<float>(e.a), ny = static_cast<float>(e.b);
			if (hasPos) { m.dx += nx - posX; m.dy += ny - posY; }
			posX = nx; posY = ny; hasPos = true;
			break;
		}
		}
	}

	void writeExtras(module::InputSnapshot& snap, const bool* prevBtn, const Motion& m) const
	{
		for (int i = 0; i < 2; ++i)
		{
			const bool now = heldBtn[3 + i], was = prevBtn[3 + i];
			snap.mouseXButtonsDown[i]         = now ? 1 : 0;
			snap.mouseXButtonsJustPressed[i]  = (now && !was) ? 1 : 0;
			snap.mouseXButtonsJustReleased[i] = (!now && was) ? 1 : 0;
		}
		snap.mouseWheel  = m.wheelY;
		snap.mouseWheelH = m.wheelX;
		mitiru::detail::fillSnapshotIme(ime, snap);
		if (hasPos) { snap.mouseX = posX; snap.mouseY = posY; }
	}
};

namespace detail
{
	[[nodiscard]] inline std::string toLower(std::string s)
	{
		for (auto& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		return s;
	}

	[[nodiscard]] inline int secToFrame(double sec, double targetTps) noexcept
	{
		return static_cast<int>(std::lround(sec * targetTps));
	}

	[[nodiscard]] inline bool parseInt(const std::string& s, int& out)
	{
		try { std::size_t used = 0; out = std::stoi(s, &used); return used == s.size(); }
		catch (...) { return false; }
	}

	/// 0=L 1=R 2=M 3=X1 4=X2、マウスのボタン名でなければ -1。
	[[nodiscard]] inline int mouseButtonIndex(const std::string& name)
	{
		static constexpr const char* kNames[] = {"mousel", "mouser", "mousem", "mousex1", "mousex2"};
		const std::string l = toLower(name);
		for (int i = 0; i < 5; ++i) { if (l == kNames[i]) { return i; } }
		return -1;
	}

	[[nodiscard]] inline bool isPadVerb(const std::string& verb)
	{
		for (const char* p : {"pad", "axis", "gyro", "accel", "touch"}) { if (padIndexOf(verb, p) >= 0) { return true; } }
		return false;
	}

	/// 1 行を読む途中の状態。pos の補間は「台本上の直前の pos」から始める (台本は frame の昇順に書く前提)。
	struct ScriptReader
	{
		InputScriptPlayer& out;
		double targetTps;
		bool hasPos = false;
		int posX = 0, posY = 0;
		std::string error;  ///< 空でなければ、その行は読めなかった

		bool fail(std::string msg) { error = std::move(msg); return false; }

		bool readFrame(const std::string& tok, int& frame, bool& seconds)
		{
			seconds = tok.rfind("t=", 0) == 0;
			if (!seconds) { return parseInt(tok, frame) && frame >= 0 ? true : fail("行の先頭はフレーム番号か t=<秒> にする: '" + tok + "'"); }
			try { frame = secToFrame(std::stod(tok.substr(2)), targetTps); return true; }
			catch (...) { return fail("秒が読めない: '" + tok + "'"); }
		}

		bool readMove(int frame, bool seconds, const std::string& t3, std::istream& is)
		{
			int dx = 0, dy = 0;
			if (!parseInt(t3, dx) || !(is >> dy)) { return fail("move <dx> <dy> [長さ] と書く"); }
			int count = 1;
			if (double len = 0.0; is >> len) { count = std::max(1, seconds ? secToFrame(len, targetTps) : static_cast<int>(len)); }
			for (int k = 0; k < count; ++k) { out.events.push_back({frame + k, InputScriptEvent::MouseMove, dx, dy}); }
			return true;
		}

		bool readPos(int frame, const std::string& t3, std::istream& is)
		{
			int x = 0, y = 0, count = 1;
			if (!parseInt(t3, x) || !(is >> y)) { return fail("pos <x> <y> [フレーム数] と書く"); }
			if (!(is >> count) || count < 1) { count = 1; }
			if (count == 1 || !hasPos) { out.events.push_back({frame + count - 1, InputScriptEvent::MousePos, x, y}); }
			for (int k = 1; count > 1 && hasPos && k <= count; ++k)
			{
				out.events.push_back({frame + k - 1, InputScriptEvent::MousePos,
					posX + (x - posX) * k / count, posY + (y - posY) * k / count});
			}
			posX = x; posY = y; hasPos = true;
			return true;
		}

		bool readKey(int frame, const std::string& key, int down, bool tap)
		{
			if (const int idx = mouseButtonIndex(key); idx >= 0)
			{
				out.events.push_back({frame, InputScriptEvent::MouseBtn, idx, down});
				if (tap) { out.events.push_back({frame + 1, InputScriptEvent::MouseBtn, idx, 0}); }
				return true;
			}
			if (ambiguousDigitName(key) >= 0) { return fail(ambiguousDigitMessage(key)); }
			const int vk = vkFromKeyName(key);
			if (vk < 0)
			{
				return fail("知らないキー名 '" + key + "'。近い名前は " + std::string(nearestKeyName(key))
					+ " (使える名前は docs/INPUT_SCRIPT.md)");
			}
			out.events.push_back({frame, InputScriptEvent::Key, vk, down});
			if (tap) { out.events.push_back({frame + 1, InputScriptEvent::Key, vk, 0}); }
			return true;
		}

		bool readLine(const std::string& raw);
	};

	inline bool ScriptReader::readLine(const std::string& raw)
	{
		std::istringstream is(raw.substr(0, raw.find('#')));
		std::string t1, t2, t3;
		if (!(is >> t1)) { return true; }
		int frame = 0;
		bool seconds = false;
		if (!readFrame(t1, frame, seconds)) { return false; }
		if (!(is >> t2 >> t3)) { return fail("フレームの後に 2 語以上要る (例: 10 Space down)"); }
		const std::string verb = toLower(t2);
		if (verb == "move")  { return readMove(frame, seconds, t3, is); }
		if (verb == "pos")   { return readPos(frame, t3, is); }
		if (verb == "press") { return readKey(frame, t3, 1, true); }
		if (verb == "wheel")
		{
			int notches = 0, horizontal = 0;
			if (!parseInt(t3, notches)) { return fail("wheel <縦> [横] と書く"); }
			if (!(is >> horizontal)) { horizontal = 0; }
			out.events.push_back({frame, InputScriptEvent::Wheel, notches, horizontal});
			return true;
		}
		if (verb == "ime")
		{
			// `-` は変換の終了 (確定・取り消し)。キャレットを省くと末尾。
			const std::string text = (t3 == "-") ? std::string{} : t3;
			int caret = static_cast<int>(text.size());
			if (int c = 0; is >> c) { caret = c; }
			out.events.push_back({frame, InputScriptEvent::Ime, caret, 0, text});
			return true;
		}
		if (isPadVerb(t2))
		{
			return parsePadScriptLine(frame, t2, t3, is, out.pad.events) ? true : fail("パッドの行が読めない: '" + raw + "'");
		}
		// "<frame> <down|up> <KEY>" と "<frame> <KEY> <down|up>" の両方を読む (--input-record の出力は後者)。
		const std::string l3 = toLower(t3);
		const bool actFirst = verb == "down" || verb == "up" || verb == "d" || verb == "u";
		const std::string& act = actFirst ? verb : l3;
		if (act != "down" && act != "up" && act != "d" && act != "u")
		{
			return fail("down / up / press / move / pos / wheel / ime / pad のどれかが要る: '" + raw + "'");
		}
		return readKey(frame, actFirst ? t3 : t2, (act == "down" || act == "d") ? 1 : 0, false);
	}
}  // namespace detail

/// @brief 台本を読む。読めない行が 1 つでもあれば false を返し、error に `ファイル:行: 理由` を入れる。
/// @param targetTps 秒指定行 (t=<sec>) のフレーム換算に使う TPS (通常 EngineConfig::targetTps と同値)
[[nodiscard]] inline bool loadInputScript(const std::string& path, InputScriptPlayer& out, std::string& error,
	double targetTps = 60.0)
{
	std::ifstream f(path);
	if (!f) { error = path + ": 開けない"; return false; }
	detail::ScriptReader reader{out, targetTps};
	std::string line;
	for (int lineNo = 1; std::getline(f, line); ++lineNo)
	{
		if (!line.empty() && line.back() == '\r') { line.pop_back(); }
		if (!reader.readLine(line))
		{
			error = path + ":" + std::to_string(lineNo) + ": " + reader.error;
			return false;
		}
	}
	std::stable_sort(out.events.begin(), out.events.end(),
		[](const InputScriptEvent& a, const InputScriptEvent& b) { return a.frame < b.frame; });
	out.pad.finalize();
	return true;
}

}  // namespace mitiru::input
