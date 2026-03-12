#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/Mitiru.hpp>
#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/scene/MitiruScene.hpp>

// --- minimal Game implementation for testing ---

class TestGame : public mitiru::Game
{
public:
	int updateCount = 0;
	int drawCount = 0;
	float lastDt = 0.0f;  ///< 最後に受け取ったデルタタイム

	void update(float dt) override
	{
		lastDt = dt;
		++updateCount;
	}

	void draw(mitiru::Screen& screen) override
	{
		screen.drawRect(sgc::Rectf{10.0f, 10.0f, 100.0f, 50.0f},
		                sgc::Colorf{1.0f, 0.0f, 0.0f, 1.0f});
		++drawCount;
	}

	mitiru::Size layout(int outsideWidth, int outsideHeight) override
	{
		static_cast<void>(outsideWidth);
		static_cast<void>(outsideHeight);
		return {800, 600};
	}
};

// --- Engine tests ---

TEST_CASE("Engine stepFrames runs headless", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;
	config.targetTps = 60.0f;

	engine.stepFrames(game, 100, config);

	REQUIRE(game.updateCount == 100);
	REQUIRE(game.drawCount == 100);
}

TEST_CASE("Engine frameNumber advances", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 50, config);

	REQUIRE(engine.frameNumber() == 50);
}

TEST_CASE("Engine snapshot returns valid JSON", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 10, config);

	const auto json = engine.snapshot();
	REQUIRE(json.find("\"frameNumber\":10") != std::string::npos);
}

TEST_CASE("Engine capture returns pixel data in headless", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 1, config);

	const auto pixels = engine.capture();
	// NullDevice returns width*height*4 bytes (RGBA)
	REQUIRE(pixels.size() == 800 * 600 * 4);
}

TEST_CASE("Engine passes dt to Game::update in deterministic mode", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;
	config.targetTps = 60.0f;
	config.deterministic = true;

	engine.stepFrames(game, 1, config);

	// 決定性モードでは dt は 1/60 秒になる
	REQUIRE(game.lastDt == Catch::Approx(1.0f / 60.0f));
}

// --- Clock tests ---

TEST_CASE("Clock deterministic mode", "[mitiru]")
{
	mitiru::Clock clock(60.0f, true);

	const float dt = clock.tick();
	REQUIRE(dt == Catch::Approx(1.0f / 60.0f));
	REQUIRE(clock.frameNumber() == 1);

	clock.tick();
	clock.tick();
	REQUIRE(clock.frameNumber() == 3);
	REQUIRE(clock.elapsed() == Catch::Approx(3.0 / 60.0));
}

// --- Screen tests ---

TEST_CASE("Screen tracks draw calls", "[mitiru]")
{
	mitiru::Screen screen(800, 600);

	REQUIRE(screen.drawCallCount() == 0);

	screen.drawRect(sgc::Rectf{0, 0, 100, 50}, sgc::Colorf{1, 1, 1, 1});
	screen.drawCircle(sgc::Vec2f{50, 50}, 25.0f, sgc::Colorf{0, 1, 0, 1});
	screen.drawLine(sgc::Vec2f{0, 0}, sgc::Vec2f{100, 100}, sgc::Colorf{1, 0, 0, 1});

	REQUIRE(screen.drawCallCount() == 3);

	screen.resetDrawCallCount();
	REQUIRE(screen.drawCallCount() == 0);
}

TEST_CASE("Screen reports correct size", "[mitiru]")
{
	mitiru::Screen screen(1920, 1080);
	REQUIRE(screen.width() == 1920);
	REQUIRE(screen.height() == 1080);

	screen.resize(1280, 720);
	REQUIRE(screen.width() == 1280);
	REQUIRE(screen.height() == 720);
}

// --- InputState tests ---

TEST_CASE("InputState key tracking", "[mitiru]")
{
	mitiru::InputState state;

	REQUIRE_FALSE(state.isKeyDown(65));

	state.setKeyDown(65, true);
	REQUIRE(state.isKeyDown(65));

	state.setKeyDown(65, false);
	REQUIRE_FALSE(state.isKeyDown(65));
}

TEST_CASE("InputState mouse tracking", "[mitiru]")
{
	mitiru::InputState state;

	state.setMousePosition(400.0f, 300.0f);
	const auto [mx, my] = state.mousePosition();
	REQUIRE(mx == Catch::Approx(400.0f));
	REQUIRE(my == Catch::Approx(300.0f));

	REQUIRE_FALSE(state.isMouseButtonDown(mitiru::MouseButton::Left));
	state.setMouseButtonDown(mitiru::MouseButton::Left, true);
	REQUIRE(state.isMouseButtonDown(mitiru::MouseButton::Left));
}

