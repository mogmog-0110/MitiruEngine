#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include <mitiru/audio/NullAudioEngine.hpp>
#include <mitiru/audio/SoftAudioEngine.hpp>
#include <mitiru/audio/MitiruAudioPlayer.hpp>
#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/AssetManager.hpp>
#include <mitiru/resource/HotReloadManager.hpp>
#include <mitiru/bridge/PhysicsBridge.hpp>
#include <mitiru/bridge/AiBridge.hpp>
#include <mitiru/bridge/UiBridge.hpp>

// ── Test asset types ────────────────────────────────────────

struct TestTexture
{
	int width = 0;
	int height = 0;
	std::string name;
};

struct TestSound
{
	std::string path;
	float duration = 0.0f;
};

// ============================================================
// NullAudioEngine
// ============================================================

TEST_CASE("NullAudioEngine playSound does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	// All methods are no-ops; should not crash
	engine.playSound("explosion");
	engine.playSound("");
}

TEST_CASE("NullAudioEngine stopSound does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	engine.stopSound("explosion");
}

TEST_CASE("NullAudioEngine playMusic does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	engine.playMusic("bgm_battle");
}

TEST_CASE("NullAudioEngine stopMusic does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	engine.stopMusic();
}

TEST_CASE("NullAudioEngine setVolume does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	engine.setVolume(0.5f);
	engine.setVolume(0.0f);
	engine.setVolume(1.0f);
}

TEST_CASE("NullAudioEngine isPlaying always returns false", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	REQUIRE_FALSE(engine.isPlaying("anything"));
	REQUIRE_FALSE(engine.isPlaying(""));

	// Even after playSound, still false (null engine)
	engine.playSound("test");
	REQUIRE_FALSE(engine.isPlaying("test"));
}

TEST_CASE("NullAudioEngine full sequence does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	engine.setVolume(0.8f);
	engine.playMusic("bgm");
	engine.playSound("click");
	engine.stopSound("click");
	engine.stopMusic();
	REQUIRE_FALSE(engine.isPlaying("bgm"));
}

// ============================================================
// SoftAudioEngine
// ============================================================

TEST_CASE("SoftAudioEngine playSound tracks playing state", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	REQUIRE_FALSE(engine.isPlaying("explosion"));

	engine.playSound("explosion");
	REQUIRE(engine.isPlaying("explosion"));
}

TEST_CASE("SoftAudioEngine stopSound removes from tracking", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.playSound("click");
	REQUIRE(engine.isPlaying("click"));

	engine.stopSound("click");
	REQUIRE_FALSE(engine.isPlaying("click"));
}

TEST_CASE("SoftAudioEngine multiple sounds tracked independently", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.playSound("click");
	engine.playSound("boom");
	engine.playSound("ding");

	REQUIRE(engine.playingSoundCount() == 3);
	REQUIRE(engine.isPlaying("click"));
	REQUIRE(engine.isPlaying("boom"));
	REQUIRE(engine.isPlaying("ding"));

	engine.stopSound("boom");
	REQUIRE(engine.playingSoundCount() == 2);
	REQUIRE_FALSE(engine.isPlaying("boom"));
	REQUIRE(engine.isPlaying("click"));
	REQUIRE(engine.isPlaying("ding"));
}

TEST_CASE("SoftAudioEngine playMusic and stopMusic", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	REQUIRE_FALSE(engine.isMusicPlaying());
	REQUIRE(engine.currentMusic().empty());

	engine.playMusic("bgm_battle");
	REQUIRE(engine.isMusicPlaying());
	REQUIRE(engine.currentMusic() == "bgm_battle");
	REQUIRE(engine.isPlaying("bgm_battle"));

	engine.stopMusic();
	REQUIRE_FALSE(engine.isMusicPlaying());
	REQUIRE(engine.currentMusic().empty());
	REQUIRE_FALSE(engine.isPlaying("bgm_battle"));
}

TEST_CASE("SoftAudioEngine playMusic replaces previous music", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.playMusic("bgm_title");
	REQUIRE(engine.currentMusic() == "bgm_title");

	engine.playMusic("bgm_battle");
	REQUIRE(engine.currentMusic() == "bgm_battle");
	REQUIRE_FALSE(engine.isPlaying("bgm_title"));
	REQUIRE(engine.isPlaying("bgm_battle"));
}

