#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/graphwalker/Effects.hpp"
#include "mitiru/graphwalker/BgmManager.hpp"
#include "mitiru/graphwalker/GlossaryData.hpp"
#include "mitiru/graphwalker/GameApp.hpp"

using namespace mitiru::gw;
using Catch::Approx;

// ── ParticleEmitter ──────────────────────────────────────

TEST_CASE("ParticleEmitter emit creates particles", "[mitiru][gw][effects]")
{
	ParticleEmitter emitter;
	emitter.emit({100.0f, 200.0f}, 5, NeonColor::player(), 100.0f, 1.0f);
	REQUIRE(emitter.getParticles().size() == 5);
}

TEST_CASE("ParticleEmitter update removes dead particles", "[mitiru][gw][effects]")
{
	ParticleEmitter emitter;
	emitter.emit({0.0f, 0.0f}, 3, NeonColor::player(), 50.0f, 0.5f);
	REQUIRE(emitter.getParticles().size() == 3);

	// 寿命を超えるまで更新
	emitter.update(0.6f);
	REQUIRE(emitter.getParticles().empty());
}

TEST_CASE("ParticleEmitter update moves particles", "[mitiru][gw][effects]")
{
	ParticleEmitter emitter;
	emitter.emit({0.0f, 0.0f}, 1, NeonColor::player(), 100.0f, 2.0f);

	const auto posBefore = emitter.getParticles()[0].position;
	emitter.update(0.1f);
	const auto posAfter = emitter.getParticles()[0].position;

	// 位置が変化しているはず
	const bool moved = (posBefore.x != posAfter.x) || (posBefore.y != posAfter.y);
	REQUIRE(moved);
}

TEST_CASE("ParticleEmitter clear removes all particles", "[mitiru][gw][effects]")
{
	ParticleEmitter emitter;
	emitter.emit({0.0f, 0.0f}, 10, NeonColor::player(), 50.0f, 5.0f);
	REQUIRE(emitter.getParticles().size() == 10);

	emitter.clear();
	REQUIRE(emitter.getParticles().empty());
}

// ── CameraEffect ──────────────────────────────────────

TEST_CASE("CameraEffect shake decays over time", "[mitiru][gw][effects]")
{
	CameraEffect effect;
	effect.startShake(10.0f, 1.0f);
	REQUIRE(effect.isShaking());

	const float initial = effect.getCurrentIntensity();
	effect.update(0.5f);
	const float mid = effect.getCurrentIntensity();
	REQUIRE(mid < initial);

	effect.update(0.6f);
	REQUIRE_FALSE(effect.isShaking());
	REQUIRE(effect.getCurrentIntensity() == Approx(0.0f));
}

// ── ScreenFade ──────────────────────────────────────

TEST_CASE("ScreenFade fades toward target", "[mitiru][gw][effects]")
{
	ScreenFade fade;
	fade.alpha = 0.0f;
	fade.fadeOut(2.0f); // target=1.0, speed=2.0

	REQUIRE(fade.isFading);
	fade.update(0.25f); // alpha should increase by 0.5
	REQUIRE(fade.alpha == Approx(0.5f));

	fade.update(0.25f);
	REQUIRE(fade.alpha == Approx(1.0f));
	REQUIRE_FALSE(fade.isFading);
}

TEST_CASE("ScreenFade fadeIn reduces alpha", "[mitiru][gw][effects]")
{
	ScreenFade fade;
	fade.alpha = 1.0f;
	fade.fadeIn(4.0f);

	fade.update(0.25f);
	REQUIRE(fade.alpha == Approx(0.0f));
	REQUIRE_FALSE(fade.isFading);
}

// ── ZoneAnnouncement ──────────────────────────────────────

TEST_CASE("ZoneAnnouncement shows and hides", "[mitiru][gw][effects]")
{
	ZoneAnnouncement ann;
	REQUIRE_FALSE(ann.isShowing);

	ann.show("Test Zone", NeonColor::player(), 1.0f);
	REQUIRE(ann.isShowing);
	REQUIRE(ann.zoneName == "Test Zone");

	ann.update(0.5f);
	REQUIRE(ann.isShowing);
	REQUIRE(ann.getProgress() == Approx(0.5f));

	ann.update(0.6f);
	REQUIRE_FALSE(ann.isShowing);
}

