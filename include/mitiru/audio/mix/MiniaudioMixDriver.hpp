#pragma once

/// @file MiniaudioMixDriver.hpp
/// @brief mix.json のダッキングと場所ごとの残響を、毎ステップ MiniaudioEngine のバスへ反映する
/// @details BusDucker が決めた BGM と効果音バスの音量、ReverbZoneTracker が決めた残響への送りと響きを書き込む。MiniaudioMusicPlayer の音量には、
///          host が step() の返り値を掛ける。

#include <array>
#include <string_view>

#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/mix/BusDucker.hpp>
#include <mitiru/audio/mix/MixConfigJson.hpp>
#include <mitiru/audio/mix/ReverbZones.hpp>

namespace mitiru::audio::mix
{

class MiniaudioMixDriver
{
public:
	MiniaudioMixDriver() { configure(MixConfig{}); }

	void configure(MixConfig config)
	{
		m_ducker.setRules(std::move(config.ducking));
		m_reverb.setSettings(std::move(config.reverb));
	}

	/// @brief bus で音量 volume の音を鳴らしたことを伝える (大きな効果音で BGM を下げる規則のきっかけ)
	void onSoundPlayed(MixBus bus, float volume) noexcept { m_ducker.trigger(bus, volume); }

	void setListener(const std::array<float, 3>& position) noexcept { m_reverb.setListener(position); }

	/// @brief 残響ゾーンを名前で決める。空なら聞き手の位置から決め、名前がなければ false を返す
	bool forceReverbZone(std::string_view id) noexcept
	{
		if (id.empty()) { m_reverb.forceZone(ReverbZoneTracker::kNoOverride); return true; }
		const auto& zones = m_reverb.settings().zones;
		for (std::size_t i = 0; i < zones.size(); ++i)
		{
			if (zones[i].id == id) { m_reverb.forceZone(static_cast<int>(i)); return true; }
		}
		return false;
	}

	/// @brief dt 秒進めてバスへ書き、BGM に掛けるダッキング音量を返す
	float step(MiniaudioEngine& engine, float dt)
	{
		m_ducker.setActive(MixBus::Voice, engine.voicePlaying());
		m_ducker.step(dt);
		const float music = m_ducker.gain(MixBus::Music);
		engine.setMusicDuck(music);
		engine.sfxBus().setGain(m_ducker.gain(MixBus::Sfx));
		m_reverb.step(dt);
		engine.sfxBus().setReverb(m_reverb.roomSize(), m_reverb.damping());
		engine.sfxBus().setSend(m_reverb.send());
		return music;
	}

	[[nodiscard]] const BusDucker& ducker() const noexcept { return m_ducker; }
	[[nodiscard]] const ReverbZoneTracker& reverb() const noexcept { return m_reverb; }

private:
	BusDucker         m_ducker;
	ReverbZoneTracker m_reverb;
};

}  // namespace mitiru::audio::mix