TEST_CASE("SoftAudioEngine setVolume clamps and stores value", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	REQUIRE(engine.masterVolume() == Catch::Approx(1.0f));

	engine.setVolume(0.5f);
	REQUIRE(engine.masterVolume() == Catch::Approx(0.5f));

	engine.setVolume(-0.1f);
	REQUIRE(engine.masterVolume() == Catch::Approx(0.0f));

	engine.setVolume(1.5f);
	REQUIRE(engine.masterVolume() == Catch::Approx(1.0f));
}

TEST_CASE("SoftAudioEngine stopAll clears everything", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.playSound("click");
	engine.playSound("boom");
	engine.playMusic("bgm");

	REQUIRE(engine.playingSoundCount() == 2);
	REQUIRE(engine.isMusicPlaying());

	engine.stopAll();
	REQUIRE(engine.playingSoundCount() == 0);
	REQUIRE_FALSE(engine.isMusicPlaying());
	REQUIRE_FALSE(engine.isPlaying("click"));
	REQUIRE_FALSE(engine.isPlaying("bgm"));
}

TEST_CASE("SoftAudioEngine stopSound on non-playing sound is no-op", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.stopSound("nonexistent");
	REQUIRE(engine.playingSoundCount() == 0);
}

TEST_CASE("SoftAudioEngine isPlaying distinguishes SE from BGM", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	engine.playSound("effect");
	engine.playMusic("bgm");

	REQUIRE(engine.isPlaying("effect"));
	REQUIRE(engine.isPlaying("bgm"));

	// Stop only SE
	engine.stopSound("effect");
	REQUIRE_FALSE(engine.isPlaying("effect"));
	REQUIRE(engine.isPlaying("bgm"));

	// Stop only BGM
	engine.stopMusic();
	REQUIRE_FALSE(engine.isPlaying("bgm"));
}

TEST_CASE("SoftAudioEngine works with MitiruAudioPlayer adapter", "[mitiru][audio]")
{
	mitiru::audio::SoftAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	player.playBgm("music.ogg", 0.8f);
	REQUIRE(engine.isMusicPlaying());
	REQUIRE(engine.currentMusic() == "music.ogg");
	REQUIRE(engine.masterVolume() == Catch::Approx(0.8f));

	player.playSe("click.wav", 1.0f);
	REQUIRE(engine.isPlaying("click.wav"));

	player.stopBgm(0.0f);
	REQUIRE_FALSE(engine.isMusicPlaying());
}

// ============================================================
// MitiruAudioPlayer
// ============================================================

TEST_CASE("MitiruAudioPlayer implements IAudioPlayer interface", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	// Verify it can be used as sgc::IAudioPlayer
	sgc::IAudioPlayer* base = &player;
	REQUIRE(base != nullptr);
}

TEST_CASE("MitiruAudioPlayer playBgm and isBgmPlaying", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	REQUIRE_FALSE(player.isBgmPlaying());

	player.playBgm("music.ogg", 0.8f);
	REQUIRE(player.isBgmPlaying());
}

TEST_CASE("MitiruAudioPlayer stopBgm clears playing state", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	player.playBgm("music.ogg", 1.0f);
	REQUIRE(player.isBgmPlaying());

	player.stopBgm(0.0f);
	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("MitiruAudioPlayer pauseBgm and resumeBgm", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	player.playBgm("music.ogg", 1.0f);
	REQUIRE(player.isBgmPlaying());

	player.pauseBgm();
	REQUIRE_FALSE(player.isBgmPlaying());

	player.resumeBgm();
	// After resume, paused flag is cleared (though stub does not actually play)
	// isBgmPlaying checks m_bgmPlaying && !m_bgmPaused
	// After pause: m_bgmPlaying=true, m_bgmPaused=true -> false
	// After resume: m_bgmPlaying=true, m_bgmPaused=false -> true
	REQUIRE(player.isBgmPlaying());
}

TEST_CASE("MitiruAudioPlayer playSe returns incrementing handles", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	const int h1 = player.playSe("click.wav", 1.0f);
	const int h2 = player.playSe("boom.wav", 0.5f);
	const int h3 = player.playSe("ding.wav", 0.7f);

	REQUIRE(h2 > h1);
	REQUIRE(h3 > h2);
}

TEST_CASE("MitiruAudioPlayer stopSe and stopAllSe do not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);

	const int handle = player.playSe("test.wav", 1.0f);
	player.stopSe(handle);
	player.stopAllSe();
}

TEST_CASE("MitiruAudioPlayer setBgmVolume does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);
	player.setBgmVolume(0.5f);
}

TEST_CASE("MitiruAudioPlayer setSeVolume does not crash", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);
	player.setSeVolume(0.3f);
}

