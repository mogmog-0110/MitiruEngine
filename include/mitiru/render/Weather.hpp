#pragma once

/// @file Weather.hpp
/// @brief 雨と雪 (R4)。専用の描画機能は持たず、GPU パーティクルのエミッターを 1 つ組んでゲームの particles3D に渡す純関数
/// @details 粒は世界に残るので、エミッターはカメラの真上へ毎フレーム置き直す (位置は新しく出る粒だけに効く)。
///          Collision::Kill で深度の面に当たった粒が消えるため、屋根の下には降らない。強さ intensity を急に変えると
///          出し方の周期がずれて粒が一斉に出直すので、ゲームがなだらかに動かす。着地の波紋は出さない
///          (深度で消えた位置をゲームは知らないため。必要なら床の高さの平面にエミッターを足す)。
///          見た目の値は JSON の presets で替えられる (parseWeatherPresets。慣例の置き場は assets/weather.json)。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/GpuParticles.hpp>

namespace mitiru::render
{

/// 風。dir は水平の向き (長さは見ない)、strength は粒の落ちる向きを横へ倒す量 (0 で真下、1 で約 45 度)
struct WeatherWind
{
	float dirX = 1.0f;
	float dirZ = 0.0f;
	float strength = 0.0f;
};

struct WeatherPreset
{
	std::string name;
	float ratePerSec = 4000.0f;      ///< 強さ 1 のときの毎秒の数
	float height = 20.0f;            ///< カメラの真上、粒を出す高さ (m)。球の下端 (height - radius) はカメラより上に置く
	float radius = 14.0f;            ///< 出る位置を散らす球の半径 (m)。下端がカメラより下だと、屋内で粒が屋根の下に直に出る
	float speed = 14.0f;             ///< 落ちる速さ (m/s)
	float sizeMeters = 0.012f;       ///< 粒の直径 (m)
	float stretchSec = 0.035f;       ///< 速度の向きへ伸ばす秒。雨の筋
	float spreadDeg = 3.0f;
	float color[4] = {0.75f, 0.82f, 0.95f, 0.45f};
	float nearFadeStart = 0.6f;      ///< ごく近い粒を消す距離 (m)
	float nearFadeEnd = 1.5f;
	bool lit = false;
};

[[nodiscard]] inline WeatherPreset defaultRainPreset()
{
	WeatherPreset p;
	p.name = "rain";
	return p;
}

[[nodiscard]] inline WeatherPreset defaultSnowPreset()
{
	WeatherPreset p;
	p.name = "snow";
	p.ratePerSec = 1200.0f;
	p.speed = 1.3f;
	p.sizeMeters = 0.06f;
	p.stretchSec = 0.0f;
	p.height = 12.0f;
	p.radius = 9.0f;
	p.spreadDeg = 25.0f;
	p.color[0] = 0.96f; p.color[1] = 0.97f; p.color[2] = 1.0f; p.color[3] = 0.85f;
	p.nearFadeStart = 0.4f;
	p.nearFadeEnd = 1.0f;
	return p;
}

namespace detail
{

[[nodiscard]] inline float weatherNumber(const nlohmann::json& j, const char* key, float fallback, float lo, float hi)
{
	if (!j.contains(key) || !j[key].is_number()) { return fallback; }
	return std::clamp(j[key].get<float>(), lo, hi);
}

}  // namespace detail

/// assets/weather.json の `presets` を読む。既定の rain / snow の上に、同じ name の項目は上書き、別の name は足す。
/// 形が壊れていれば既定だけを返し、warnOnce で知らせる
[[nodiscard]] inline std::vector<WeatherPreset> parseWeatherPresets(std::string_view text, std::string_view source = "weather.json")
{
	std::vector<WeatherPreset> out{defaultRainPreset(), defaultSnowPreset()};
	if (text.empty()) { return out; }
	nlohmann::json doc = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object() || !doc.contains("presets") || !doc["presets"].is_array())
	{
		debug::warnOnce("weather.json.bad", std::string(source) + " の形が違い、presets の配列が無いので、既定の rain と snow だけを使います");
		return out;
	}
	for (const nlohmann::json& item : doc["presets"])
	{
		if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) { continue; }
		const std::string name = item["name"].get<std::string>();
		auto it = std::find_if(out.begin(), out.end(), [&](const WeatherPreset& p) { return p.name == name; });
		if (it == out.end()) { out.push_back(WeatherPreset{}); out.back().name = name; it = out.end() - 1; }
		WeatherPreset& p = *it;
		p.ratePerSec = detail::weatherNumber(item, "rate", p.ratePerSec, 0.0f, 20000.0f);
		p.height = detail::weatherNumber(item, "height", p.height, 1.0f, 200.0f);
		p.radius = detail::weatherNumber(item, "radius", p.radius, 0.5f, 100.0f);
		p.speed = detail::weatherNumber(item, "speed", p.speed, 0.1f, 80.0f);
		p.sizeMeters = detail::weatherNumber(item, "size", p.sizeMeters, 0.001f, 2.0f);
		p.stretchSec = detail::weatherNumber(item, "stretch", p.stretchSec, 0.0f, 1.0f);
		p.spreadDeg = detail::weatherNumber(item, "spread", p.spreadDeg, 0.0f, 90.0f);
		p.nearFadeStart = detail::weatherNumber(item, "nearFadeStart", p.nearFadeStart, 0.0f, 50.0f);
		p.nearFadeEnd = detail::weatherNumber(item, "nearFadeEnd", p.nearFadeEnd, 0.0f, 50.0f);
		if (item.contains("lit") && item["lit"].is_boolean()) { p.lit = item["lit"].get<bool>(); }
		if (item.contains("color") && item["color"].is_array() && item["color"].size() == 4)
		{
			for (std::size_t i = 0; i < 4; ++i)
			{
				if (item["color"][i].is_number()) { p.color[i] = std::clamp(item["color"][i].get<float>(), 0.0f, 1.0f); }
			}
		}
	}
	return out;
}

