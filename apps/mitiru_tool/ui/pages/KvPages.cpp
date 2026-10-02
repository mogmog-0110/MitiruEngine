// inspect / input / scene (木と game memory) の 3 ページ。どれも SharedSnapshot を表に並べるだけで、
// 開閉や tab の切り替えは表示の都合で窓の中に閉じた状態 (ゲームの状態には触らない)。

#include "Pages.hpp"

#include "../PageUtil.hpp"

namespace mitiru::tool
{

namespace
{

using Names = std::set<std::string, std::less<>>;

void toggle(Names& set, const std::string& name)
{
	if (name.empty()) { return; }
	if (const auto it = set.find(name); it != set.end()) { set.erase(it); }
	else { set.insert(name); }
}

class KvPage final : public ToolPage
{
public:
	KvPage(const PageContext& ctx, KvFilter filter) : m_view(ctx.view), m_filter(std::move(filter)) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name != "kv.group") { return; }
		toggle(m_openGroups, payloadString(payload, "name"));
		push();
	}

private:
	void push()
	{
		m_view->set("ready", kvSectionCount(m_snap, m_filter) > 0);
		m_view->set("sections", kvSections(m_snap, m_filter, m_openGroups));
	}

	ToolView* m_view;
	KvFilter m_filter;
	Snapshot m_snap = Snapshot::object();
	Names m_openGroups;
};

// 木 (exportedInspectables) と game memory (reflect) は別系統のデータ源 (docs/INSPECTOR_DATA_SOURCES.md)。
// tab は表示の切り替えだけで、どちらも読み取り専用のまま。
class SceneTreePage final : public ToolPage
{
public:
	explicit SceneTreePage(const PageContext& ctx)
		: m_view(ctx.view)
		, m_tab(ctx.query.find("tab=memory") != std::string::npos ? "memory" : "tree")
	{
	}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name == "tree.toggle")   { toggle(m_collapsed, payloadString(payload, "path")); }
		else if (name == "kv.group") { toggle(m_openGroups, payloadString(payload, "name")); }
		else if (name == "tab")
		{
			const std::string tab = payloadString(payload, "name");
			if (tab == "tree" || tab == "memory") { m_tab = tab; }
		}
		else { return; }
		push();
	}

private:
	void push()
	{
		const bool any = m_snap.is_object() && !m_snap.empty();
		m_view->set("ready", any);
		m_view->set("tab", m_tab);
		// 空の snapshot が来ても木は前のまま残す (待っている表示は ready が出す)。
		if (any || !m_haveRows) { m_view->set("rows", any ? treeRows(m_snap, "scene", m_collapsed) : nlohmann::json::array()); }
		m_haveRows = true;
		m_view->set("sections", kvSections(m_snap, KvFilter{ { "gameMemory" }, {} }, m_openGroups));
	}

	ToolView* m_view;
	std::string m_tab;
	Snapshot m_snap = Snapshot::object();
	Names m_collapsed;
	Names m_openGroups;
	bool m_haveRows = false;
};

// ゲームが assets/<名前>.rml を自分で書いたページ。snapshot をそのまま "snap" で見せる。
class GameDefinedPage final : public ToolPage
{
public:
	explicit GameDefinedPage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool ready) override
	{
		m_snap = snap;
		m_ready = ready;
		push();
	}

private:
	void push()
	{
		m_view->set("ready", m_ready);
		m_view->set("snap", nlohmann::json(m_snap));
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	bool m_ready = false;
};

} // namespace

std::unique_ptr<ToolPage> makeInspectPage(const PageContext& ctx)
{
	// perf / audio は専用窓 (perf / mixer)、rewind は巻き戻し窓、sideState は side_state 窓があるので inspector では出さない
	return std::make_unique<KvPage>(ctx, KvFilter{ {}, { "perf", "audio", "rewind", "sideState" } });
}

std::unique_ptr<ToolPage> makeInputPage(const PageContext& ctx)
{
	return std::make_unique<KvPage>(ctx, KvFilter{ { "input" }, {} });
}

std::unique_ptr<ToolPage> makeScenePage(const PageContext& ctx)
{
	return std::make_unique<SceneTreePage>(ctx);
}

std::unique_ptr<ToolPage> makeGameDefinedPage(const PageContext& ctx)
{
	return std::make_unique<GameDefinedPage>(ctx);
}

} // namespace mitiru::tool