TEST_CASE("InputState boundary checks", "[mitiru]")
{
	mitiru::InputState state;

	// Out of range keys should not crash
	REQUIRE_FALSE(state.isKeyDown(-1));
	REQUIRE_FALSE(state.isKeyDown(300));

	state.setKeyDown(-1, true);   // should be no-op
	state.setKeyDown(300, true);  // should be no-op
}

TEST_CASE("InputState justPressed detection", "[mitiru]")
{
	mitiru::InputState state;

	// Initially nothing is just pressed
	REQUIRE_FALSE(state.isKeyJustPressed(65));

	// Press key
	state.setKeyDown(65, true);
	// Before beginFrame, justPressed should work based on prev=false, current=true
	REQUIRE(state.isKeyJustPressed(65));

	// After beginFrame, prev becomes true, so justPressed is false
	state.beginFrame();
	REQUIRE_FALSE(state.isKeyJustPressed(65));
	REQUIRE(state.isKeyDown(65)); // Still held
}

TEST_CASE("InputState justReleased detection", "[mitiru]")
{
	mitiru::InputState state;

	// Press key
	state.setKeyDown(65, true);
	state.beginFrame();

	// Release key
	state.setKeyDown(65, false);
	REQUIRE(state.isKeyJustReleased(65));

	// After beginFrame, prev becomes false too
	state.beginFrame();
	REQUIRE_FALSE(state.isKeyJustReleased(65));
}

TEST_CASE("InputState mouse justPressed detection", "[mitiru]")
{
	mitiru::InputState state;

	REQUIRE_FALSE(state.isMouseButtonJustPressed(mitiru::MouseButton::Left));

	state.setMouseButtonDown(mitiru::MouseButton::Left, true);
	REQUIRE(state.isMouseButtonJustPressed(mitiru::MouseButton::Left));

	state.beginFrame();
	REQUIRE_FALSE(state.isMouseButtonJustPressed(mitiru::MouseButton::Left));
}

TEST_CASE("InputState justPressed boundary checks", "[mitiru]")
{
	mitiru::InputState state;

	// Out of range should not crash and return false
	REQUIRE_FALSE(state.isKeyJustPressed(-1));
	REQUIRE_FALSE(state.isKeyJustPressed(300));
	REQUIRE_FALSE(state.isKeyJustReleased(-1));
	REQUIRE_FALSE(state.isKeyJustReleased(300));
}

// --- InputInjector tests ---

TEST_CASE("InputInjector inject and consume", "[mitiru]")
{
	mitiru::InputInjector injector;

	REQUIRE(injector.pendingCount() == 0);

	mitiru::InputCommand cmd;
	cmd.type = mitiru::InputCommandType::KeyDown;
	cmd.keyCode = 65;
	injector.inject(cmd);

	REQUIRE(injector.pendingCount() == 1);

	auto commands = injector.consumePending();
	REQUIRE(commands.size() == 1);
	REQUIRE(commands[0].keyCode == 65);

	REQUIRE(injector.pendingCount() == 0);
}

TEST_CASE("Engine input injection integration", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	// Inject a key press before stepping
	mitiru::InputCommand cmd;
	cmd.type = mitiru::InputCommandType::KeyDown;
	cmd.keyCode = 32; // Space
	engine.inputInjector().inject(cmd);

	engine.stepFrames(game, 1, config);

	// Verify input state reflects the injection
	REQUIRE(engine.inputState().isKeyDown(32));
}

// --- Snapshot tests ---

TEST_CASE("SnapshotData toJson", "[mitiru]")
{
	mitiru::SnapshotData data;
	data.frameNumber = 42;
	data.timestamp = 0.7f;
	data.entityCount = 5;

	const auto json = data.toJson();
	REQUIRE(json.find("\"frameNumber\":42") != std::string::npos);
	REQUIRE(json.find("\"entityCount\":5") != std::string::npos);
}

// --- NullDevice tests ---

TEST_CASE("NullDevice readPixels returns correct size", "[mitiru]")
{
	mitiru::gfx::NullDevice device;

	auto pixels = device.readPixels(320, 240);
	REQUIRE(pixels.size() == 320 * 240 * 4);

	// All pixels should be black with alpha=255
	for (std::size_t i = 0; i < pixels.size(); i += 4)
	{
		REQUIRE(pixels[i] == 0);       // R
		REQUIRE(pixels[i + 1] == 0);   // G
		REQUIRE(pixels[i + 2] == 0);   // B
		REQUIRE(pixels[i + 3] == 255); // A
	}
}

// --- HeadlessPlatform tests ---

TEST_CASE("HeadlessPlatform creates window", "[mitiru]")
{
	mitiru::HeadlessPlatform platform;
	auto window = platform.createWindow("Test", 800, 600);

	REQUIRE(window != nullptr);
	REQUIRE(window->width() == 800);
	REQUIRE(window->height() == 600);
	REQUIRE_FALSE(window->shouldClose());
}

// --- helper scene for snapshot tests ---

