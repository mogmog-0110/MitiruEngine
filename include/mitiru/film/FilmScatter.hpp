#pragma once

/// @file FilmScatter.hpp
/// @brief 映像の `scatter` 項目: 草・木・岩のような同じ素材を、種から決まる位置・向き・大きさで何千も置く。
/// @details 配置は読み込み時に 1 回だけ生成する。実装ごとに結果が異なる標準の乱数分布を避け、splitmix64 を使う。
/// 風の傾きは時刻と位置の正弦から CPU で求め、頂点シェーダーは使わない。
/// 描画には依存しない。JSON の読み込みは FilmScatterLoad.hpp、描画は FilmScatterDraw.hpp が扱う。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/render/DrawParams3D.hpp>

namespace mitiru::film
{

/// @brief 配置を避ける領域。signedDistance が margin 未満の点には置かない
struct ScatterAvoid
{
	enum class Kind : std::uint8_t { Circle, Rect, Path };
	Kind  kind = Kind::Circle;
	float x = 0.0f, z = 0.0f;                 ///< Circle と Rect の中心
	float radius = 0.0f;                      ///< Circle の半径
	float halfX = 0.0f, halfZ = 0.0f;         ///< Rect の半幅 (回す前の x と z)
	float cosY = 1.0f, sinY = 0.0f;           ///< Rect の向き (y まわり)
	std::vector<std::pair<float, float>> path;   ///< Path の折れ線 (x, z)
	float margin = 0.0f;                      ///< この距離まで近づかない (形の縁から)
};

struct ScatterInstance
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float sx = 1.0f, sy = 1.0f, sz = 1.0f;
	float base[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   ///< 傾きと y まわりの回転 (行優先)。風の揺れはこの左から掛ける
	float phase = 0.0f;                              ///< 風の位相 (ラジアン)。位置の進行波と個体差
	float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct Scatter
{
	std::string name;
	std::string model;      ///< glb (assets からの相対)。shape と同時には書けない
	std::string shape;      ///< 組み込みメッシュ (cube / sphere など)
	float center[3] = {0.0f, 0.0f, 0.0f};
	float radius = 10.0f;
	float inner = 0.0f;     ///< この半径の内側には置かない (輪の形)
	int   count = 0;        ///< 置きたい数。避けて置けなかった分は減る (placed を見る)
	std::uint32_t seed = 1;
	float scaleMin = 1.0f, scaleMax = 1.0f;
	float scaleYMin = 1.0f, scaleYMax = 1.0f;   ///< 大きさに掛ける縦の伸び縮み
	float minDist = 0.0f;   ///< 隣との最小の間隔 (0 なら見ない)
	float leanDeg = 0.0f;   ///< 立ち方の乱れ (最大の傾き)
	float color[3] = {1.0f, 1.0f, 1.0f};
	float colorJitter = 0.0f;   ///< 明るさを 1 - jitter まで落とす個体差
	float swayAmpDeg = 0.0f;
	float swayFreq = 0.4f;
	float windX = 1.0f, windZ = 0.0f;           ///< 風が吹いていく向き (水平。正規化して使う)
	std::vector<ScatterAvoid> avoid;
	std::vector<ScatterInstance> instances;     ///< placeScatter が埋める
	mutable std::vector<render::MeshInstance> frame;   ///< 毎コマの描画に渡す行列の作業領域。placeScatter が大きさを決める (draw で確保しない)
	int   attempts = 0;                          ///< 置こうとした回数 (避けられた分を含む)
};

/// @brief world.json の地形、草、水面を映像の時刻で描く指定。world が空なら使わない。詳細は docs/TERRAIN_AND_WATER.md
struct Outdoor
{
	std::string world;                       ///< assets からの相対 (host の drawOutdoor に渡す)
	float windDir[2] = {0.0f, 0.0f};         ///< 両方 0 なら world.json の向き
	float windStrength = -1.0f;              ///< 負なら world.json の値
	float waveScale = 1.0f;
	float waterOffset = 0.0f;
	bool  grass = true, scatter = true, water = true;
};

namespace scatter_detail
{

constexpr float kPi = 3.14159265358979f;
constexpr int   kMaxCount = 400000;
constexpr int   kAttemptsPerInstance = 40;

struct Rng
{
	std::uint64_t s;
	explicit Rng(std::uint32_t seed) : s(static_cast<std::uint64_t>(seed) * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
	std::uint32_t next()
	{
		std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
	}
	float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
	float range(float a, float b) { return a + (b - a) * uniform(); }
};

inline float distToSegment(float px, float pz, float ax, float az, float bx, float bz)
{
	const float dx = bx - ax, dz = bz - az;
	const float len2 = dx * dx + dz * dz;
	float u = len2 > 0.0f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
	u = std::clamp(u, 0.0f, 1.0f);
	return std::hypot(px - (ax + dx * u), pz - (az + dz * u));
}

/// @brief 形の縁からの符号付き距離。内側は負
inline float signedDistance(const ScatterAvoid& a, float x, float z)
{
	switch (a.kind)
	{
	case ScatterAvoid::Kind::Circle: return std::hypot(x - a.x, z - a.z) - a.radius;
	case ScatterAvoid::Kind::Rect:
	{
		const float dx = x - a.x, dz = z - a.z;
		const float lx = dx * a.cosY - dz * a.sinY, lz = dx * a.sinY + dz * a.cosY;   // 形の座標へ戻す
		const float qx = std::fabs(lx) - a.halfX, qz = std::fabs(lz) - a.halfZ;
		return std::hypot(std::max(qx, 0.0f), std::max(qz, 0.0f)) + std::min(std::max(qx, qz), 0.0f);
	}
	case ScatterAvoid::Kind::Path:
	{
		float best = 1.0e30f;
		for (std::size_t i = 0; i + 1 < a.path.size(); ++i)
		{
			best = std::min(best, distToSegment(x, z, a.path[i].first, a.path[i].second, a.path[i + 1].first, a.path[i + 1].second));
		}
		if (a.path.size() == 1) { best = std::hypot(x - a.path[0].first, z - a.path[0].second); }
		return best;
	}
	}
	return 1.0e30f;
}

inline bool blocked(const Scatter& s, float x, float z)
{
	for (const ScatterAvoid& a : s.avoid)
	{
		if (signedDistance(a, x, z) < a.margin) { return true; }
	}
	return false;
}

/// @brief 行優先の 3×3 行列の積。out = a * b
inline void mul3(const float* a, const float* b, float* out)
{
	float t[9];
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c) { t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c]; }
	}
	for (int i = 0; i < 9; ++i) { out[i] = t[i]; }
}

/// @brief 単位軸の周りを angle ラジアン回転する行列を Rodrigues の公式で求める
inline void axisAngle(float ax, float ay, float az, float angle, float* m)
{
	const float c = std::cos(angle), s = std::sin(angle), k = 1.0f - c;
	m[0] = c + ax * ax * k;      m[1] = ax * ay * k - az * s; m[2] = ax * az * k + ay * s;
	m[3] = ay * ax * k + az * s; m[4] = c + ay * ay * k;      m[5] = ay * az * k - ax * s;
	m[6] = az * ax * k - ay * s; m[7] = az * ay * k + ax * s; m[8] = c + az * az * k;
}

}  // namespace scatter_detail

