#pragma once

/// @file GfxFactory.hpp
/// @brief グラフィックスデバイスファクトリ
/// @details Backend列挙に応じたIDevice実装を生成するファクトリ関数を提供する。
///          Windows環境ではDX12が本命、DX11は明示fallback (ADR 0023)。

#include <memory>
#include <stdexcept>
#include <string>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>
#include <mitiru/platform/IWindow.hpp>

#ifdef _WIN32
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

#ifdef MITIRU_HAS_OPENGL
#include <mitiru/gfx/opengl/GlDevice.hpp>
#ifdef MITIRU_HAS_SDL2
#include <mitiru/platform/sdl2/Sdl2Window.hpp>
#endif
#ifdef MITIRU_HAS_GLFW
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif
#endif

#ifdef MITIRU_HAS_VULKAN
#include <mitiru/gfx/vulkan/VulkanDevice.hpp>
#ifdef MITIRU_HAS_GLFW
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif
#endif

#ifdef __EMSCRIPTEN__
#include <mitiru/gfx/webgl/WebGLDevice.hpp>
#ifdef MITIRU_HAS_WEBGPU
#include <mitiru/gfx/webgpu/WebGPUDevice.hpp>
#endif
#endif

namespace mitiru::gfx
{

/// @brief バックエンド種別に応じたGPUデバイスを生成する
/// @param backend 使用するGPUバックエンド
/// @param window ウィンドウへのポインタ（DX11生成時に必要、nullptrならNull使用）
/// @return 生成されたデバイスのユニークポインタ
/// @throw std::runtime_error 未実装のバックエンドが指定された場合
///
/// @code
/// auto device = mitiru::gfx::createDevice(mitiru::gfx::Backend::Null, nullptr);
/// device->beginFrame();
/// device->endFrame();
/// @endcode
[[nodiscard]] inline std::unique_ptr<IDevice> createDevice(
	Backend backend, [[maybe_unused]] IWindow* window = nullptr)
{
	switch (backend)
	{
	case Backend::Null:
		return std::make_unique<NullDevice>();

	case Backend::Auto:
#ifdef _WIN32
	{
		/// Windows環境ではDX12を優先し、失敗時にDX11へ明示フォールバックする (ADR 0023)。
		/// 無言 fallback 禁止 — 失敗理由と失われる機能を stderr 1 行で通知する。
		/// 明示 Backend::Dx11 指定 (下の case) は fallback ではないため通知しない。
		auto* win32Window = dynamic_cast<Win32Window*>(window);
		if (win32Window)
		{
			try
			{
				return std::make_unique<Dx12Device>(win32Window);
			}
			catch (const std::exception& e)
			{
				debug::warnOnce("gfx.dx12.fallback",
					std::string("gfx: DX12 生成失敗 (") + e.what()
						+ ") — DX11 へ fallback。WBOIT/HDR/MSAA/FXAA/影は無効 (ADR 0023)");
				return std::make_unique<Dx11Device>(win32Window);
			}
			catch (...)
			{
				debug::warnOnce("gfx.dx12.fallback",
					"gfx: DX12 生成失敗 (unknown) — DX11 へ fallback。"
					"WBOIT/HDR/MSAA/FXAA/影は無効 (ADR 0023)");
				return std::make_unique<Dx11Device>(win32Window);
			}
		}
		return std::make_unique<NullDevice>();
	}
#else
#ifdef __EMSCRIPTEN__
#ifdef MITIRU_HAS_WEBGPU
		/// Emscripten環境でWebGPUが利用可能な場合はWebGPUを優先する
		return std::make_unique<WebGPUDevice>();
#else
		/// Emscripten環境ではWebGLにフォールバックする
		return std::make_unique<WebGLDevice>();
#endif
#else
	{
#if defined(MITIRU_HAS_OPENGL) && defined(MITIRU_HAS_GLFW)
		/// OpenGL+GLFWを最優先 (メインプラットフォーム)
		{
			auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
			if (glfwWindow && glfwWindow->graphicsMode() == GlfwGraphicsMode::OpenGL)
			{
				return std::make_unique<GlDevice>(glfwWindow);
			}
		}
#endif
#if defined(MITIRU_HAS_VULKAN) && defined(MITIRU_HAS_GLFW)
		/// Vulkan+GLFWが利用可能な場合
		{
			auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
			if (glfwWindow)
			{
				return std::make_unique<VulkanDevice>(glfwWindow);
			}
		}
#endif
#if defined(MITIRU_HAS_OPENGL) && defined(MITIRU_HAS_SDL2)
		/// OpenGL+SDL2 フォールバック
		{
			auto* sdl2Window = dynamic_cast<mitiru::Sdl2Window*>(window);
			if (sdl2Window)
			{
				return std::make_unique<GlDevice>(sdl2Window);
			}
		}
#endif
		/// いずれも該当しない場合はNullにフォールバック
		return std::make_unique<NullDevice>();
	}
#endif
#endif

	case Backend::Dx11:
#ifdef _WIN32
	{
		auto* win32Window = dynamic_cast<Win32Window*>(window);
		if (!win32Window)
		{
			throw std::runtime_error(
				"Dx11 backend requires a Win32Window");
		}
		return std::make_unique<Dx11Device>(win32Window);
	}
#else
		throw std::runtime_error(
			"Dx11 backend is only available on Windows");
#endif

	case Backend::Dx12:
#ifdef _WIN32
	{
		auto* win32Window = dynamic_cast<Win32Window*>(window);
		if (!win32Window)
		{
			/// Win32ウィンドウなしの場合はNullにフォールバック
			return std::make_unique<NullDevice>();
		}
		return std::make_unique<Dx12Device>(win32Window);
	}
#else
		/// 非Windows環境ではNullDeviceにフォールバック
		return std::make_unique<NullDevice>();
#endif

	case Backend::Vulkan:
#ifdef MITIRU_HAS_VULKAN
	{
		if (!window)
		{
			throw std::runtime_error(
				"Vulkan backend requires a window for surface creation");
		}
#ifdef MITIRU_HAS_GLFW
		auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
		if (!glfwWindow)
		{
			throw std::runtime_error(
				"Vulkan backend requires a GlfwWindow");
		}
		return std::make_unique<VulkanDevice>(glfwWindow);
#else
		throw std::runtime_error(
			"Vulkan backend requires GLFW window support");
#endif
	}
#else
		throw std::runtime_error(
			"Vulkan backend requires MITIRU_HAS_VULKAN to be defined");
#endif

	case Backend::WebGL:
#ifdef __EMSCRIPTEN__
		/// Emscripten環境でWebGL2デバイスを生成する
		return std::make_unique<WebGLDevice>();
#else
		throw std::runtime_error(
			"WebGL backend is only available on Emscripten");
#endif

	case Backend::WebGPU:
#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
		/// Emscripten環境でWebGPUデバイスを生成する
		return std::make_unique<WebGPUDevice>();
#else
		throw std::runtime_error(
			"WebGPU backend requires Emscripten with MITIRU_HAS_WEBGPU");
#endif

	case Backend::OpenGL:
#ifdef MITIRU_HAS_OPENGL
	{
#if defined(MITIRU_HAS_GLFW)
		auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
		if (glfwWindow)
		{
			return std::make_unique<GlDevice>(glfwWindow);
		}
#endif
#if defined(MITIRU_HAS_SDL2)
		auto* sdl2Window = dynamic_cast<mitiru::Sdl2Window*>(window);
		if (sdl2Window)
		{
			return std::make_unique<GlDevice>(sdl2Window);
		}
#endif
		throw std::runtime_error(
			"OpenGL backend requires a GLFW or SDL2 window");
	}
#else
		throw std::runtime_error(
			"OpenGL backend requires MITIRU_HAS_OPENGL");
#endif
	}

	throw std::runtime_error("Unknown graphics backend");
}

} // namespace mitiru::gfx
