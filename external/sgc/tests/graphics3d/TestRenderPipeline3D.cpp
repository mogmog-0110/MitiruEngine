#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/graphics3d/RenderPipeline3D.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

// ── Light3D ──────────────────────────────────────────────────

TEST_CASE("Light3D - default is directional", "[graphics3d][pipeline]")
{
	Light3D light;
	REQUIRE(light.type == LightType::Directional);
	REQUIRE(light.intensity == Catch::Approx(1.0f));
	REQUIRE(light.range == Catch::Approx(100.0f));
}

TEST_CASE("Light3D - toLightData conversion", "[graphics3d][pipeline]")
{
	Light3D light;
	light.type = LightType::Point;
	light.position = {5.0f, 3.0f, 1.0f};
	light.intensity = 2.0f;

	const auto data = light.toLightData();
	REQUIRE(data.type == LightType::Point);
	REQUIRE(data.position.x == Catch::Approx(5.0f));
	REQUIRE(data.intensity == Catch::Approx(2.0f));
}

// ── DepthState ──────────────────────────────────────────────

TEST_CASE("DepthState - all values accessible", "[graphics3d][pipeline]")
{
	REQUIRE(static_cast<int>(DepthState::ReadWrite) == 0);
	REQUIRE(static_cast<int>(DepthState::ReadOnly) == 1);
	REQUIRE(static_cast<int>(DepthState::Disabled) == 2);
}

// ── RenderCommand3D ─────────────────────────────────────────

TEST_CASE("RenderCommand3D - default values", "[graphics3d][pipeline]")
{
	RenderCommand3D cmd;
	REQUIRE(cmd.meshId.empty());
	REQUIRE(cmd.materialId.empty());
	REQUIRE(cmd.sortKey == Catch::Approx(0.0f));
	REQUIRE_FALSE(cmd.transparent);
}

// ── RenderQueue3D ───────────────────────────────────────────

TEST_CASE("RenderQueue3D - empty by default", "[graphics3d][pipeline]")
{
	RenderQueue3D queue;
	REQUIRE(queue.totalCommandCount() == 0);
	REQUIRE(queue.opaqueCommands().empty());
	REQUIRE(queue.transparentCommands().empty());
}

TEST_CASE("RenderQueue3D - opaque and transparent separation", "[graphics3d][pipeline]")
{
	RenderQueue3D queue;

	RenderCommand3D opaque;
	opaque.meshId = "cube";
	opaque.transparent = false;
	queue.addCommand(opaque);

	RenderCommand3D trans;
	trans.meshId = "glass";
	trans.transparent = true;
	queue.addCommand(trans);

	REQUIRE(queue.opaqueCommands().size() == 1);
	REQUIRE(queue.transparentCommands().size() == 1);
	REQUIRE(queue.totalCommandCount() == 2);
}

TEST_CASE("RenderQueue3D - opaque sorted front to back", "[graphics3d][pipeline]")
{
	RenderQueue3D queue;

	RenderCommand3D far_cmd;
	far_cmd.meshId = "far";
	far_cmd.sortKey = 100.0f;
	queue.addCommand(far_cmd);

	RenderCommand3D near_cmd;
	near_cmd.meshId = "near";
	near_cmd.sortKey = 10.0f;
	queue.addCommand(near_cmd);

	queue.sort();

	REQUIRE(queue.opaqueCommands()[0].meshId == "near");
	REQUIRE(queue.opaqueCommands()[1].meshId == "far");
}

TEST_CASE("RenderQueue3D - transparent sorted back to front", "[graphics3d][pipeline]")
{
	RenderQueue3D queue;

	RenderCommand3D near_cmd;
	near_cmd.meshId = "near_glass";
	near_cmd.sortKey = 10.0f;
	near_cmd.transparent = true;
	queue.addCommand(near_cmd);

	RenderCommand3D far_cmd;
	far_cmd.meshId = "far_glass";
	far_cmd.sortKey = 100.0f;
	far_cmd.transparent = true;
	queue.addCommand(far_cmd);

	queue.sort();

	REQUIRE(queue.transparentCommands()[0].meshId == "far_glass");
	REQUIRE(queue.transparentCommands()[1].meshId == "near_glass");
}

