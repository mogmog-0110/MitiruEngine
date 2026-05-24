#pragma once

/// @file SpatialAudio.hpp
/// @brief 3D空間音響ユーティリティ
///
/// 距離減衰・パンニング・ドップラー効果の計算関数を提供する。
/// フレームワーク非依存で、任意のオーディオバックエンドと組み合わせ可能。
///
/// @code
/// sgc::Vec3f src{10, 0, 0};
/// sgc::audio::AudioListener listener;
/// float atten = sgc::audio::calculateAttenuation(src, listener.position, 1.0f, 50.0f, 1.0f);
/// float pan = sgc::audio::calculatePan(src, listener);
/// @endcode

#include <algorithm>
#include <cmath>

#include "sgc/math/Vec3.hpp"

namespace sgc::audio
{

/// @brief 3D空間音響パラメータ
struct SpatialAudioParams
{
	Vec3f position{0, 0, 0};       ///< 音源位置
	Vec3f velocity{0, 0, 0};       ///< 音源速度（ドップラー効果用）
	float minDistance = 1.0f;       ///< 最小距離（これ以下は最大音量）
	float maxDistance = 100.0f;     ///< 最大距離（これ以上は無音）
	float rolloffFactor = 1.0f;    ///< 減衰係数
};

/// @brief リスナー（聴取者）のパラメータ
struct AudioListener
{
	Vec3f position{0, 0, 0};       ///< リスナー位置
	Vec3f forward{0, 0, -1};       ///< 前方ベクトル
	Vec3f up{0, 1, 0};             ///< 上方ベクトル
	Vec3f velocity{0, 0, 0};       ///< リスナー速度
};

/// @brief 空間音響の距離減衰を計算する
///
/// 逆二乗則に基づく減衰。minDist以下は1.0、maxDist以上は0.0を返す。
///
/// @param sourcePos 音源位置
/// @param listenerPos リスナー位置
/// @param minDist 最小距離（これ以下で最大音量）
/// @param maxDist 最大距離（これ以上で無音）
/// @param rolloff 減衰係数（1.0で通常の逆二乗則）
/// @return 減衰値 [0.0, 1.0]
[[nodiscard]] inline float calculateAttenuation(
	const Vec3f& sourcePos, const Vec3f& listenerPos,
	float minDist, float maxDist, float rolloff)
{
	const float dist = sourcePos.distanceTo(listenerPos);

	if (dist <= minDist) return 1.0f;
	if (dist >= maxDist) return 0.0f;

	/// 逆距離減衰モデル
	return minDist / (minDist + rolloff * (dist - minDist));
}

/// @brief パンニング計算（-1=左, 0=中央, 1=右）
///
/// リスナーの前方・上方ベクトルから右方向を算出し、
/// 音源方向との内積でパン値を決定する。
///
/// @param sourcePos 音源位置
/// @param listener リスナーパラメータ
/// @return パン値 [-1.0, 1.0]
[[nodiscard]] inline float calculatePan(
	const Vec3f& sourcePos, const AudioListener& listener)
{
	const Vec3f toSource = sourcePos - listener.position;
	const float dist = toSource.length();
	if (dist < 1e-6f) return 0.0f;

	const Vec3f dir = toSource / dist;

	/// 右方向 = forward × up
	const Vec3f right = listener.forward.cross(listener.up).normalized();
	return std::clamp(dir.dot(right), -1.0f, 1.0f);
}

/// @brief ドップラー効果の周波数シフト計算
///
/// 音源とリスナーの相対速度から周波数の倍率を計算する。
/// 互いに近づくと高くなり、遠ざかると低くなる。
///
/// @param sourcePos 音源位置
/// @param sourceVel 音源速度
/// @param listenerPos リスナー位置
/// @param listenerVel リスナー速度
/// @param speedOfSound 音速（m/s、デフォルト343.0）
/// @return 周波数倍率（1.0が基準）
[[nodiscard]] inline float calculateDopplerShift(
	const Vec3f& sourcePos, const Vec3f& sourceVel,
	const Vec3f& listenerPos, const Vec3f& listenerVel,
	float speedOfSound = 343.0f)
{
	const Vec3f toSource = sourcePos - listenerPos;
	const float dist = toSource.length();
	if (dist < 1e-6f) return 1.0f;

	const Vec3f dir = toSource / dist;

	/// リスナー→音源方向への速度成分（近づく = 正）
	const float vListener = listenerVel.dot(dir);
	const float vSource = sourceVel.dot(dir);

	/// ドップラー公式: f' = f * (c + vL) / (c + vS)
	/// vL > 0 でリスナーが接近 → 周波数上昇
	/// vS > 0 で音源が逃走 → 周波数低下
	const float denom = speedOfSound + vSource;
	if (std::abs(denom) < 1e-6f) return 1.0f;

	return (speedOfSound + vListener) / denom;
}

} // namespace sgc::audio
