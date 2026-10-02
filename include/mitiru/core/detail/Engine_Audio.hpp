// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── audio subsystem のクラス外定義 ──────────────────────────────

MITIRU_INLINE void mitiru::Engine::setAudioEngine(std::shared_ptr<audio::IAudioEngine> engine) noexcept
{
	m_audioEngine = std::move(engine);
	applyVolumes();
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
