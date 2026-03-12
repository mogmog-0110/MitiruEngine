#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <vector>

#include <mitiru/scene/MitiruScene.hpp>
#include <mitiru/scene/SceneSerializer.hpp>
#include <mitiru/scene/SceneGraph.hpp>

// ── Concrete test scene ─────────────────────────────────────

class StubScene : public mitiru::scene::MitiruScene
{
public:
	int enterCount = 0;
	int exitCount = 0;
	int updateCount = 0;
	int drawCount = 0;
	float lastDt = 0.0f;
	std::string sceneName;

	explicit StubScene(std::string name = "StubScene")
		: sceneName(std::move(name))
	{
	}

	void onEnter() override { ++enterCount; }
	void onExit() override { ++exitCount; }

	void onUpdate(float dt) override
	{
		++updateCount;
		lastDt = dt;
	}

	void onDraw(mitiru::Screen& screen) override
	{
		static_cast<void>(screen);
		++drawCount;
	}

	[[nodiscard]] std::string name() const override
	{
		return sceneName;
	}
};

// ============================================================
// MitiruScene (lifecycle + labels)
// ============================================================

TEST_CASE("MitiruScene onEnter/onExit/onUpdate/onDraw lifecycle", "[mitiru][scene]")
{
	mitiru::Screen screen(800, 600);
	StubScene scene("TestLevel");

	REQUIRE(scene.updateCount == 0);
	REQUIRE(scene.drawCount == 0);

	scene.onEnter();
	REQUIRE(scene.enterCount == 1);

	scene.onUpdate(0.016f);
	REQUIRE(scene.updateCount == 1);
	REQUIRE(scene.lastDt == Catch::Approx(0.016f));

	scene.onDraw(screen);
	REQUIRE(scene.drawCount == 1);

	scene.onExit();
	REQUIRE(scene.exitCount == 1);
}

TEST_CASE("MitiruScene name returns correct string", "[mitiru][scene]")
{
	StubScene scene("MainMenu");
	REQUIRE(scene.name() == "MainMenu");
}

TEST_CASE("MitiruScene addLabel and hasLabel", "[mitiru][scene]")
{
	StubScene scene;

	REQUIRE_FALSE(scene.hasLabel("active"));

	scene.addLabel("active");
	REQUIRE(scene.hasLabel("active"));
	REQUIRE_FALSE(scene.hasLabel("paused"));
}

TEST_CASE("MitiruScene addLabel is idempotent", "[mitiru][scene]")
{
	StubScene scene;
	scene.addLabel("unique");
	scene.addLabel("unique");

	REQUIRE(scene.labels().size() == 1);
}

TEST_CASE("MitiruScene labels returns all labels", "[mitiru][scene]")
{
	StubScene scene;
	scene.addLabel("first");
	scene.addLabel("second");

	const auto& labels = scene.labels();
	REQUIRE(labels.size() == 2);
	REQUIRE(labels[0] == "first");
	REQUIRE(labels[1] == "second");
}

// ============================================================
// MitiruSceneManager
// ============================================================

TEST_CASE("MitiruSceneManager initially empty", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	REQUIRE(mgr.empty());
	REQUIRE(mgr.depth() == 0);
	REQUIRE(mgr.currentScene() == nullptr);
}

TEST_CASE("MitiruSceneManager pushScene adds scene and calls onEnter", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;

	auto scene = std::make_unique<StubScene>("Level1");
	auto* raw = scene.get();

	mgr.pushScene(std::move(scene));

	REQUIRE(mgr.depth() == 1);
	REQUIRE_FALSE(mgr.empty());
	REQUIRE(mgr.currentScene() == raw);
	REQUIRE(raw->enterCount == 1);
}

TEST_CASE("MitiruSceneManager pushScene with nullptr is no-op", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(nullptr);
	REQUIRE(mgr.empty());
	REQUIRE(mgr.depth() == 0);
}

TEST_CASE("MitiruSceneManager pushScene stacks multiple scenes", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(std::make_unique<StubScene>("A"));
	mgr.pushScene(std::make_unique<StubScene>("B"));

	REQUIRE(mgr.depth() == 2);
	REQUIRE(mgr.currentScene()->name() == "B");
}

