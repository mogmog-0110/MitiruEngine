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
#include <mitiru/audio/sfx/SfxCaption.hpp>

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
	std::uint8_t             avoidRepeat = 0;    ///< 直前の何本と同じ変種を避けるか (0 = 避けない。variants の数 - 1 以下)
	bool                     shuffle = false;    ///< true なら変種を 1 周するまで同じものを選ばない (avoidRepeat とは同時に書けない)
	SfxAttenuation           attenuation;
	std::uint8_t             priority = 128;     ///< 大きいほど残す
	int                      maxInstances = 0;   ///< 0 = 制限なし
	StealMode                steal = StealMode::Oldest;
	float                    dopplerFactor = 0.0f;
	float                    occlusionDb = -12.0f;          ///< 完全に遮られたときに下げる量
	float                    occlusionLowpassHz = 1200.0f;  ///< 完全に遮られたときの low-pass
	SfxCaption               caption;                       ///< 字幕 (defaults からは受け継がない)
};

/// @brief 遮蔽の自動計算 (sounds.json の autoOcclusion、ADR 0113)。host が聞き手から 3D の声へレイを飛ばし、遮られていれば遮蔽の量を 1 にする
struct AutoOcclusion
{
	bool  enabled = false;
	int   intervalSteps = 6;     ///< 1 本の声を調べ直す間隔 (ステップ)
	int   maxRaysPerStep = 8;    ///< 1 ステップに飛ばすレイの数の上限。越える分は次のステップに回す
	float smoothSec = 0.1f;      ///< 遮蔽の量が 0 と 1 の間を動く秒数
	float marginM = 0.25f;       ///< 声の位置のこの手前までを調べる (声が壁に埋まっていても自分の壁に遮られない)
};

struct SfxBank
{
	std::vector<SfxDef> sounds;
	SfxDef              defaults;
	std::uint32_t       seed = 0;
	int                 maxReal = 32;            ///< 実際に鳴らす声の数
	int                 maxVirtual = 128;        ///< 聞こえなくても覚えておく声の数 (実際に鳴らす分を含む)
	float               virtualBelowDb = -60.0f; ///< これより小さく聞こえる声は鳴らさず覚えておくだけにする
	AutoOcclusion       autoOcclusion;           ///< 既定では使わない

	[[nodiscard]] const SfxDef& find(std::string_view id) const noexcept
	{
		const auto it = std::find_if(sounds.begin(), sounds.end(), [&](const SfxDef& d) { return d.id == id; });
		return (it == sounds.end()) ? defaults : *it;
	}

	/// @brief 書いてある音だけを探す (無ければ nullptr)。defaults を返す find と違い、字幕の有無を見分けられる
	[[nodiscard]] const SfxDef* findListed(std::string_view id) const noexcept
	{
		const auto it = std::find_if(sounds.begin(), sounds.end(), [&](const SfxDef& d) { return d.id == id; });
		return it == sounds.end() ? nullptr : &*it;
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
