#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/bridge/AnimationBridge.hpp>
#include <mitiru/bridge/DialogueBridge.hpp>
#include <mitiru/bridge/EventBridge.hpp>
#include <mitiru/bridge/ParticleBridge.hpp>
#include <mitiru/bridge/SaveBridge.hpp>
#include <mitiru/bridge/TransitionBridge.hpp>

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
