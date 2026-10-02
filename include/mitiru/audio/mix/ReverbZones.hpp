#pragma once

/// @file ReverbZones.hpp
/// @brief 聞き手がいる箱で、効果音へ送る残響の量と響きを決める
/// @details 箱が重なるときは priority が大きい方、同じなら体積が小さい方を選ぶ。響きを変えるときは、送る量を 0 まで下げてから変更し、再び上げる。鳴っている残響の性質を途中で変えると、不自然に揺れるため。
///          fadeSec は送る量が 0 から 1 へ変わる速さ。

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace mitiru::audio::mix
{

struct ReverbPreset
{
	float roomSize = 0.5f;
	float damping = 0.5f;
	float send = 0.0f;   ///< 効果音を残響へ送る量 (0..1)
};

struct ReverbZone
{
	std::string          id;
	std::array<float, 3> min{};
	std::array<float, 3> max{};
	ReverbPreset         preset;
	int                  priority = 0;

	[[nodiscard]] bool contains(const std::array<float, 3>& p) const noexcept
	{
		for (int i = 0; i < 3; ++i) { if (p[i] < min[i] || p[i] > max[i]) { return false; } }
		return true;
	}
	[[nodiscard]] float volume() const noexcept { return (max[0] - min[0]) * (max[1] - min[1]) * (max[2] - min[2]); }
};

struct ReverbSettings
{
	std::vector<ReverbZone> zones;
	ReverbPreset            outside;      ///< どの箱にも入っていないとき
	float                   fadeSec = 0.5f;
};

class ReverbZoneTracker
{
public:
	static constexpr int kNoOverride = -2;
	static constexpr int kOutside = -1;

	void setSettings(ReverbSettings s)
	{
		m_settings = std::move(s);
		m_room = m_settings.outside.roomSize;
		m_damp = m_settings.outside.damping;
		m_send = 0.0f;
	}

	void setListener(const std::array<float, 3>& position) noexcept { m_listener = position; }

	/// @brief 箱を番号で決める。kOutside は箱の外、kNoOverride は聞き手の位置で決める。
	void forceZone(int zone) noexcept { m_forced = zone; }

	[[nodiscard]] int zoneAt(const std::array<float, 3>& p) const noexcept
	{
		int best = kOutside;
		for (int i = 0; i < static_cast<int>(m_settings.zones.size()); ++i)
		{
			const ReverbZone& z = m_settings.zones[static_cast<std::size_t>(i)];
			if (!z.contains(p)) { continue; }
			if (best < 0 || better(z, m_settings.zones[static_cast<std::size_t>(best)])) { best = i; }
		}
		return best;
	}

	void step(float dt) noexcept
	{
		m_zone = (m_forced != kNoOverride) ? m_forced : zoneAt(m_listener);
		const ReverbPreset& target = (m_zone >= 0 && m_zone < static_cast<int>(m_settings.zones.size()))
			? m_settings.zones[static_cast<std::size_t>(m_zone)].preset : m_settings.outside;
		const float rate = (m_settings.fadeSec > 0.0f) ? dt / m_settings.fadeSec : 1.0f;
		if (target.roomSize != m_room || target.damping != m_damp)
		{
			m_send = std::max(0.0f, m_send - 2.0f * rate);
			if (m_send > 0.0f) { return; }
			m_room = target.roomSize;
			m_damp = target.damping;
		}
		m_send = (m_send < target.send) ? std::min(target.send, m_send + rate) : std::max(target.send, m_send - rate);
	}

	[[nodiscard]] int zone() const noexcept { return m_zone; }
	[[nodiscard]] float send() const noexcept { return m_send; }
	[[nodiscard]] float roomSize() const noexcept { return m_room; }
	[[nodiscard]] float damping() const noexcept { return m_damp; }
	[[nodiscard]] const ReverbSettings& settings() const noexcept { return m_settings; }

private:
	static bool better(const ReverbZone& a, const ReverbZone& b) noexcept
	{
		if (a.priority != b.priority) { return a.priority > b.priority; }
		return a.volume() < b.volume();
	}

	ReverbSettings       m_settings;
	std::array<float, 3> m_listener{};
	int                  m_forced = kNoOverride;
	int                  m_zone = kOutside;
	float                m_room = 0.5f;
	float                m_damp = 0.5f;
	float                m_send = 0.0f;
};

}  // namespace mitiru::audio::mix
