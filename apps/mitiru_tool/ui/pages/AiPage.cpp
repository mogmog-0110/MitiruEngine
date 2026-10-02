// ai。選んだ敵のビヘイビアツリーのノードの結果、知覚の記憶、攻撃トークンを見る (読み取り専用)。
// 値は gameMemory の中の "$type" 付きの object (host が解いたエンジンの型) から拾う。木の形は GameMemory に無い
// (DLL の static に置く、docs/GAME_AI.md) ので、DLL が MITIRU_INSPECT_ASSETS で渡した木の JSON (host が snapshot の
// "assets" に置く) からノードの種類と葉の名前を出す。"--page ai?tree=<木の JSON>" を渡せばそのファイルを優先する。
// JSON は BtTree と同じ前順に並ぶので、ノード番号で BtState の結果と突き合わせる。木が複数あれば、敵の結果
// (最後の idle でないノードまで) が収まる木のうち、ノードの最も少ない木を使う。

#include "Pages.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace mitiru::tool
{

namespace
{

struct TreeNode
{
	int depth = 0;
	std::string kind;
	std::string name;   ///< 葉の名前か、種類ごとの欄 (frames / count / success) の要約
};

std::string nodeDetail(const nlohmann::json& n)
{
	if (const auto it = n.find("name"); it != n.end() && it->is_string()) { return it->get<std::string>(); }
	for (const char* key : { "frames", "count", "success" })
	{
		if (const auto it = n.find(key); it != n.end() && it->is_number_integer())
		{
			return std::string(key) + " " + std::to_string(it->get<long long>());
		}
	}
	return {};
}

void flatten(const nlohmann::json& n, int depth, std::vector<TreeNode>& out)
{
	if (!n.is_object() || out.size() >= 64) { return; }
	const auto type = n.find("type");
	out.push_back({ depth, type != n.end() && type->is_string() ? type->get<std::string>() : std::string("?"), nodeDetail(n) });
	if (const auto it = n.find("children"); it != n.end() && it->is_array())
	{
		for (const auto& c : *it) { flatten(c, depth + 1, out); }
	}
	if (const auto it = n.find("child"); it != n.end()) { flatten(*it, depth + 1, out); }
}

/// 相対パスは今の作業フォルダ (host が開いた窓は host と同じ) と MITIRU_ASSET_ROOT から探す。
std::filesystem::path resolveTreePath(const std::string& query)
{
	std::filesystem::path p(query);
	std::error_code ec;
	if (p.is_absolute() || std::filesystem::exists(p, ec)) { return p; }
	if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && *root != '\0')
	{
		if (auto q = std::filesystem::path(root) / p; std::filesystem::exists(q, ec)) { return q; }
	}
	return p;
}

std::string loadTree(const std::string& query, std::vector<TreeNode>& out)
{
	std::ifstream f(resolveTreePath(query), std::ios::binary);
	if (!f) { return "木の JSON を開けない: " + query; }
	const auto j = nlohmann::json::parse(f, nullptr, false);
	if (j.is_discarded() || !j.is_object()) { return "木の JSON を読めない: " + query; }
	flatten(j, 0, out);
	return {};
}

const char* statusName(char c)
{
	switch (c)
	{
	case 'R': return "running";
	case 'S': return "success";
	case 'F': return "failure";
	default: return "idle";
	}
}

class AiPage final : public ToolPage
{
public:
	explicit AiPage(const PageContext& ctx) : m_view(ctx.view)
	{
		m_treeFile = queryValue(ctx.query, "tree");
		m_queryTree.name = std::filesystem::path(m_treeFile).filename().string();
		if (!m_treeFile.empty()) { m_treeError = loadTree(m_treeFile, m_queryTree.nodes); }
	}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		loadAssetTrees(snapshotAssets(snap, "bt_tree"));
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name != "ai.select") { return; }
		m_selected = payloadString(payload, "label");
		push();
	}