TEST_CASE("MitiruAudioPlayer setMasterVolume delegates to engine", "[mitiru][audio]")
{
	mitiru::audio::NullAudioEngine engine;
	mitiru::audio::MitiruAudioPlayer player(&engine);
	// Should not crash; delegates to NullAudioEngine::setVolume
	player.setMasterVolume(0.9f);
}

TEST_CASE("MitiruAudioPlayer with null engine does not crash", "[mitiru][audio]")
{
	mitiru::audio::MitiruAudioPlayer player(nullptr);

	// All methods should be safe with null engine
	player.playBgm("x", 1.0f);
	REQUIRE_FALSE(player.isBgmPlaying());

	player.stopBgm(0.0f);
	player.pauseBgm();
	player.resumeBgm();
	player.setBgmVolume(0.5f);
	player.setMasterVolume(0.5f);

	const int handle = player.playSe("y", 1.0f);
	REQUIRE(handle > 0);
	player.stopSe(handle);
	player.stopAllSe();
	player.setSeVolume(0.5f);
}

// ============================================================
// AssetHandle
// ============================================================

TEST_CASE("AssetHandle default constructed is not loaded", "[mitiru][resource]")
{
	mitiru::resource::AssetHandle<TestTexture> handle;
	REQUIRE_FALSE(handle.isLoaded());
	REQUIRE_FALSE(static_cast<bool>(handle));
	REQUIRE(handle.get() == nullptr);
	REQUIRE(handle.id().empty());
}

TEST_CASE("AssetHandle with valid asset is loaded", "[mitiru][resource]")
{
	auto tex = std::make_shared<TestTexture>(TestTexture{256, 256, "player"});
	mitiru::resource::AssetHandle<TestTexture> handle("player_tex", tex);

	REQUIRE(handle.isLoaded());
	REQUIRE(static_cast<bool>(handle));
	REQUIRE(handle.id() == "player_tex");
	REQUIRE(handle.get() != nullptr);
	REQUIRE(handle.get()->width == 256);
}

TEST_CASE("AssetHandle arrow operator accesses asset", "[mitiru][resource]")
{
	auto tex = std::make_shared<TestTexture>(TestTexture{128, 64, "sprite"});
	mitiru::resource::AssetHandle<TestTexture> handle("sprite_tex", tex);

	REQUIRE(handle->width == 128);
	REQUIRE(handle->height == 64);
	REQUIRE(handle->name == "sprite");
}

TEST_CASE("AssetHandle dereference operator", "[mitiru][resource]")
{
	auto tex = std::make_shared<TestTexture>(TestTexture{512, 512, "bg"});
	mitiru::resource::AssetHandle<TestTexture> handle("bg_tex", tex);

	TestTexture& ref = *handle;
	REQUIRE(ref.width == 512);
}

TEST_CASE("AssetHandle useCount tracks shared_ptr refs", "[mitiru][resource]")
{
	auto tex = std::make_shared<TestTexture>();
	mitiru::resource::AssetHandle<TestTexture> h1("tex", tex);
	REQUIRE(h1.useCount() == 2); // tex + h1

	mitiru::resource::AssetHandle<TestTexture> h2 = h1;
	REQUIRE(h1.useCount() == 3); // tex + h1 + h2
}

TEST_CASE("AssetHandle reset makes it unloaded", "[mitiru][resource]")
{
	auto tex = std::make_shared<TestTexture>();
	mitiru::resource::AssetHandle<TestTexture> handle("tex", tex);
	REQUIRE(handle.isLoaded());

	handle.reset();
	REQUIRE_FALSE(handle.isLoaded());
	REQUIRE(handle.get() == nullptr);
	// id is preserved
	REQUIRE(handle.id() == "tex");
}

