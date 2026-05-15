#pragma once

/// @file MitiruCore.hpp
/// @brief Core, Platform, Entry Point

// Entry Point
#include <mitiru/Main.hpp>

// Core
#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Engine.hpp>
#include <mitiru/core/FrameTimer.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/GameLoop.hpp>
#include <mitiru/core/MitiruApp.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/core/ServiceRegistry.hpp>

// Platform
#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/PlatformInfo.hpp>
#include <mitiru/platform/WindowFactory.hpp>
#include <mitiru/platform/headless/HeadlessPlatform.hpp>

#ifdef __EMSCRIPTEN__
#include <mitiru/platform/emscripten/EmscriptenPlatform.hpp>
#include <mitiru/platform/emscripten/EmscriptenWindow.hpp>
#endif

#ifdef MITIRU_HAS_GLFW
#include <mitiru/platform/glfw/GlfwInput.hpp>
#include <mitiru/platform/glfw/GlfwVulkanSurface.hpp>
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif

#ifdef MITIRU_HAS_SDL2
#include <mitiru/platform/sdl2/Sdl2Audio.hpp>
#include <mitiru/platform/sdl2/Sdl2Input.hpp>
#include <mitiru/platform/sdl2/Sdl2Window.hpp>
#endif

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Platform.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#endif