// ── BgmManager ──────────────────────────────────────

TEST_CASE("BgmManager setZone changes params", "[mitiru][gw][bgm]")
{
	BgmManager bgm;
	bgm.setZone(ZoneId::Tutorial);
	bgm.update(2.0f); // クロスフェード完了

	const auto tutorialParams = bgm.getBgmParams();

	bgm.setZone(ZoneId::Chaos);
	bgm.update(2.0f); // クロスフェード完了

	const auto chaosParams = bgm.getBgmParams();

	// テンポが異なるはず
	REQUIRE(tutorialParams.baseTempo != chaosParams.baseTempo);
}

TEST_CASE("BgmManager generateBgmSamples produces samples", "[mitiru][gw][bgm]")
{
	BgmManager bgm;
	bgm.setZone(ZoneId::Linear);
	bgm.update(2.0f);

	const auto samples = bgm.generateBgmSamples(0.1f, 44100);
	REQUIRE_FALSE(samples.empty());

	// サンプル数は約 0.1 * 44100 = 4410
	REQUIRE(samples.size() > 4000);

	// 全サンプルが-1〜1の範囲内
	for (const auto s : samples)
	{
		REQUIRE(s >= -1.0f);
		REQUIRE(s <= 1.0f);
	}
}

TEST_CASE("BgmManager crossfade during zone change", "[mitiru][gw][bgm]")
{
	BgmManager bgm;
	bgm.setZone(ZoneId::Tutorial);
	bgm.update(2.0f); // 完了

	bgm.setZone(ZoneId::Trig);
	REQUIRE(bgm.isCrossfading());
	REQUIRE(bgm.getCrossfadeProgress() == Approx(0.0f));

	bgm.update(0.5f);
	REQUIRE(bgm.isCrossfading());
	REQUIRE(bgm.getCrossfadeProgress() > 0.0f);
	REQUIRE(bgm.getCrossfadeProgress() < 1.0f);

	bgm.update(1.0f);
	REQUIRE_FALSE(bgm.isCrossfading());
}

TEST_CASE("BgmManager volume control", "[mitiru][gw][bgm]")
{
	BgmManager bgm;
	bgm.setVolume(0.5f);
	REQUIRE(bgm.getVolume() == Approx(0.5f));

	bgm.setVolume(1.5f); // clamped
	REQUIRE(bgm.getVolume() == Approx(1.0f));

	bgm.setVolume(-0.1f); // clamped
	REQUIRE(bgm.getVolume() == Approx(0.0f));
}

// ── GlossaryDatabase ──────────────────────────────────────

TEST_CASE("GlossaryDatabase search finds entry", "[mitiru][gw][glossary]")
{
	GlossaryDatabase db;
	db.addEntry({"sin", "sine function", "sin(pi/2) = 1", "trigonometry"});

	auto result = db.search("sin");
	REQUIRE(result.has_value());
	REQUIRE(result->term == "sin");
	REQUIRE(result->category == "trigonometry");

	auto notFound = db.search("nonexistent");
	REQUIRE_FALSE(notFound.has_value());
}

TEST_CASE("GlossaryDatabase getByCategory filters", "[mitiru][gw][glossary]")
{
	GlossaryDatabase db;
	db.loadBuiltinEntries();

	auto trig = db.getByCategory("trigonometry");
	REQUIRE(trig.size() == 3); // sin, cos, tan

	auto basic = db.getByCategory("basic");
	REQUIRE(basic.size() == 6); // +, -, *, /, ^, abs
}

TEST_CASE("GlossaryDatabase loadFromJson works", "[mitiru][gw][glossary]")
{
	GlossaryDatabase db;
	const std::string data = "sqrt\tsquare root\tsqrt(4) = 2\tbasic\n"
	                         "pow\tpower function\tpow(2,3) = 8\tbasic\n";

	REQUIRE(db.loadFromJson(data));
	REQUIRE(db.size() == 2);

	auto sqrtEntry = db.search("sqrt");
	REQUIRE(sqrtEntry.has_value());
	REQUIRE(sqrtEntry->definition == "square root");
}

