#pragma once

/// @file Decals.hpp
/// @brief 投影デカール (血・焦げ跡・足跡) の記述、描画に使う形への詰め替え、froxel への割り当て
/// @details デカールは毎フレーム、生きている分をゲームが渡す (経過秒つき)。どこに貼るかはゲームが決め、
///          レンダラは状態を持たない。だから巻き戻しや replay で戻した時刻の跡がそのまま出る。
///          前方描画の PS が froxel ごとのビット集合を引き、照明の前に基本色・法線・粗さを書き換える。
///          ここは CPU 側の規則で、GPU の結果を確かめる基準にもなる。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/LocalLights.hpp>

namespace mitiru::render
{

/// @brief 1 フレームに受け付けるデカールの数 (見えるかを選ぶ前)
inline constexpr int kMaxDecals = 1024;
/// @brief 描画に使う数。froxel のビット集合の幅がこれで決まる
inline constexpr int kMaxVisibleDecals = 256;
inline constexpr int kDecalMaskWords = kMaxVisibleDecals / 32;
/// @brief ビット集合の先頭に置く語の数 (見えているデカールの数を入れる)
inline constexpr int kDecalMaskHeaderWords = 4;

/// @brief ゲームが毎フレーム渡すデカール 1 枚 (112 byte の POD)
/// @details 箱は局所 x・y が面に貼る向き、局所 +z が面から外へ向く。箱の中にある面へ -z の向きに投影する。
///          rotation は四元数 (x, y, z, w)。色は基本色に掛け、a は不透明度。
struct DecalDesc
{
	float position[3] = {0.0f, 0.0f, 0.0f};
	float halfExtent[3] = {0.5f, 0.5f, 0.25f};   ///< z は投影する奥行きの半分
	float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float roughness = 0.5f;          ///< 書き換え先の粗さ
	float roughnessBlend = 0.0f;     ///< 粗さをどれだけ書き換えるか 0..1。0 なら面の粗さのまま
	float normalStrength = 1.0f;     ///< normalLayer の法線をどれだけ乗せるか 0..1
	float angleFadeStartDeg = 60.0f; ///< 面の法線と局所 +z の角がここまでは濃さそのまま
	float angleFadeEndDeg = 80.0f;   ///< ここで消える。回り込んだ壁へ伸びた跡を消す
	float age = 0.0f;                ///< 貼ってからの経過秒
	float lifetime = 0.0f;           ///< 0 以下なら消えない
	float fadeIn = 0.0f;             ///< 貼った直後に濃くなるまでの秒
	float fadeOut = 0.5f;            ///< lifetime の終わりに薄くなる秒
	float edgeSoftness = 0.15f;      ///< 箱の縁 (投影の奥行きと手続きの円) をぼかす割合 0..1
	std::int32_t albedoLayer = -1;   ///< VFX テクスチャの層。-1 は手続きの柔らかい円
	std::int32_t normalLayer = -1;   ///< 法線の層 (RG に接空間の xy)。-1 は法線を変えない
	std::int32_t sortOrder = 0;      ///< 大きいほど後に重ねる。同じなら渡した順
	std::uint32_t reserved = 0;
};

static_assert(sizeof(DecalDesc) == 112, "DecalDesc は 112 byte の POD");
static_assert(std::is_standard_layout_v<DecalDesc> && std::is_trivially_copyable_v<DecalDesc>);

/// @brief シェーダーが読む 1 枚 (StructuredBuffer の要素、128 byte)
/// @details toDecal はワールドから箱の [-0.5, 0.5]^3 への 3x4 (行優先)。fade は角度の窓を cos で持つ。
struct DecalGpu
{
	float toDecal[3][4];
	float color[4];    ///< rgb = 基本色に掛ける色 (線形)、a = 経過秒の濃さを掛けた不透明度
	float axisZ[4];    ///< xyz = 局所 +z (ワールド)、w = 縁のぼかし
	float axisX[4];    ///< xyz = 局所 +x (ワールド、法線の接線)、w = 法線の強さ
	float params[4];   ///< x = 基本色の層、y = 法線の層、z = 粗さ、w = 粗さの書き換え量
	float fade[4];     ///< x = cos(開始角)、y = cos(終了角)
};

static_assert(sizeof(DecalGpu) == 128, "HLSL の DecalGpu と同じ 128 byte");

/// @brief 四元数で回した局所の x・y・z 軸 (ワールド)
struct DecalAxes
{
	sgc::Vec3f x{1.0f, 0.0f, 0.0f};
	sgc::Vec3f y{0.0f, 1.0f, 0.0f};
	sgc::Vec3f z{0.0f, 0.0f, 1.0f};
};

[[nodiscard]] inline DecalAxes decalAxes(const DecalDesc& d) noexcept
{
	float qx = d.rotation[0], qy = d.rotation[1], qz = d.rotation[2], qw = d.rotation[3];
	const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
	if (!(n > 1e-8f)) { return {}; }
	qx /= n; qy /= n; qz /= n; qw /= n;
	DecalAxes a;
	a.x = {1.0f - 2.0f * (qy * qy + qz * qz), 2.0f * (qx * qy + qz * qw), 2.0f * (qx * qz - qy * qw)};
	a.y = {2.0f * (qx * qy - qz * qw), 1.0f - 2.0f * (qx * qx + qz * qz), 2.0f * (qy * qz + qx * qw)};
	a.z = {2.0f * (qx * qz + qy * qw), 2.0f * (qy * qz - qx * qw), 1.0f - 2.0f * (qx * qx + qy * qy)};
	return a;
}

/// @brief 面の点 point に法線 normal で貼るデカール。spinDeg は法線まわりの回転、size は面に貼る一辺
/// @details 当たった所に跡を残す時の普通の作り方。奥行きは size の半分で、凹凸の多い面にも届く。
[[nodiscard]] inline DecalDesc decalOnSurface(const sgc::Vec3f& point, const sgc::Vec3f& normal, float size,
                                              float spinDeg = 0.0f) noexcept
{
	DecalDesc d;
	d.position[0] = point.x; d.position[1] = point.y; d.position[2] = point.z;
	d.halfExtent[0] = d.halfExtent[1] = size * 0.5f;
	d.halfExtent[2] = size * 0.25f;
	const sgc::Vec3f z = (normal.lengthSquared() > 1e-12f) ? normal.normalized() : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	// +z を法線へ向ける最短の回転に、法線まわりの回転を重ねる
	const sgc::Vec3f from{0.0f, 0.0f, 1.0f};
	const float c = from.dot(z);
	float q[4];
	if (c < -0.99999f) { q[0] = 1.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 0.0f; }
	else
	{
		const sgc::Vec3f a = from.cross(z);
		const float s = std::sqrt((1.0f + c) * 2.0f);
		q[0] = a.x / s; q[1] = a.y / s; q[2] = a.z / s; q[3] = s * 0.5f;
	}
	const float h = spinDeg * 0.5f * 0.017453292519943295f;
	const float sx = 0.0f, sy = 0.0f, sz = std::sin(h), sw = std::cos(h);   // 局所 z まわり (右から掛ける)
	d.rotation[0] = q[3] * sx + q[0] * sw + q[1] * sz - q[2] * sy;
	d.rotation[1] = q[3] * sy - q[0] * sz + q[1] * sw + q[2] * sx;
	d.rotation[2] = q[3] * sz + q[0] * sy - q[1] * sx + q[2] * sw;
	d.rotation[3] = q[3] * sw - q[0] * sx - q[1] * sy - q[2] * sz;
	return d;
}

/// @brief 経過秒から決まる不透明度 (color[3] を掛ける前)。寿命が尽きていれば 0
[[nodiscard]] inline float decalAgeOpacity(const DecalDesc& d) noexcept
{
	if (d.age < 0.0f) { return 0.0f; }
	float o = 1.0f;
	if (d.fadeIn > 0.0f) { o = std::min(o, d.age / d.fadeIn); }
	if (d.lifetime > 0.0f)
	{
		if (d.age >= d.lifetime) { return 0.0f; }
		if (d.fadeOut > 0.0f) { o = std::min(o, (d.lifetime - d.age) / d.fadeOut); }
	}
	return std::clamp(o, 0.0f, 1.0f);
}

/// @brief 箱を包む球の半径
[[nodiscard]] inline float decalBoundRadius(const DecalDesc& d) noexcept
{
	const float x = std::fabs(d.halfExtent[0]), y = std::fabs(d.halfExtent[1]), z = std::fabs(d.halfExtent[2]);
	return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] inline DecalGpu packDecal(const DecalDesc& d) noexcept
{
	const DecalAxes a = decalAxes(d);
	const sgc::Vec3f p{d.position[0], d.position[1], d.position[2]};
	const sgc::Vec3f axis[3] = {a.x, a.y, a.z};
	DecalGpu g{};
	for (int r = 0; r < 3; ++r)
	{
		// 箱は一辺 1 なので、局所座標を全幅 (半分の 2 倍) で割る
		const float inv = 0.5f / std::max(std::fabs(d.halfExtent[r]), 1e-5f);
		g.toDecal[r][0] = axis[r].x * inv;
		g.toDecal[r][1] = axis[r].y * inv;
		g.toDecal[r][2] = axis[r].z * inv;
		g.toDecal[r][3] = -axis[r].dot(p) * inv;
	}
	const auto c = linearRgb(d.color[0], d.color[1], d.color[2]);
	for (int i = 0; i < 3; ++i) { g.color[i] = c[i]; }
	g.color[3] = std::clamp(d.color[3], 0.0f, 1.0f) * decalAgeOpacity(d);
	g.axisZ[0] = a.z.x; g.axisZ[1] = a.z.y; g.axisZ[2] = a.z.z;
	g.axisZ[3] = std::clamp(d.edgeSoftness, 0.0f, 1.0f);
	g.axisX[0] = a.x.x; g.axisX[1] = a.x.y; g.axisX[2] = a.x.z;
	g.axisX[3] = std::clamp(d.normalStrength, 0.0f, 1.0f);
	g.params[0] = static_cast<float>(d.albedoLayer);
	g.params[1] = static_cast<float>(d.normalLayer);
	g.params[2] = std::clamp(d.roughness, 0.0f, 1.0f);
	g.params[3] = std::clamp(d.roughnessBlend, 0.0f, 1.0f);
	constexpr float kDeg = 0.017453292519943295f;
	const float start = std::clamp(d.angleFadeStartDeg, 0.0f, 180.0f);
	const float end = std::clamp(std::max(d.angleFadeEndDeg, start + 0.5f), 0.0f, 180.0f);
	g.fade[0] = std::cos(start * kDeg);
	g.fade[1] = std::cos(end * kDeg);
	return g;
}

/// @brief world の点が入る箱の座標 ([-0.5, 0.5]^3 の中なら跡が乗る)。シェーダーと同じ式
[[nodiscard]] inline sgc::Vec3f decalLocal(const DecalGpu& g, const sgc::Vec3f& world) noexcept
{
	float o[3];
	for (int r = 0; r < 3; ++r)
	{
		o[r] = g.toDecal[r][0] * world.x + g.toDecal[r][1] * world.y + g.toDecal[r][2] * world.z + g.toDecal[r][3];
	}
	return {o[0], o[1], o[2]};
}

/// @brief 見えるデカールを選んで描く順に詰める
/// @details 上限を超えたら近いものから残す。残したものは sortOrder、同じなら渡した順に並べる
///          (後のものが前のものの上に重なる)。
/// @param scratch 作業領域 (呼び出し側が持ち回り、毎フレームの確保を避ける)
/// @return 見えていたのに上限で落とした数
inline int selectVisibleDecals(std::span<const DecalDesc> decals, const ClusterView& v,
                               std::vector<std::pair<float, int>>& scratch, std::vector<DecalGpu>& out,
                               std::vector<LocalLightGpu>& bounds)
{
	scratch.clear();
	out.clear();
	bounds.clear();
	for (int i = 0; i < static_cast<int>(decals.size()); ++i)
	{
		const DecalDesc& d = decals[static_cast<std::size_t>(i)];
		if (!(d.color[3] * decalAgeOpacity(d) > 0.0f)) { continue; }
		LocalLightGpu b{};
		const sgc::Vec3f c = v.toView({d.position[0], d.position[1], d.position[2]});
		b.boundCenterVS[0] = c.x; b.boundCenterVS[1] = c.y; b.boundCenterVS[2] = c.z;
		b.boundRadius = decalBoundRadius(d);
		if (!localLightInView(b, v)) { continue; }
		scratch.emplace_back(c.length() - b.boundRadius, i);
	}
	const int dropped = std::max(0, static_cast<int>(scratch.size()) - kMaxVisibleDecals);
	if (dropped > 0)
	{
		std::nth_element(scratch.begin(), scratch.begin() + kMaxVisibleDecals, scratch.end());
		scratch.resize(kMaxVisibleDecals);
	}
	std::sort(scratch.begin(), scratch.end(), [&decals](const auto& a, const auto& b) {
		const int oa = decals[static_cast<std::size_t>(a.second)].sortOrder;
		const int ob = decals[static_cast<std::size_t>(b.second)].sortOrder;
		return (oa != ob) ? (oa < ob) : (a.second < b.second);
	});
	for (const auto& s : scratch)
	{
		const DecalDesc& d = decals[static_cast<std::size_t>(s.second)];
		out.push_back(packDecal(d));
		LocalLightGpu b{};
		const sgc::Vec3f c = v.toView({d.position[0], d.position[1], d.position[2]});
		b.boundCenterVS[0] = c.x; b.boundCenterVS[1] = c.y; b.boundCenterVS[2] = c.z;
		b.boundRadius = decalBoundRadius(d);
		bounds.push_back(b);
	}
	return dropped;
}

/// @brief 球が掛かりうる froxel の範囲 (タイルと z スライス)。端は含む
struct ClusterRange
{
	int x0 = 0, x1 = -1, y0 = 0, y1 = -1, z0 = 0, z1 = -1;
};

[[nodiscard]] inline ClusterRange clusterRangeOfSphere(const float c[3], float r, const ClusterView& v) noexcept
{
	ClusterRange out;
	const float zMin = std::max(c[2] - r, v.nearZ);
	const float zMax = std::min(c[2] + r, v.farZ);
	if (zMax < zMin) { return out; }
	out.z0 = clusterSliceOfDepth(zMin, v.nearZ, v.farZ);
	out.z1 = clusterSliceOfDepth(zMax, v.nearZ, v.farZ);
	// 分子を固定すると割合は奥行きに単調なので、両端の奥行きだけ見れば外側を包める
	const auto ndcRange = [&](float lo, float hi, float tanHalf, float& mn, float& mx) {
		mn = 1e30f; mx = -1e30f;
		for (const float z : {zMin, zMax})
		{
			for (const float s : {lo, hi})
			{
				const float n = s / (z * tanHalf);
				mn = std::min(mn, n); mx = std::max(mx, n);
			}
		}
	};
	float nx0, nx1, ny0, ny1;
	ndcRange(c[0] - r, c[0] + r, v.tanHalfX, nx0, nx1);
	ndcRange(c[1] - r, c[1] + r, v.tanHalfY, ny0, ny1);
	const auto tile = [](float t, int n) { return std::clamp(static_cast<int>(std::floor(t * static_cast<float>(n))), 0, n - 1); };
	if (nx1 < -1.0f || nx0 > 1.0f || ny1 < -1.0f || ny0 > 1.0f) { return out; }
	out.x0 = tile((nx0 + 1.0f) * 0.5f, kClusterGridX);
	out.x1 = tile((nx1 + 1.0f) * 0.5f, kClusterGridX);
	out.y0 = tile((1.0f - ny1) * 0.5f, kClusterGridY);
	out.y1 = tile((1.0f - ny0) * 0.5f, kClusterGridY);
	return out;
}

/// @brief froxel ごとのビット集合を作る。先頭 kDecalMaskHeaderWords 語の [0] にデカールの数を入れる
/// @details 球が掛かりうる範囲だけを光と同じ球と箱の判定で調べるので、1 枚あたり数十 froxel で済む。
inline void assignDecalsToClusters(std::span<const LocalLightGpu> bounds, const ClusterView& v,
                                   std::span<std::uint32_t> masks) noexcept
{
	std::fill(masks.begin(), masks.end(), 0u);
	const int n = std::min(static_cast<int>(bounds.size()), kMaxVisibleDecals);
	masks[0] = static_cast<std::uint32_t>(n);
	for (int i = 0; i < n; ++i)
	{
		const LocalLightGpu& b = bounds[static_cast<std::size_t>(i)];
		const ClusterRange r = clusterRangeOfSphere(b.boundCenterVS, b.boundRadius, v);
		for (int z = r.z0; z <= r.z1; ++z)
		{
			for (int y = r.y0; y <= r.y1; ++y)
			{
				for (int x = r.x0; x <= r.x1; ++x)
				{
					if (!sphereTouchesBox(b.boundCenterVS, b.boundRadius, clusterBounds(x, y, z, v))) { continue; }
					const std::size_t c = static_cast<std::size_t>((z * kClusterGridY + y) * kClusterGridX + x);
					masks[kDecalMaskHeaderWords + c * kDecalMaskWords + static_cast<std::size_t>(i >> 5)] |= 1u << (i & 31);
				}
			}
		}
	}
}

/// @brief assignDecalsToClusters が書く語の数
inline constexpr std::size_t kDecalMaskBufferWords =
	static_cast<std::size_t>(kDecalMaskHeaderWords) + static_cast<std::size_t>(kClusterCount) * kDecalMaskWords;

} // namespace mitiru::render
