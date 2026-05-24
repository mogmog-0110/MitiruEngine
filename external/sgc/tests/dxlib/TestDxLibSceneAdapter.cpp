/// @file TestDxLibSceneAdapter.cpp
/// @brief DxLibSceneAdapter tests

#include <catch2/catch_test_macros.hpp>

#include "sgc/dxlib/DxLibSceneAdapter.hpp"
#include "sgc/core/Hash.hpp"

using namespace sgc::literals;

namespace
{

/// @brief test shared data
struct TestData
{
	int score = 0;
	int drawCount = 0;
	int updateCount = 0;
};

/// @brief test helper that exposes protected tickFrame() for testing
template <typename SD>
class TestableApp : public sgc::dxlib::App<SD>
{
public:
	using sgc::dxlib::App<SD>::App;

	/// @brief tickFrame() を公開する（テスト用）
	bool testTickFrame() { return this->tickFrame(); }
};

/// @brief test scene
class TestScene : public sgc::dxlib::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override
	{
		getData().score = 100;
	}

	void update(float /*dt*/) override
	{
		++getData().updateCount;
	}

	void draw() const override
	{
		++const_cast<TestScene*>(this)->getData().drawCount;
	}
};

/// @brief scene that requests exit by emptying stack
class ExitScene : public sgc::dxlib::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void update(float /*dt*/) override
	{
		++getData().updateCount;
		getSceneManager().pop();
	}

	void draw() const override
	{
		++const_cast<ExitScene*>(this)->getData().drawCount;
	}
};

/// @brief scene that transitions to another scene
class TransitionScene : public sgc::dxlib::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void update(float /*dt*/) override
	{
		++getData().updateCount;
		getSceneManager().changeScene("target"_hash, 0.0f);
	}

	void draw() const override {}
};

class TargetScene : public sgc::dxlib::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override
	{
		getData().score = 999;
	}

	void update(float /*dt*/) override {}
	void draw() const override {}
};

} // anonymous namespace

TEST_CASE("DxLibSceneAdapter - App initialization and getData", "[dxlib][scene-adapter]")
{
	sgc::dxlib::App<TestData> app;

	CHECK(app.getData().score == 0);
	CHECK(app.getData().drawCount == 0);
	CHECK(app.getData().updateCount == 0);
}

TEST_CASE("DxLibSceneAdapter - App with initial shared data", "[dxlib][scene-adapter]")
{
	TestData initial{42, 0, 0};
	sgc::dxlib::App<TestData> app(initial);

	CHECK(app.getData().score == 42);
}

TEST_CASE("DxLibSceneAdapter - registerScene and setInitialScene", "[dxlib][scene-adapter]")
{
	sgc::dxlib::App<TestData> app;
	app.registerScene<TestScene>("test"_hash);

	CHECK(app.setInitialScene("test"_hash));
	CHECK(app.getData().score == 100);
}

TEST_CASE("DxLibSceneAdapter - setInitialScene with unknown ID returns false", "[dxlib][scene-adapter]")
{
	sgc::dxlib::App<TestData> app;

	CHECK_FALSE(app.setInitialScene("unknown"_hash));
}

TEST_CASE("DxLibSceneAdapter - tickFrame calls scene update and draw", "[dxlib][scene-adapter]")
{
	TestableApp<TestData> app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);
	app.getClock().reset();

	bool finished = app.testTickFrame();
	CHECK_FALSE(finished);
	CHECK(app.getData().updateCount == 1);
	CHECK(app.getData().drawCount == 1);
}

TEST_CASE("DxLibSceneAdapter - run exits when ProcessMessage returns non-zero", "[dxlib][scene-adapter]")
{
	dxlib_stub::reset();
	dxlib_stub::processMessageResult() = -1;

	sgc::dxlib::App<TestData> app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);

	app.run();

	// run() should exit immediately without processing any frame
	CHECK(app.getData().updateCount == 0);
}

TEST_CASE("DxLibSceneAdapter - run exits when scene stack empties", "[dxlib][scene-adapter]")
{
	dxlib_stub::reset();
	dxlib_stub::processMessageResult() = 0;

	sgc::dxlib::App<TestData> app;
	app.registerScene<ExitScene>("exit"_hash);
	app.setInitialScene("exit"_hash);

	app.run();

	// ExitScene pops itself in update, so run() exits after 1 frame
	CHECK(app.getData().updateCount == 1);
}

TEST_CASE("DxLibSceneAdapter - scene transition via run", "[dxlib][scene-adapter]")
{
	dxlib_stub::reset();
	TestableApp<TestData> app;
	app.registerScene<TransitionScene>("src"_hash);
	app.registerScene<TargetScene>("target"_hash);
	app.setInitialScene("src"_hash);
	app.getClock().reset();

	// First tick: TransitionScene calls changeScene with 0 fade
	app.testTickFrame();
	// Second tick: transition resolves, TargetScene's onEnter sets score=999
	app.testTickFrame();
	CHECK(app.getData().score == 999);
}

TEST_CASE("DxLibSceneAdapter - setRenderer with nullptr", "[dxlib][scene-adapter]")
{
	TestableApp<TestData> app;

	app.setRenderer(nullptr);

	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);
	app.getClock().reset();
	CHECK_FALSE(app.testTickFrame());
}
