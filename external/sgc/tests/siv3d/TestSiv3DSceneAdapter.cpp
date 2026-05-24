/// @file TestSiv3DSceneAdapter.cpp
/// @brief Siv3D SceneAdapter tests with stub

#include <catch2/catch_test_macros.hpp>

#include "sgc/siv3d/SceneAdapter.hpp"
#include "sgc/core/Hash.hpp"

using namespace sgc::literals;

namespace
{

struct ResetFixture
{
	ResetFixture() { siv3d_stub::reset(); }
};

/// @brief テスト用共有データ
struct TestData
{
	int score = 0;
	int drawCount = 0;
	int updateCount = 0;
};

/// @brief tickFrame()を外部から呼べるテスト用App
template <typename SD>
class TestableApp : public sgc::siv3d::App<SD>
{
public:
	using sgc::siv3d::App<SD>::App;

	/// @brief tickFrame()を公開する（テスト用）
	bool testTickFrame() { return this->tickFrame(); }
};

/// @brief 基本的なテストシーン
class TestScene : public sgc::siv3d::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override
	{
		this->getData().score = 100;
	}

	void update(float /*dt*/) override
	{
		++this->getData().updateCount;
	}

	void draw() const override
	{
		++const_cast<TestScene*>(this)->getData().drawCount;
	}
};

/// @brief スタックをpopして終了するシーン
class ExitScene : public sgc::siv3d::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void update(float /*dt*/) override
	{
		++this->getData().updateCount;
		this->getSceneManager().pop();
	}

	void draw() const override
	{
		++const_cast<ExitScene*>(this)->getData().drawCount;
	}
};

/// @brief シーン遷移を行うシーン
class TransitionScene : public sgc::siv3d::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void update(float /*dt*/) override
	{
		++this->getData().updateCount;
		this->getSceneManager().changeScene("target"_hash, 0.0f);
	}

	void draw() const override {}
};

/// @brief 遷移先シーン
class TargetScene : public sgc::siv3d::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override
	{
		this->getData().score = 999;
	}

	void update(float /*dt*/) override {}
	void draw() const override {}
};

} // anonymous namespace

// ── initialization ───────────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - App initialization with default data", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	sgc::siv3d::App<TestData> app;

	CHECK(app.getData().score == 0);
	CHECK(app.getData().drawCount == 0);
	CHECK(app.getData().updateCount == 0);
}

TEST_CASE("Siv3DSceneAdapter - App with initial shared data", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	TestData initial{42, 0, 0};
	sgc::siv3d::App<TestData> app(initial);

	CHECK(app.getData().score == 42);
}

// ── scene registration ──────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - registerScene and setInitialScene", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	sgc::siv3d::App<TestData> app;
	app.registerScene<TestScene>("test"_hash);

	CHECK(app.setInitialScene("test"_hash));
	CHECK(app.getData().score == 100);
}

TEST_CASE("Siv3DSceneAdapter - setInitialScene with unknown ID returns false", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	sgc::siv3d::App<TestData> app;

	CHECK_FALSE(app.setInitialScene("unknown"_hash));
}

// ── tickFrame ────────────────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - tickFrame calls scene update and draw", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	TestableApp<TestData> app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);
	app.getClock().reset();

	bool finished = app.testTickFrame();

	CHECK_FALSE(finished);
	CHECK(app.getData().updateCount == 1);
	CHECK(app.getData().drawCount == 1);
}

TEST_CASE("Siv3DSceneAdapter - tickFrame returns true when stack empties", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	TestableApp<TestData> app;
	app.registerScene<ExitScene>("exit"_hash);
	app.setInitialScene("exit"_hash);
	app.getClock().reset();

	bool finished = app.testTickFrame();

	// ExitSceneはupdate内でpopするため、tickFrameはtrueを返す
	CHECK(finished);
	CHECK(app.getData().updateCount == 1);
}

// ── run loop ─────────────────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - run exits when System::Update returns false", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	siv3d_stub::systemUpdateResult() = false;

	sgc::siv3d::App<TestData> app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);

	app.run();

	// System::Update()が即座にfalseを返すのでフレーム処理されない
	CHECK(app.getData().updateCount == 0);
}

TEST_CASE("Siv3DSceneAdapter - run exits when scene stack empties", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	// System::Updateは常にtrueを返す（remainingFrames=-1, result=true）

	sgc::siv3d::App<TestData> app;
	app.registerScene<ExitScene>("exit"_hash);
	app.setInitialScene("exit"_hash);

	app.run();

	// ExitSceneが1フレームでpopするのでrunが終了する
	CHECK(app.getData().updateCount == 1);
}

TEST_CASE("Siv3DSceneAdapter - run with limited frames", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	siv3d_stub::systemUpdateRemainingFrames() = 3;

	TestableApp<TestData> app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);

	app.run();

	// 3フレーム分のupdate/drawが実行される
	CHECK(app.getData().updateCount == 3);
	CHECK(app.getData().drawCount == 3);
}

// ── scene transition ─────────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - scene transition via tickFrame", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	TestableApp<TestData> app;
	app.registerScene<TransitionScene>("src"_hash);
	app.registerScene<TargetScene>("target"_hash);
	app.setInitialScene("src"_hash);
	app.getClock().reset();

	// 1フレーム目: TransitionSceneがchangeSceneを呼ぶ
	app.testTickFrame();
	// 2フレーム目: 遷移が解決し、TargetSceneのonEnterでscore=999
	app.testTickFrame();

	CHECK(app.getData().score == 999);
}

// ── setRenderer ──────────────────────────────────────────

TEST_CASE("Siv3DSceneAdapter - setRenderer with nullptr does not crash", "[siv3d][scene-adapter]")
{
	ResetFixture fix;
	TestableApp<TestData> app;
	app.setRenderer(nullptr);

	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);
	app.getClock().reset();

	CHECK_FALSE(app.testTickFrame());
}
