#pragma once

/// @file Atmosphere.hpp
/// @brief 物理ベースの空と空気遠近 (Hillaire 2020) の設定と、CPU の基準計算
/// @details GPU (DX12AtmosphereShaders.hpp) は同じ式を LUT に焼いて使う。CPU 側は太陽光の色、空からの環境光、
///          テストの基準に使う。長さは km、散乱係数は 1/km。世界の単位は SkySettings::metersPerUnit で km に直す。
///          描画だけが読み、GameMemory には触れない。

#include <algorithm>
#include <cmath>

#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

/// @brief 大気の組成。既定は地球 (Hillaire 2020 の表 1)
struct AtmosphereParams
{
	float bottomRadiusKm = 6360.0f;
	float topRadiusKm    = 6460.0f;
	sgc::Vec3f rayleighScattering{5.802e-3f, 13.558e-3f, 33.1e-3f};
	float rayleighScaleHeightKm = 8.0f;
	float mieScattering   = 3.996e-3f;
	float mieExtinction   = 4.40e-3f;
	float mieScaleHeightKm = 1.2f;
	float mieG = 0.8f;
	sgc::Vec3f ozoneAbsorption{0.650e-3f, 1.881e-3f, 0.085e-3f};
	float ozoneCenterKm    = 25.0f;
	float ozoneHalfWidthKm = 15.0f;
	sgc::Vec3f groundAlbedo{0.3f, 0.3f, 0.3f};
};

/// @brief 空と空気遠近の指定。太陽の向きは主光源 (Light::direction の逆) から取る
struct SkySettings
{
	bool enabled = false;
	AtmosphereParams atmosphere{};
	float sunIlluminance = 8.0f;          ///< 空の明るさ。太陽の放射照度に掛かる (HDR の値の大きさ)
	float sunDiskAngularRadius = 0.0093f; ///< 太陽の円盤の見かけの半径 (rad)。0 で円盤を描かない
	float metersPerUnit = 1.0f;           ///< 世界の 1 単位が何 m か
	float groundHeight = 0.0f;            ///< 地表の世界 y。カメラの高度はここから測る
	float aerialPerspectiveScale = 1.0f;  ///< 空気遠近の距離の倍率。大きいほど近くから霞む
	int   toonBands = 0;                  ///< 0 = 連続。2..8 なら空と空気遠近の明るさを段に切る
	bool  driveSunLight = false;          ///< 主光源の色をカメラ位置で減衰した太陽の色にする
	bool  driveAmbient  = false;          ///< 半球の環境光を空の明るさから決める
};