TEST_CASE("AssetHandle with null shared_ptr is not loaded", "[mitiru][resource]")
{
	mitiru::resource::AssetHandle<TestTexture> handle("null_tex", nullptr);
	REQUIRE_FALSE(handle.isLoaded());
	REQUIRE(handle.id() == "null_tex");
}

// ============================================================
// AssetManager
// ============================================================

TEST_CASE("AssetManager store and get", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;

	auto tex = std::make_shared<TestTexture>(TestTexture{100, 100, "test"});
	manager.store<TestTexture>("my_tex", tex);

	REQUIRE(manager.isLoaded("my_tex"));
	REQUIRE(manager.cacheSize() == 1);

	auto handle = manager.get<TestTexture>("my_tex");
	REQUIRE(handle.isLoaded());
	REQUIRE(handle.get()->width == 100);
}

TEST_CASE("AssetManager get missing returns empty handle", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	auto handle = manager.get<TestTexture>("nonexistent");
	REQUIRE_FALSE(handle.isLoaded());
	REQUIRE(handle.id() == "nonexistent");
}

TEST_CASE("AssetManager unload removes asset", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	auto tex = std::make_shared<TestTexture>();
	manager.store<TestTexture>("temp", tex);
	REQUIRE(manager.isLoaded("temp"));

	manager.unload("temp");
	REQUIRE_FALSE(manager.isLoaded("temp"));
	REQUIRE(manager.cacheSize() == 0);
}

TEST_CASE("AssetManager unload nonexistent is no-op", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	// Should not crash
	manager.unload("nothing");
	REQUIRE(manager.cacheSize() == 0);
}

TEST_CASE("AssetManager isLoaded checks existence", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	REQUIRE_FALSE(manager.isLoaded("x"));

	manager.store<TestTexture>("x", std::make_shared<TestTexture>());
	REQUIRE(manager.isLoaded("x"));
}

TEST_CASE("AssetManager store multiple types", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.store<TestTexture>("tex1", std::make_shared<TestTexture>());
	manager.store<TestSound>("snd1", std::make_shared<TestSound>());

	REQUIRE(manager.cacheSize() == 2);
	REQUIRE(manager.isLoaded("tex1"));
	REQUIRE(manager.isLoaded("snd1"));

	auto texHandle = manager.get<TestTexture>("tex1");
	REQUIRE(texHandle.isLoaded());

	auto sndHandle = manager.get<TestSound>("snd1");
	REQUIRE(sndHandle.isLoaded());
}

TEST_CASE("AssetManager get with wrong type returns empty handle", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.store<TestTexture>("tex", std::make_shared<TestTexture>());

	// Try to get as TestSound - should fail due to bad_any_cast
	auto handle = manager.get<TestSound>("tex");
	REQUIRE_FALSE(handle.isLoaded());
}

TEST_CASE("AssetManager loadedIds returns all stored IDs", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.store<TestTexture>("a", std::make_shared<TestTexture>());
	manager.store<TestTexture>("b", std::make_shared<TestTexture>());
	manager.store<TestSound>("c", std::make_shared<TestSound>());

	const auto ids = manager.loadedIds();
	REQUIRE(ids.size() == 3);
	REQUIRE(std::find(ids.begin(), ids.end(), "a") != ids.end());
	REQUIRE(std::find(ids.begin(), ids.end(), "b") != ids.end());
	REQUIRE(std::find(ids.begin(), ids.end(), "c") != ids.end());
}

TEST_CASE("AssetManager unloadAll clears cache", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.store<TestTexture>("x", std::make_shared<TestTexture>());
	manager.store<TestTexture>("y", std::make_shared<TestTexture>());
	REQUIRE(manager.cacheSize() == 2);

	manager.unloadAll();
	REQUIRE(manager.cacheSize() == 0);
	REQUIRE_FALSE(manager.isLoaded("x"));
}