/// @brief s.instances を種から決まる配置で埋める。同じ Scatter からは同じ配置になる。標準の乱数分布は使わない
inline void placeScatter(Scatter& s)
{
	using namespace scatter_detail;
	s.instances.clear();
	s.frame.clear();
	s.attempts = 0;
	if (s.count <= 0 || !(s.radius > 0.0f)) { return; }
	const int want = std::min(s.count, kMaxCount);
	s.instances.reserve(static_cast<std::size_t>(want));
	Rng rng(s.seed);
	const float inner2 = s.inner * s.inner, outer2 = s.radius * s.radius;
	const float cell = s.minDist > 0.0f ? s.minDist : 1.0f;
	const int gridN = static_cast<int>(std::ceil(2.0f * s.radius / cell)) + 2;
	std::vector<std::vector<int>> grid(s.minDist > 0.0f ? static_cast<std::size_t>(gridN) * static_cast<std::size_t>(gridN) : 0);
	const float wl = std::hypot(s.windX, s.windZ);
	const float wx = wl > 0.0f ? s.windX / wl : 1.0f, wz = wl > 0.0f ? s.windZ / wl : 0.0f;
	const int maxAttempts = want * kAttemptsPerInstance;
	while (static_cast<int>(s.instances.size()) < want && s.attempts < maxAttempts)
	{
		++s.attempts;
		const float rr = std::sqrt(inner2 + (outer2 - inner2) * rng.uniform());
		const float th = 2.0f * kPi * rng.uniform();
		const float x = s.center[0] + std::cos(th) * rr, z = s.center[2] + std::sin(th) * rr;
		if (blocked(s, x, z)) { continue; }
		int gx = 0, gz = 0;
		if (s.minDist > 0.0f)
		{
			gx = std::clamp(static_cast<int>((x - (s.center[0] - s.radius)) / cell) + 1, 1, gridN - 2);
			gz = std::clamp(static_cast<int>((z - (s.center[2] - s.radius)) / cell) + 1, 1, gridN - 2);
			bool near = false;
			for (int dz = -1; dz <= 1 && !near; ++dz)
			{
				for (int dx = -1; dx <= 1 && !near; ++dx)
				{
					for (int id : grid[static_cast<std::size_t>((gz + dz) * gridN + gx + dx)])
					{
						const ScatterInstance& o = s.instances[static_cast<std::size_t>(id)];
						if (std::hypot(o.x - x, o.z - z) < s.minDist) { near = true; break; }
					}
				}
			}
			if (near) { continue; }
			grid[static_cast<std::size_t>(gz * gridN + gx)].push_back(static_cast<int>(s.instances.size()));
		}
		ScatterInstance in;
		in.x = x; in.y = s.center[1]; in.z = z;
		const float u = rng.range(s.scaleMin, s.scaleMax);
		in.sx = in.sz = u;
		in.sy = u * rng.range(s.scaleYMin, s.scaleYMax);
		float yaw[9], lean[9];
		axisAngle(0, 1, 0, 2.0f * kPi * rng.uniform(), yaw);
		const float lp = 2.0f * kPi * rng.uniform(), la = s.leanDeg * (kPi / 180.0f) * rng.uniform();
		axisAngle(std::cos(lp), 0, std::sin(lp), la, lean);
		mul3(lean, yaw, in.base);
		in.phase = rng.uniform() * kPi + 0.35f * (x * wx + z * wz);
		const float shade = 1.0f - s.colorJitter * rng.uniform();
		in.tint[0] = s.color[0] * shade; in.tint[1] = s.color[1] * shade; in.tint[2] = s.color[2] * shade;
		s.instances.push_back(in);
	}
	s.frame.resize(s.instances.size());
}

