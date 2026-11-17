#pragma once

/// @file CameraZone3.hpp
/// @brief 3D の箱に入ると、カメラのパラメータを重みで混ぜる領域。
/// @details 箱ごとに指定したパラメータだけを持ち、指定のないパラメータは混ぜない。重みは出入りの秒数に応じて SpringF で変化する。CameraZoneState は flat POD として GameMemory に置く。
///
/// updateCameraZones はパラメータごとの値と効きを返し、applyCameraZones は CameraRigConfig に反映する。
/// CameraRigConfig にないゲーム固有のパラメータは Custom に入れ、mixedValue で基準値に反映する。
/// ZoneParam は、上書き、DistanceScale (乗算、基準値 1)、FovAddDeg (加算、基準値 0) のいずれかで混ぜる。
/// 箱は添字順に重ね、重なった箱は ZoneOverlap に従って扱う。すべての重みが 0 の間は値を変えない。

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/action/CameraRig.hpp>

namespace mitiru::camera
{

enum class ZoneParam : std::uint8_t
{
	Distance = 0,     ///< CameraRigConfig::distance を上書き
	DistanceScale,    ///< distance に掛ける (上書きの後)。1 で変えない
	MinDistance,
	PivotHeight,
	DefaultPitchDeg,
	MinPitchDeg,
	MaxPitchDeg,
	FovDeg,           ///< 上書き
	FovAddDeg,        ///< fovDeg に足す (上書きの後)
	FollowTime,
	Custom0,          ///< ここから先は CameraRigConfig に当てない。ゲーム固有の値を mixedValue で読む
	Custom1,
	Custom2,
	Custom3,
	Custom4,
	Custom5,
};

inline constexpr int kZoneParamCount = 16;
static_assert(static_cast<int>(ZoneParam::Custom5) + 1 == kZoneParamCount);

inline constexpr float kZoneParamUnset = std::numeric_limits<float>::quiet_NaN();

/// @brief 箱が重なったときの扱い
enum class ZoneOverlap : std::uint8_t
{
	LastWins = 0,  ///< 添字の大きい箱を上に重ねる。重みの効きが滑らかにつながる
	Average,       ///< 指定した箱の重みで平均する。順序に依らず、滑らかにつながる
	Strongest,     ///< パラメータごとに重みの一番大きい箱だけ使う (同点は添字の大きい方)。勝つ箱が替わる所で値が跳びうる
};

struct ZoneParams
{
	float value[kZoneParamCount];
	constexpr ZoneParams() noexcept : value{}
	{
		for (float& v : value) { v = kZoneParamUnset; }
	}
};

struct CameraZone3
{
	action::Vec3 min{};   ///< 箱の下限。含む
	action::Vec3 max{};   ///< 箱の上限。含まない
	float        blendIn = 0.5f;   ///< 箱に入って重みが 1 に近づくまでのおよその秒数。0 以下で即座
	float        blendOut = 0.5f;  ///< 箱を出て重みが 0 に近づくまで
	ZoneParams   params{};

	constexpr CameraZone3& set(ZoneParam p, float v) noexcept
	{
		params.value[static_cast<int>(p)] = v;
		return *this;
	}
	[[nodiscard]] bool has(ZoneParam p) const noexcept { return std::isfinite(params.value[static_cast<int>(p)]); }
	[[nodiscard]] float get(ZoneParam p) const noexcept { return params.value[static_cast<int>(p)]; }
};

[[nodiscard]] inline bool insideCameraZone(const CameraZone3& z, const action::Vec3& p) noexcept
{
	return p.x >= z.min.x && p.x < z.max.x && p.y >= z.min.y && p.y < z.max.y && p.z >= z.min.z && p.z < z.max.z;
}

template <int N = 16>
struct CameraZoneState
{
	static_assert(N > 0 && N <= 256);

	action::SpringF weight[N]{};     ///< zones の添字ごとの重み (0..1)
	std::uint32_t   initialized = 0; ///< 0 の最初の update は、箱の中なら重み 1 から始める (入った所から滑らせない)

