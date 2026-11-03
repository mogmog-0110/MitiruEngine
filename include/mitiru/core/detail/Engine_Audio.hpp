// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── audio subsystem のクラス外定義 ──────────────────────────────

MITIRU_INLINE void mitiru::Engine::setAudioEngine(std::shared_ptr<audio::IAudioEngine> engine) noexcept
{
	m_audioEngine = std::move(engine);
	applyVolumes();
	installAudioOcclusionProbe();
}

// sounds.json の autoOcclusion (ADR 0113)。--collision の静的な世界へ、聞き手から声へレイを 1 本飛ばす。世界が無ければ遮らない。
// 世界は読むだけで、シミュレーションには触れない。
MITIRU_INLINE void mitiru::Engine::installAudioOcclusionProbe()
{
	m_soundIntentRouter.voices().setOcclusionProbe(
		[this](const audio::AudioVec3& listener, const audio::AudioVec3& source, float margin) -> float {
			if (!m_modulePhysics) { return 0.0f; }
			const float dx = source.x - listener.x;
			const float dy = source.y - listener.y;
			const float dz = source.z - listener.z;
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (!(dist > margin) || dist <= 1e-4f) { return 0.0f; }
			const physics3d::Ray3D ray{sgc::Vec3f{listener.x, listener.y, listener.z}, sgc::Vec3f{dx / dist, dy / dist, dz / dist}};
			return m_modulePhysics->raycast(ray, dist - margin, 0xFFFFFFFFu).has_value() ? 1.0f : 0.0f;
		});
}

MITIRU_INLINE void mitiru::Engine::setMasterVolume(float v) noexcept
{
	m_masterVolume = clampVol(v);
	applyVolumes();
	if (m_loopConfig)
	{
		const_cast<EngineConfig*>(m_loopConfig)->masterVolume = m_masterVolume;
	}
}

MITIRU_INLINE void mitiru::Engine::setBgmVolume(float v) noexcept
{
	m_bgmVolume = clampVol(v);
	applyVolumes();
	if (m_loopConfig)
	{
		const_cast<EngineConfig*>(m_loopConfig)->bgmVolume = m_bgmVolume;
	}
}

MITIRU_INLINE void mitiru::Engine::setSeVolume(float v) noexcept
{
	m_seVolume = clampVol(v);
	if (m_loopConfig)
	{
		const_cast<EngineConfig*>(m_loopConfig)->seVolume = m_seVolume;
	}
}

MITIRU_INLINE void mitiru::Engine::setVoiceVolume(float v) noexcept
{
	m_voiceVolume = clampVol(v);
	if (m_loopConfig)
	{
		const_cast<EngineConfig*>(m_loopConfig)->voiceVolume = m_voiceVolume;
	}
}

MITIRU_INLINE void mitiru::Engine::setUserBusVolumes(const std::array<float, module::kSoundBusCount>& volumes)
{
	m_soundIntentRouter.setUserBusVolumes(m_audioEngine.get(), volumes);
}

MITIRU_INLINE void mitiru::Engine::applyVolumes() noexcept
{
	if (m_audioEngine)
	{
		m_audioEngine->setVolume(m_masterVolume * m_bgmVolume);
	}
}
