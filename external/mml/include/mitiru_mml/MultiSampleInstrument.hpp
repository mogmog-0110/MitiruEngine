#pragma once
/// @file MultiSampleInstrument.hpp
/// @brief 鍵域 / ベロシティゾーンごとに異なるサンプルを持つマルチサンプル楽器。
/// @details 1 サンプル = SampleInstrument を「ゾーン（鍵域・ベロシティ範囲）」付きで束ねる。
///          サウンドフォント（SF2 / SFZ）の楽器を表現する共通基盤(#9)。発音時は MIDI ノート番号と
///          ベロシティで該当ゾーンを選び、その SampleInstrument にピッチ付きで委譲する。
///          サンプルのネイティブレートと出力レートの差は実効 baseFreq に畳み込むため、
///          SampleInstrument 側のリサンプラはそのまま使える。
///
/// @code
/// mitiru_mml::MultiSampleInstrument inst;
/// inst.addSampleZone(pcm, 22050, 60, 44100, 0, pcm.size(), true, {}, 48, 72); // C4 中心の鍵域
/// auto note = inst.renderNote(64 /*E4*/, 100, 18000, 22050, 1.0f);
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/SampleInstrument.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief マルチサンプル楽器（モノラル）
class MultiSampleInstrument
{
public:
	/// @brief 1 ゾーン = 鍵域 / ベロシティ範囲 + 発音ボイス。
	struct Zone
	{
		int keyLo = 0;   ///< 鍵域下限（MIDI ノート番号 0..127）
		int keyHi = 127; ///< 鍵域上限
		int velLo = 0;   ///< ベロシティ下限 0..127
		int velHi = 127; ///< ベロシティ上限
		SampleInstrument voice;
		float gain = 1.0f; ///< ゾーン固有ゲイン（SF2 initialAttenuation 等）
	};

	/// @brief 既製ゾーンを追加する。
	void addZone(Zone z) { m_zones.push_back(std::move(z)); }

	/// @brief サンプルから 1 ゾーンを構築して追加する。
	/// @param pcm 16bit モノラルサンプル
	/// @param sampleRate サンプルのネイティブレート
	/// @param rootKey そのサンプルが原音で鳴る MIDI ノート番号
	/// @param outputRate 出力レート（再生時のレート）
	/// @param loopStart,loopEnd サステインループ区間（looped=false なら無視）
	/// @param looped サステイン中にループするか
	/// @param adsr アンプ ADSR
	/// @param keyLo,keyHi,velLo,velHi ゾーンの適用範囲
	void addSampleZone(const PcmBuffer& pcm, std::uint32_t sampleRate, int rootKey,
	                   std::uint32_t outputRate, std::size_t loopStart, std::size_t loopEnd,
	                   bool looped, const SampleInstrument::Adsr& adsr,
	                   int keyLo = 0, int keyHi = 127, int velLo = 0, int velHi = 127)
	{
		Zone z;
		z.keyLo = keyLo; z.keyHi = keyHi; z.velLo = velLo; z.velHi = velHi;
		// レート差をネイティブの基準周波数に畳む: step = freq/baseFreq で 22050→44100 を正しく補間する。
		const float sr = sampleRate > 0 ? static_cast<float>(sampleRate) : static_cast<float>(outputRate);
		const float baseFreq = midiToFreq(rootKey) * static_cast<float>(outputRate) / sr;
		z.voice.setSample(pcm, baseFreq, outputRate);
		if (looped) z.voice.setLoop(loopStart, loopEnd);
		z.voice.setAdsr(adsr);
		m_zones.push_back(std::move(z));
	}

	void clear() { m_zones.clear(); }
	[[nodiscard]] bool valid() const noexcept { return !m_zones.empty(); }
	[[nodiscard]] int zoneCount() const noexcept { return static_cast<int>(m_zones.size()); }

	/// @brief 1 音を描画する。該当ゾーンが無ければ空を返す。
	/// @param midiKey MIDI ノート番号 0..127
	/// @param velocity 0..127
	[[nodiscard]] PcmBuffer renderNote(int midiKey, int velocity, int gateSamples,
	                                   int totalSamples, float volume) const
	{
		const Zone* z = selectZone(midiKey, velocity);
		if (!z) return {};
		return z->voice.renderNote(midiToFreq(midiKey), gateSamples, totalSamples, volume * z->gain);
	}

	/// @brief MIDI ノート番号 → 周波数（A4=69=440Hz）。
	[[nodiscard]] static float midiToFreq(int key) noexcept
	{
		return 440.0f * std::pow(2.0f, (static_cast<float>(key) - 69.0f) / 12.0f);
	}

private:
	/// @brief 鍵域 + ベロシティに合致する最初のゾーンを返す。
	[[nodiscard]] const Zone* selectZone(int key, int vel) const noexcept
	{
		for (const auto& z : m_zones)
			if (key >= z.keyLo && key <= z.keyHi && vel >= z.velLo && vel <= z.velHi)
				return &z;
		return nullptr;
	}

	std::vector<Zone> m_zones;
};

} // namespace mitiru_mml