private:
	struct NamedTree
	{
		std::string name;
		std::vector<TreeNode> nodes;
	};

	/// 資産のファイルの並びが変わった時だけ読み直す (snapshot は 10 Hz で届く)
	void loadAssetTrees(const std::vector<SnapshotAsset>& assets)
	{
		std::string key;
		for (const SnapshotAsset& a : assets) { key += a.file + "\n"; }
		if (key == m_assetKey) { return; }
		m_assetKey = key;
		m_assetTrees.clear();
		for (const SnapshotAsset& a : assets)
		{
			NamedTree t{ a.name.empty() ? std::string("tree") : a.name, {} };
			if (loadTree(a.file, t.nodes).empty()) { m_assetTrees.push_back(std::move(t)); }
		}
	}

	/// --page ai?tree= を優先し、無ければ DLL の資産から選ぶ
	const NamedTree* treeFor(const Snapshot* bt) const
	{
		if (!m_treeFile.empty()) { return &m_queryTree; }
		const std::size_t used = bt != nullptr ? stringOr(findAt(*bt, { "status" }), "").size() : 0;
		const NamedTree* best = nullptr;
		for (const NamedTree& t : m_assetTrees)
		{
			if (t.nodes.size() >= used && (best == nullptr || t.nodes.size() < best->nodes.size())) { best = &t; }
		}
		return best != nullptr ? best : (m_assetTrees.empty() ? nullptr : &m_assetTrees.front());
	}

	void push()
	{
		const Snapshot* state = gameMemoryState(m_snap);
		const auto agents = state != nullptr ? findTyped(*state, "mitiru.BtState") : std::vector<TypedValue>{};
		const TypedValue* sel = selected(agents);
		const NamedTree* tree = treeFor(sel != nullptr ? sel->value : nullptr);
		m_view->set("ready", state != nullptr);
		m_view->set("agents", agentRows(agents, sel));
		m_view->set("no_agents", state != nullptr && agents.empty());
		m_view->set("sel_label", sel != nullptr ? ownerLabel(*sel) : std::string());
		m_view->set("has_sel", sel != nullptr);
		m_view->set("tree_file", tree != nullptr ? tree->name : std::string());
		m_view->set("tree_error", m_treeError);
		m_view->set("nodes", sel != nullptr ? nodeRows(*sel->value, tree) : nlohmann::json::array());
		pushMind(state, sel != nullptr && ownerIsUnique(agents, sel->owner) ? sel : nullptr);
		pushTokens(state);
	}

	const TypedValue* selected(const std::vector<TypedValue>& agents) const
	{
		for (const TypedValue& a : agents) { if (ownerLabel(a) == m_selected) { return &a; } }
		return agents.empty() ? nullptr : &agents.front();
	}

	static nlohmann::json agentRows(const std::vector<TypedValue>& agents, const TypedValue* sel)
	{
		nlohmann::json rows = nlohmann::json::array();
		for (const TypedValue& a : agents)
		{
			const int running = static_cast<int>(numberOr(findAt(*a.value, { "runningNode" }), -1.0));
			rows.push_back({ { "label", ownerLabel(a) }, { "root", stringOr(findAt(*a.value, { "root" }), "idle") },
			                 { "running", running >= 0 ? "#" + std::to_string(running) : std::string() },
			                 { "selected", &a == sel } });
		}
		return rows;
	}

	static nlohmann::json nodeRows(const Snapshot& bt, const NamedTree* tree)
	{
		const std::string status = stringOr(findAt(bt, { "status" }), "");
		const int running = static_cast<int>(numberOr(findAt(bt, { "runningNode" }), -1.0));
		const std::vector<TreeNode> none;
		const std::vector<TreeNode>& nodes = tree != nullptr ? tree->nodes : none;
		const std::size_t count = nodes.empty() ? status.size() : nodes.size();
		nlohmann::json rows = nlohmann::json::array();
		for (std::size_t i = 0; i < count; ++i)
		{
			const char letter = i < status.size() ? status[i] : '.';
			const TreeNode node = i < nodes.size() ? nodes[i] : TreeNode{ 0, "node", {} };
			rows.push_back({ { "index", std::to_string(i) }, { "pad", std::to_string(node.depth * 14) + "dp" },
			                 { "kind", node.kind }, { "name", node.name }, { "st", statusName(letter) },
			                 { "running", static_cast<int>(i) == running } });
		}
		return rows;
	}

	/// linked は知覚と組にできる敵 (持ち主が一意のとき)。
	void pushMind(const Snapshot* state, const TypedValue* linked)
	{
		const auto minds = state != nullptr ? findTyped(*state, "mitiru.PerceptionMemory") : std::vector<TypedValue>{};
		const TypedValue* mind = linked != nullptr ? findOwned(minds, linked->owner) : nullptr;
		// 文書を読む時点で data-style の式が値を引くので、知覚が無くても全部の値を写す。
		const Snapshot empty = Snapshot::object();
		const Snapshot& m = mind != nullptr ? *mind->value : empty;
		const double aware = numberOr(findAt(m, { "awareness" }), 0.0);
		m_view->set("has_mind", mind != nullptr);
		m_view->set("mind_level", stringOr(findAt(m, { "level" }), "unaware"));
		m_view->set("mind_aware", toFixed(aware, 2));
		m_view->set("mind_aware_w", toFixed(std::clamp(aware, 0.0, 1.0) * 100.0, 1) + "%");
		m_view->set("mind_seen", truthy(findAt(m, { "seenNow" })));
		m_view->set("mind_last", vec3Text(findAt(m, { "lastKnown" })));
	}

	void pushTokens(const Snapshot* state)
	{
		const auto pools = state != nullptr ? findTyped(*state, "mitiru.AttackTokenPool") : std::vector<TypedValue>{};
		nlohmann::json rows = nlohmann::json::array();
		for (const TypedValue& p : pools)
		{
			nlohmann::json holders = nlohmann::json::array();
			if (const Snapshot* hs = findAt(*p.value, { "holders" }); hs != nullptr && hs->is_array())
			{
				for (const Snapshot& h : *hs)
				{
					holders.push_back({ { "id", jsString(h.value("id", Snapshot(0))) }, { "cost", jsString(h.value("cost", Snapshot(0))) },
					                    { "since", jsString(h.value("since", Snapshot(0))) } });
				}
			}
			const Snapshot* requests = findAt(*p.value, { "requests" });
			rows.push_back({ { "label", p.path }, { "used", jsString(p.value->value("used", Snapshot(0))) },
			                 { "capacity", jsString(p.value->value("capacity", Snapshot(0))) },
			                 { "requests", requests != nullptr && requests->is_array() ? requests->size() : 0 },
			                 { "holders", std::move(holders) } });
		}
		m_view->set("no_pool", rows.empty());
		m_view->set("pools", std::move(rows));
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	std::string m_selected;
	std::string m_treeFile;
	std::string m_treeError;
	NamedTree m_queryTree;
	std::string m_assetKey;
	std::vector<NamedTree> m_assetTrees;
};

} // namespace

std::unique_ptr<ToolPage> makeAiPage(const PageContext& ctx)
{
	return std::make_unique<AiPage>(ctx);
}

} // namespace mitiru::tool
