#pragma once
/// @file SendBus.hpp
/// @brief ModernSong のエフェクトの送りバス (リバーブ、ディレイ、コーラス)
/// @details トラックごとの送りの量を、エフェクトごとのモノラルのバスに足し、既存の Reverb、Delay、Chorus (MixBus と同じ部品)
///          を 1 サンプルずつ通して、戻りをステレオの左右に同じ値で足す。足すだけで、トラックの dry の音は変えない。
///          一括の render() と ModernSongStream が同じ部品を呼ぶので、塊の切り方によらず同じ float になる。
///          戻りは reverb、delay、chorus の順に足す。使わないエフェクトは通らない (状態も持たない)。

#include <mitiru_mml/Chorus.hpp>
#include <mitiru_mml/Delay.hpp>
#include <mitiru_mml/Reverb.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mitiru_mml
{

class SendBus
{
public:
	static constexpr std::size_t EFFECT_COUNT = 3;
	static constexpr std::size_t REVERB = 0;
	static constexpr std::size_t DELAY = 1;
	static constexpr std::size_t CHORUS = 2;

	using Amounts = std::array<float, EFFECT_COUNT>;

	/// @param used 通すエフェクト。false のエフェクトは状態を作らず、戻りもない
	SendBus(const Reverb& reverb, const Delay& delay, const Chorus& chorus, std::uint32_t sampleRate,
		const std::array<bool, EFFECT_COUNT>& used)
	{
		if (used[REVERB]) m_reverb.emplace(reverb, sampleRate);
		if (used[DELAY]) m_delay.emplace(delay, sampleRate);
		if (used[CHORUS]) m_chorus.emplace(chorus, sampleRate);
		m_used = used;
	}

	[[nodiscard]] bool used(std::size_t effect) const noexcept
	{
		return m_used[effect];
	}

	/// @brief frames フレーム分のバスを空にする。ブロックの先頭で呼ぶ
	void begin(std::size_t frames)
	{
		for (auto& bus : m_input)
		{
			bus.assign(frames, 0.0f);
		}
	}

	/// @brief ステレオ音声 (L, R, L, R...) をモノラルにし、amounts の量で各バスに足す。量が 0 のエフェクトには足さない
	void addTap(const float* stereo, std::size_t frames, const Amounts& amounts)
	{
		for (std::size_t e = 0; e < EFFECT_COUNT; ++e)
		{
			if (amounts[e] <= 0.0f || !m_used[e])
			{
				continue;
			}
			std::vector<float>& bus = m_input[e];
			for (std::size_t i = 0; i < frames; ++i)
			{
				bus[i] += (0.5f * (stereo[i * 2] + stereo[i * 2 + 1])) * amounts[e];
			}
		}
	}

	/// @brief バスをエフェクトに通し、戻りを mix (L, R, L, R...) の左右に足す
	/// @details 戻りは returns() にも残る。使わないエフェクトの戻りは空になる。
	void process(float* mix, std::size_t frames)
	{
		for (std::size_t e = 0; e < EFFECT_COUNT; ++e)
		{
			m_return[e].clear();
			if (!m_used[e])
			{
				continue;
			}
			m_return[e].resize(frames);
			for (std::size_t i = 0; i < frames; ++i)
			{
				const float x = m_input[e][i];
				m_return[e][i] = (e == REVERB) ? m_reverb->wet(x) : (e == DELAY) ? m_delay->wet(x) : m_chorus->wet(x);
			}
			for (std::size_t i = 0; i < frames; ++i)
			{
				mix[i * 2] += m_return[e][i];
				mix[i * 2 + 1] += m_return[e][i];
			}
		}
	}

	/// @brief 直前の process() で指定したエフェクトから返されたモノラル音声
	[[nodiscard]] const std::vector<float>& returns(std::size_t effect) const noexcept
	{
		return m_return[effect];
	}

	[[nodiscard]] static const char* effectName(std::size_t effect) noexcept
	{
		return effect == REVERB ? "reverb" : effect == DELAY ? "delay" : "chorus";
	}

private:
	std::array<bool, EFFECT_COUNT> m_used{};
	std::optional<Reverb::Stream> m_reverb;
	std::optional<Delay::Stream> m_delay;
	std::optional<Chorus::Stream> m_chorus;
	std::array<std::vector<float>, EFFECT_COUNT> m_input;
	std::array<std::vector<float>, EFFECT_COUNT> m_return;
};

} // namespace mitiru_mml
