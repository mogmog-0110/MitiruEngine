#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/SceneManager.hpp"

namespace
{

std::vector<std::string> g_log;

class TestScene : public sgc::Scene
{
public:
	explicit TestScene(std::string name) : m_name(std::move(name)) {}

	void onEnter() override { g_log.push_back(m_name + ":enter"); }
	void onExit() override { g_log.push_back(m_name + ":exit"); }
	void onPause() override { g_log.push_back(m_name + ":pause"); }
	void onResume() override { g_log.push_back(m_name + ":resume"); }
	void update(float) override { g_log.push_back(m_name + ":update"); }

	const std::string& name() const { return m_name; }

private:
	std::string m_name;
};

} // anonymous namespace

TEST_CASE("SceneManager starts empty", "[scene]")
{
	sgc::SceneManager manager;
	REQUIRE(manager.empty());
	REQUIRE(manager.depth() == 0);
	REQUIRE(manager.top() == nullptr);
}

TEST_CASE("SceneManager push calls onEnter", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");

	REQUIRE(manager.depth() == 1);
	REQUIRE_FALSE(manager.empty());
	REQUIRE(g_log.size() == 1);
	REQUIRE(g_log[0] == "Title:enter");
}

TEST_CASE("SceneManager pop calls onExit", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	g_log.clear();

	manager.pop();
	REQUIRE(manager.empty());
	REQUIRE(g_log.size() == 1);
	REQUIRE(g_log[0] == "Title:exit");
}

TEST_CASE("SceneManager push pauses previous scene", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	g_log.clear();

	manager.push<TestScene>("Game");
	REQUIRE(manager.depth() == 2);
	REQUIRE(g_log.size() == 2);
	REQUIRE(g_log[0] == "Title:pause");
	REQUIRE(g_log[1] == "Game:enter");
}

TEST_CASE("SceneManager pop resumes previous scene", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	manager.push<TestScene>("Game");
	g_log.clear();

	manager.pop();
	REQUIRE(manager.depth() == 1);
	REQUIRE(g_log.size() == 2);
	REQUIRE(g_log[0] == "Game:exit");
	REQUIRE(g_log[1] == "Title:resume");
}

TEST_CASE("SceneManager update calls top scene", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	g_log.clear();

	manager.update(0.016f);
	REQUIRE(g_log.size() == 1);
	REQUIRE(g_log[0] == "Title:update");
}

TEST_CASE("SceneManager replace swaps top scene", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	g_log.clear();

	manager.replace<TestScene>("Game");
	REQUIRE(manager.depth() == 1);
	REQUIRE(g_log.size() == 2);
	REQUIRE(g_log[0] == "Title:exit");
	REQUIRE(g_log[1] == "Game:enter");
}

TEST_CASE("SceneManager update on empty is safe", "[scene]")
{
	sgc::SceneManager manager;
	manager.update(0.016f); // should not crash
	REQUIRE(manager.empty());
}

TEST_CASE("SceneManager pop on empty is safe", "[scene]")
{
	sgc::SceneManager manager;
	manager.pop(); // should not crash
	REQUIRE(manager.empty());
}

TEST_CASE("SceneManager top returns current scene", "[scene]")
{
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");
	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top != nullptr);
	REQUIRE(top->name() == "Title");
}

// ── draw ────────────────────────────────────────────────

class DrawScene : public sgc::Scene
{
public:
	mutable std::string lastDraw;
	void update(float) override {}
	void draw() const override { lastDraw = "draw"; }
	void drawFadeIn(float t) const override { lastDraw = "fadeIn:" + std::to_string(t); }
	void drawFadeOut(float t) const override { lastDraw = "fadeOut:" + std::to_string(t); }
};

TEST_CASE("SceneManager draw calls scene draw", "[scene]")
{
	sgc::SceneManager manager;
	manager.push<DrawScene>();
	manager.draw();
	auto* scene = dynamic_cast<DrawScene*>(manager.top());
	REQUIRE(scene->lastDraw == "draw");
}

// ── fade transition ─────────────────────────────────────

