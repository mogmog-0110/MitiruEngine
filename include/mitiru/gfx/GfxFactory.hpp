#pragma once

/// @file GfxFactory.hpp
/// @brief グラフィックスデバイスファクトリの宣言

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

/// @brief 指定したバックエンドの GPU デバイスを生成する
/// @param backend 使用する GPU バックエンド
/// @param window DX11 の生成に使うウィンドウ。nullptr の場合は Null を使う
/// @return 生成したデバイス
/// @details backend と window の組み合わせが使えない場合は、端末に 1 行知らせて Backend::Auto で作り直す。
/// @throw std::runtime_error 未知の Backend 値が渡された場合
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
		/// DX12 の失敗時は、理由と失われる機能を stderr に 1 行出して DX11 へフォールバックする。
		/// 明示された Backend::Dx11 では通知しない。
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
					std::string("DX12 のデバイス作成に失敗しました (") + e.what() + ")。"
					"代わりに DX11 で描きますが影・輪郭線・HDR・MSAA・FXAA・半透明の重なりの処理は使えないので、GPU ドライバーを更新し、GPU が DirectX 12 に対応しているか確かめてください。");
				return std::make_unique<Dx11Device>(win32Window);
			}
			catch (...)
			{
				debug::warnOnce("gfx.dx12.fallback",
					"DX12 のデバイス作成に失敗しました。"
					"代わりに DX11 で描きますが影・輪郭線・HDR・MSAA・FXAA・半透明の重なりの処理は使えないので、GPU ドライバーを更新し、GPU が DirectX 12 に対応しているか確かめてください。");
				return std::make_unique<Dx11Device>(win32Window);
			}
		}
		return std::make_unique<NullDevice>();
	}
#else
#ifdef __EMSCRIPTEN__
#ifdef MITIRU_HAS_WEBGPU
		return std::make_unique<WebGPUDevice>();
#else
		return std::make_unique<WebGLDevice>();
