#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/bridge/AnimationBridge.hpp>
#include <mitiru/bridge/DebugDrawBridge.hpp>
#include <mitiru/bridge/DialogueBridge.hpp>
#include <mitiru/bridge/EventBridge.hpp>
#include <mitiru/bridge/I18nBridge.hpp>
#include <mitiru/bridge/ParticleBridge.hpp>
#include <mitiru/bridge/ProceduralBridge.hpp>
#include <mitiru/bridge/SaveBridge.hpp>
#include <mitiru/bridge/SteeringBridge.hpp>
#include <mitiru/bridge/TilemapBridge.hpp>
#include <mitiru/bridge/TransitionBridge.hpp>
#include <mitiru/bridge/VNBridge.hpp>

// ── AnimationBridge ─────────────────────────────────────────

TEST_CASE("AnimationBridge add tween and update", "[bridge][animation]")
{
	mitiru::bridge::AnimationBridge anim;

	sgc::Tween<float> tween;
	tween.from(0.0f).to(100.0f).during(1.0f);
	anim.addTween("test", std::move(tween));

	REQUIRE(anim.tweenCount() == 1);
	REQUIRE_FALSE(anim.isTweenComplete("test"));

	anim.update(0.5f);
	REQUIRE(anim.getTweenValue("test") == Catch::Approx(50.0f));
	REQUIRE_FALSE(anim.isTweenComplete("test"));

	anim.update(0.5f);
	REQUIRE(anim.getTweenValue("test") == Catch::Approx(100.0f));
	REQUIRE(anim.isTweenComplete("test"));
}

TEST_CASE("AnimationBridge remove tween", "[bridge][animation]")
{
	mitiru::bridge::AnimationBridge anim;

	sgc::Tween<float> tween;
	tween.from(0.0f).to(10.0f).during(1.0f);
	anim.addTween("temp", std::move(tween));
	REQUIRE(anim.tweenCount() == 1);

	anim.removeTween("temp");
	REQUIRE(anim.tweenCount() == 0);
	REQUIRE(anim.isTweenComplete("temp")); // not found -> true
}

TEST_CASE("AnimationBridge toJson contains tween data", "[bridge][animation]")
{
	mitiru::bridge::AnimationBridge anim;

	sgc::Tween<float> tween;
	tween.from(0.0f).to(50.0f).during(1.0f);
	anim.addTween("alpha", std::move(tween));

	const auto json = anim.toJson();
	REQUIRE(json.find("\"tweenCount\":1") != std::string::npos);
	REQUIRE(json.find("\"alpha\"") != std::string::npos);
}

// ── DialogueBridge ──────────────────────────────────────────

TEST_CASE("DialogueBridge load and start dialogue", "[bridge][dialogue]")
{
	mitiru::bridge::DialogueBridge dlg;

	sgc::dialogue::DialogueGraph graph;
	graph.addNode({"start", "NPC", "Hello!", {{"Hi!", "greet"}}});
	graph.addNode({"greet", "NPC", "Nice to meet you!", {}});
	graph.setStartNodeId("start");

	dlg.loadDialogue(std::move(graph));
	dlg.startDialogue("start");

	REQUIRE(dlg.isActive());
	REQUIRE(dlg.currentText() == "Hello!");
	REQUIRE(dlg.currentSpeaker() == "NPC");
}

TEST_CASE("DialogueBridge advance with single choice", "[bridge][dialogue]")
{
	mitiru::bridge::DialogueBridge dlg;

	sgc::dialogue::DialogueGraph graph;
	graph.addNode({"start", "NPC", "Hello!", {{"next", "end"}}});
	graph.addNode({"end", "NPC", "Goodbye!", {}});
	graph.setStartNodeId("start");

	dlg.loadDialogue(std::move(graph));
	dlg.startDialogue("start");

	REQUIRE(dlg.currentText() == "Hello!");

	dlg.advance(); // single choice -> auto advance
	REQUIRE(dlg.currentText() == "Goodbye!");

	dlg.advance(); // no choices -> ends
	REQUIRE_FALSE(dlg.isActive());
}