TEST_CASE("RenderQueue3D - clear removes all", "[graphics3d][pipeline]")
{
	RenderQueue3D queue;
	RenderCommand3D cmd;
	cmd.meshId = "test";
	queue.addCommand(cmd);
	cmd.transparent = true;
	queue.addCommand(cmd);

	queue.clear();
	REQUIRE(queue.totalCommandCount() == 0);
}

// ── RenderPipeline3D ────────────────────────────────────────

TEST_CASE("RenderPipeline3D - default state", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;
	REQUIRE_FALSE(pipeline.isFrameActive());
	REQUIRE(pipeline.depthState() == DepthState::ReadWrite);
	REQUIRE(pipeline.lights().empty());
	REQUIRE(pipeline.queue().totalCommandCount() == 0);
}

TEST_CASE("RenderPipeline3D - beginFrame/endFrame cycle", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;

	pipeline.beginFrame();
	REQUIRE(pipeline.isFrameActive());

	RenderCommand3D cmd;
	cmd.meshId = "mesh1";
	pipeline.submit(cmd);
	REQUIRE(pipeline.queue().totalCommandCount() == 1);

	pipeline.endFrame();
	REQUIRE_FALSE(pipeline.isFrameActive());
}

TEST_CASE("RenderPipeline3D - beginFrame clears previous", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;

	pipeline.beginFrame();
	RenderCommand3D cmd;
	cmd.meshId = "mesh1";
	pipeline.submit(cmd);
	pipeline.addLight(Light3D{});
	pipeline.endFrame();

	pipeline.beginFrame();
	REQUIRE(pipeline.queue().totalCommandCount() == 0);
	REQUIRE(pipeline.lights().empty());
}

TEST_CASE("RenderPipeline3D - camera management", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;

	Camera3D cam;
	cam.position = {0.0f, 5.0f, 10.0f};
	cam.target = {0.0f, 0.0f, 0.0f};
	pipeline.setCamera(cam);

	REQUIRE(pipeline.camera().position.x == Catch::Approx(0.0f));
	REQUIRE(pipeline.camera().position.y == Catch::Approx(5.0f));
	REQUIRE(pipeline.camera().position.z == Catch::Approx(10.0f));
}

TEST_CASE("RenderPipeline3D - light management", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;
	pipeline.beginFrame();

	Light3D dir;
	dir.type = LightType::Directional;

	Light3D point;
	point.type = LightType::Point;
	point.position = {1.0f, 2.0f, 3.0f};

	pipeline.addLight(dir);
	pipeline.addLight(point);

	REQUIRE(pipeline.lights().size() == 2);
	REQUIRE(pipeline.lights()[0].type == LightType::Directional);
	REQUIRE(pipeline.lights()[1].type == LightType::Point);
}

TEST_CASE("RenderPipeline3D - depth state", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;

	pipeline.setDepthState(DepthState::ReadOnly);
	REQUIRE(pipeline.depthState() == DepthState::ReadOnly);

	pipeline.setDepthState(DepthState::Disabled);
	REQUIRE(pipeline.depthState() == DepthState::Disabled);
}

TEST_CASE("RenderPipeline3D - calculateSortKey", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;
	Camera3D cam;
	cam.position = {0.0f, 0.0f, 0.0f};
	pipeline.setCamera(cam);

	const float key1 = pipeline.calculateSortKey({10.0f, 0.0f, 0.0f});
	const float key2 = pipeline.calculateSortKey({20.0f, 0.0f, 0.0f});

	REQUIRE(key1 == Catch::Approx(100.0f));
	REQUIRE(key2 == Catch::Approx(400.0f));
	REQUIRE(key1 < key2);
}

TEST_CASE("RenderPipeline3D - viewProjectionMatrix", "[graphics3d][pipeline]")
{
	RenderPipeline3D pipeline;
	Camera3D cam;
	cam.position = {0.0f, 0.0f, 5.0f};
	cam.target = {0.0f, 0.0f, 0.0f};
	pipeline.setCamera(cam);

	const auto vp = pipeline.viewProjectionMatrix();
	// ビュー投影行列は非ゼロであること
	REQUIRE_FALSE(vp == Mat4f{});
}