TEST_CASE("AssetManager store overwrites existing asset", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.store<TestTexture>("tex", std::make_shared<TestTexture>(TestTexture{10, 10, "old"}));
	manager.store<TestTexture>("tex", std::make_shared<TestTexture>(TestTexture{20, 20, "new"}));

	REQUIRE(manager.cacheSize() == 1);
	auto handle = manager.get<TestTexture>("tex");
	REQUIRE(handle.isLoaded());
	REQUIRE(handle.get()->name == "new");
}

// ── テスト用ローダー（AssetManager::load() 型消去検証） ─────

struct TestTextureLoader
{
	using AssetType = TestTexture;
	int loadCount = 0;

	std::shared_ptr<TestTexture> load(std::string_view path)
	{
		++loadCount;
		auto tex = std::make_shared<TestTexture>();
		tex->width = 64;
		tex->height = 64;
		tex->name = std::string(path);
		return tex;
	}

	bool canLoad(std::string_view path) const
	{
		return path.ends_with(".png") || path.ends_with(".jpg");
	}
};

TEST_CASE("AssetManager load invokes registered loader", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	TestTextureLoader loader;
	manager.registerLoader<TestTextureLoader>(loader);

	auto handle = manager.load<TestTexture>("player", "textures/player.png");
	REQUIRE(handle.isLoaded());
	REQUIRE(handle.get()->width == 64);
	REQUIRE(handle.get()->name == "textures/player.png");
}

TEST_CASE("AssetManager load caches result", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	manager.registerLoader<TestTextureLoader>(TestTextureLoader{});

	auto h1 = manager.load<TestTexture>("tex1", "a.png");
	REQUIRE(h1.isLoaded());
	REQUIRE(manager.isLoaded("tex1"));

	// Second load should return cached version
	auto h2 = manager.load<TestTexture>("tex1", "a.png");
	REQUIRE(h2.isLoaded());
	REQUIRE(h2.get() == h1.get()); // Same pointer
}

TEST_CASE("AssetManager load without registered loader returns empty", "[mitiru][resource]")
{
	mitiru::resource::AssetManager manager;
	auto handle = manager.load<TestTexture>("x", "missing.png");
	REQUIRE_FALSE(handle.isLoaded());
}

// ============================================================
// HotReloadManager
// ============================================================

TEST_CASE("HotReloadManager initially has zero watches", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	REQUIRE(hrm.watchCount() == 0);
	REQUIRE(hrm.watchedPaths().empty());
}

TEST_CASE("HotReloadManager watch increments watchCount", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.watch("fake_path_1.json", [](const std::string&) {});
	REQUIRE(hrm.watchCount() == 1);

	hrm.watch("fake_path_2.json", [](const std::string&) {});
	REQUIRE(hrm.watchCount() == 2);
}

TEST_CASE("HotReloadManager unwatch removes entry", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.watch("test.json", [](const std::string&) {});
	REQUIRE(hrm.watchCount() == 1);

	hrm.unwatch("test.json");
	REQUIRE(hrm.watchCount() == 0);
}

TEST_CASE("HotReloadManager unwatch nonexistent is no-op", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.unwatch("nothing");
	REQUIRE(hrm.watchCount() == 0);
}

TEST_CASE("HotReloadManager watchedPaths returns all paths", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.watch("a.json", [](const std::string&) {});
	hrm.watch("b.json", [](const std::string&) {});

	const auto paths = hrm.watchedPaths();
	REQUIRE(paths.size() == 2);
	REQUIRE(std::find(paths.begin(), paths.end(), "a.json") != paths.end());
	REQUIRE(std::find(paths.begin(), paths.end(), "b.json") != paths.end());
}

TEST_CASE("HotReloadManager unwatchAll clears all", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.watch("x.json", [](const std::string&) {});
	hrm.watch("y.json", [](const std::string&) {});
	REQUIRE(hrm.watchCount() == 2);

	hrm.unwatchAll();
	REQUIRE(hrm.watchCount() == 0);
}

TEST_CASE("HotReloadManager pollChanges with nonexistent files returns empty", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	hrm.watch("does_not_exist_12345.json", [](const std::string&) {});

	// File doesn't exist, so pollChanges should skip it
	const auto changed = hrm.pollChanges();
	REQUIRE(changed.empty());
}

