/// @file TestSceneIntegration.cpp
/// @brief ID遷移 + モックレンダラー統合テスト

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/graphics/IRenderer.hpp"
#include "sgc/scene/SceneManager.hpp"

using namespace sgc::literals;

namespace
{

/// @brief 描画コール記録
struct DrawCall
{
	std::string type;
};

/// @brief 最小モックレンダラー
class MockRenderer : public sgc::IRenderer
{
public:
	std::vector<DrawCall> calls;

	void drawRect(const sgc::AABB2f&, const sgc::Colorf&) override { calls.push_back({"rect"}); }
	void drawRectFrame(const sgc::AABB2f&, float, const sgc::Colorf&) override { calls.push_back({"rectFrame"}); }
	void drawCircle(const sgc::Vec2f&, float, const sgc::Colorf&) override { calls.push_back({"circle"}); }
	void drawCircleFrame(const sgc::Vec2f&, float, float, const sgc::Colorf&) override { calls.push_back({"circleFrame"}); }
	void drawLine(const sgc::Vec2f&, const sgc::Vec2f&, float, const sgc::Colorf&) override { calls.push_back({"line"}); }
	void drawTriangle(const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Colorf&) override { calls.push_back({"triangle"}); }
	void drawFadeOverlay(float, const sgc::Colorf&) override { calls.push_back({"fadeOverlay"}); }
	void clearBackground(const sgc::Colorf&) override { calls.push_back({"clearBg"}); }
};

/// @brief テスト用シーン（レンダラー参照を保持）
class RenderScene : public sgc::Scene
{
public:
	explicit RenderScene(sgc::IRenderer& renderer, std::string name)
		: m_renderer(renderer), m_name(std::move(name)) {}

	void update(float) override {}
	void draw() const override
	{
		m_renderer.clearBackground(sgc::Colorf::black());
		m_renderer.drawRect(sgc::AABB2f{{0, 0}, {100, 100}}, sgc::Colorf::white());
	}

	const std::string& name() const { return m_name; }

private:
	sgc::IRenderer& m_renderer;
	std::string m_name;
};

/// @brief 共有データ付きシーン（シーン間データ永続化テスト用）
struct SharedCounter
{
	int value{0};
};

class CounterScene : public sgc::Scene
{
public:
	explicit CounterScene(SharedCounter& data, std::string name)
		: m_data(data), m_name(std::move(name)) {}

	void onEnter() override { ++m_data.value; }
	void update(float) override {}
	const std::string& name() const { return m_name; }

private:
	SharedCounter& m_data;
	std::string m_name;
};

} // anonymous namespace

// ── シーンライフサイクル with ID遷移 ────────────────────

TEST_CASE("Scene lifecycle with ID transition and mock renderer", "[scene][integration]")
{
	MockRenderer renderer;
	sgc::SceneManager manager;

	manager.registerScene("menu"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "Menu");
	});
	manager.registerScene("play"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "Play");
	});

	manager.pushScene("menu"_hash);
	REQUIRE(manager.depth() == 1);

	// 描画テスト
	manager.draw();
	REQUIRE(renderer.calls.size() == 2); // clearBg + drawRect

	// ID遷移
	REQUIRE(manager.changeScene("play"_hash, 0.4f));
	manager.update(0.3f); // フェードアウト完了
	REQUIRE(manager.fadeState() == sgc::FadeState::FadingIn);

	auto* top = dynamic_cast<RenderScene*>(manager.top());
	REQUIRE(top != nullptr);
	REQUIRE(top->name() == "Play");
}

TEST_CASE("Shared data persists across ID scene transitions", "[scene][integration]")
{
	SharedCounter counter;
	sgc::SceneManager manager;

	manager.registerScene("a"_hash, [&counter]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<CounterScene>(counter, "A");
	});
	manager.registerScene("b"_hash, [&counter]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<CounterScene>(counter, "B");
	});

	manager.pushScene("a"_hash);
	REQUIRE(counter.value == 1);

	manager.replaceScene("b"_hash);
	REQUIRE(counter.value == 2);

	manager.replaceScene("a"_hash);
	REQUIRE(counter.value == 3);
}

TEST_CASE("Unregistered ID fails gracefully", "[scene][integration]")
{
	sgc::SceneManager manager;

	// 登録されていないIDでの操作は全てfalseを返す
	REQUIRE_FALSE(manager.pushScene("nonexistent"_hash));
	REQUIRE_FALSE(manager.replaceScene("nonexistent"_hash));
	REQUIRE_FALSE(manager.changeScene("nonexistent"_hash, 1.0f));
	REQUIRE(manager.empty());
}

TEST_CASE("Multiple scene transitions with mock renderer", "[scene][integration]")
{
	MockRenderer renderer;
	sgc::SceneManager manager;

	manager.registerScene("s1"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "S1");
	});
	manager.registerScene("s2"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "S2");
	});
	manager.registerScene("s3"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "S3");
	});

	// S1 → S2 → S3 push
	manager.pushScene("s1"_hash);
	manager.pushScene("s2"_hash);
	manager.pushScene("s3"_hash);
	REQUIRE(manager.depth() == 3);

	auto* top = dynamic_cast<RenderScene*>(manager.top());
	REQUIRE(top->name() == "S3");

	// pop back to S2
	manager.pop();
	REQUIRE(manager.depth() == 2);
	top = dynamic_cast<RenderScene*>(manager.top());
	REQUIRE(top->name() == "S2");
}

TEST_CASE("Scene draw uses renderer through interface", "[scene][integration]")
{
	MockRenderer renderer;
	sgc::SceneManager manager;

	manager.registerScene("game"_hash, [&renderer]() -> std::unique_ptr<sgc::Scene>
	{
		return std::make_unique<RenderScene>(renderer, "Game");
	});

	manager.pushScene("game"_hash);
	renderer.calls.clear();

	manager.draw();

	// RenderScene::draw() calls clearBackground + drawRect
	REQUIRE(renderer.calls.size() == 2);
	REQUIRE(renderer.calls[0].type == "clearBg");
	REQUIRE(renderer.calls[1].type == "rect");
}
