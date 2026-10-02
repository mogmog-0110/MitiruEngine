#pragma once

/// @file LocalLights.hpp
/// @brief 点光源とスポットライト (1 フレームに数百置く局所光) と、画面を区切った froxel への割り当て
/// @details 画面を kClusterGridX x kClusterGridY のタイルに、奥行きを kClusterGridZ 枚に対数で切った箱
///          (froxel) ごとに、届く光をビットの集合で持つ。DX12 の compute (DX12ClusteredLights.hpp) と
///          同じ規則をここに置き、CPU は見える光の選別と GPU の結果を確かめる基準に使う。
///          ビュー空間は x = 画面右、y = 画面上、z = 視線の先 (奥行き、正) で数える。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/ColorSpace.hpp>

namespace mitiru::render
{

enum class LocalLightType : std::uint32_t
{
	Point = 0,
	Spot  = 1,
};

/// @brief ゲームが置く局所光 1 個 (64 byte の POD。DLL 境界をそのまま越えられる)
/// @details 明るさは intensity / (距離^2 + 1) で落ち、range で 0 に閉じる (窓関数で滑らかに)。
///          トゥーンの陰影は距離の減衰を掛けず、届く範囲の形を主光源と同じ段で刻む。
///          スポットの角度は軸からの半角で、inner まで減衰せず outer で 0。
struct LocalLight
{
	float position[3] = {0.0f, 0.0f, 0.0f};
	float range = 5.0f;
	float color[3] = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float direction[3] = {0.0f, -1.0f, 0.0f};
	float innerConeDeg = 20.0f;
	float outerConeDeg = 30.0f;
	LocalLightType type = LocalLightType::Point;
	/// 1 ならスポットは影を落とす (見えていて提出順で先頭の 4 灯まで。影は 1 フレーム前の姿勢と物で描く)。点光源は無視
	std::uint32_t castShadow = 0;
	float reserved = 0.0f;

	[[nodiscard]] static LocalLight point(const sgc::Vec3f& pos, float lightRange, const sgc::Colorf& col,
	                                      float lightIntensity = 1.0f) noexcept
	{
		LocalLight l;
		l.position[0] = pos.x; l.position[1] = pos.y; l.position[2] = pos.z;
		l.range = lightRange;
		l.color[0] = col.r; l.color[1] = col.g; l.color[2] = col.b;
		l.intensity = lightIntensity;
		return l;
	}

	[[nodiscard]] static LocalLight spot(const sgc::Vec3f& pos, const sgc::Vec3f& dir, float lightRange,
	                                     float innerDeg, float outerDeg, const sgc::Colorf& col,
	                                     float lightIntensity = 1.0f) noexcept
	{
		LocalLight l = point(pos, lightRange, col, lightIntensity);
		l.type = LocalLightType::Spot;
		l.direction[0] = dir.x; l.direction[1] = dir.y; l.direction[2] = dir.z;
		l.innerConeDeg = innerDeg;
		l.outerConeDeg = outerDeg;
		return l;
	}
};

static_assert(sizeof(LocalLight) == 64, "LocalLight は 64 byte の POD");
static_assert(std::is_standard_layout_v<LocalLight> && std::is_trivially_copyable_v<LocalLight>);

/// @brief シェーダーが読む 1 灯 (StructuredBuffer の要素、64 byte)
/// @details color は線形にして intensity を掛け込み済み。スポットの減衰は saturate(dot(-L, dir) * spotScale + spotOffset)
///          で、点光源は scale 0 / offset 1 で常に 1 になる。bound* はビュー空間で光が届く範囲を包む球。
struct LocalLightGpu
{
	float positionWS[3];
	float range;
	float color[3];
	float spotScale;
	float directionWS[3];
	float spotOffset;
	float boundCenterVS[3];
	float boundRadius;
};

static_assert(sizeof(LocalLightGpu) == 64, "HLSL の struct と同じ 64 byte");

inline constexpr int kClusterGridX = 16;
inline constexpr int kClusterGridY = 9;
inline constexpr int kClusterGridZ = 24;
inline constexpr int kClusterCount = kClusterGridX * kClusterGridY * kClusterGridZ;
/// @brief 1 フレームに描画へ使う光の上限。froxel のビット集合の幅がこれで決まる
inline constexpr int kMaxVisibleLocalLights = 256;
inline constexpr int kClusterMaskWords = kMaxVisibleLocalLights / 32;
/// @brief 1 フレームに受け付ける光の上限 (見えるかどうかを選ぶ前)
inline constexpr int kMaxLocalLights = 1024;

/// @brief froxel を組むためのカメラ (ビュー空間の基底と視錐台)
struct ClusterView
{
	sgc::Vec3f eye{};
	sgc::Vec3f right{1.0f, 0.0f, 0.0f};
	sgc::Vec3f up{0.0f, 1.0f, 0.0f};
	sgc::Vec3f forward{0.0f, 0.0f, -1.0f};
	float tanHalfX = 1.0f;
	float tanHalfY = 1.0f;
	float nearZ = 0.1f;
	float farZ = 100.0f;