TEST_CASE("SceneManager changeScene starts fade out", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");

	manager.changeScene<TestScene>(1.0f, "Game");
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingOut);
}

TEST_CASE("SceneManager fade transitions through states", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");

	manager.changeScene<TestScene>(1.0f, "Game");

	// フェードアウト中
	manager.update(0.3f);
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingOut);
	REQUIRE(manager.getFadeProgress() > 0.0f);

	// フェードアウト完了 → フェードイン開始
	manager.update(0.3f);
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingIn);

	// フェードイン中
	manager.update(0.3f);
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingIn);

	// フェードイン完了
	manager.update(0.3f);
	REQUIRE(manager.fadeState() == sgc::FadeState::None);
}

TEST_CASE("SceneManager changeScene replaces scene after fade out", "[scene]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.push<TestScene>("Title");

	manager.changeScene<TestScene>(0.2f, "Game");
	// フェードアウト完了
	manager.update(0.2f);

	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top != nullptr);
	REQUIRE(top->name() == "Game");
}

TEST_CASE("SceneManager draw on empty is safe", "[scene]")
{
	sgc::SceneManager manager;
	manager.draw(); // should not crash
	REQUIRE(manager.empty());
}

TEST_CASE("SceneManager getFadeProgress is 0 when no fade", "[scene]")
{
	sgc::SceneManager manager;
	REQUIRE(manager.getFadeProgress() == 0.0f);
}

// ── ID-based scene transitions ──────────────────────────

using namespace sgc::literals;

TEST_CASE("SceneManager registerScene and changeScene by ID", "[scene][id]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.registerScene<TestScene>("title"_hash, "Title");
	manager.registerScene<TestScene>("game"_hash, "Game");

	manager.pushScene("title"_hash);
	REQUIRE(manager.depth() == 1);

	manager.changeScene("game"_hash, 1.0f);
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingOut);

	// フェードアウト完了 → シーン切り替え
	manager.update(0.6f);
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingIn);

	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top != nullptr);
	REQUIRE(top->name() == "Game");
}

TEST_CASE("SceneManager changeScene with unknown ID returns false", "[scene][id]")
{
	sgc::SceneManager manager;
	manager.registerScene<TestScene>("title"_hash, "Title");
	manager.pushScene("title"_hash);

	REQUIRE_FALSE(manager.changeScene("unknown"_hash, 1.0f));
	REQUIRE(manager.fadeState() == sgc::FadeState::None);
}

TEST_CASE("SceneManager replaceScene by ID", "[scene][id]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.registerScene<TestScene>("title"_hash, "Title");
	manager.registerScene<TestScene>("game"_hash, "Game");

	manager.pushScene("title"_hash);
	REQUIRE(manager.replaceScene("game"_hash));
	REQUIRE(manager.depth() == 1);

	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top != nullptr);
	REQUIRE(top->name() == "Game");
}

TEST_CASE("SceneManager pushScene by ID", "[scene][id]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.registerScene<TestScene>("title"_hash, "Title");
	manager.registerScene<TestScene>("game"_hash, "Game");

	manager.pushScene("title"_hash);
	manager.pushScene("game"_hash);
	REQUIRE(manager.depth() == 2);

	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top->name() == "Game");
}

TEST_CASE("SceneManager template and ID APIs coexist", "[scene][id]")
{
	g_log.clear();
	sgc::SceneManager manager;
	manager.registerScene<TestScene>("game"_hash, "Game");

	// テンプレートAPIでpush
	manager.push<TestScene>("Title");
	REQUIRE(manager.depth() == 1);

	// ID APIでchangeScene
	manager.changeScene("game"_hash, 0.2f);
	manager.update(0.2f); // フェードアウト完了
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingIn);

	auto* top = dynamic_cast<TestScene*>(manager.top());
	REQUIRE(top->name() == "Game");
}

TEST_CASE("SceneManager pushScene with unknown ID returns false", "[scene][id]")
{
	sgc::SceneManager manager;
	REQUIRE_FALSE(manager.pushScene("nonexistent"_hash));
	REQUIRE(manager.empty());
}