TEST_CASE("DialogueBridge selectChoice navigates to correct node", "[bridge][dialogue]")
{
	mitiru::bridge::DialogueBridge dlg;

	sgc::dialogue::DialogueGraph graph;
	graph.addNode({"start", "NPC", "Choose!", {{"A", "nodeA"}, {"B", "nodeB"}}});
	graph.addNode({"nodeA", "NPC", "You chose A", {}});
	graph.addNode({"nodeB", "NPC", "You chose B", {}});
	graph.setStartNodeId("start");

	dlg.loadDialogue(std::move(graph));
	dlg.startDialogue("start");

	auto choices = dlg.currentChoices();
	REQUIRE(choices.size() == 2);
	REQUIRE(choices[0] == "A");
	REQUIRE(choices[1] == "B");

	dlg.selectChoice(1);
	REQUIRE(dlg.currentText() == "You chose B");
}

TEST_CASE("DialogueBridge backlog records entries", "[bridge][dialogue]")
{
	mitiru::bridge::DialogueBridge dlg;

	sgc::dialogue::DialogueGraph graph;
	graph.addNode({"start", "NPC", "Line 1", {{"next", "end"}}});
	graph.addNode({"end", "NPC", "Line 2", {}});
	graph.setStartNodeId("start");

	dlg.loadDialogue(std::move(graph));
	dlg.startDialogue("start");
	dlg.advance();

	REQUIRE(dlg.backlogSize() == 2);
}

TEST_CASE("DialogueBridge toJson contains state", "[bridge][dialogue]")
{
	mitiru::bridge::DialogueBridge dlg;

	sgc::dialogue::DialogueGraph graph;
	graph.addNode({"start", "NPC", "Test", {}});
	graph.setStartNodeId("start");

	dlg.loadDialogue(std::move(graph));
	dlg.startDialogue("start");

	const auto json = dlg.toJson();
	REQUIRE(json.find("\"active\":true") != std::string::npos);
	REQUIRE(json.find("\"speaker\":\"NPC\"") != std::string::npos);
}

// ── ParticleBridge ──────────────────────────────────────────

TEST_CASE("ParticleBridge add system and emit", "[bridge][particle]")
{
	mitiru::bridge::ParticleBridge particles;

	sgc::EmitterConfig config;
	config.positionX = 100.0f;
	config.positionY = 200.0f;
	config.rate = 0.0f; // disable auto-emit
	config.lifetime = 5.0f;
	config.speed = 50.0f;

	particles.addSystem("sparks", config);
	REQUIRE(particles.systemCount() == 1);
	REQUIRE(particles.activeParticleCount("sparks") == 0);

	particles.emit("sparks", 10);
	REQUIRE(particles.activeParticleCount("sparks") == 10);
}

TEST_CASE("ParticleBridge update reduces lifetime", "[bridge][particle]")
{
	mitiru::bridge::ParticleBridge particles;

	sgc::EmitterConfig config;
	config.rate = 0.0f;
	config.lifetime = 0.5f;

	particles.addSystem("short", config);
	particles.emit("short", 5);
	REQUIRE(particles.activeParticleCount("short") == 5);

	// Update past lifetime -> particles should die
	particles.update(0.6f);
	REQUIRE(particles.activeParticleCount("short") == 0);
}

TEST_CASE("ParticleBridge removeSystem", "[bridge][particle]")
{
	mitiru::bridge::ParticleBridge particles;

	sgc::EmitterConfig config;
	particles.addSystem("temp", config);
	REQUIRE(particles.systemCount() == 1);

	particles.removeSystem("temp");
	REQUIRE(particles.systemCount() == 0);
}

TEST_CASE("ParticleBridge toJson contains system info", "[bridge][particle]")
{
	mitiru::bridge::ParticleBridge particles;

	sgc::EmitterConfig config;
	particles.addSystem("fx", config);

	const auto json = particles.toJson();
	REQUIRE(json.find("\"systemCount\":1") != std::string::npos);
	REQUIRE(json.find("\"fx\"") != std::string::npos);
}

// ── TransitionBridge ────────────────────────────────────────

TEST_CASE("TransitionBridge startFadeOut and update", "[bridge][transition]")
{
	mitiru::bridge::TransitionBridge transition;

	REQUIRE_FALSE(transition.isActive());
	REQUIRE_FALSE(transition.isComplete());

	transition.startFadeOut(0.5f);
	REQUIRE(transition.isActive());

	// At start, alpha should be near 0
	REQUIRE(transition.alpha() == Catch::Approx(0.0f));

	// After half the out duration
	transition.update(0.25f);
	REQUIRE(transition.alpha() == Catch::Approx(0.5f));
	REQUIRE(transition.isActive());

	// Complete the out phase, enter in phase
	transition.update(0.25f);
	REQUIRE(transition.phase() == sgc::TransitionPhase::In);

	// Complete the in phase
	transition.update(0.5f);
	REQUIRE(transition.isComplete());
}

