#pragma once

/// @file MitiruInput.hpp
/// @brief Input module

#include <mitiru/input/GamepadInput.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/input/InputMapper.hpp>
#include <mitiru/input/InputRecorder.hpp>
#include <mitiru/input/InputReplayer.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/KeyCode.hpp>

#ifdef _WIN32
#include <mitiru/input/Win32Input.hpp>
#endif