#endif
#else
	{
#if defined(MITIRU_HAS_OPENGL) && defined(MITIRU_HAS_GLFW)
		/// OpenGL と GLFW を最優先する。
		{
			auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
			if (glfwWindow && glfwWindow->graphicsMode() == GlfwGraphicsMode::OpenGL)
			{
				return std::make_unique<GlDevice>(glfwWindow);
			}
		}
#endif
#if defined(MITIRU_HAS_VULKAN) && defined(MITIRU_HAS_GLFW)
		{
			auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
			if (glfwWindow)
			{
				return std::make_unique<VulkanDevice>(glfwWindow);
			}
		}
#endif
#if defined(MITIRU_HAS_OPENGL) && defined(MITIRU_HAS_SDL2)
		{
			auto* sdl2Window = dynamic_cast<mitiru::Sdl2Window*>(window);
			if (sdl2Window)
			{
				return std::make_unique<GlDevice>(sdl2Window);
			}
		}
#endif
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
		debug::warnOnce("gfx.dx11.mismatch",
			"Backend::Dx11 には Win32Window が要るので、代わりに描画方式を自動で選びます。"
			"Win32Window を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<Dx11Device>(win32Window);
	}
#else
		debug::warnOnce("gfx.dx11.platform",
			"Backend::Dx11 は Windows でしか使えないので、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::Dx12:
#ifdef _WIN32
	{
		auto* win32Window = dynamic_cast<Win32Window*>(window);
		if (!win32Window)
		{
			return std::make_unique<NullDevice>();
		}
		return std::make_unique<Dx12Device>(win32Window);
	}
#else
		return std::make_unique<NullDevice>();
#endif

	case Backend::Vulkan:
#ifdef MITIRU_HAS_VULKAN
	{
		if (!window)
		{
		debug::warnOnce("gfx.vulkan.mismatch",
			"Backend::Vulkan には窓が要るので、代わりに描画方式を自動で選びます。"
			"GlfwWindow を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
#ifdef MITIRU_HAS_GLFW
		auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
		if (!glfwWindow)
		{
		debug::warnOnce("gfx.vulkan.mismatch",
			"Backend::Vulkan には GlfwWindow が要るので、代わりに描画方式を自動で選びます。"
			"GlfwWindow を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<VulkanDevice>(glfwWindow);
#else
		debug::warnOnce("gfx.vulkan.platform",
			"このビルドには GLFW が入っていないため Backend::Vulkan を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif
	}
#else
		debug::warnOnce("gfx.vulkan.platform",
			"このビルドには Vulkan が入っていないため Backend::Vulkan を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGL:
#ifdef __EMSCRIPTEN__
		return std::make_unique<WebGLDevice>();
#else
		debug::warnOnce("gfx.webgl.platform",
			"Backend::WebGL は Emscripten でビルドしたときだけ使えるので、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGPU:
#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
		return std::make_unique<WebGPUDevice>();
#else
		debug::warnOnce("gfx.webgpu.platform",
			"Backend::WebGPU は Emscripten で MITIRU_ENABLE_WEBGPU=ON にしてビルドしたときだけ使えるので、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
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
		debug::warnOnce("gfx.opengl.mismatch",
			"Backend::OpenGL には GlfwWindow か Sdl2Window が要るので、代わりに描画方式を自動で選びます。"
			"どちらかの窓を渡すか、Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
	}
#else
		debug::warnOnce("gfx.opengl.platform",
			"このビルドには OpenGL が入っていないため Backend::OpenGL を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif
	}

	throw std::runtime_error("Unknown graphics backend");
}

/// @brief window を持たず、3D の描画結果を readPixels() で読み戻せるデバイスを生成する
/// @details createDevice の headless 分岐とは別の経路。Auto と Dx11 は単一のオフスクリーン RT を持つ Dx11Device、Dx12 は同型の Dx12Device を使う。Dx12 はトリプルバッファリングを使わず、beginFrame と endFrame のたびに GPU の完了を待つ。対応しないバックエンドでは NullDevice を返し、gfx.headless3d.unsupported を 1 回だけ警告する。
/// @param backend 要求するバックエンド
/// @param width オフスクリーン RT の論理幅
/// @param height オフスクリーン RT の論理高さ
[[nodiscard]] inline std::unique_ptr<IDevice> createWindowlessDevice3D(
	[[maybe_unused]] Backend backend, [[maybe_unused]] int width, [[maybe_unused]] int height)
{
#ifdef _WIN32
	if (backend == Backend::Auto || backend == Backend::Dx11)
	{
		try
		{
			return std::make_unique<Dx11Device>(width, height);
		}
		catch (const std::exception& e)
		{
			debug::warnOnce("gfx.headless3d.dx11.fail",
				std::string("窓なしの 3D 描画で DX11 のデバイス作成に失敗しました (") + e.what() + ")。"
				"絵は出ないので、GPU ドライバーか WARP (d3d10warp.dll) が使える環境で実行してください。");
			return std::make_unique<NullDevice>();
		}
	}
	if (backend == Backend::Dx12)
	{
		try
		{
			return std::make_unique<Dx12Device>(width, height);
		}
		catch (const std::exception& e)
		{
			debug::warnOnce("gfx.headless3d.dx12.fail",
				std::string("窓なしの 3D 描画で DX12 のデバイス作成に失敗しました (") + e.what() + ")。"
				"絵は出ないので、GPU ドライバーか WARP (d3d12warp.dll) が使える環境で実行してください。");
			return std::make_unique<NullDevice>();
		}
	}
	debug::warnOnce("gfx.headless3d.unsupported",
		"窓なしの 3D 描画は Backend::Auto / Dx11 / Dx12 でしか使えないため、絵は出ません。"
		"Backend::Auto を指定してください。");
	return std::make_unique<NullDevice>();
#else
	debug::warnOnce("gfx.headless3d.platform",
		"窓なしの 3D 描画は Windows でしか使えないため、絵は出ません。Windows で実行してください。");
	return std::make_unique<NullDevice>();
#endif
}

} // namespace mitiru::gfx