namespace atmosphere
{

inline constexpr float kPi = 3.14159265358979f;

/// @brief 高度 hKm での消散係数 (散乱 + 吸収、1/km)
[[nodiscard]] inline sgc::Vec3f extinctionAt(const AtmosphereParams& a, float hKm) noexcept
{
	const float r = std::exp(-hKm / a.rayleighScaleHeightKm);
	const float m = std::exp(-hKm / a.mieScaleHeightKm);
	const float o = std::max(0.0f, 1.0f - std::fabs(hKm - a.ozoneCenterKm) / a.ozoneHalfWidthKm);
	return a.rayleighScattering * r + sgc::Vec3f{a.mieExtinction, a.mieExtinction, a.mieExtinction} * m +
	       a.ozoneAbsorption * o;
}

/// @brief 半径 rKm から天頂角の余弦 mu の向きに進み、半径 radiusKm の球に出るまでの距離。当たらなければ負
[[nodiscard]] inline float distanceToSphere(float rKm, float mu, float radiusKm) noexcept
{
	const float disc = rKm * rKm * (mu * mu - 1.0f) + radiusKm * radiusKm;
	if (disc < 0.0f) { return -1.0f; }
	const float s = std::sqrt(disc);
	const float tFar = -rKm * mu + s;
	const float tNear = -rKm * mu - s;
	if (tNear > 0.0f) { return tNear; }
	return tFar;
}

/// @brief 地面に当たる向きか
[[nodiscard]] inline bool hitsGround(const AtmosphereParams& a, float rKm, float mu) noexcept
{
	return mu < 0.0f && rKm * rKm * (mu * mu - 1.0f) + a.bottomRadiusKm * a.bottomRadiusKm >= 0.0f;
}

/// @brief 半径 rKm・向き mu から距離 distKm 進む間の透過率
[[nodiscard]] inline sgc::Vec3f transmittanceAlong(const AtmosphereParams& a, float rKm, float mu, float distKm,
                                                   int steps = 40) noexcept
{
	sgc::Vec3f optical{};
	const float dt = distKm / static_cast<float>(steps);
	for (int i = 0; i < steps; ++i)
	{
		const float t = (static_cast<float>(i) + 0.5f) * dt;
		const float r = std::sqrt(rKm * rKm + t * t + 2.0f * rKm * mu * t);
		optical += extinctionAt(a, r - a.bottomRadiusKm) * dt;
	}
	return {std::exp(-optical.x), std::exp(-optical.y), std::exp(-optical.z)};
}

/// @brief 大気の上端までの透過率。地面に当たる向きは 0
[[nodiscard]] inline sgc::Vec3f transmittanceToTop(const AtmosphereParams& a, float rKm, float mu) noexcept
{
	if (hitsGround(a, rKm, mu)) { return {}; }
	return transmittanceAlong(a, rKm, mu, std::max(distanceToSphere(rKm, mu, a.topRadiusKm), 0.0f));
}

[[nodiscard]] inline float rayleighPhase(float cosTheta) noexcept
{
	return 3.0f / (16.0f * kPi) * (1.0f + cosTheta * cosTheta);
}

/// @brief Cornette-Shanks の Mie の位相関数
[[nodiscard]] inline float miePhase(float g, float cosTheta) noexcept
{
	const float g2 = g * g;
	const float num = 3.0f * (1.0f - g2) * (1.0f + cosTheta * cosTheta);
	const float den = 8.0f * kPi * (2.0f + g2) * std::pow(std::max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
	return num / den;
}

/// @brief 高度 altitudeKm から viewDir を見たときの一次散乱の輝度 (太陽の放射照度 1 あたり)。y が上
[[nodiscard]] inline sgc::Vec3f singleScatteringSky(const AtmosphereParams& a, float altitudeKm, const sgc::Vec3f& viewDir,
                                                    const sgc::Vec3f& sunDir, int steps = 32) noexcept
{
	const float r0 = a.bottomRadiusKm + std::max(altitudeKm, 0.001f);
	const float mu = viewDir.y;
	const bool ground = hitsGround(a, r0, mu);
	const float dist = ground ? distanceToSphere(r0, mu, a.bottomRadiusKm) : distanceToSphere(r0, mu, a.topRadiusKm);
	if (!(dist > 0.0f)) { return {}; }
	const float cosTheta = viewDir.dot(sunDir);
	const float pr = rayleighPhase(cosTheta);
	const float pm = miePhase(a.mieG, cosTheta);
	const float dt = dist / static_cast<float>(steps);
	sgc::Vec3f optical{};
	sgc::Vec3f radiance{};
	for (int i = 0; i < steps; ++i)
	{
		const float t = (static_cast<float>(i) + 0.5f) * dt;
		const sgc::Vec3f p{viewDir.x * t, r0 + viewDir.y * t, viewDir.z * t};
		const float r = p.length();
		const float h = r - a.bottomRadiusKm;
		optical += extinctionAt(a, h) * dt;
		const sgc::Vec3f up = p * (1.0f / r);
		const sgc::Vec3f sunT = transmittanceToTop(a, r, up.dot(sunDir));
		const sgc::Vec3f scatter = a.rayleighScattering * (std::exp(-h / a.rayleighScaleHeightKm) * pr) +
		                           sgc::Vec3f{1.0f, 1.0f, 1.0f} * (a.mieScattering * std::exp(-h / a.mieScaleHeightKm) * pm);
		const sgc::Vec3f viewT{std::exp(-optical.x), std::exp(-optical.y), std::exp(-optical.z)};
		radiance += viewT * sunT * scatter * dt;
	}
	return radiance;
}

/// @brief 半球の環境光 (上と下)。上は空の輝度の余弦重みの平均、下は地面の反射。どちらも sunIlluminance を掛け込む
struct SkyAmbient
{
	sgc::Vec3f sky{};
	sgc::Vec3f ground{};
};

[[nodiscard]] inline SkyAmbient skyAmbient(const SkySettings& s, float altitudeKm, const sgc::Vec3f& sunDir) noexcept
{
	constexpr int kRings = 4;
	constexpr int kSegments = 8;
	sgc::Vec3f sum{};
	float weight = 0.0f;
	for (int i = 0; i < kRings; ++i)
	{
		const float elev = (static_cast<float>(i) + 0.5f) / kRings * (kPi * 0.5f);
		for (int j = 0; j < kSegments; ++j)
		{
			const float az = static_cast<float>(j) / kSegments * 2.0f * kPi;
			const sgc::Vec3f d{std::cos(elev) * std::cos(az), std::sin(elev), std::cos(elev) * std::sin(az)};
			const float w = std::sin(elev) * std::cos(elev);
			sum += singleScatteringSky(s.atmosphere, altitudeKm, d, sunDir, 12) * w;
			weight += w;
		}
	}
	SkyAmbient out;
	out.sky = sum * (s.sunIlluminance / std::max(weight, 1e-6f));
	const float r0 = s.atmosphere.bottomRadiusKm + std::max(altitudeKm, 0.001f);
	const sgc::Vec3f sunT = transmittanceToTop(s.atmosphere, r0, sunDir.y);
	const sgc::Vec3f direct = sunT * (std::max(sunDir.y, 0.0f) * s.sunIlluminance / kPi);
	out.ground = s.atmosphere.groundAlbedo * (direct + out.sky);
	return out;
}

/// @brief カメラ位置から見た太陽光の色 (透過率)。地平線の下なら 0
[[nodiscard]] inline sgc::Vec3f sunTransmittance(const AtmosphereParams& a, float altitudeKm, const sgc::Vec3f& sunDir) noexcept
{
	return transmittanceToTop(a, a.bottomRadiusKm + std::max(altitudeKm, 0.001f), sunDir.y);
}

/// @brief 時刻 (時、12 = 南中) と緯度 (度) と赤緯 (度、春分 = 0) から太陽への向き。東 = +x、北 = -z、上 = +y
[[nodiscard]] inline sgc::Vec3f sunDirectionFromTimeOfDay(float hours, float latitudeDeg,
                                                          float declinationDeg = 0.0f) noexcept
{
	const float h = (hours - 12.0f) * (kPi / 12.0f);
	const float phi = latitudeDeg * (kPi / 180.0f);
	const float dec = declinationDeg * (kPi / 180.0f);
	const float east  = -std::cos(dec) * std::sin(h);
	const float north = std::sin(dec) * std::cos(phi) - std::cos(dec) * std::cos(h) * std::sin(phi);
	const float up    = std::sin(dec) * std::sin(phi) + std::cos(dec) * std::cos(h) * std::cos(phi);
	return sgc::Vec3f{east, up, -north}.normalized();
}

/// @brief 世界の y からカメラの高度 (km) を出す。地表の下は地表すれすれにする
[[nodiscard]] inline float cameraAltitudeKm(const SkySettings& s, float worldY) noexcept
{
	return std::max((worldY - s.groundHeight) * s.metersPerUnit * 0.001f, 0.0005f);
}

} // namespace atmosphere

} // namespace mitiru::render
