#pragma once

/// @file BusDucker.hpp
/// @brief あるバスの再生中や大きな音の直後に、別のバスの音量を下げる
/// @details step(dt) ごとに包絡を進める。包絡は attackSec で 0 から 1 まで上がり、きっかけがなくなってから holdSec 待ち、releaseSec で下がる。
///          音量は depthDb × 包絡の dB 値で下げる。同じバスへの規則が重なった場合は、最も深く下げる規則だけを使う。シミュレーションのステップで進むため、同じ入力の並びから同じ音量の列が得られる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::audio::mix
{

/// @brief バスの番号。ModuleApi.hpp の kSoundBus* と同じ並びで、SoundIntentRouter.hpp で一致を確かめる
enum class MixBus : std::uint8_t
{
	Master = 0, Music = 1, Sfx = 2, Voice = 3, Ui = 4, Ambient = 5, User6 = 6, User7 = 7,
};
constexpr std::size_t kMixBusCount = 8;

struct DuckRule
{
	MixBus sidechain = MixBus::Voice;  ///< これが鳴ると
	MixBus target = MixBus::Music;     ///< これを下げる
	float  depthDb = -6.0f;            ///< 下げる量 (負の dB)
	float  attackSec = 0.05f;
	float  holdSec = 0.0f;             ///< きっかけが無くなってから戻し始めるまで
	float  releaseSec = 0.4f;
	float  threshold = 0.0f;           ///< trigger の音量がこれ以上の時だけ下げる (0 = いつでも)
};

class BusDucker
{
public:
	void setRules(std::vector<DuckRule> rules)
	{
		m_rules = std::move(rules);
		m_state.assign(m_rules.size(), State{});
	}

	[[nodiscard]] const std::vector<DuckRule>& rules() const noexcept { return m_rules; }

	/// @brief bus で volume の音が 1 回鳴ったことを記録し、次の step で下げ始める
	void trigger(MixBus bus, float volume) noexcept
	{
		for (std::size_t i = 0; i < m_rules.size(); ++i)
		{
			if (m_rules[i].sidechain == bus && volume >= m_rules[i].threshold) { m_state[i].triggered = true; }
		}
	}

	/// @brief bus が鳴り続けているかを設定し、次に変更するまで保つ
	void setActive(MixBus bus, bool active) noexcept { m_active[static_cast<std::size_t>(bus)] = active; }

	void step(float dt) noexcept
	{
		for (std::size_t i = 0; i < m_rules.size(); ++i)
		{
			const DuckRule& r = m_rules[i];
			State& s = m_state[i];
			if (m_active[static_cast<std::size_t>(r.sidechain)] || s.triggered) { s.hold = r.holdSec; s.driving = true; }
			else if (s.hold > 0.0f) { s.hold = std::max(0.0f, s.hold - dt); s.driving = true; }
			else { s.driving = false; }
			s.triggered = false;
			if (s.driving) { s.envelope = (r.attackSec > 0.0f) ? std::min(1.0f, s.envelope + dt / r.attackSec) : 1.0f; }
			else { s.envelope = (r.releaseSec > 0.0f) ? std::max(0.0f, s.envelope - dt / r.releaseSec) : 0.0f; }
		}
	}

	/// @brief bus に掛ける線形音量。1 は下げない
	[[nodiscard]] float gain(MixBus bus) const noexcept
	{
		float db = 0.0f;
		for (std::size_t i = 0; i < m_rules.size(); ++i)
		{
			if (m_rules[i].target == bus) { db = std::min(db, m_rules[i].depthDb * m_state[i].envelope); }
		}
		return std::pow(10.0f, db / 20.0f);
	}

private:
	struct State
	{
		float envelope = 0.0f;
		float hold = 0.0f;
		bool  triggered = false;
		bool  driving = false;
	};

	std::vector<DuckRule>             m_rules;
	std::vector<State>                m_state;
	std::array<bool, kMixBusCount>    m_active{};
};

}  // namespace mitiru::audio::mix
