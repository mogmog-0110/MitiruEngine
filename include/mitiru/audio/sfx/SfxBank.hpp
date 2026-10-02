#pragma once

/// @file SfxBank.hpp
/// @brief 効果音ごとの鳴らし方と、その計算
/// @details 差し替え、揺らぎ、距離での減衰、優先度、同時数、遮蔽を扱う。assets/audio/sounds.json を SfxBankJson.hpp が読み、この形にする。記載のない音には defaults を使う。
/// 揺らぎの乱数は seed、フレーム、id、そのフレームで何本目か、番号だけから作るため、録画の再生でも同じ差し替えとピッチになる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/audio/SpatialAudio.hpp>

namespace mitiru::audio::sfx
{

/// @brief 同時数が maxInstances を超えたときに止める音
enum class StealMode : std::uint8_t
{
	Oldest,     ///< いちばん前に鳴らし始めたもの
	Quietest,   ///< いま聞こえる大きさがいちばん小さいもの (新しい音の方が小さければ新しい音を鳴らさない)
	None,       ///< 止めない (新しい音を鳴らさない)
};

/// @brief 距離による減衰。curve があれば距離と音量の折れ線、なければ model の式を使う
struct SfxAttenuation
{
	AttenuationModel                  model = AttenuationModel::InverseDistance;
	float                             minDistance = 1.0f;
	float                             maxDistance = 100.0f;
	float                             rolloff = 1.0f;
	std::vector<std::array<float, 2>> curve;
};

struct SfxDef
{
	std::string              id;
	std::vector<std::string> variants;          ///< 鳴らすファイルの候補 (空なら id そのもの)
	std::array<float, 2>     volumeDb{0.0f, 0.0f};
	std::array<float, 2>     pitchSemitones{0.0f, 0.0f};
	SfxAttenuation           attenuation;
	std::uint8_t             priority = 128;     ///< 大きいほど残す
	int                      maxInstances = 0;   ///< 0 = 制限なし
	StealMode                steal = StealMode::Oldest;
	float                    dopplerFactor = 0.0f;
	float                    occlusionDb = -12.0f;          ///< 完全に遮られたときに下げる量
	float                    occlusionLowpassHz = 1200.0f;  ///< 完全に遮られたときの low-pass
};

struct SfxBank
{
	std::vector<SfxDef> sounds;
	SfxDef              defaults;
	std::uint32_t       seed = 0;
	int                 maxReal = 32;            ///< 実際に鳴らす声の数
	int                 maxVirtual = 128;        ///< 聞こえなくても覚えておく声の数 (実際に鳴らす分を含む)
	float               virtualBelowDb = -60.0f; ///< これより小さく聞こえる声は鳴らさず覚えておくだけにする

	[[nodiscard]] const SfxDef& find(std::string_view id) const noexcept
	{
		const auto it = std::find_if(sounds.begin(), sounds.end(), [&](const SfxDef& d) { return d.id == id; });
		return (it == sounds.end()) ? defaults : *it;
	}
};

/// @brief distance での音量。範囲は 0..1
[[nodiscard]] inline float attenuate(const SfxAttenuation& a, float distance) noexcept
{
	if (a.curve.empty())
	{
		SpatialSourceConfig cfg;
		cfg.minDistance = a.minDistance;
		cfg.maxDistance = a.maxDistance;
		cfg.rolloffFactor = a.rolloff;
		cfg.attenuation = a.model;
		SpatialAudio s;
		return s.calculate(AudioVec3{distance, 0.0f, 0.0f}, cfg).volume;
	}
	const auto& c = a.curve;
	if (distance <= c.front()[0]) { return c.front()[1]; }
	for (std::size_t i = 1; i < c.size(); ++i)
	{
		if (distance > c[i][0]) { continue; }
		const float span = c[i][0] - c[i - 1][0];
		const float u = (span > 0.0f) ? (distance - c[i - 1][0]) / span : 1.0f;
		return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * u;
	}
	return c.back()[1];
}

/// @brief 遮蔽に応じて掛ける音量。occlusion は 0 が遮蔽なし、1 が完全な遮蔽
[[nodiscard]] inline float occlusionGain(const SfxDef& d, float occlusion) noexcept
{
	return std::pow(10.0f, d.occlusionDb * std::clamp(occlusion, 0.0f, 1.0f) / 20.0f);
}

/// @brief 遮蔽に応じた low-pass の周波数。0 は適用なし。耳には周波数の比で聞こえるため、中間値は対数で補間する
[[nodiscard]] inline float occlusionLowpass(const SfxDef& d, float occlusion) noexcept
{
	const float o = std::clamp(occlusion, 0.0f, 1.0f);
	if (o <= 0.0f || d.occlusionLowpassHz <= 0.0f) { return 0.0f; }
	constexpr float kOpenHz = 20000.0f;
	return kOpenHz * std::pow(d.occlusionLowpassHz / kOpenHz, o);
}

/// @brief 揺らぎの乱数の元に使う splitmix64
[[nodiscard]] constexpr std::uint64_t mixBits(std::uint64_t x) noexcept
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

[[nodiscard]] constexpr std::uint64_t hashId(std::string_view id) noexcept
{
	std::uint64_t h = 14695981039346656037ull;
	for (const char c : id) { h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull; }
	return h;
}

/// @brief 1 本分の揺らぎ。同じ入力からは同じ値になる
struct SfxRoll
{
	std::size_t variant = 0;
	float       gain = 1.0f;
	float       pitch = 1.0f;
};

[[nodiscard]] inline SfxRoll rollSfx(const SfxDef& d, std::uint32_t seed, std::uint64_t frame,
	std::uint64_t idHash, std::uint32_t ordinal, std::uint32_t handle) noexcept
{
	const std::uint64_t slot = (std::uint64_t{ordinal} << 32) | handle;
	std::uint64_t h = mixBits(seed ^ mixBits(frame ^ mixBits(idHash ^ mixBits(slot))));
	auto unit = [&h] { h = mixBits(h); return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24); };
	SfxRoll r;
	r.variant = d.variants.empty() ? 0 : static_cast<std::size_t>(mixBits(h) % d.variants.size());
	r.gain = std::pow(10.0f, (d.volumeDb[0] + (d.volumeDb[1] - d.volumeDb[0]) * unit()) / 20.0f);
	r.pitch = std::pow(2.0f, (d.pitchSemitones[0] + (d.pitchSemitones[1] - d.pitchSemitones[0]) * unit()) / 12.0f);
	return r;
}

}  // namespace mitiru::audio::sfx
