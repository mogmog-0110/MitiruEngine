/// @file TestAppBase.cpp
/// @brief AppBase unit tests (framework-independent)

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/graphics/IRenderer.hpp"
#include "sgc/scene/App.hpp"

using namespace sgc::literals;

namespace
{

/// @brief テスト用共有データ
struct TestData
{
	int score{0};
	std::string lastScene;
};

/// @brief 最小モックレンダラー
class MockRenderer : public sgc::IRenderer
{
public:
	std::vector<std::string> calls;

	void drawRect(const sgc::AABB2f&, const sgc::Colorf&) override { calls.push_back("rect"); }
	void drawRectFrame(const sgc::AABB2f&, float, const sgc::Colorf&) override {}
	void drawCircle(const sgc::Vec2f&, float, const sgc::Colorf&) override {}
	void drawCircleFrame(const sgc::Vec2f&, float, float, const sgc::Colorf&) override {}
	void drawLine(const sgc::Vec2f&, const sgc::Vec2f&, float, const sgc::Colorf&) override {}
	void drawTriangle(const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Colorf&) override {}
	void drawFadeOverlay(float, const sgc::Colorf&) override { calls.push_back("fadeOverlay"); }
	void clearBackground(const sgc::Colorf&) override { calls.push_back("clearBg"); }
};

/// @brief テスト用AppBase（tickFrameを公開）
class TestApp : public sgc::AppBase<TestData>
{
public:
	using sgc::AppBase<TestData>::AppBase;

	/// @brief tickFrameを外部から呼べるようにする
	bool tick() { return tickFrame(); }
};

/// @brief テスト用シーン
class TestScene : public sgc::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override { getData().lastScene = "TestScene"; }
	void update(float) override { ++m_updateCount; }
	void draw() const override { ++m_drawCount; }

	mutable int m_updateCount{0};
	mutable int m_drawCount{0};
};

/// @brief 別のテスト用シーン
class OtherScene : public sgc::AppScene<TestData>
{
public:
	using AppScene::AppScene;

	void onEnter() override { getData().lastScene = "OtherScene"; }
	void update(float) override {}
};

} // anonymous namespace

// ── AppBase基本機能 ─────────────────────────────────────

TEST_CASE("AppBase getData returns shared data", "[scene][appbase]")
{
	TestApp app;
	REQUIRE(app.getData().score == 0);

	app.getData().score = 42;
	REQUIRE(app.getData().score == 42);
}

TEST_CASE("AppBase getData with initial value", "[scene][appbase]")
{
	TestApp app(TestData{100, "init"});
	REQUIRE(app.getData().score == 100);
	REQUIRE(app.getData().lastScene == "init");
}

TEST_CASE("AppBase const getData", "[scene][appbase]")
{
	const TestApp app(TestData{50, "const"});
	REQUIRE(app.getData().score == 50);
	REQUIRE(app.getData().lastScene == "const");
}

// ── シーン登録とライフサイクル ───────────────────────────

TEST_CASE("AppBase registerScene and setInitialScene", "[scene][appbase]")
{
	TestApp app;
	app.registerScene<TestScene>("test"_hash);
	REQUIRE(app.setInitialScene("test"_hash));
	REQUIRE(app.getData().lastScene == "TestScene");
}

TEST_CASE("AppBase setInitialScene fails for unregistered ID", "[scene][appbase]")
{
	TestApp app;
	REQUIRE_FALSE(app.setInitialScene("nonexistent"_hash));
}

TEST_CASE("AppBase addScene pushes scene immediately", "[scene][appbase]")
{
	TestApp app;
	app.addScene<TestScene>();
	REQUIRE(app.getData().lastScene == "TestScene");
	REQUIRE(app.getSceneManager().depth() == 1);
}

// ── tickFrame ────────────────────────────────────────────

TEST_CASE("AppBase tickFrame returns true when scene stack is empty", "[scene][appbase]")
{
	TestApp app;
	// スタックが空のまま → true（ループ終了）
	REQUIRE(app.tick());
}

TEST_CASE("AppBase tickFrame returns false when scene is active", "[scene][appbase]")
{
	TestApp app;
	app.registerScene<TestScene>("test"_hash);
	app.setInitialScene("test"_hash);

	REQUIRE_FALSE(app.tick());
}

TEST_CASE("AppBase tickFrame calls update and draw", "[scene][appbase]")
{
	TestApp app;
	app.addScene<TestScene>();

	auto* scene = dynamic_cast<TestScene*>(app.getSceneManager().top());
	REQUIRE(scene != nullptr);

	(void)app.tick();

	REQUIRE(scene->m_updateCount == 1);
	REQUIRE(scene->m_drawCount == 1);
}

// ── setRenderer + フェードオーバーレイ ──────────────────

TEST_CASE("AppBase setRenderer enables fade overlay", "[scene][appbase]")
{
	TestApp app;
	MockRenderer renderer;
	app.setRenderer(&renderer);

	app.registerScene<TestScene>("a"_hash);
	app.registerScene<OtherScene>("b"_hash);
	app.setInitialScene("a"_hash);

	// フェード付き遷移を開始
	app.getSceneManager().changeScene("b"_hash, 1.0f);
	renderer.calls.clear();

	// tickFrameでフェードオーバーレイが描画される
	(void)app.tick();

	bool hasFadeOverlay = false;
	for (const auto& c : renderer.calls)
	{
		if (c == "fadeOverlay") hasFadeOverlay = true;
	}
	REQUIRE(hasFadeOverlay);
}

TEST_CASE("AppBase fade overlay not drawn without renderer", "[scene][appbase]")
{
	TestApp app;
	// レンダラー未設定

	app.registerScene<TestScene>("a"_hash);
	app.registerScene<OtherScene>("b"_hash);
	app.setInitialScene("a"_hash);

	app.getSceneManager().changeScene("b"_hash, 1.0f);

	// クラッシュしない（nullptrチェック）
	REQUIRE_NOTHROW(app.tick());
}

// ── シーン遷移 ──────────────────────────────────────────

TEST_CASE("AppBase scene transition via SceneManager", "[scene][appbase]")
{
	TestApp app;
	app.registerScene<TestScene>("test"_hash);
	app.registerScene<OtherScene>("other"_hash);
	app.setInitialScene("test"_hash);

	REQUIRE(app.getData().lastScene == "TestScene");

	// 即時遷移
	app.getSceneManager().replaceScene("other"_hash);
	REQUIRE(app.getData().lastScene == "OtherScene");
}

TEST_CASE("AppBase getClock returns DeltaClock", "[scene][appbase]")
{
	TestApp app;
	app.getClock().setTimeScale(0.5f);
	REQUIRE(app.getClock().getTimeScale() == 0.5f);
}
