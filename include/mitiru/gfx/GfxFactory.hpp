#pragma once

/// @file GfxFactory.hpp
/// @brief グラフィックスデバイスファクトリ
/// @details Backend列挙に応じたIDevice実装を生成するファクトリ関数を提供する。
///          Windows環境ではDX11バックエンドをサポートする。

#include <memory>
#include <stdexcept>

#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>
#include <mitiru/platform/IWindow.hpp>

#ifdef _WIN32
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
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
		/// Windows環境ではDX11を自動選択する
		auto* win32Window = dynamic_cast<Win32Window*>(window);
		if (win32Window)
		{
			return std::make_unique<Dx11Device>(win32Window);
		}
		/// Win32ウィンドウでない場合はNullにフォールバック
		return std::make_unique<NullDevice>();
	}
#else
		/// 非Windows環境ではNullにフォールバック
		return std::make_unique<NullDevice>();
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
		throw std::runtime_error(
			"Dx12 backend is not yet implemented");

	case Backend::Vulkan:
#ifdef MITIRU_HAS_VULKAN
		/// Vulkan SDKが利用可能な場合にVulkanDeviceを生成する
		/// TODO: 実際のVulkan初期化を実装する
		return std::make_unique<NullDevice>();
#else
		throw std::runtime_error(
			"Vulkan backend requires MITIRU_HAS_VULKAN to be defined");
#endif

	case Backend::WebGL:
#ifdef __EMSCRIPTEN__
		/// Emscripten環境でWebGL2デバイスを生成する
		/// TODO: WebGLDeviceの実際の初期化を実装する
		return std::make_unique<NullDevice>();
#else
		throw std::runtime_error(
			"WebGL backend is only available on Emscripten");
#endif

	case Backend::OpenGL:
		throw std::runtime_error(
			"OpenGL backend is not yet implemented");
	}

	throw std::runtime_error("Unknown graphics backend");
}

} // namespace mitiru::gfx