TEST_CASE("MitiruSceneManager popScene removes top and calls onExit", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;

	auto sceneA = std::make_unique<StubScene>("A");
	auto* rawA = sceneA.get();
	auto sceneB = std::make_unique<StubScene>("B");
	auto* rawB = sceneB.get();

	mgr.pushScene(std::move(sceneA));
	mgr.pushScene(std::move(sceneB));

	mgr.popScene();

	// rawB は popScene で破棄されるためアクセス不可
	REQUIRE(mgr.depth() == 1);
	REQUIRE(mgr.currentScene() == rawA);
}

TEST_CASE("MitiruSceneManager popScene on empty is no-op", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	// Should not crash
	mgr.popScene();
	REQUIRE(mgr.empty());
}

TEST_CASE("MitiruSceneManager replaceScene swaps top scene", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;

	auto sceneA = std::make_unique<StubScene>("Old");
	auto* rawA = sceneA.get();
	mgr.pushScene(std::move(sceneA));

	auto sceneB = std::make_unique<StubScene>("New");
	auto* rawB = sceneB.get();
	mgr.replaceScene(std::move(sceneB));

	// rawA は replaceScene で破棄されるためアクセス不可
	REQUIRE(rawB->enterCount == 1);
	REQUIRE(mgr.depth() == 1);
	REQUIRE(mgr.currentScene() == rawB);
}

TEST_CASE("MitiruSceneManager replaceScene on empty just pushes", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;

	auto scene = std::make_unique<StubScene>("First");
	auto* raw = scene.get();
	mgr.replaceScene(std::move(scene));

	REQUIRE(mgr.depth() == 1);
	REQUIRE(mgr.currentScene() == raw);
	REQUIRE(raw->enterCount == 1);
}

TEST_CASE("MitiruSceneManager replaceScene with nullptr just pops", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(std::make_unique<StubScene>("A"));

	mgr.replaceScene(nullptr);
	REQUIRE(mgr.empty());
}

TEST_CASE("MitiruSceneManager currentScene const version", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(std::make_unique<StubScene>("Const"));

	const auto& constMgr = mgr;
	REQUIRE(constMgr.currentScene() != nullptr);
	REQUIRE(constMgr.currentScene()->name() == "Const");
}

TEST_CASE("MitiruSceneManager sceneStack returns all scenes", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(std::make_unique<StubScene>("Bottom"));
	mgr.pushScene(std::make_unique<StubScene>("Middle"));
	mgr.pushScene(std::make_unique<StubScene>("Top"));

	const auto stack = mgr.sceneStack();
	REQUIRE(stack.size() == 3);
	REQUIRE(stack[0]->name() == "Bottom");
	REQUIRE(stack[1]->name() == "Middle");
	REQUIRE(stack[2]->name() == "Top");
}

TEST_CASE("MitiruSceneManager toJson includes depth and stack", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	mgr.pushScene(std::make_unique<StubScene>("Title"));
	mgr.pushScene(std::make_unique<StubScene>("Game"));

	const auto json = mgr.toJson();
	REQUIRE(json.find("\"depth\":2") != std::string::npos);
	REQUIRE(json.find("\"stack\":[") != std::string::npos);
	REQUIRE(json.find("\"Title\"") != std::string::npos);
	REQUIRE(json.find("\"Game\"") != std::string::npos);
}

TEST_CASE("MitiruSceneManager toJson empty", "[mitiru][scene]")
{
	mitiru::scene::MitiruSceneManager mgr;
	const auto json = mgr.toJson();
	REQUIRE(json.find("\"depth\":0") != std::string::npos);
	REQUIRE(json.find("\"stack\":[]") != std::string::npos);
}

// ============================================================
// SceneSerializer
// ============================================================

TEST_CASE("SceneSerializer serialize produces JSON with sceneName", "[mitiru][scene]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();
	world.setTag(entity, "npc");

	const auto json = mitiru::scene::SceneSerializer::serialize(world, "Level1");
	REQUIRE(json.find("\"sceneName\":\"Level1\"") != std::string::npos);
	REQUIRE(json.find("\"entities\":") != std::string::npos);
	REQUIRE(json.find("\"metadata\":") != std::string::npos);
}

TEST_CASE("SceneSerializer deserialize parses sceneName", "[mitiru][scene]")
{
	mitiru::ecs::MitiruWorld world;
	world.world().createEntity();

	const auto json = mitiru::scene::SceneSerializer::serialize(world, "TestScene");
	const auto data = mitiru::scene::SceneSerializer::deserialize(json);

	REQUIRE(data.sceneName == "TestScene");
}