TEST_CASE("TransitionBridge reset clears state", "[bridge][transition]")
{
	mitiru::bridge::TransitionBridge transition;

	transition.startFadeOut(0.3f);
	transition.update(0.1f);
	REQUIRE(transition.isActive());

	transition.reset();
	REQUIRE_FALSE(transition.isActive());
	REQUIRE_FALSE(transition.isComplete());
}

TEST_CASE("TransitionBridge toJson contains state", "[bridge][transition]")
{
	mitiru::bridge::TransitionBridge transition;
	transition.startFadeOut(0.5f);

	const auto json = transition.toJson();
	REQUIRE(json.find("\"active\":true") != std::string::npos);
	REQUIRE(json.find("\"phase\":\"Out\"") != std::string::npos);
}

// ── EventBridge ─────────────────────────────────────────────

namespace
{
	struct TestEvent
	{
		int value;
	};
} // namespace

TEST_CASE("EventBridge emit and receive event", "[bridge][event]")
{
	mitiru::bridge::EventBridge events;

	int received = 0;
	auto id = events.on<TestEvent>([&received](const TestEvent& e)
	{
		received = e.value;
	});

	events.emit(TestEvent{42});
	REQUIRE(received == 42);

	events.off(id);
	events.emit(TestEvent{99});
	REQUIRE(received == 42); // should not change after off
}

TEST_CASE("EventBridge multiple listeners", "[bridge][event]")
{
	mitiru::bridge::EventBridge events;

	int count = 0;
	events.on<TestEvent>([&count](const TestEvent&) { ++count; });
	events.on<TestEvent>([&count](const TestEvent&) { ++count; });

	events.emit(TestEvent{1});
	REQUIRE(count == 2);
}

TEST_CASE("EventBridge toJson returns valid JSON", "[bridge][event]")
{
	mitiru::bridge::EventBridge events;
	const auto json = events.toJson();
	REQUIRE(json.find("\"type\":\"EventBridge\"") != std::string::npos);
}

// ── SaveBridge ──────────────────────────────────────────────

TEST_CASE("SaveBridge save and load", "[bridge][save]")
{
	mitiru::bridge::SaveBridge save;

	REQUIRE_FALSE(save.exists(1));

	save.save(1, "hello world");
	REQUIRE(save.exists(1));
	REQUIRE(save.load(1) == "hello world");
	REQUIRE(save.slotCount() == 1);
}

TEST_CASE("SaveBridge deleteSave removes slot", "[bridge][save]")
{
	mitiru::bridge::SaveBridge save;

	save.save(1, "data1");
	save.save(2, "data2");
	REQUIRE(save.slotCount() == 2);

	save.deleteSave(1);
	REQUIRE_FALSE(save.exists(1));
	REQUIRE(save.exists(2));
	REQUIRE(save.slotCount() == 1);
}

TEST_CASE("SaveBridge listSlots returns correct slot numbers", "[bridge][save]")
{
	mitiru::bridge::SaveBridge save;

	save.save(3, "a");
	save.save(7, "b");

	const auto slots = save.listSlots();
	REQUIRE(slots.size() == 2);

	// Check both slot numbers are present (order may vary)
	const bool has3 = std::find(slots.begin(), slots.end(), 3) != slots.end();
	const bool has7 = std::find(slots.begin(), slots.end(), 7) != slots.end();
	REQUIRE(has3);
	REQUIRE(has7);
}

TEST_CASE("SaveBridge load nonexistent slot returns empty", "[bridge][save]")
{
	mitiru::bridge::SaveBridge save;
	REQUIRE(save.load(999) == "");
}

TEST_CASE("SaveBridge toJson contains slot info", "[bridge][save]")
{
	mitiru::bridge::SaveBridge save;
	save.save(1, "test");

	const auto json = save.toJson();
	REQUIRE(json.find("\"slotCount\":1") != std::string::npos);
	REQUIRE(json.find("\"slot_1\"") != std::string::npos);
}

// ── VNBridge ────────────────────────────────────────────────

