#pragma once
/// @file MixBus.hpp
/// @brief トラックごとの音量・パン・リバーブセンドを持つステレオミックスバス（マスター 1 系統）。
/// @details モノラル素材 PcmBuffer を複数まとめ、各トラックを equal-power パンで定位し、
///          センド量に応じてマスターリバーブへ送り、ステレオにまとめる。チップチューンの
///          団子ミックスを、定位と残響のある「曲」に整えるための最終段。
///
/// @code
/// mitiru_mml::MixBus bus;
/// bus.add(leadPcm, 0.8f, -0.3f, 0.2f)   // 左寄り・少しリバーブ
///    .add(bassPcm, 0.9f,  0.0f, 0.0f)   // センター・ドライ
///    .add(padPcm,  0.5f,  0.4f, 0.5f);  // 右寄り・たっぷりリバーブ
/// mitiru_mml::Reverb rv; rv.setRoomSize(0.7f).setWet(0.35f);
/// bus.setReverb(rv);
/// auto stereo = bus.renderStereo(44100);            // L,R 交互の interleaved 16bit
/// auto wav = mitiru_mml::WavWriter::toWav(stereo, 44100, 2);
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/Reverb.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mitiru_mml
{

/// @brief ミックス 1 トラック分の設定
struct MixTrack
{
	PcmBuffer pcm;             ///< モノラル素材（16bit）
	float     volume = 1.0f;   ///< トラック音量（0..1 目安、>1 も可）
	float     pan = 0.0f;      ///< 定位 -1=左 / 0=中央 / +1=右
	float     reverbSend = 0.0f; ///< マスターリバーブへの送り量（0..1）
};

/// @brief ステレオミックスバス（マスターリバーブ 1 系統）
class MixBus
{
public:
	/// @brief トラックを追加する（pcm はムーブで受け取る）。
	MixBus& add(PcmBuffer pcm, float volume = 1.0f, float pan = 0.0f, float reverbSend = 0.0f)
	{
		m_tracks.push_back(MixTrack{std::move(pcm), volume, pan, reverbSend});
		return *this;
	}
	MixBus& add(const MixTrack& t) { m_tracks.push_back(t); return *this; }

	/// @brief マスターリバーブを設定する（センド先）。設定すると send>0 のトラックが残響を持つ。
	MixBus& setReverb(const Reverb& r) { m_reverb = r; m_useReverb = true; return *this; }
	MixBus& setMasterVolume(float v)   { m_masterVolume = (v > 0.0f) ? v : 0.0f; return *this; }

	[[nodiscard]] std::size_t trackCount() const noexcept { return m_tracks.size(); }

	/// @brief 全トラックをステレオにミックスして返す（L,R 交互の interleaved 16bit）。
	[[nodiscard]] PcmBuffer renderStereo(std::uint32_t sampleRate) const
	{
		std::size_t len = 0;
		for (const auto& t : m_tracks) { len = std::max(len, t.pcm.size()); }
		if (len == 0) { return {}; }

		std::vector<float> left(len, 0.0f);
		std::vector<float> right(len, 0.0f);
		std::vector<float> sendBus(len, 0.0f);

		for (const auto& t : m_tracks)
		{
			// equal-power パン: 中央で L=R=0.707、端で片側 1.0（合算パワー一定）。
			const float theta = (std::clamp(t.pan, -1.0f, 1.0f) * 0.5f + 0.5f) * (kPi * 0.5f);
			const float gl = std::cos(theta) * t.volume;
			const float gr = std::sin(theta) * t.volume;
			for (std::size_t i = 0; i < t.pcm.size(); ++i)
			{
				const float s = static_cast<float>(t.pcm[i]) / 32768.0f;
				left[i]  += s * gl;
				right[i] += s * gr;
				if (t.reverbSend > 0.0f) { sendBus[i] += s * t.volume * t.reverbSend; }
			}
		}

		// センドバス → マスターリバーブ → L/R にセンターで加算。
		if (m_useReverb)
		{
			PcmBuffer sendPcm(len);
			for (std::size_t i = 0; i < len; ++i)
			{
				sendPcm[i] = static_cast<std::int16_t>(std::clamp(sendBus[i], -1.0f, 1.0f) * 32767.0f);
			}
			const PcmBuffer wet = m_reverb.process(sendPcm, sampleRate);
			for (std::size_t i = 0; i < wet.size() && i < len; ++i)
			{
				const float w = static_cast<float>(wet[i]) / 32768.0f;
				left[i]  += w;
				right[i] += w;
			}
		}

		PcmBuffer out(len * 2);
		for (std::size_t i = 0; i < len; ++i)
		{
			out[i * 2]     = toI16(left[i] * m_masterVolume);
			out[i * 2 + 1] = toI16(right[i] * m_masterVolume);
		}
		return out;
	}

	/// @brief モノラルに畳んで返す（pan を無視し L,R を平均）。WavWriter::toWav() 互換。
	[[nodiscard]] PcmBuffer renderMono(std::uint32_t sampleRate) const
	{
		const PcmBuffer st = renderStereo(sampleRate);
		PcmBuffer mono(st.size() / 2);
		for (std::size_t i = 0; i < mono.size(); ++i)
		{
			const int l = st[i * 2];
			const int r = st[i * 2 + 1];
			mono[i] = static_cast<std::int16_t>((l + r) / 2);
		}
		return mono;
	}

private:
	static constexpr float kPi = 3.14159265358979323846f;

	[[nodiscard]] static std::int16_t toI16(float v) noexcept
	{
		return static_cast<std::int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
	}

	std::vector<MixTrack> m_tracks;
	Reverb m_reverb;
	bool   m_useReverb = false;
	float  m_masterVolume = 0.9f;  ///< 既定で少し下げ、3 トラック以上でも clip しにくくする
};

} // namespace mitiru_mml
