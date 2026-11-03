#pragma once
/// @file VirtualList.hpp
/// @brief 大量の行を持つ一覧を、見えている範囲の行だけ UI に送る。
///
/// UI (RmlUi の `data-for`) は渡された配列の数だけ要素を作るので、10 万行をそのまま送ると 10 万個の要素ができる。
/// この型は「どこから何行見せるか」を C++ が持ち、その範囲の行だけを JSON にして `hud.set` する。
/// ホイール・クリック・十字キーは UI と入力から受け、先頭の番号 (`first`) と選んでいる行 (`cursor`) を動かす。
/// 状態は int だけで、GameMemory にそのまま置ける (巻き戻しても一覧の位置が戻る)。使い方は docs/UI_VIRTUAL_LIST.md。
///
///   VirtualList list;                 // GameMemory のメンバー
///   list.setCount(items.size());
///   const int clicked = list.handle(in, "inv");
///   list.push(hud, "inv", [&](int i, mitiru::ui::RowWriter& row) { row.set("name", items[i].name); });

#include <mitiru/module/Game.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace mitiru::ui
{

/// 1 行分の JSON オブジェクトにフィールドを足す。文字列の `"` と `\` はエスケープする。
class RowWriter
{
public:
	explicit RowWriter(std::string& out) : out_(out) {}

	RowWriter& set(const char* key, int v)
	{
		char buf[24];
		std::snprintf(buf, sizeof(buf), "%d", v);
		return raw(key, buf);
	}
	RowWriter& set(const char* key, double v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.4g", v);
		return raw(key, buf);
	}
	RowWriter& set(const char* key, bool v) { return raw(key, v ? "true" : "false"); }
	RowWriter& set(const char* key, const char* v)
	{
		std::string quoted = "\"";
		for (const char* p = v != nullptr ? v : ""; *p != '\0'; ++p)
		{
			const unsigned char c = static_cast<unsigned char>(*p);
			if (c == '"' || c == '\\') { quoted += '\\'; quoted += static_cast<char>(c); }
			else if (c == '\n') { quoted += "\\n"; }
			else if (c < 0x20) { quoted += ' '; }
			else { quoted += static_cast<char>(c); }
		}
		quoted += '"';
		return raw(key, quoted.c_str());
	}
	RowWriter& set(const char* key, const std::string& v) { return set(key, v.c_str()); }

private:
	RowWriter& raw(const char* key, const char* value)
	{
		out_ += ",\"";
		out_ += key;
		out_ += "\":";
		out_ += value;
		return *this;
	}

	std::string& out_;
};

struct VirtualList
{
	/// `hud.set` 1 回の上限は 3900 バイト。`statePush` の文字列は 3968 バイトまでで、超える行は送らない。
	static constexpr std::size_t kMaxJsonBytes = 3900;
	static constexpr int kWheelRows = 3;      ///< ホイール 1 目盛りで動く行数
	static constexpr int kRepeatDelay = 20;   ///< 十字キーを押し続けて繰り返しが始まるまで (フレーム)
	static constexpr int kRepeatEvery = 4;

	int count = 0;    ///< 全部の行数
	int rows = 8;     ///< 一度に見せる行数 (UI の枠の高さと合わせる)
	int first = 0;    ///< 先頭に見えている行の番号
	int cursor = 0;   ///< 選んでいる行の番号
	int hold = 0;     ///< 十字キーの上下を押し続けているフレーム数

	void setCount(int n) noexcept
	{
		count = std::max(0, n);
		clamp();
	}

	/// 選んだ行が見える範囲から外れたときは、先頭を動かして範囲内に戻す。
	void moveCursor(int delta) noexcept
	{
		if (count <= 0) { return; }
		rows = visibleRows();
		cursor = std::clamp(cursor + delta, 0, count - 1);
		if (cursor < first) { first = cursor; }
		if (cursor >= first + rows) { first = cursor - rows + 1; }
		clamp();
	}

	void select(int index) noexcept
	{
		if (count <= 0) { return; }
		moveCursor(std::clamp(index, 0, count - 1) - cursor);
	}

	/// 先頭だけを動かす。選んだ行は動かないため、見える範囲から外れることがある。
	void scrollBy(int delta) noexcept
	{
		first += delta;
		clamp();
	}

	/// 見えている行の番号の範囲 [begin, end)
	[[nodiscard]] int begin() const noexcept { return first; }
	[[nodiscard]] int end() const noexcept { return std::min(count, first + visibleRows()); }

	/// クリックされた行の番号を返す。クリックがなければ -1 を返す。
	/// UI の `<name>.wheel` と `<name>.pick`、十字キーの上下、PageUp、PageDown、LB、RB、Home、End を受ける。
	/// `<name>.pick` の payload は `{"i":番号}`。十字キーはパッドとキーに対応し、押し続けると繰り返す。
	template <class In>
	int handle(const In& in, const char* name) noexcept
	{
		char wheelName[96];
		char pickName[96];
		std::snprintf(wheelName, sizeof(wheelName), "%s.wheel", name);
		std::snprintf(pickName, sizeof(pickName), "%s.pick", name);

		if (in.action(wheelName))
		{
			const char* payload = in.actionPayload(wheelName);
			const double notches = payload != nullptr ? std::atof(payload) : 0.0;
			scrollBy(static_cast<int>(std::lround(notches * kWheelRows)));
		}
		int clicked = -1;
		if (in.action(pickName))
		{
			clicked = in.actionPayloadInt(pickName, "i", -1);
			if (clicked >= 0 && clicked < count) { select(clicked); }
			else { clicked = -1; }
		}
		stepFromButtons(in);
		return clicked;
	}

	/// 見えている行を `view.<name>_rows` に送り、つまみの位置と長さを `view.<name>_bar_on`、`_bar_top`、`_bar_h` に送る。
	/// つまみの値を分けることで、UI の初回読み込み時に未送信の変数を 0 で補える。
	/// 行は `{"i":番号,"sel":選んでいるか,…}` とし、`makeRow(番号, 書き手)` が残りのフィールドを足す。
	/// 文字列の上限に達して一部の行を送れなかったときは false を返す。
	template <class Hud, class MakeRow>
	bool push(Hud& hud, const char* name, MakeRow&& makeRow) const
	{
		std::string json = "[";
		bool complete = true;
		for (int i = begin(); i < end(); ++i)
		{
			std::string row = "{\"i\":" + std::to_string(i) + ",\"sel\":" + (i == cursor ? "true" : "false");
			RowWriter writer(row);
			makeRow(i, writer);
			row += '}';
			if (json.size() + row.size() + 2 > kMaxJsonBytes) { complete = false; break; }
			if (json.size() > 1) { json += ','; }
			json += row;
		}
		json += ']';

		char key[128];
		std::snprintf(key, sizeof(key), "view.%s_rows", name);
		hud.set(key, json.c_str());

		const int shown = visibleRows();
		const bool scrollable = count > shown;
		const double thumb = scrollable ? std::max(8.0, 100.0 * shown / count) : 100.0;
		const double top = scrollable ? (100.0 - thumb) * first / (count - shown) : 0.0;
		char number[32];
		std::snprintf(key, sizeof(key), "view.%s_bar_on", name);
		hud.set(key, scrollable ? "true" : "false");
		std::snprintf(number, sizeof(number), "%.2f", top);
		std::snprintf(key, sizeof(key), "view.%s_bar_top", name);
		hud.set(key, number);
		std::snprintf(number, sizeof(number), "%.2f", thumb);
		std::snprintf(key, sizeof(key), "view.%s_bar_h", name);
		hud.set(key, number);
		return complete;
	}

private:
	// `rows` が 0 以下に書き換えられても、使うときは 1 以上に直す。
	[[nodiscard]] int visibleRows() const noexcept { return std::max(1, rows); }

	void clamp() noexcept
	{
		rows = visibleRows();
		first = std::clamp(first, 0, std::max(0, count - rows));
		cursor = std::clamp(cursor, 0, std::max(0, count - 1));
	}

	template <class In>
	void stepFromButtons(const In& in) noexcept
	{
		using mitiru::Key;
		using mitiru::Pad;
		if (in.padPressed(Pad::LB) || in.pressed(Key::PageUp)) { moveCursor(-rows); return; }
		if (in.padPressed(Pad::RB) || in.pressed(Key::PageDown)) { moveCursor(rows); return; }
		if (in.pressed(Key::Home)) { select(0); return; }
		if (in.pressed(Key::End)) { select(count - 1); return; }

		const bool downHeld = in.padDown(Pad::Down) || in.down(Key::Down);
		const bool upHeld = in.padDown(Pad::Up) || in.down(Key::Up);
		if (in.padPressed(Pad::Down) || in.pressed(Key::Down)) { hold = 0; moveCursor(1); return; }
		if (in.padPressed(Pad::Up) || in.pressed(Key::Up)) { hold = 0; moveCursor(-1); return; }
		if (downHeld == upHeld) { hold = 0; return; }
		++hold;
		if (hold > kRepeatDelay && (hold - kRepeatDelay) % kRepeatEvery == 0) { moveCursor(downHeld ? 1 : -1); }
	}
};

} // namespace mitiru::ui
