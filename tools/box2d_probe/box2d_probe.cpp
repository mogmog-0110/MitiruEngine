// Box2D の結果が CPU・SIMD 幅・スレッド数で変わるかを測るツール。
// 同じ実行ファイルを別の PC へ持って行き、出力の hash を比べる。
//
// box2d_probe [--width 8|4] [--workers N] [--nudge] [--bridge] [--steps N] [--rows N]
//
// --width 4 は Box2D 3.2 の b2World_EnableSSE2Fallback で SSE2 の経路に切り替える。AVX2 を持たない CPU の代用にする。
// 3.1.1 は組み込み時に AVX2 の使用を決めるため、--width を無視し、スレッドも使わない。
#include <box2d/box2d.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mitiru/physics/Box2DBridge.hpp"

namespace
{

struct Options
{
	int  width   = 8;
	int  workers = 1;
	int  steps   = 600;
	int  rows    = 20;
	bool nudge   = false;
	bool bridge  = false;
};

void hashBytes(std::uint64_t& h, const void* data, std::size_t size)
{
	const auto* p = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
}

// 箱 210 個のピラミッドと、斜めに投げ込む球 40 個。接触が数百本でき、8 本ずつの束が満ちる。
struct Scene
{
	std::vector<b2BodyId> bodies;
};

Scene buildScene(b2WorldId world, bool nudge, int rows)
{
	Scene scene;
	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = b2_staticBody;
	bd.position = {0.0f, -0.5f};
	const b2BodyId ground = b2CreateBody(world, &bd);
	b2ShapeDef gsd = b2DefaultShapeDef();
	gsd.enableContactEvents = true;
	const b2Polygon gbox = b2MakeBox(120.0f, 0.5f);
	b2CreatePolygonShape(ground, &gsd, &gbox);

	for (int row = 0; row < rows; ++row)
	{
		for (int col = 0; col < rows - row; ++col)
		{
			b2BodyDef d = b2DefaultBodyDef();
			d.type = b2_dynamicBody;
			d.position = {(static_cast<float>(col) - 0.5f * static_cast<float>(rows - row - 1)) * 1.05f,
			              0.5f + static_cast<float>(row) * 1.0f};
			const b2BodyId b = b2CreateBody(world, &d);
			b2ShapeDef sd = b2DefaultShapeDef();
			sd.density = 1.0f;
			sd.enableContactEvents = true;
			const b2Polygon box = b2MakeBox(0.5f, 0.5f);
			b2CreatePolygonShape(b, &sd, &box);
			scene.bodies.push_back(b);
		}
	}

	const int ballCount = 2 * rows;
	for (int i = 0; i < ballCount; ++i)
	{
		float x = -30.0f - 0.8f * static_cast<float>(i % 5);
		if (nudge && i == ballCount / 2) x = std::nextafter(x, 0.0f);
		b2BodyDef d = b2DefaultBodyDef();
		d.type = b2_dynamicBody;
		d.position = {x, 1.0f + 0.9f * static_cast<float>(i / 5)};
		const b2BodyId b = b2CreateBody(world, &d);
		b2ShapeDef sd = b2DefaultShapeDef();
		sd.density = 2.0f;
		sd.material.restitution = 0.3f;
		sd.material.friction = 0.6f;
		sd.enableContactEvents = true;
		const b2Circle c = {{0.0f, 0.0f}, 0.45f};
		b2CreateCircleShape(b, &sd, &c);
		b2Body_SetLinearVelocity(b, {18.0f, 3.0f});
		scene.bodies.push_back(b);
	}
	return scene;
}

struct Result
{
	std::uint64_t finalHash = 0;
	std::vector<std::uint64_t> checkpoints;
	double seconds = 0.0;
};

Result run(b2WorldId world, const Scene& scene, int steps)
{
	Result r;
	std::uint64_t h = 1469598103934665603ull;
	const auto t0 = std::chrono::steady_clock::now();
	for (int step = 0; step < steps; ++step)
	{
		b2World_Step(world, 1.0f / 60.0f, 4);
		for (const b2BodyId body : scene.bodies)
		{
			const b2Vec2 p = b2Body_GetPosition(body);
			const b2Vec2 v = b2Body_GetLinearVelocity(body);
			const float a = b2Rot_GetAngle(b2Body_GetRotation(body));
			hashBytes(h, &p, sizeof(p));
			hashBytes(h, &v, sizeof(v));
			hashBytes(h, &a, sizeof(a));
		}
		if ((step + 1) % 100 == 0) r.checkpoints.push_back(h);
	}
	r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	r.finalHash = h;
	return r;
}

} // namespace

int main(int argc, char** argv)
{
	Options opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--width" && i + 1 < argc) opt.width = std::atoi(argv[++i]);
		else if (a == "--workers" && i + 1 < argc) opt.workers = std::atoi(argv[++i]);
		else if (a == "--steps" && i + 1 < argc) opt.steps = std::atoi(argv[++i]);
		else if (a == "--rows" && i + 1 < argc) opt.rows = std::atoi(argv[++i]);
		else if (a == "--nudge") opt.nudge = true;
		else if (a == "--bridge") opt.bridge = true;
		else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
	}

#ifdef PROBE_B2_V32
	const bool hasAvx2 = b2IsAVX2Available();
#else
	const bool hasAvx2 = false;
#endif

	Result res;
	if (opt.bridge)
	{
		mitiru::physics::Box2DWorld w;
#ifdef PROBE_B2_V32
		if (opt.width == 4) b2World_EnableSSE2Fallback(w.worldId(), true);
#endif
		const Scene scene = buildScene(w.worldId(), opt.nudge, opt.rows);
		res = run(w.worldId(), scene, opt.steps);
	}
	else
	{
		b2WorldDef def = b2DefaultWorldDef();
		def.gravity = {0.0f, -9.81f};
#ifdef PROBE_B2_V32
		def.workerCount = opt.workers;
#endif
		const b2WorldId world = b2CreateWorld(&def);
#ifdef PROBE_B2_V32
		if (opt.width == 4) b2World_EnableSSE2Fallback(world, true);
#endif
		const Scene scene = buildScene(world, opt.nudge, opt.rows);
		res = run(world, scene, opt.steps);
		b2DestroyWorld(world);
	}

	const b2Version ver = b2GetVersion();
	std::printf("box2d=%d.%d.%d avx2_cpu=%d width=%d workers=%d bridge=%d nudge=%d steps=%d rows=%d time=%.3fs\n",
	            ver.major, ver.minor, ver.revision, hasAvx2 ? 1 : 0, opt.width, opt.workers,
	            opt.bridge ? 1 : 0, opt.nudge ? 1 : 0, opt.steps, opt.rows, res.seconds);
	for (std::size_t i = 0; i < res.checkpoints.size(); ++i)
		std::printf("  step%zu00 %016llx\n", i + 1, static_cast<unsigned long long>(res.checkpoints[i]));
	std::printf("  final %016llx\n", static_cast<unsigned long long>(res.finalHash));
	return 0;
}