class DummyScene : public mitiru::scene::MitiruScene
{
public:
	void onUpdate(float /*dt*/) override {}
	void onDraw(mitiru::Screen& /*screen*/) override {}
	[[nodiscard]] std::string name() const override { return "DummyScene"; }
};

// --- enriched snapshot tests ---

TEST_CASE("Engine snapshot includes world entity data", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 1, config);

	/// ワールドを設定してエンティティを作成
	mitiru::ecs::MitiruWorld world;
	auto e1 = world.world().createEntity();
	auto e2 = world.world().createEntity();
	world.setTag(e1, "player");
	world.addLabel(e1, "controllable");

	engine.setWorld(&world);

	const auto json = engine.snapshot();

	/// entityCount がワールドのエンティティ数を反映する
	REQUIRE(json.find("\"entityCount\":2") != std::string::npos);
	/// world スナップショットが含まれる
	REQUIRE(json.find("\"world\":") != std::string::npos);
	REQUIRE(json.find("\"tags\":{") != std::string::npos);
	REQUIRE(json.find("\"player\"") != std::string::npos);
}

TEST_CASE("Engine snapshot includes scene info", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 1, config);

	/// シーンマネージャーを設定
	mitiru::scene::MitiruSceneManager sceneMgr;
	sceneMgr.pushScene(std::make_unique<DummyScene>());

	engine.setSceneManager(&sceneMgr);

	const auto json = engine.snapshot();

	REQUIRE(json.find("\"sceneInfo\":\"DummyScene\"") != std::string::npos);
}

TEST_CASE("Engine snapshot includes draw call count", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 1, config);

	const auto json = engine.snapshot();

	/// drawCallCount フィールドが存在する
	REQUIRE(json.find("\"drawCallCount\":") != std::string::npos);
}

TEST_CASE("Engine snapshot without world or scene manager", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	engine.stepFrames(game, 5, config);

	const auto json = engine.snapshot();

	/// ワールド/シーン未設定時はentityCount=0、world/sceneInfoフィールドなし
	REQUIRE(json.find("\"entityCount\":0") != std::string::npos);
	REQUIRE(json.find("\"world\":") == std::string::npos);
	REQUIRE(json.find("\"sceneInfo\":") == std::string::npos);
}

TEST_CASE("Engine setWorld with nullptr clears world", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;
	engine.stepFrames(game, 1, config);

	mitiru::ecs::MitiruWorld world;
	world.world().createEntity();

	engine.setWorld(&world);
	auto json1 = engine.snapshot();
	REQUIRE(json1.find("\"entityCount\":1") != std::string::npos);

	engine.setWorld(nullptr);
	auto json2 = engine.snapshot();
	REQUIRE(json2.find("\"entityCount\":0") != std::string::npos);
	REQUIRE(json2.find("\"world\":") == std::string::npos);
}

// --- Scene that counts update/draw calls ---

class CountingScene : public mitiru::scene::MitiruScene
{
public:
	int updateCount = 0;
	int drawCount = 0;

	void onUpdate(float /*dt*/) override
	{
		++updateCount;
	}

	void onDraw(mitiru::Screen& /*screen*/) override
	{
		++drawCount;
	}

	[[nodiscard]] std::string name() const override { return "CountingScene"; }
};

// --- SceneManager integration in game loop ---

TEST_CASE("Engine stepFrames calls scene onUpdate and onDraw", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;
	config.targetTps = 60.0f;

	/// シーンマネージャーを作成しシーンをプッシュ
	mitiru::scene::MitiruSceneManager sceneMgr;
	auto scene = std::make_unique<CountingScene>();
	auto* scenePtr = scene.get();
	sceneMgr.pushScene(std::move(scene));

	engine.setSceneManager(&sceneMgr);

	engine.stepFrames(game, 10, config);

	/// シーンの onUpdate/onDraw がフレーム数分呼ばれる
	REQUIRE(scenePtr->updateCount == 10);
	REQUIRE(scenePtr->drawCount == 10);

	/// ゲーム側の update/draw も依然として呼ばれる
	REQUIRE(game.updateCount == 10);
	REQUIRE(game.drawCount == 10);
}

TEST_CASE("Engine stepFrames without scene manager does not crash", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	/// シーンマネージャー未設定でもクラッシュしない
	engine.stepFrames(game, 5, config);

	REQUIRE(game.updateCount == 5);
	REQUIRE(game.drawCount == 5);
}

TEST_CASE("Engine stepFrames with empty scene manager does not crash", "[mitiru]")
{
	TestGame game;
	mitiru::Engine engine;

	mitiru::EngineConfig config;
	config.headless = true;

	/// 空のシーンマネージャーを設定（currentScene() == nullptr）
	mitiru::scene::MitiruSceneManager sceneMgr;
	engine.setSceneManager(&sceneMgr);

	engine.stepFrames(game, 5, config);

	REQUIRE(game.updateCount == 5);
	REQUIRE(game.drawCount == 5);
}
