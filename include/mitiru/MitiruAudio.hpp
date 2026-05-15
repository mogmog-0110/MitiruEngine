#pragma once

/// @file MitiruAudio.hpp
/// @brief Audio module

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/AudioMixer.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/MitiruAudioPlayer.hpp>
#include <mitiru/audio/NullAudioEngine.hpp>
#include <mitiru/audio/SoftAudioEngine.hpp>
#include <mitiru/audio/WaveAudioEngine.hpp>

#ifdef _WIN32
#include <mitiru/audio/Win32AudioOutput.hpp>
#endif
