// story。カットシーンの再生位置、会話の位置、旗、クエストの進みを名前で見る (読み取り専用)。
// どこに何があるかと鍵の名前の表は、DLL が MITIRU_INSPECT_ASSETS で渡した kind "story" の JSON (narrative/StoryInspect.hpp)。
// host はその JSON が指す GameMemory の範囲を snapshot の "story" に bytes のまま写すだけで、読み解くのはこのページ。
// 値を変えるのは分岐エディタの仕事で、ここではしない。

#include "Pages.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <fstream>
#include <iterator>

#include <mitiru/narrative/StoryInspect.hpp>

namespace mitiru::tool
{

namespace
{

[[nodiscard]] std::vector<std::uint8_t> fromHex(const std::string& s)
{
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') { return c - '0'; }
		if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
		return -1;
	};
	std::vector<std::uint8_t> out;
	out.reserve(s.size() / 2);
	for (std::size_t i = 0; i + 1 < s.size(); i += 2)
	{
		const int hi = nibble(s[i]);
		const int lo = nibble(s[i + 1]);
		if (hi < 0 || lo < 0) { return {}; }
		out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
	}
	return out;
}

[[nodiscard]] Snapshot readJsonFile(const std::string& utf8Path)
{
	const std::u8string u(utf8Path.begin(), utf8Path.end());
	std::ifstream f(std::filesystem::path(u), std::ios::binary);
	if (!f) { return Snapshot(); }
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return Snapshot::parse(text, nullptr, false);
}

[[nodiscard]] std::string textOf(const Snapshot* v)
{
	return v != nullptr ? jsString(*v) : std::string("-");
}

/// 1 行に出す文。種類ごとに今の様子を 1 行にまとめる
[[nodiscard]] Snapshot rowsOf(const Snapshot& decoded)
{
	Snapshot rows = Snapshot::array();
	for (const Snapshot& d : decoded)
	{
		const std::string kind = stringOr(findAt(d, { "kind" }), "");
		const Snapshot* v = findAt(d, { "value" });
		Snapshot row = { { "kind", kind }, { "label", stringOr(findAt(d, { "label" }), "") },
		                 { "error", stringOr(findAt(d, { "error" }), "") }, { "items", Snapshot::array() }, { "line", "" } };
		if (v == nullptr) { rows.push_back(std::move(row)); continue; }
		if (kind == "playback")
		{
			const std::string seq = stringOr(findAt(*v, { "sequence" }), "");
			row["line"] = (seq.empty() ? std::string("(なし)") : seq) + "  " + stringOr(findAt(*v, { "state" }), "") + "  "
			              + toFixed(numberOr(findAt(*v, { "time" }), 0.0), 2) + " / " + toFixed(numberOr(findAt(*v, { "duration" }), 0.0), 2) + " 秒";
		}
		else if (kind == "dialogue")
		{
			const std::string script = stringOr(findAt(*v, { "script" }), "");
			row["line"] = (script.empty() ? std::string("(話していない)") : script) + "  " + stringOr(findAt(*v, { "mode" }), "")
			              + "  pc " + textOf(findAt(*v, { "pc" })) + "  serial " + textOf(findAt(*v, { "serial" }));
		}
		else if (kind == "vars")
		{
			for (const Snapshot& x : *v) { row["items"].push_back({ { "k", stringOr(findAt(x, { "name" }), "") }, { "v", textOf(findAt(x, { "value" })) } }); }
		}
		else if (kind == "quests")
		{
			for (const Snapshot& q : *v)
			{
				int done = 0, total = 0;
				if (const Snapshot* obj = findAt(q, { "objectives" }); obj != nullptr && obj->is_array())
				{
					for (const Snapshot& o : *obj) { ++total; done += truthy(findAt(o, { "done" })) ? 1 : 0; }
				}
				row["items"].push_back({ { "k", stringOr(findAt(q, { "name" }), "") },
				                         { "v", stringOr(findAt(q, { "status" }), "") + "  " + std::to_string(done) + " / " + std::to_string(total) } });
			}
		}
		rows.push_back(std::move(row));
	}
	return rows;
}

class StoryPage final : public ToolPage
{
public:
	explicit StoryPage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(Snapshot()); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		const auto assets = snapshotAssets(snap, narrative::kStoryInspectKind);
		if (!assets.empty() && assets.front().file != m_layoutFile)
		{
			m_layoutFile = assets.front().file;
			m_layout = readJsonFile(m_layoutFile);
		}
		push(snap);
	}

private:
	void push(const Snapshot& snap)
	{
		const Snapshot* slots = findAt(snap, { "story", "state", "slots" });
		const bool ready = slots != nullptr && slots->is_array() && m_layout.is_object();
		m_view->set("ready", ready);
		if (!ready) { m_view->set("rows", Snapshot::array()); return; }
		std::vector<std::vector<std::uint8_t>> bytes;
		for (const Snapshot& s : *slots) { bytes.push_back(fromHex(s.is_string() ? s.get<std::string>() : std::string())); }
		m_view->set("rows", rowsOf(narrative::decodeStory(m_layout, bytes)));
	}

	ToolView* m_view;
	std::string m_layoutFile;
	Snapshot m_layout;
};

} // namespace

std::unique_ptr<ToolPage> makeStoryPage(const PageContext& ctx)
{
	return std::make_unique<StoryPage>(ctx);
}

} // namespace mitiru::tool
