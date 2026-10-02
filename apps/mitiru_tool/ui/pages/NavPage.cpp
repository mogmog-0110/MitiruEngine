// nav。群衆の agent と障害物を、真上から見た地図に描く (読み取り専用)。
// agent は gameMemory の CrowdAgentView、障害物は NavObstacle (どちらも "$type" 付き、host が解いた値)。
// ナビメッシュは GameMemory に無い (DLL が読み込み時に作る) ので、DLL が MITIRU_INSPECT_ASSETS で渡した焼いた bytes
// (host が snapshot の "assets" に置く) から床の形を出す。"--page nav?mesh=<.navmesh か .navcache>" を渡せばそちらを優先する。

#include "NavMap.hpp"
#include "Pages.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <filesystem>

namespace mitiru::tool
{

namespace
{

constexpr int kMapWidth = 520;

float component(const Snapshot* v, std::size_t i)
{
	return (v != nullptr && v->is_array() && v->size() > i && (*v)[i].is_number()) ? (*v)[i].get<float>() : 0.0f;
}

MapAgent toAgent(const Snapshot& a)
{
	const Snapshot* p = findAt(a, { "p" });
	const Snapshot* vel = findAt(a, { "v" });
	const std::string move = stringOr(findAt(a, { "move" }), "inactive");
	return MapAgent{ component(p, 0), component(p, 2), component(vel, 0), component(vel, 2),
	                 move == "failed" || move == "offMesh" || move == "inactive", move == "moving" || move == "velocity" };
}

MapObstacle toObstacle(const Snapshot& o)
{
	const Snapshot* c = findAt(o, { "center" });
	const Snapshot* h = findAt(o, { "half" });
	return MapObstacle{ component(c, 0), component(c, 2), component(h, 0), component(h, 2),
	                    static_cast<float>(numberOr(findAt(o, { "yaw" }), 0.0)),
	                    stringOr(findAt(o, { "shape" }), "box") == "cylinder", truthy(findAt(o, { "enabled" })) };
}

class NavPage final : public ToolPage
{
public:
	explicit NavPage(const PageContext& ctx) : m_view(ctx.view)
	{
		m_meshFile = queryValue(ctx.query, "mesh");
		m_fromQuery = !m_meshFile.empty();
		if (m_fromQuery) { m_meshError = loadNavFootprint(m_meshFile, m_mesh); }
	}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		loadAssetMesh(snapshotAssets(snap, "navmesh"));
		push();
	}

private:
	/// --page nav?mesh= が無い時だけ、DLL の資産の最初のナビメッシュを読む (ファイルが替わった時だけ)
	void loadAssetMesh(const std::vector<SnapshotAsset>& assets)
	{
		if (m_fromQuery || assets.empty() || assets.front().file == m_assetFile) { return; }
		m_assetFile = assets.front().file;
		m_meshFile = assets.front().name.empty() ? std::string("navmesh") : assets.front().name;
		m_meshError = loadNavFootprint(m_assetFile, m_mesh);
	}

	void push()
	{
		const Snapshot* state = gameMemoryState(m_snap);
		std::vector<MapAgent> agents;
		std::vector<MapObstacle> obstacles;
		if (state != nullptr)
		{
			for (const TypedValue& a : findTyped(*state, "mitiru.CrowdAgentView")) { agents.push_back(toAgent(*a.value)); }
			for (const TypedValue& o : findTyped(*state, "mitiru.NavObstacle")) { obstacles.push_back(toObstacle(*o.value)); }
		}
		pushCounts(agents, obstacles);
		m_view->set("ready", state != nullptr);
		m_view->set("mesh_file", std::filesystem::path(m_meshFile).filename().string());
		m_view->set("mesh_error", m_meshError);
		m_view->set("mesh_tris", std::to_string(m_mesh.triangleCount()));
		MapImage img = renderNavMap(m_mesh, agents, obstacles, kMapWidth);
		m_view->set("grid", toFixed(img.gridMeters, 0) + " m");
		m_view->setImage("map", img.width, img.height, std::move(img.rgba));
	}

	void pushCounts(const std::vector<MapAgent>& agents, const std::vector<MapObstacle>& obstacles)
	{
		std::size_t moving = 0, stuck = 0, closed = 0;
		for (const MapAgent& a : agents) { moving += a.moving ? 1 : 0; stuck += a.stuck ? 1 : 0; }
		for (const MapObstacle& o : obstacles) { closed += o.enabled ? 1 : 0; }
		m_view->set("agents", agents.size());
		m_view->set("moving", moving);
		m_view->set("stuck", stuck);
		m_view->set("obstacles", obstacles.size());
		m_view->set("closed", closed);
		m_view->set("nothing", agents.empty() && obstacles.empty());
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	std::string m_meshFile;
	std::string m_meshError;
	std::string m_assetFile;
	bool m_fromQuery = false;
	NavFootprint m_mesh;
};

} // namespace

std::unique_ptr<ToolPage> makeNavPage(const PageContext& ctx)
{
	return std::make_unique<NavPage>(ctx);
}

} // namespace mitiru::tool