TEST_CASE("GlossaryDatabase loadFromJson empty returns false", "[mitiru][gw][glossary]")
{
	GlossaryDatabase db;
	REQUIRE_FALSE(db.loadFromJson(""));
}

TEST_CASE("GlossaryDatabase builtin entries loaded", "[mitiru][gw][glossary]")
{
	GlossaryDatabase db;
	db.loadBuiltinEntries();
	REQUIRE(db.size() == 13); // 4 basic ops + 3 trig + 2 exp + 2 const + ^ + abs
}

// ── GameApp ──────────────────────────────────────

TEST_CASE("GameApp init creates subsystems", "[mitiru][gw][gameapp]")
{
	GameApp app;
	REQUIRE_FALSE(app.isInitialized());

	app.init(1280.0f, 720.0f);
	REQUIRE(app.isInitialized());
	REQUIRE(app.isRunning());
	REQUIRE(app.getScreenWidth() == Approx(1280.0f));
	REQUIRE(app.getScreenHeight() == Approx(720.0f));

	// 用語辞典がロード済み
	REQUIRE(app.getGlossary().size() > 0);
}

TEST_CASE("GameApp starts at TitleScene", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);

	REQUIRE(app.getCurrentScene() == GameScene::TitleScene);
	REQUIRE(app.getSharedData().state == GameState::Title);
}

TEST_CASE("GameApp changeScene transitions", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);

	app.changeScene(GameScene::PlayScene);
	REQUIRE(app.getCurrentScene() == GameScene::PlayScene);
	REQUIRE(app.getSharedData().state == GameState::Playing);

	app.changeScene(GameScene::ClearScene);
	REQUIRE(app.getCurrentScene() == GameScene::ClearScene);
	REQUIRE(app.getSharedData().state == GameState::Clear);

	app.changeScene(GameScene::TitleScene);
	REQUIRE(app.getCurrentScene() == GameScene::TitleScene);
	REQUIRE(app.getSharedData().state == GameState::Title);
}

TEST_CASE("GameApp update processes input in title", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);

	// メニューを下に移動
	GWInputState input;
	input.down = true;
	app.update(1.0f / 60.0f, input);

	REQUIRE(app.getTitleMenuIndex() == 1);
}

TEST_CASE("GameApp title confirm starts game", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);

	// スロット0を選択して決定
	GWInputState input;
	input.confirm = true;
	app.update(1.0f / 60.0f, input);

	REQUIRE(app.getCurrentScene() == GameScene::PlayScene);
	REQUIRE(app.getSharedData().currentSlot == 0);
}

TEST_CASE("GameApp quit sets running false", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);

	REQUIRE(app.isRunning());
	app.quit();
	REQUIRE_FALSE(app.isRunning());
}

TEST_CASE("GameApp playing tracks play time", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);
	app.changeScene(GameScene::PlayScene);

	GWInputState input;
	app.update(1.0f, input);
	REQUIRE(app.getSharedData().playTime == Approx(1.0f));

	app.update(0.5f, input);
	REQUIRE(app.getSharedData().playTime == Approx(1.5f));
}

TEST_CASE("GameApp playing cancel returns to title", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);
	app.changeScene(GameScene::PlayScene);

	GWInputState input;
	input.cancel = true;
	app.update(1.0f / 60.0f, input);

	REQUIRE(app.getCurrentScene() == GameScene::TitleScene);
}

TEST_CASE("GameApp playing pause toggles state", "[mitiru][gw][gameapp]")
{
	GameApp app;
	app.init(1280.0f, 720.0f);
	app.changeScene(GameScene::PlayScene);

	GWInputState input;
	input.pause = true;
	app.update(1.0f / 60.0f, input);
	REQUIRE(app.getSharedData().state == GameState::Paused);

	// もう一度パルスでPlayingに戻る
	app.update(1.0f / 60.0f, input);
	REQUIRE(app.getSharedData().state == GameState::Playing);
}