/// 天候のエミッターを 1 つ組む。粒の数は rate が決め、1 つあたりの上限 (16,384) に収まる範囲で寿命を詰める。
/// @param cameraPos カメラの世界位置。エミッターはこの真上に置く
/// @param intensity 0..1。0 でも rate 0 のエミッターを返す (key を保つため)
/// @param gameTime ゲームの時刻 (秒)。GameMemory 由来にすること (壁時計だとリプレイで外れる)
/// @param quality 0..1。Dawn のように粒が CPU の板ポリになる backend では下げる
[[nodiscard]] inline ParticleEmitterDesc weatherEmitter(const WeatherPreset& preset, const float cameraPos[3], float intensity,
                                                        const WeatherWind& wind, float gameTime, float quality = 1.0f,
                                                        std::uint32_t key = 0x57454101u)
{
	constexpr std::uint32_t kMaxPerEmitter = 16384;
	const float level = std::clamp(intensity, 0.0f, 1.0f);
	ParticleEmitterDesc e;
	e.key = key;
	e.seed = key;
	e.age = std::max(gameTime, 0.0f);
	e.duration = 0.0f;
	e.position[0] = cameraPos[0];
	e.position[1] = cameraPos[1] + preset.height;
	e.position[2] = cameraPos[2];
	e.spawnRadius = preset.radius;

	const float wl = std::sqrt(wind.dirX * wind.dirX + wind.dirZ * wind.dirZ);
	const float lean = std::clamp(wind.strength, 0.0f, 1.0f);
	const float wx = wl > 1e-6f ? wind.dirX / wl * lean : 0.0f;
	const float wz = wl > 1e-6f ? wind.dirZ / wl * lean : 0.0f;
	const float len = std::sqrt(wx * wx + 1.0f + wz * wz);
	e.direction[0] = wx / len; e.direction[1] = -1.0f / len; e.direction[2] = wz / len;
	e.spreadDeg = preset.spreadDeg;
	const float speed = std::max(preset.speed, 0.1f);
	e.speedMin = speed * 0.9f;
	e.speedMax = speed * 1.1f;
	e.gravity[0] = 0.0f; e.gravity[1] = 0.0f; e.gravity[2] = 0.0f;   // 終端速度で落ちる。加速する雨は筋の長さが変わってしまう
	e.drag = 0.0f;

	// 球の上端から下端まで落ちる時間より少し長く生かし、地面や屋根に当たる前に消えないようにする (消すのは Kill)
	// 風で倒した分だけ鉛直の速さが落ちるので、direction の鉛直成分で割る
	const float verticalSpeed = std::max(speed * 0.9f * (-e.direction[1]), 0.05f);
	const float fall = (preset.height + preset.radius + 2.0f) / verticalSpeed;
	e.lifeMin = fall; e.lifeMax = fall;
	const float rate = preset.ratePerSec * level * std::clamp(quality, 0.0f, 1.0f);
	const float cap = static_cast<float>(kMaxPerEmitter) / std::max(fall, 0.01f);
	e.rate = std::min(rate, cap);
	e.burst = 0;
	e.maxParticles = kMaxPerEmitter;

	e.size[0] = e.size[1] = e.size[2] = preset.sizeMeters;
	for (int i = 0; i < 3; ++i)
	{
		for (int c = 0; c < 4; ++c) { e.color[i][c] = preset.color[c]; }
	}
	e.stretch = preset.stretchSec;
	e.softDistance = 0.0f;
	e.lit = preset.lit ? 1.0f : 0.0f;
	e.blend = ParticleBlend::Alpha;
	e.collision = ParticleCollision::Kill;
	e.shape = ParticleShape::SoftDisc;
	e.nearFadeStart = preset.nearFadeStart;
	e.nearFadeEnd = preset.nearFadeEnd;
	return e;
}

}  // namespace mitiru::render
