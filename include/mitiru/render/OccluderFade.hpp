#pragma once

/// @file OccluderFade.hpp
/// @brief カメラと的 (プレイヤー) の間で的を隠す面を、画面の網点で透かす指定 (Screen::fadeOccluders3D、ABI v54、ADR 0077)。
/// @details 透かすのは、目から的の球を見込む円錐の中にあって、的の中心より手前にあり、的がその面の裏にある画素だけ。
///          的の足元の床や、的が貼り付いて走る壁は的がその面の表側にあるので抜けない。影と DDGI はそのまま残る。
///          OccluderFadeGpu は前方の描画の CbCluster と clod の CB の末尾に同じ並びで載る (DX12OccluderFadeShaders.hpp)。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

/// @brief 透かす的 1 個。position は世界の座標、radius は的を包む球の半径 (m)
struct OccluderFadeTarget
{
	float position[3] = {0.0f, 0.0f, 0.0f};
	float radius = 0.5f;
};

/// @brief フレームごとに渡す透かしの指定。targetCount が 0 なら何も透かさない
struct OccluderFade
{
	static constexpr int kMaxTargets = 4;

	OccluderFadeTarget targets[kMaxTargets]{};
	std::int32_t targetCount = 0;
	/// 円の半径の下限 (画面の高さに対する割合)。遠くの的でも、この大きさの円は抜く
	float minScreenRadius = 0.1f;
	/// 円の縁でぼかす幅 (円の半径に対する割合)
	float edge = 0.4f;
	/// 円の中心で抜く画素の割合。1 で全部抜き、0.8 なら 2 割の画素を網点として残す
	float strength = 1.0f;
	/// 隠れた所に描く的の影絵の色 (書いた sRGB、a が濃さ。a = 0 なら描かない)。影絵になるのは
	/// Screen::fadeTarget3D(true) の間に描いた物だけ
	float silhouette[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	/// @brief 的 1 個の指定。ほかは既定のまま
	[[nodiscard]] static OccluderFade around(const sgc::Vec3f& position, float radius) noexcept
	{
		OccluderFade f;
		f.targets[0] = OccluderFadeTarget{{position.x, position.y, position.z}, radius};
		f.targetCount = 1;
		return f;
	}
};

static_assert(sizeof(OccluderFadeTarget) == 16 && std::is_trivially_copyable_v<OccluderFadeTarget>);
static_assert(sizeof(OccluderFade) == 96 && std::is_trivially_copyable_v<OccluderFade> &&
              std::is_standard_layout_v<OccluderFade>, "OccluderFade の並びは ABI v54");

/// @brief シェーダーが読む透かしの定数 (HLSL の OCCLUDER_FADE_FIELDS と同じ並び)
struct OccluderFadeGpu
{
	float camera[4] = {};      ///< xyz = 目、w = 的の数
	float params[4] = {};      ///< x = 中心で抜く割合 y = 縁の幅 z = 閾値のずらし (TAA の間だけフレームごとに動く)
	float targets[4][4] = {};  ///< xyz = 的、w = 半径
	float cones[4][4] = {};    ///< xyz = 目から的への単位ベクトル、w = 円錐の半角の tan
};

static_assert(sizeof(OccluderFadeGpu) == 160);

/// @brief 目と縦の画角 (ラジアン) で、指定をシェーダーの定数へ直す。目が球の中にある的は使わない
/// @param thresholdShift 網点の閾値に足す量 (0..1)。止めた絵では 0 にして、毎フレーム同じ網点にする
[[nodiscard]] inline OccluderFadeGpu makeOccluderFadeGpu(const OccluderFade& fade, const sgc::Vec3f& eye, float fovY,
                                                         float thresholdShift) noexcept
{
	OccluderFadeGpu gpu;
	gpu.camera[0] = eye.x;
	gpu.camera[1] = eye.y;
	gpu.camera[2] = eye.z;
	gpu.params[0] = std::clamp(fade.strength, 0.0f, 1.0f);
	gpu.params[1] = std::clamp(fade.edge, 0.01f, 1.0f);
	gpu.params[2] = thresholdShift - std::floor(thresholdShift);
	const float minTan = 2.0f * std::clamp(fade.minScreenRadius, 0.0f, 1.0f) * std::tan(fovY * 0.5f);
	int used = 0;
	for (int i = 0; i < std::clamp(fade.targetCount, 0, OccluderFade::kMaxTargets); ++i)
	{
		const OccluderFadeTarget& t = fade.targets[i];
		const sgc::Vec3f to{t.position[0] - eye.x, t.position[1] - eye.y, t.position[2] - eye.z};
		const float dist = to.length();
		if (!(t.radius > 0.0f) || !(dist > t.radius * 1.01f)) { continue; }
		const float tanHalf = t.radius / std::sqrt(dist * dist - t.radius * t.radius);
		float* target = gpu.targets[used];
		float* cone = gpu.cones[used];
		target[0] = t.position[0];
		target[1] = t.position[1];
		target[2] = t.position[2];
		target[3] = t.radius;
		cone[0] = to.x / dist;
		cone[1] = to.y / dist;
		cone[2] = to.z / dist;
		cone[3] = std::max(tanHalf, minTan);
		++used;
	}
	gpu.camera[3] = static_cast<float>(used);
	return gpu;
}

} // namespace mitiru::render