	/// @brief 描画と同じ右手系の lookAt / 垂直 fov の射影から作る
	[[nodiscard]] static ClusterView fromCamera(const Camera3D& cam) noexcept
	{
		ClusterView v;
		v.eye = cam.position();
		v.forward = (cam.target() - cam.position()).normalized();
		v.right = v.forward.cross(cam.up()).normalized();
		v.up = v.right.cross(v.forward);
		v.tanHalfY = std::tan(cam.fov() * 0.5f);
		v.tanHalfX = v.tanHalfY * cam.aspectRatio();
		v.nearZ = cam.nearClip();
		v.farZ = (cam.farClip() > cam.nearClip()) ? cam.farClip() : cam.nearClip() + 1.0f;
		return v;
	}

	[[nodiscard]] sgc::Vec3f toView(const sgc::Vec3f& world) const noexcept
	{
		const sgc::Vec3f d = world - eye;
		return {d.dot(right), d.dot(up), d.dot(forward)};
	}
};

/// @brief froxel のビュー空間の外接箱
struct ClusterBox
{
	float min[3];
	float max[3];
};

/// @brief 奥行き depth が入る z スライス。near より手前は先頭、far より奥は最後のスライス
[[nodiscard]] inline int clusterSliceOfDepth(float depth, float nearZ, float farZ) noexcept
{
	const float d = std::max(depth, nearZ);
	const auto s = static_cast<int>(std::floor(std::log(d / nearZ) / std::log(farZ / nearZ) *
	                                           static_cast<float>(kClusterGridZ)));
	return std::clamp(s, 0, kClusterGridZ - 1);
}

/// @brief z スライス slice の手前の奥行き (slice = kClusterGridZ で far)
[[nodiscard]] inline float clusterSliceDepth(int slice, float nearZ, float farZ) noexcept
{
	return nearZ * std::pow(farZ / nearZ, static_cast<float>(slice) / static_cast<float>(kClusterGridZ));
}

/// @brief froxel (x, y はタイル、y は画面の上から) の外接箱
[[nodiscard]] inline ClusterBox clusterBounds(int x, int y, int z, const ClusterView& v) noexcept
{
	const float zn = clusterSliceDepth(z, v.nearZ, v.farZ);
	const float zf = clusterSliceDepth(z + 1, v.nearZ, v.farZ);
	const float x0 = -1.0f + 2.0f * static_cast<float>(x) / kClusterGridX;
	const float x1 = -1.0f + 2.0f * static_cast<float>(x + 1) / kClusterGridX;
	const float y0 = 1.0f - 2.0f * static_cast<float>(y + 1) / kClusterGridY;
	const float y1 = 1.0f - 2.0f * static_cast<float>(y) / kClusterGridY;
	ClusterBox b{};
	b.min[0] = std::min(x0 * zn, x0 * zf) * v.tanHalfX;
	b.max[0] = std::max(x1 * zn, x1 * zf) * v.tanHalfX;
	b.min[1] = std::min(y0 * zn, y0 * zf) * v.tanHalfY;
	b.max[1] = std::max(y1 * zn, y1 * zf) * v.tanHalfY;
	b.min[2] = zn;
	b.max[2] = zf;
	return b;
}

[[nodiscard]] inline bool sphereTouchesBox(const float c[3], float r, const ClusterBox& b) noexcept
{
	float d2 = 0.0f;
	for (int i = 0; i < 3; ++i)
	{
		const float d = std::max({b.min[i] - c[i], 0.0f, c[i] - b.max[i]});
		d2 += d * d;
	}
	return d2 <= r * r;
}

/// @brief 光が届く範囲を包む球 (ワールド)。スポットは扇形の立体を包む最小に近い球にする
inline void localLightBounds(const LocalLight& l, sgc::Vec3f& center, float& radius) noexcept
{
	const sgc::Vec3f p{l.position[0], l.position[1], l.position[2]};
	center = p;
	radius = l.range;
	if (l.type != LocalLightType::Spot || l.outerConeDeg >= 89.0f) { return; }
	const sgc::Vec3f dir = sgc::Vec3f{l.direction[0], l.direction[1], l.direction[2]}.normalized();
	const float a = std::max(l.outerConeDeg, 0.0f) * 0.017453292519943295f;
	if (a > 0.7853981633974483f)
	{
		center = p + dir * (l.range * std::cos(a));
		radius = l.range * std::sin(a);
		return;
	}
	const float t = l.range / (2.0f * std::cos(a));
	center = p + dir * t;
	radius = t;
}

[[nodiscard]] inline LocalLightGpu packLocalLight(const LocalLight& l, const ClusterView& v) noexcept
{
	LocalLightGpu g{};
	const auto color = linearRgb(l.color[0], l.color[1], l.color[2]);
	for (int i = 0; i < 3; ++i)
	{
		g.positionWS[i] = l.position[i];
		g.color[i] = color[i] * l.intensity;
	}
	g.range = std::max(l.range, 1e-3f);
	const sgc::Vec3f dir = sgc::Vec3f{l.direction[0], l.direction[1], l.direction[2]}.normalized();
	g.directionWS[0] = dir.x; g.directionWS[1] = dir.y; g.directionWS[2] = dir.z;
	g.spotScale = 0.0f;
	g.spotOffset = 1.0f;
	if (l.type == LocalLightType::Spot)
	{
		constexpr float kDeg = 0.017453292519943295f;
		const float outer = std::cos(std::clamp(l.outerConeDeg, 0.0f, 89.9f) * kDeg);
		const float inner = std::cos(std::clamp(std::min(l.innerConeDeg, l.outerConeDeg), 0.0f, 89.9f) * kDeg);
		g.spotScale = 1.0f / std::max(inner - outer, 1e-3f);
		g.spotOffset = -outer * g.spotScale;
	}
	sgc::Vec3f c;
	float r = 0.0f;
	localLightBounds(l, c, r);
	const sgc::Vec3f cv = v.toView(c);
	g.boundCenterVS[0] = cv.x; g.boundCenterVS[1] = cv.y; g.boundCenterVS[2] = cv.z;
	g.boundRadius = r;
	return g;
}

/// @brief 届く範囲の球が視錐台に掛かるか
[[nodiscard]] inline bool localLightInView(const LocalLightGpu& g, const ClusterView& v) noexcept
{
	const float x = g.boundCenterVS[0], y = g.boundCenterVS[1], z = g.boundCenterVS[2], r = g.boundRadius;
	if (z + r < v.nearZ || z - r > v.farZ) { return false; }
	const float nx = 1.0f / std::sqrt(1.0f + v.tanHalfX * v.tanHalfX);
	const float ny = 1.0f / std::sqrt(1.0f + v.tanHalfY * v.tanHalfY);
	if ((x - z * v.tanHalfX) * nx > r || (-x - z * v.tanHalfX) * nx > r) { return false; }
	if ((y - z * v.tanHalfY) * ny > r || (-y - z * v.tanHalfY) * ny > r) { return false; }
	return true;
}

/// @brief 見える光を近い順に kMaxVisibleLocalLights まで選んで詰める (同じ距離は入力の順)
/// @param scratch 並べ替えの作業領域 (呼び出し側が持ち回り、毎フレームの確保を避ける)
/// @return 見えていたのに上限で落とした数
inline int selectVisibleLocalLights(std::span<const LocalLight> lights, const ClusterView& v,
                                    std::vector<std::pair<float, int>>& scratch,
                                    std::vector<LocalLightGpu>& out)
{
	scratch.clear();
	out.clear();
	for (int i = 0; i < static_cast<int>(lights.size()); ++i)
	{
		const LocalLightGpu g = packLocalLight(lights[static_cast<std::size_t>(i)], v);
		if (!localLightInView(g, v)) { continue; }
		const float dist = std::sqrt(g.boundCenterVS[0] * g.boundCenterVS[0] + g.boundCenterVS[1] * g.boundCenterVS[1] +
		                             g.boundCenterVS[2] * g.boundCenterVS[2]);
		scratch.emplace_back(dist - g.boundRadius, i);
	}
	std::sort(scratch.begin(), scratch.end());
	const int keep = std::min(static_cast<int>(scratch.size()), kMaxVisibleLocalLights);
	for (int k = 0; k < keep; ++k)
	{
		out.push_back(packLocalLight(lights[static_cast<std::size_t>(scratch[static_cast<std::size_t>(k)].second)], v));
	}
	return static_cast<int>(scratch.size()) - keep;
}

/// @brief froxel ごとのビット集合を CPU で作る (GPU の割り当てと同じ判定。基準と検証用)
inline void assignLocalLightsToClusters(std::span<const LocalLightGpu> lights, const ClusterView& v,
                                        std::vector<std::uint32_t>& masks)
{
	masks.assign(static_cast<std::size_t>(kClusterCount) * kClusterMaskWords, 0u);
	const int n = std::min(static_cast<int>(lights.size()), kMaxVisibleLocalLights);
	for (int c = 0; c < kClusterCount; ++c)
	{
		const ClusterBox box = clusterBounds(c % kClusterGridX, (c / kClusterGridX) % kClusterGridY,
		                                     c / (kClusterGridX * kClusterGridY), v);
		for (int i = 0; i < n; ++i)
		{
			const LocalLightGpu& g = lights[static_cast<std::size_t>(i)];
			if (sphereTouchesBox(g.boundCenterVS, g.boundRadius, box))
			{
				masks[static_cast<std::size_t>(c) * kClusterMaskWords + static_cast<std::size_t>(i >> 5)] |= 1u << (i & 31);
			}
		}
	}
}

/// @brief 画素 (px, py) と奥行き depth が入る froxel の通し番号 (シェーダーの clusterIndex と同じ)
[[nodiscard]] inline int clusterIndexAt(float px, float py, float depth, float viewportW, float viewportH,
                                        const ClusterView& v) noexcept
{
	const int x = std::clamp(static_cast<int>(px * kClusterGridX / viewportW), 0, kClusterGridX - 1);
	const int y = std::clamp(static_cast<int>(py * kClusterGridY / viewportH), 0, kClusterGridY - 1);
	const int z = clusterSliceOfDepth(depth, v.nearZ, v.farZ);
	return (z * kClusterGridY + y) * kClusterGridX + x;
}

} // namespace mitiru::render
