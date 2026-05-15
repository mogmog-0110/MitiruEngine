#pragma once

/// @file EffectsAll.hpp
/// @brief 全エフェクトのインクルード集約ヘッダー

#include <mitiru/effects/Bloom.hpp>
#include <mitiru/effects/CameraEffects.hpp>
#include <mitiru/effects/ColorFlash.hpp>
#include <mitiru/effects/Dissolve.hpp>
#include <mitiru/effects/Fade.hpp>
#include <mitiru/effects/GpuParticleSystem.hpp>
#include <mitiru/effects/ParticleEmitter.hpp>
#include <mitiru/effects/ScreenShake.hpp>
#include <mitiru/effects/Trail.hpp>

#ifdef _WIN32
#include <mitiru/effects/GpuParticleDx11.hpp>
#include <mitiru/effects/GpuParticleDx12.hpp>
#endif