TEST_CASE("HotReloadManager watch overwrites duplicate path", "[mitiru][resource]")
{
	mitiru::resource::HotReloadManager hrm;
	int callCount = 0;

	hrm.watch("dup.json", [](const std::string&) {});
	hrm.watch("dup.json", [&callCount](const std::string&) { ++callCount; });

	// Should only have one entry (overwritten)
	REQUIRE(hrm.watchCount() == 1);
}

// ============================================================
// PhysicsBridge
// ============================================================

TEST_CASE("PhysicsBridge default step size is 1/60", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics;
	REQUIRE(physics.stepSize() == Catch::Approx(1.0 / 60.0));
}

TEST_CASE("PhysicsBridge custom step size", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 30.0);
	REQUIRE(physics.stepSize() == Catch::Approx(1.0 / 30.0));
}

TEST_CASE("PhysicsBridge step executes callback", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
	int stepCount = 0;

	// Feed enough dt for at least one step
	physics.step(1.0 / 60.0, [&](double)
	{
		++stepCount;
	});

	REQUIRE(stepCount >= 1);
}

TEST_CASE("PhysicsBridge totalSteps accumulates", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
	REQUIRE(physics.totalSteps() == 0);

	physics.step(1.0 / 60.0, [](double) {});
	const int afterFirst = physics.totalSteps();
	REQUIRE(afterFirst >= 1);

	physics.step(1.0 / 60.0, [](double) {});
	REQUIRE(physics.totalSteps() >= afterFirst);
}

TEST_CASE("PhysicsBridge setStepSize changes step interval", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
	physics.setStepSize(1.0 / 30.0);
	REQUIRE(physics.stepSize() == Catch::Approx(1.0 / 30.0));
}

TEST_CASE("PhysicsBridge resetAccumulator does not crash", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics;
	physics.step(0.1, [](double) {});
	physics.resetAccumulator();
	// Should not crash, accumulator is reset
}

TEST_CASE("PhysicsBridge interpolationFactor is in range", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
	physics.step(0.01, [](double) {});

	const double factor = physics.interpolationFactor();
	REQUIRE(factor >= 0.0);
	REQUIRE(factor <= 1.0);
}

TEST_CASE("PhysicsBridge setMaxSteps does not crash", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics;
	physics.setMaxSteps(5);
	// Should not crash
	physics.step(1.0, [](double) {});
}

TEST_CASE("PhysicsBridge toJson contains expected fields", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics;
	const auto json = physics.toJson();

	REQUIRE(json.find("\"stepSize\":") != std::string::npos);
	REQUIRE(json.find("\"accumulated\":") != std::string::npos);
	REQUIRE(json.find("\"interpolationFactor\":") != std::string::npos);
	REQUIRE(json.find("\"totalSteps\":") != std::string::npos);
	REQUIRE(json.find("\"maxSteps\":") != std::string::npos);
}

TEST_CASE("PhysicsBridge step returns step count", "[mitiru][bridge]")
{
	mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
	const int steps = physics.step(2.0 / 60.0, [](double) {});
	REQUIRE(steps >= 1);
}

// ============================================================
// AiBridge
// ============================================================

TEST_CASE("AiBridge initially has no trees", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	REQUIRE(ai.registeredNames().empty());
}

TEST_CASE("AiBridge registerBehaviorTree and registeredNames", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	ai.registerBehaviorTree("patrol", nullptr);
	ai.registerBehaviorTree("attack", nullptr);

	const auto names = ai.registeredNames();
	REQUIRE(names.size() == 2);
	REQUIRE(std::find(names.begin(), names.end(), "patrol") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "attack") != names.end());
}

TEST_CASE("AiBridge unregisterBehaviorTree removes entry", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	ai.registerBehaviorTree("temp", nullptr);
	REQUIRE(ai.registeredNames().size() == 1);

	ai.unregisterBehaviorTree("temp");
	REQUIRE(ai.registeredNames().empty());
}

TEST_CASE("AiBridge setState and getState", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	ai.registerBehaviorTree("patrol", nullptr);

	REQUIRE(ai.getState("patrol") == mitiru::bridge::AiState::Idle);

	ai.setState("patrol", mitiru::bridge::AiState::Running);
	REQUIRE(ai.getState("patrol") == mitiru::bridge::AiState::Running);

	ai.setState("patrol", mitiru::bridge::AiState::Success);
	REQUIRE(ai.getState("patrol") == mitiru::bridge::AiState::Success);

	ai.setState("patrol", mitiru::bridge::AiState::Failure);
	REQUIRE(ai.getState("patrol") == mitiru::bridge::AiState::Failure);
}

