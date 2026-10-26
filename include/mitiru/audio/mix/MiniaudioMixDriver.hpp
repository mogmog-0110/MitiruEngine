#pragma once

/// @file MiniaudioMixDriver.hpp
/// @brief mix.json のダッキングと場所ごとの残響を、毎ステップ MiniaudioEngine のバスへ反映する
/// @details BusDucker が決めた BGM と効果音バスの音量、ReverbZoneTracker が決めた残響への送りと響きを書き込む。MiniaudioMusicPlayer の音量には、
///          host が step() の返り値を掛ける。EQ の場所は残響の箱を共有し、聞き手がいる箱の id に合う EQ の設定へ切り替える。

#include <array>
#include <string_view>
#include <vector>

#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/mix/BusDspConfig.hpp>
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
		m_dsp = std::move(config.dsp);
		m_eqOfReverbZone.clear();
		for (const ReverbZone& z : config.reverb.zones)
		{
			int found = -1;
			for (std::size_t i = 0; i < m_dsp.eqZones.size(); ++i) { if (m_dsp.eqZones[i].id == z.id) { found = static_cast<int>(i); } }
			m_eqOfReverbZone.push_back(found);
		}
		m_reverb.setSettings(std::move(config.reverb));
	}

	/// @brief configure で読んだ eq / compressor / limiter を engine のバスに差す。音を鳴らす前に 1 回呼ぶ
	void attach(MiniaudioEngine& engine) { engine.configureDsp(m_dsp); }

	[[nodiscard]] const BusDspConfig& dsp() const noexcept { return m_dsp; }

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
		engine.buses().setGain(MixBus::Sfx, m_ducker.gain(MixBus::Sfx));
		engine.buses().setGain(MixBus::Voice, m_ducker.gain(MixBus::Voice));
		m_reverb.step(dt);
		engine.buses().setReverb(m_reverb.roomSize(), m_reverb.damping());
		engine.buses().setReverbReturn(m_reverb.send());
		const int zone = m_reverb.zone();
		engine.setEqZone((zone >= 0 && static_cast<std::size_t>(zone) < m_eqOfReverbZone.size()) ? m_eqOfReverbZone[static_cast<std::size_t>(zone)] : -1);
		return music;
	}

	[[nodiscard]] const BusDucker& ducker() const noexcept { return m_ducker; }
	[[nodiscard]] const ReverbZoneTracker& reverb() const noexcept { return m_reverb; }

private:
	BusDucker         m_ducker;
	ReverbZoneTracker m_reverb;
	BusDspConfig      m_dsp;
	std::vector<int>  m_eqOfReverbZone;   ///< 残響の箱の番号 → m_dsp.eqZones の番号 (-1 は既定の EQ)
};

}  // namespace mitiru::audio::mix
