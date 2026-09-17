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
/// @details backend と window の組み合わせが使えない場合は、stderr に警告を 1 行出して Backend::Auto へフォールバックする。
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
				debug::warnOnceFix("gfx.dx12.fallback",
					std::string("gfx: DX12 生成失敗 (") + e.what() + ") のため DX11 へ fallback",
					"DX12 デバイス生成が例外を投げた (feature level 不足等)",
					"WBOIT/HDR/MSAA/FXAA/影/outline PSO は無効になる。GPU/ドライバが DX12 feature level を満たすか確認する");
				return std::make_unique<Dx11Device>(win32Window);
			}
			catch (...)
			{
				debug::warnOnceFix("gfx.dx12.fallback",
					"gfx: DX12 生成失敗 (unknown 例外) のため DX11 へ fallback",
					"DX12 デバイス生成中に原因不明の例外が発生した",
					"WBOIT/HDR/MSAA/FXAA/影/outline PSO は無効になる。GPU/ドライバが DX12 feature level を満たすか確認する");
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
		debug::warnOnceFix("gfx.dx11.mismatch",
			"gfx: Dx11 backend を要求されたが Win32Window が渡されていない",
			"window が Win32Window ではない",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<Dx11Device>(win32Window);
	}
#else
		debug::warnOnceFix("gfx.dx11.platform",
			"gfx: Dx11 backend を要求されたが非 Windows でビルドされている",
			"Dx11 backend は Windows 専用",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
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
		debug::warnOnceFix("gfx.vulkan.mismatch",
			"gfx: Vulkan backend を要求されたが window が渡されていない (surface 生成に必須)",
			"window 引数が nullptr",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
			return createDevice(Backend::Auto, window);
		}
#ifdef MITIRU_HAS_GLFW
		auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
		if (!glfwWindow)
		{
		debug::warnOnceFix("gfx.vulkan.mismatch",
			"gfx: Vulkan backend を要求されたが GlfwWindow が渡されていない",
			"window が GlfwWindow ではない",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<VulkanDevice>(glfwWindow);
#else
		debug::warnOnceFix("gfx.vulkan.platform",
			"gfx: Vulkan backend を要求されたが GLFW window support がない",
			"MITIRU_HAS_GLFW が定義されていない構成でビルドされた",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
		return createDevice(Backend::Auto, window);
#endif
	}
#else
		debug::warnOnceFix("gfx.vulkan.platform",
			"gfx: Vulkan backend を要求されたがビルドに含まれていない",
			"MITIRU_HAS_VULKAN が定義されていない構成でビルドされた",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGL:
#ifdef __EMSCRIPTEN__
		return std::make_unique<WebGLDevice>();
#else
		debug::warnOnceFix("gfx.webgl.platform",
			"gfx: WebGL backend を要求されたが Emscripten ビルドではない",
			"WebGL backend は Emscripten 専用",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGPU:
#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
		return std::make_unique<WebGPUDevice>();
#else
		debug::warnOnceFix("gfx.webgpu.platform",
			"gfx: WebGPU backend を要求されたがビルドに含まれていない",
			"Emscripten + MITIRU_HAS_WEBGPU の組み合わせでビルドされていない",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
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
		debug::warnOnceFix("gfx.opengl.mismatch",
			"gfx: OpenGL backend を要求されたが GLFW/SDL2 の window が渡されていない",
			"window が GlfwWindow でも Sdl2Window でもない",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
		return createDevice(Backend::Auto, window);
	}
#else
		debug::warnOnceFix("gfx.opengl.platform",
			"gfx: OpenGL backend を要求されたがビルドに含まれていない",
			"MITIRU_HAS_OPENGL が定義されていない構成でビルドされた",
			"Backend::Auto を使うか、要件を満たす backend/window の組み合わせに変える");
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
			debug::warnOnceFix("gfx.headless3d.dx11.fail",
				std::string("gfx: windowless Dx11Device 生成失敗 (") + e.what() + ") のため NullDevice へ",
				"D3D11CreateDevice がハード/WARP 双方で失敗した",
				"GPU ドライバ/WARP (d3d10warp.dll) が有効な環境で実行する");
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
			debug::warnOnceFix("gfx.headless3d.dx12.fail",
				std::string("gfx: windowless Dx12Device 生成失敗 (") + e.what() + ") のため NullDevice へ",
				"D3D12CreateDevice がハード/WARP 双方で失敗した",
				"GPU ドライバ/WARP (d3d12warp.dll) が有効な環境で実行する");
			return std::make_unique<NullDevice>();
		}
	}
	debug::warnOnceFix("gfx.headless3d.unsupported",
		"gfx: windowless 3D は現状 Dx11/Dx12/Auto のみ対応。指定 backend は NullDevice で代替",
		"Vulkan/OpenGL 等の windowless 経路が未実装",
		"Backend::Auto か Backend::Dx11/Dx12 を指定する");
	return std::make_unique<NullDevice>();
#else
	debug::warnOnceFix("gfx.headless3d.platform",
		"gfx: windowless 3D は Windows (DX11) のみ対応。NullDevice で代替",
		"非 Windows ビルドでは windowless 3D 経路が未実装",
		"Windows 上で DX11 backend を使う");
	return std::make_unique<NullDevice>();
#endif
}

} // namespace mitiru::gfx