TEST_CASE("AiBridge getState for unregistered returns Idle", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	REQUIRE(ai.getState("unknown") == mitiru::bridge::AiState::Idle);
}

TEST_CASE("AiBridge setState on unregistered is no-op", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	// Should not crash
	ai.setState("ghost", mitiru::bridge::AiState::Running);
	REQUIRE(ai.getState("ghost") == mitiru::bridge::AiState::Idle);
}

TEST_CASE("AiBridge toJson includes treeCount and trees", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	ai.registerBehaviorTree("patrol", nullptr);
	ai.setState("patrol", mitiru::bridge::AiState::Running);

	const auto json = ai.toJson();
	REQUIRE(json.find("\"treeCount\":1") != std::string::npos);
	REQUIRE(json.find("\"trees\":[") != std::string::npos);
	REQUIRE(json.find("\"patrol\"") != std::string::npos);
	REQUIRE(json.find("\"Running\"") != std::string::npos);
}

TEST_CASE("AiBridge toJson empty", "[mitiru][bridge]")
{
	mitiru::bridge::AiBridge ai;
	const auto json = ai.toJson();
	REQUIRE(json.find("\"treeCount\":0") != std::string::npos);
	REQUIRE(json.find("\"trees\":[]") != std::string::npos);
}

// ============================================================
// UiBridge
// ============================================================

TEST_CASE("UiBridge initially has no widgets", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	REQUIRE(ui.widgetCount() == 0);
}

TEST_CASE("UiBridge addWidget increments count", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"btn_start", "Button", true});
	REQUIRE(ui.widgetCount() == 1);

	ui.addWidget({"slider_vol", "Slider", true});
	REQUIRE(ui.widgetCount() == 2);
}

TEST_CASE("UiBridge removeWidget removes by name", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"btn_a", "Button", true});
	ui.addWidget({"btn_b", "Button", true});
	REQUIRE(ui.widgetCount() == 2);

	ui.removeWidget("btn_a");
	REQUIRE(ui.widgetCount() == 1);
}

TEST_CASE("UiBridge removeWidget nonexistent is no-op", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"x", "Button", true});
	ui.removeWidget("nonexistent");
	REQUIRE(ui.widgetCount() == 1);
}

TEST_CASE("UiBridge clear removes all widgets", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"a", "Button", true});
	ui.addWidget({"b", "Slider", false});
	ui.clear();
	REQUIRE(ui.widgetCount() == 0);
}

TEST_CASE("UiBridge render does not crash", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"btn", "Button", true});
	ui.addWidget({"hidden", "Panel", false});

	mitiru::Screen screen(800, 600);
	// Should not crash
	ui.render(screen);
}

TEST_CASE("UiBridge toJson includes widgetCount and widgets", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	ui.addWidget({"btn_start", "Button", true});
	ui.addWidget({"lbl_title", "Label", false});

	const auto json = ui.toJson();
	REQUIRE(json.find("\"widgetCount\":2") != std::string::npos);
	REQUIRE(json.find("\"widgets\":[") != std::string::npos);
	REQUIRE(json.find("\"btn_start\"") != std::string::npos);
	REQUIRE(json.find("\"Button\"") != std::string::npos);
	REQUIRE(json.find("\"visible\":true") != std::string::npos);
	REQUIRE(json.find("\"visible\":false") != std::string::npos);
}

TEST_CASE("UiBridge toJson empty", "[mitiru][bridge]")
{
	mitiru::bridge::UiBridge ui;
	const auto json = ui.toJson();
	REQUIRE(json.find("\"widgetCount\":0") != std::string::npos);
	REQUIRE(json.find("\"widgets\":[]") != std::string::npos);
}

TEST_CASE("UiWidgetInfo default visible is true", "[mitiru][bridge]")
{
	mitiru::bridge::UiWidgetInfo info;
	info.name = "test";
	info.type = "Button";
	REQUIRE(info.visible == true);
}