/// @brief 時刻 t の風による傾き。単位は度。位置の進行波に 2.3 倍の速さの小さな揺れを足す。正負を取り、amp が 0 なら 0
[[nodiscard]] inline float swayAngleDeg(const Scatter& s, const ScatterInstance& in, float t)
{
	if (!(s.swayAmpDeg > 0.0f)) { return 0.0f; }
	using scatter_detail::kPi;
	const float w = 2.0f * kPi * s.swayFreq;
	return s.swayAmpDeg * (std::sin(w * t + in.phase) + 0.4f * std::sin(w * 2.3f * t + 1.7f * in.phase + 0.7f)) / 1.4f;
}

/// @brief 時刻 t の配置行列。行優先の 4×4 行列で列ベクトルを使い、平行移動は 3、7、11 番に置く
inline void scatterWorld(const Scatter& s, const ScatterInstance& in, float t, float* out16)
{
	using namespace scatter_detail;
	const float wl = std::hypot(s.windX, s.windZ);
	const float wx = wl > 0.0f ? s.windX / wl : 1.0f, wz = wl > 0.0f ? s.windZ / wl : 0.0f;
	float sway[9], rot[9];
	axisAngle(wz, 0.0f, -wx, swayAngleDeg(s, in, t) * (kPi / 180.0f), sway);   // y を風下 (wx, wz) へ倒す軸
	mul3(sway, in.base, rot);
	const float sc[3] = {in.sx, in.sy, in.sz};
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c) { out16[r * 4 + c] = rot[r * 3 + c] * sc[c]; }
	}
	out16[3] = in.x; out16[7] = in.y; out16[11] = in.z;
	out16[12] = 0.0f; out16[13] = 0.0f; out16[14] = 0.0f; out16[15] = 1.0f;
}

}  // namespace mitiru::film