TEST_CASE("SceneSerializer roundtrip preserves sceneName and entities", "[mitiru][scene]")
{
	mitiru::ecs::MitiruWorld world;
	auto e1 = world.world().createEntity();
	auto e2 = world.world().createEntity();
	world.setTag(e1, "hero");
	world.setTag(e2, "villain");
	static_cast<void>(e1);
	static_cast<void>(e2);

	const auto json = mitiru::scene::SceneSerializer::serialize(world, "BattleScene");
	const auto data = mitiru::scene::SceneSerializer::deserialize(json);

	REQUIRE(data.sceneName == "BattleScene");
	REQUIRE_FALSE(data.entitiesJson.empty());
	REQUIRE(data.entitiesJson.find("\"entityCount\":2") != std::string::npos);
}

TEST_CASE("SceneSerializer deserialize with empty world", "[mitiru][scene]")
{
	mitiru::ecs::MitiruWorld world;
	const auto json = mitiru::scene::SceneSerializer::serialize(world, "Empty");
	const auto data = mitiru::scene::SceneSerializer::deserialize(json);

	REQUIRE(data.sceneName == "Empty");
	REQUIRE(data.entitiesJson.find("\"entityCount\":0") != std::string::npos);
}

TEST_CASE("SceneData toJson produces valid structure", "[mitiru][scene]")
{
	mitiru::scene::SceneData data;
	data.sceneName = "Test";
	data.entitiesJson = "[]";
	data.metadata = "{\"version\":1}";

	const auto json = data.toJson();
	REQUIRE(json.find("\"sceneName\":\"Test\"") != std::string::npos);
	REQUIRE(json.find("\"entities\":[]") != std::string::npos);
	REQUIRE(json.find("\"metadata\":{\"version\":1}") != std::string::npos);
}

TEST_CASE("SceneData toJson with empty metadata defaults to {}", "[mitiru][scene]")
{
	mitiru::scene::SceneData data;
	data.sceneName = "X";
	data.entitiesJson = "{}";
	data.metadata = "";

	const auto json = data.toJson();
	REQUIRE(json.find("\"metadata\":{}") != std::string::npos);
}

// ============================================================
// SceneGraph
// ============================================================

TEST_CASE("SceneGraph addNode and nodeCount", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto e1 = world.createEntity();
	auto e2 = world.createEntity();

	graph.addNode(e1);
	REQUIRE(graph.nodeCount() == 1);

	graph.addNode(e2);
	REQUIRE(graph.nodeCount() == 2);
}

TEST_CASE("SceneGraph addNode with parent establishes hierarchy", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto parent = world.createEntity();
	auto child = world.createEntity();

	graph.addNode(parent);
	graph.addNode(child, parent);

	const auto childIds = graph.children(parent);
	REQUIRE(childIds.size() == 1);
	REQUIRE(childIds[0] == child.id);
}

TEST_CASE("SceneGraph removeNode removes node and children recursively", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto root = world.createEntity();
	auto child = world.createEntity();
	auto grandchild = world.createEntity();

	graph.addNode(root);
	graph.addNode(child, root);
	graph.addNode(grandchild, child);
	REQUIRE(graph.nodeCount() == 3);

	graph.removeNode(root);
	REQUIRE(graph.nodeCount() == 0);
}

TEST_CASE("SceneGraph removeNode updates parent child list", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto parent = world.createEntity();
	auto child1 = world.createEntity();
	auto child2 = world.createEntity();

	graph.addNode(parent);
	graph.addNode(child1, parent);
	graph.addNode(child2, parent);
	REQUIRE(graph.children(parent).size() == 2);

	graph.removeNode(child1);
	const auto remaining = graph.children(parent);
	REQUIRE(remaining.size() == 1);
	REQUIRE(remaining[0] == child2.id);
}

TEST_CASE("SceneGraph removeNode on non-existent node is no-op", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::Entity fakeEntity{999, 0};

	// Should not crash
	graph.removeNode(fakeEntity);
	REQUIRE(graph.nodeCount() == 0);
}