TEST_CASE("VNBridge load scene and start", "[bridge][vn]")
{
	mitiru::bridge::VNBridge vn;

	REQUIRE(vn.isFinished());
	REQUIRE(vn.phase() == sgc::vn::VNPhase::Idle);

	sgc::vn::VNScene scene;
	scene.addCommand({sgc::vn::VNCommand::Type::Say, "npc", "Hello!", ""});
	vn.loadScene(std::move(scene));
	vn.start();

	REQUIRE_FALSE(vn.isFinished());
	REQUIRE(vn.phase() == sgc::vn::VNPhase::Displaying);
	REQUIRE(vn.currentText() == "Hello!");
	REQUIRE(vn.currentSpeaker() == "npc");
}

TEST_CASE("VNBridge update advances text", "[bridge][vn]")
{
	mitiru::bridge::VNBridge vn;

	sgc::vn::VNScene scene;
	scene.addCommand({sgc::vn::VNCommand::Type::Say, "npc", "Hi", ""});
	vn.loadScene(std::move(scene));
	vn.start();

	// Skip to show all text
	vn.update(0.0f, false, true);
	REQUIRE(vn.phase() == sgc::vn::VNPhase::WaitingInput);

	// Advance past the text
	vn.update(0.0f, true, false);
	REQUIRE(vn.isFinished());
}

TEST_CASE("VNBridge selectChoice works", "[bridge][vn]")
{
	mitiru::bridge::VNBridge vn;

	sgc::vn::VNScene scene;
	scene.addCommand({sgc::vn::VNCommand::Type::Choice, "", "", "",
		sgc::vn::CharacterPosition::Center, 0.0f,
		{{"Yes", "y"}, {"No", "n"}}});
	vn.loadScene(std::move(scene));
	vn.start();

	REQUIRE(vn.hasChoices());
	vn.selectChoice(0);
	REQUIRE(vn.isFinished());
}

TEST_CASE("VNBridge parseEffects parses tags", "[bridge][vn]")
{
	mitiru::bridge::VNBridge vn;
	auto segments = vn.parseEffects("{shake}Quake{/shake} Normal");
	REQUIRE(segments.size() == 2);
	REQUIRE(segments[0].text == "Quake");
	REQUIRE(segments[0].effect == sgc::vn::TextEffect::Shake);
	REQUIRE(segments[1].text == " Normal");
	REQUIRE(segments[1].effect == sgc::vn::TextEffect::None);
}

TEST_CASE("VNBridge toJson contains state", "[bridge][vn]")
{
	mitiru::bridge::VNBridge vn;

	sgc::vn::VNScene scene;
	scene.addCommand({sgc::vn::VNCommand::Type::Say, "npc", "Test", ""});
	vn.loadScene(std::move(scene));
	vn.start();

	const auto json = vn.toJson();
	REQUIRE(json.find("\"hasScene\":true") != std::string::npos);
	REQUIRE(json.find("\"phase\":\"Displaying\"") != std::string::npos);
}

// ── SteeringBridge ──────────────────────────────────────────

TEST_CASE("SteeringBridge add agent and seek", "[bridge][steering]")
{
	mitiru::bridge::SteeringBridge steering;

	sgc::ai::SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.velocity = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;
	steering.addAgent("player", agent);

	REQUIRE(steering.agentCount() == 1);
	REQUIRE(steering.getAgent("player") != nullptr);

	const auto force = steering.seek("player", {100.0f, 0.0f});
	REQUIRE(force.x > 0.0f);
}

