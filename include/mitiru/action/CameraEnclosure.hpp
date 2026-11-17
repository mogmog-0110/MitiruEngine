#pragma once

/// @file CameraEnclosure.hpp
/// @brief 閉じた狭い部屋かを判定する純関数の宣言。CameraRig の中と外から呼ぶ。
/// @details 水平に等間隔で rays 本のレイを飛ばし、当たった本数と平均距離で判定する。外れは range と数える。
/// オンとオフでしきい値を分けたヒステリシスで、境のバンドでは前の結果を保つので、1 本の当たり外れで毎フレームは替わらない。
/// レイの向きは up と north だけで決まり、カメラの向きに依らない。
/// CameraOcclusion.hpp から読めるように置いている。CameraRig.hpp からも読むため、camera 側には依存しない。

#include <cmath>
#include <cstdint>

#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

struct EnclosureParams
{
	std::uint32_t rays = 8;     ///< 水平に撃つ本数。0 で判定しない (常に false)
	float         range = 11.0f; ///< レイの長さ (m)。0 以下で判定しない
	float         hitsOn = 7.0f;   ///< 当たった本数がこれ以上で、平均距離が meanOn 以下ならオン。本数の絶対値なので、既定は 8 本用
	float         hitsOff = 5.0f;  ///< 当たった本数がこれ以下か、平均距離が meanOff を超えたらオフ
	float         meanOn = 6.5f;
	float         meanOff = 7.5f;
	std::uint32_t mask = kAllLayers;
	Vec3          up{0.0f, 1.0f, 0.0f};     ///< 水平の面の法線
	Vec3          north{0.0f, 0.0f, 1.0f};  ///< 0 本目のレイの向き。up に直交する
};

struct EnclosureResult
{
	std::uint32_t hits = 0;          ///< 当たった本数
	float         meanDistance = 0.0f; ///< 外れを range と数えた平均距離
	std::uint8_t  enclosed = 0;
	std::uint8_t  pad[3]{};
};

/// @brief center のまわりが閉じた部屋か。wasEnclosed は前の結果で、しきい値のバンドの中ではそのまま返す
[[nodiscard]] inline EnclosureResult measureEnclosure(const CollisionWorld& world, const Vec3& center,
                                                      const EnclosureParams& p, bool wasEnclosed) noexcept
{
	EnclosureResult r;
	if (p.rays == 0 || !(p.range > 0.0f)) { return r; }
	if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
	{
		r.enclosed = wasEnclosed ? 1 : 0;
		return r;
	}
	const bool upright = p.up == Vec3{0.0f, 1.0f, 0.0f} && p.north == Vec3{0.0f, 0.0f, 1.0f};
	const Vec3 right   = upright ? Vec3{1.0f, 0.0f, 0.0f} : cross(p.up, p.north);
	const float step   = 2.0f * kPi / static_cast<float>(p.rays);
	float       sum    = 0.0f;
	for (std::uint32_t k = 0; k < p.rays; ++k)
	{
		const float a = static_cast<float>(k) * step;
		const float s = std::sin(a);
		const float c = std::cos(a);
		const Vec3  dir = upright ? Vec3{s, 0.0f, c} : right * s + p.north * c;
		const RayHit hit = world.raycast(center, dir, p.range, p.mask);
		r.hits += hit.hit ? 1u : 0u;
		sum += hit.hit ? hit.distance : p.range;
	}
	r.meanDistance = sum / static_cast<float>(p.rays);
	const float hits = static_cast<float>(r.hits);
	if (hits >= p.hitsOn && r.meanDistance <= p.meanOn) { r.enclosed = 1; }
	else if (hits <= p.hitsOff || r.meanDistance > p.meanOff) { r.enclosed = 0; }
	else { r.enclosed = wasEnclosed ? 1 : 0; }
	return r;
}

} // namespace mitiru::action