TEST_CASE("SceneGraph setLocalTransform and localTransform", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;
	auto entity = world.createEntity();

	graph.addNode(entity);

	// Initially identity
	const auto initial = graph.localTransform(entity);
	REQUIRE(initial.m[0][0] == Catch::Approx(1.0f));
	REQUIRE(initial.m[3][3] == Catch::Approx(1.0f));
	REQUIRE(initial.m[0][3] == Catch::Approx(0.0f));

	// Set translation
	const auto translation = sgc::Mat4f::translation({10.0f, 20.0f, 30.0f});
	graph.setLocalTransform(entity, translation);

	const auto result = graph.localTransform(entity);
	REQUIRE(result.m[0][3] == Catch::Approx(10.0f));
	REQUIRE(result.m[1][3] == Catch::Approx(20.0f));
	REQUIRE(result.m[2][3] == Catch::Approx(30.0f));
}

TEST_CASE("SceneGraph localTransform for unknown entity returns identity", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::Entity unknown{42, 0};

	const auto mat = graph.localTransform(unknown);
	REQUIRE(mat.m[0][0] == Catch::Approx(1.0f));
	REQUIRE(mat.m[1][1] == Catch::Approx(1.0f));
	REQUIRE(mat.m[2][2] == Catch::Approx(1.0f));
	REQUIRE(mat.m[3][3] == Catch::Approx(1.0f));
}

TEST_CASE("SceneGraph worldTransform accumulates parent chain", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto parent = world.createEntity();
	auto child = world.createEntity();

	graph.addNode(parent);
	graph.addNode(child, parent);

	graph.setLocalTransform(parent, sgc::Mat4f::translation({10.0f, 0.0f, 0.0f}));
	graph.setLocalTransform(child, sgc::Mat4f::translation({5.0f, 0.0f, 0.0f}));

	// Child world transform should be parent * child = translation(15, 0, 0)
	const auto childWorld = graph.worldTransform(child);
	REQUIRE(childWorld.m[0][3] == Catch::Approx(15.0f));
	REQUIRE(childWorld.m[1][3] == Catch::Approx(0.0f));

	// Parent world transform should just be its own
	const auto parentWorld = graph.worldTransform(parent);
	REQUIRE(parentWorld.m[0][3] == Catch::Approx(10.0f));
}

TEST_CASE("SceneGraph worldTransform with three levels", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto root = world.createEntity();
	auto mid = world.createEntity();
	auto leaf = world.createEntity();

	graph.addNode(root);
	graph.addNode(mid, root);
	graph.addNode(leaf, mid);

	graph.setLocalTransform(root, sgc::Mat4f::translation({1.0f, 0.0f, 0.0f}));
	graph.setLocalTransform(mid, sgc::Mat4f::translation({2.0f, 0.0f, 0.0f}));
	graph.setLocalTransform(leaf, sgc::Mat4f::translation({3.0f, 0.0f, 0.0f}));

	const auto leafWorld = graph.worldTransform(leaf);
	REQUIRE(leafWorld.m[0][3] == Catch::Approx(6.0f));
}

TEST_CASE("SceneGraph worldTransform for unknown entity returns identity", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::Entity unknown{999, 0};

	const auto mat = graph.worldTransform(unknown);
	REQUIRE(mat.m[0][0] == Catch::Approx(1.0f));
	REQUIRE(mat.m[3][3] == Catch::Approx(1.0f));
}

TEST_CASE("SceneGraph children returns empty for leaf node", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;
	auto entity = world.createEntity();

	graph.addNode(entity);
	REQUIRE(graph.children(entity).empty());
}

TEST_CASE("SceneGraph children for unknown node returns empty", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::Entity unknown{99, 0};
	REQUIRE(graph.children(unknown).empty());
}

TEST_CASE("SceneGraph rootNodes returns nodes without parents", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto root1 = world.createEntity();
	auto root2 = world.createEntity();
	auto child = world.createEntity();

	graph.addNode(root1);
	graph.addNode(root2);
	graph.addNode(child, root1);

	const auto roots = graph.rootNodes();
	REQUIRE(roots.size() == 2);
}

TEST_CASE("SceneGraph toJson includes nodeCount and roots", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	sgc::ecs::World world;

	auto root = world.createEntity();
	auto child = world.createEntity();
	graph.addNode(root);
	graph.addNode(child, root);

	const auto json = graph.toJson();
	REQUIRE(json.find("\"nodeCount\":2") != std::string::npos);
	REQUIRE(json.find("\"roots\":[") != std::string::npos);
}

TEST_CASE("SceneGraph empty toJson", "[mitiru][scene]")
{
	mitiru::scene::SceneGraph graph;
	const auto json = graph.toJson();
	REQUIRE(json.find("\"nodeCount\":0") != std::string::npos);
	REQUIRE(json.find("\"roots\":[]") != std::string::npos);
}