TEST_CASE("SteeringBridge apply force updates position", "[bridge][steering]")
{
	mitiru::bridge::SteeringBridge steering;

	sgc::ai::SteeringAgent<float> agent;
	agent.position = {0.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;
	steering.addAgent("mover", agent);

	const auto force = steering.seek("mover", {100.0f, 0.0f});
	steering.applySteering("mover", force, 1.0f);

	const auto* updated = steering.getAgent("mover");
	REQUIRE(updated != nullptr);
	REQUIRE(updated->position.x > 0.0f);
}

TEST_CASE("SteeringBridge flee returns opposite direction", "[bridge][steering]")
{
	mitiru::bridge::SteeringBridge steering;

	sgc::ai::SteeringAgent<float> agent;
	agent.position = {50.0f, 0.0f};
	agent.maxSpeed = 100.0f;
	agent.maxForce = 50.0f;
	steering.addAgent("runner", agent);

	const auto force = steering.flee("runner", {0.0f, 0.0f});
	REQUIRE(force.x > 0.0f);  // Fleeing away from origin
}

TEST_CASE("SteeringBridge toJson contains agent data", "[bridge][steering]")
{
	mitiru::bridge::SteeringBridge steering;

	sgc::ai::SteeringAgent<float> agent;
	steering.addAgent("a1", agent);

	const auto json = steering.toJson();
	REQUIRE(json.find("\"agentCount\":1") != std::string::npos);
	REQUIRE(json.find("\"a1\"") != std::string::npos);
}

// ── TilemapBridge ───────────────────────────────────────────

TEST_CASE("TilemapBridge create and set tile", "[bridge][tilemap]")
{
	mitiru::bridge::TilemapBridge tilemap;

	tilemap.create("world", 16, 16, 32);
	REQUIRE(tilemap.tilemapCount() == 1);

	tilemap.setTile("world", 3, 4, 5);
	REQUIRE(tilemap.getTile("world", 3, 4) == 5);
}

TEST_CASE("TilemapBridge getTile returns 0 for empty", "[bridge][tilemap]")
{
	mitiru::bridge::TilemapBridge tilemap;

	tilemap.create("test", 8, 8, 16);
	REQUIRE(tilemap.getTile("test", 0, 0) == 0);  // empty tile
}

TEST_CASE("TilemapBridge removeTilemap", "[bridge][tilemap]")
{
	mitiru::bridge::TilemapBridge tilemap;

	tilemap.create("temp", 4, 4, 16);
	REQUIRE(tilemap.tilemapCount() == 1);

	tilemap.removeTilemap("temp");
	REQUIRE(tilemap.tilemapCount() == 0);
}

TEST_CASE("TilemapBridge toJson contains tilemap info", "[bridge][tilemap]")
{
	mitiru::bridge::TilemapBridge tilemap;

	tilemap.create("level", 10, 10, 32);

	const auto json = tilemap.toJson();
	REQUIRE(json.find("\"tilemapCount\":1") != std::string::npos);
	REQUIRE(json.find("\"level\"") != std::string::npos);
}

// ── I18nBridge ──────────────────────────────────────────────

TEST_CASE("I18nBridge load language and translate", "[bridge][i18n]")
{
	mitiru::bridge::I18nBridge i18n;

	sgc::i18n::StringTable en;
	en.addEntry("greeting", "Hello!");
	i18n.loadLanguage("en", std::move(en));

	sgc::i18n::StringTable ja;
	ja.addEntry("greeting", "Konnichiwa!");
	i18n.loadLanguage("ja", std::move(ja));

	i18n.setCurrentLanguage("en");
	REQUIRE(i18n.translate("greeting") == "Hello!");

	i18n.setCurrentLanguage("ja");
	REQUIRE(i18n.translate("greeting") == "Konnichiwa!");
}

TEST_CASE("I18nBridge currentLanguage and availableLanguages", "[bridge][i18n]")
{
	mitiru::bridge::I18nBridge i18n;

	sgc::i18n::StringTable en;
	en.addEntry("test", "Test");
	i18n.loadLanguage("en", std::move(en));

	i18n.setCurrentLanguage("en");
	REQUIRE(i18n.currentLanguage() == "en");

	const auto langs = i18n.availableLanguages();
	REQUIRE(langs.size() == 1);
}

TEST_CASE("I18nBridge translate missing key returns MISSING", "[bridge][i18n]")
{
	mitiru::bridge::I18nBridge i18n;

	sgc::i18n::StringTable en;
	i18n.loadLanguage("en", std::move(en));
	i18n.setCurrentLanguage("en");

	REQUIRE(i18n.translate("nonexistent") == "[MISSING]");
}

TEST_CASE("I18nBridge toJson contains language info", "[bridge][i18n]")
{
	mitiru::bridge::I18nBridge i18n;

	sgc::i18n::StringTable en;
	i18n.loadLanguage("en", std::move(en));
	i18n.setCurrentLanguage("en");

	const auto json = i18n.toJson();
	REQUIRE(json.find("\"currentLanguage\":\"en\"") != std::string::npos);
	REQUIRE(json.find("\"languageCount\":1") != std::string::npos);
}

// ── DebugDrawBridge ─────────────────────────────────────────

TEST_CASE("DebugDrawBridge draw rect adds to pending", "[bridge][debugdraw]")
{
	mitiru::bridge::DebugDrawBridge debug;

	REQUIRE(debug.pendingCount() == 0);

	debug.drawRect({{0.0f, 0.0f}, {100.0f, 50.0f}}, {1.0f, 0.0f, 0.0f, 1.0f});
	REQUIRE(debug.pendingCount() == 1);

	debug.drawCircle({50.0f, 50.0f}, 25.0f, {0.0f, 1.0f, 0.0f, 1.0f});
	REQUIRE(debug.pendingCount() == 2);
}

TEST_CASE("DebugDrawBridge setEnabled toggles state", "[bridge][debugdraw]")
{
	mitiru::bridge::DebugDrawBridge debug;

	REQUIRE(debug.isEnabled());

	debug.setEnabled(false);
	REQUIRE_FALSE(debug.isEnabled());

	debug.setEnabled(true);
	REQUIRE(debug.isEnabled());
}

TEST_CASE("DebugDrawBridge drawLine and drawArrow add commands", "[bridge][debugdraw]")
{
	mitiru::bridge::DebugDrawBridge debug;

	debug.drawLine({0.0f, 0.0f}, {100.0f, 100.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
	debug.drawArrow({0.0f, 0.0f}, {50.0f, 50.0f}, {1.0f, 1.0f, 0.0f, 1.0f});
	REQUIRE(debug.pendingCount() == 2);
}

TEST_CASE("DebugDrawBridge toJson contains state", "[bridge][debugdraw]")
{
	mitiru::bridge::DebugDrawBridge debug;

	debug.drawRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1.0f, 1.0f, 1.0f, 1.0f});

	const auto json = debug.toJson();
	REQUIRE(json.find("\"enabled\":true") != std::string::npos);
	REQUIRE(json.find("\"pendingCount\":1") != std::string::npos);
}

// ── ProceduralBridge ────────────────────────────────────────

TEST_CASE("ProceduralBridge generateDungeon produces rooms", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;

	sgc::procedural::DungeonConfig config;
	config.mapWidth = 40;
	config.mapHeight = 30;
	config.maxDepth = 3;

	const auto result = proc.generateDungeon(config, 42);
	REQUIRE(result.width == 40);
	REQUIRE(result.height == 30);
	REQUIRE_FALSE(result.rooms.empty());
}

TEST_CASE("ProceduralBridge generateTerrain produces heightmap", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;

	sgc::procedural::TerrainConfig config;
	config.width = 16;
	config.height = 16;

	const auto result = proc.generateTerrain(config, 0);
	REQUIRE(result.width == 16);
	REQUIRE(result.height == 16);
	REQUIRE(result.heightmap.size() == 16);
	REQUIRE(result.biomes.size() == 16);
}

TEST_CASE("ProceduralBridge solveWFC with simple rules", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;

	sgc::procedural::WFCConfig config;
	config.width = 3;
	config.height = 3;
	config.tileSet = {0, 1};
	// Allow all adjacencies
	for (int dir = 0; dir < 4; ++dir)
	{
		config.rules.push_back({0, 0, dir});
		config.rules.push_back({0, 1, dir});
		config.rules.push_back({1, 0, dir});
		config.rules.push_back({1, 1, dir});
	}

	const auto result = proc.solveWFC(config);
	REQUIRE(result.success);
	REQUIRE(result.grid.size() == 9);
}

TEST_CASE("ProceduralBridge generateLSystem produces string", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;

	sgc::procedural::LSystemConfig config;
	config.axiom = "F";
	config.rules = {{'F', "F+F"}};
	config.iterations = 2;

	const auto lString = proc.generateLSystem(config);
	REQUIRE_FALSE(lString.empty());
	REQUIRE(lString.find('F') != std::string::npos);
	REQUIRE(lString.find('+') != std::string::npos);
}

TEST_CASE("ProceduralBridge interpretTurtle generates segments", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;

	const auto result = proc.interpretTurtle("F+F", {});
	REQUIRE(result.segments.size() == 2);
}

TEST_CASE("ProceduralBridge toJson returns valid JSON", "[bridge][procedural]")
{
	mitiru::bridge::ProceduralBridge proc;
	const auto json = proc.toJson();
	REQUIRE(json.find("\"type\":\"ProceduralBridge\"") != std::string::npos);
}