	void reset() noexcept { *this = CameraZoneState{}; }
};

/// @brief パラメータごとの値と効き (0..1)。効きが 0 なら値を変えない
struct CameraZoneMix
{
	float value[kZoneParamCount]{};
	float influence[kZoneParamCount]{};
};

static_assert(std::is_trivially_copyable_v<CameraZoneState<16>>);
static_assert(std::is_trivially_copyable_v<CameraZoneMix>);
static_assert(std::is_trivially_copyable_v<CameraZone3>);

/// @brief 基準値 base を、混ぜた指定値へ効きの分だけ寄せる。効きが 0 なら base のまま
[[nodiscard]] inline float mixedValue(const CameraZoneMix& m, ZoneParam p, float base) noexcept
{
	const int   i = static_cast<int>(p);
	const float k = m.influence[i];
	return k > 0.0f ? base + (m.value[i] - base) * k : base;
}

namespace zone_detail
{

struct Accum
{
	float value = 0.0f;
	float influence = 0.0f;
	float sumWeight = 0.0f;
};

inline void layer(Accum& a, ZoneOverlap mode, float v, float w) noexcept
{
	switch (mode)
	{
	case ZoneOverlap::LastWins:
	{
		// 基準値 b に v を重み w で重ねると b(1-I)(1-w) + ... となるため、基準値に依存しない (V, I) として合成できる
		const float next = a.influence + w - a.influence * w;
		a.value = (a.value * a.influence * (1.0f - w) + v * w) / next;
		a.influence = next;
		break;
	}
	case ZoneOverlap::Average:
		a.value = (a.value * a.sumWeight + v * w) / (a.sumWeight + w);
		a.sumWeight += w;
		a.influence = action::minf(1.0f, a.sumWeight);
		break;
	case ZoneOverlap::Strongest:
		if (w >= a.influence) { a.value = v; a.influence = w; }
		break;
	}
}

inline void stepWeight(action::SpringF& s, bool inside, float time, float dt, float settle) noexcept
{
	const float goal = inside ? 1.0f : 0.0f;
	if (time <= 0.0f) { s.reset(goal); }
	else { s.update(goal, time, dt); }
	if (!inside && s.value < settle) { s.reset(0.0f); }
	if (s.value > 1.0f || s.value < 0.0f) { s.reset(action::saturate(s.value)); }
}

}  // namespace zone_detail

/// @brief 箱の内外に応じて重みを dt だけ進め、混ぜたパラメータを返す
/// @param settle 箱の外で重みがこの値を下回ったら 0 にし、混ぜていない状態へ正確に戻す
/// @details 重みは zones の添字に結び付く。箱を足す、消す、並べ替えるときは state.reset() を呼ぶ。N 以降の添字の箱は無視する。point が有限でないときは、すべての箱の外として扱う。dt は CameraRig と同じく 0 から 0.1 秒に収める。
template <int N>
[[nodiscard]] CameraZoneMix updateCameraZones(std::span<const CameraZone3> zones, const action::Vec3& point,
                                              CameraZoneState<N>& st, float dt, ZoneOverlap overlap = ZoneOverlap::LastWins,
                                              float settle = 0.002f) noexcept
{
	dt = std::isfinite(dt) ? action::clampf(dt, 0.0f, 0.1f) : 0.0f;
	const bool  pointOk = action::isFinite(point);
	const bool  first = st.initialized == 0;
	const int   count = zones.size() < static_cast<std::size_t>(N) ? static_cast<int>(zones.size()) : N;
	if (count > 0) { st.initialized = 1; }
	for (int i = 0; i < count; ++i)
	{
		const CameraZone3& z = zones[static_cast<std::size_t>(i)];
		const bool         inside = pointOk && insideCameraZone(z, point);
		if (first) { st.weight[i].reset(inside ? 1.0f : 0.0f); continue; }
		zone_detail::stepWeight(st.weight[i], inside, inside ? z.blendIn : z.blendOut, dt, settle);
	}

	CameraZoneMix mix{};
	for (int p = 0; p < kZoneParamCount; ++p)
	{
		zone_detail::Accum acc;
		for (int i = 0; i < count; ++i)
		{
			const CameraZone3& z = zones[static_cast<std::size_t>(i)];
			const float        w = st.weight[i].value;
			if (w > 0.0f && std::isfinite(z.params.value[p])) { zone_detail::layer(acc, overlap, z.params.value[p], w); }
		}
		mix.value[p] = acc.value;
		mix.influence[p] = acc.influence;
	}
	return mix;
}

/// @brief 混ぜた結果を反映した CameraRigConfig を返す。効きが 0 のパラメータは base とバイト単位で一致する
[[nodiscard]] inline action::CameraRigConfig applyCameraZones(const action::CameraRigConfig& base, const CameraZoneMix& m) noexcept
{
	action::CameraRigConfig c = base;
	c.distance = mixedValue(m, ZoneParam::Distance, c.distance);
	if (m.influence[static_cast<int>(ZoneParam::DistanceScale)] > 0.0f)
	{
		c.distance *= mixedValue(m, ZoneParam::DistanceScale, 1.0f);
	}
	c.minDistance = mixedValue(m, ZoneParam::MinDistance, c.minDistance);
	c.pivotHeight = mixedValue(m, ZoneParam::PivotHeight, c.pivotHeight);
	c.defaultPitchDeg = mixedValue(m, ZoneParam::DefaultPitchDeg, c.defaultPitchDeg);
	c.minPitchDeg = mixedValue(m, ZoneParam::MinPitchDeg, c.minPitchDeg);
	c.maxPitchDeg = mixedValue(m, ZoneParam::MaxPitchDeg, c.maxPitchDeg);
	c.fovDeg = mixedValue(m, ZoneParam::FovDeg, c.fovDeg);
	if (m.influence[static_cast<int>(ZoneParam::FovAddDeg)] > 0.0f)
	{
		c.fovDeg += mixedValue(m, ZoneParam::FovAddDeg, 0.0f);
	}
	c.followTime = mixedValue(m, ZoneParam::FollowTime, c.followTime);
	return c;
}

}  // namespace mitiru::camera
