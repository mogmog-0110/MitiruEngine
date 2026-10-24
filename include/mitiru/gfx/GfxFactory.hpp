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

#ifdef MITIRU_HAS_DAWN
#include <mitiru/core/Env.hpp>
#include <mitiru/gfx/dawn/DawnDevice.hpp>
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

#ifdef MITIRU_HAS_DAWN
/// @brief 下の OS の API は MITIRU_DAWN_API (d3d11 / d3d12 / vulkan)。既定は D3D11 (D3D12 を作れない PC を拾うのが目的)
[[nodiscard]] inline DawnApi dawnApiFromEnv()
{
	DawnApi api = DawnApi::D3D11;
	const std::string name = env::value("MITIRU_DAWN_API");
	if (!name.empty() && !parseDawnApi(name, api))
	{
		debug::warnOnce("gfx.dawn.api", "MITIRU_DAWN_API は d3d11 / d3d12 / vulkan のどれかを指定してください (" + name + " は使えません)");
	}
	return api;
}
#endif

#if defined(MITIRU_HAS_DAWN) && defined(_WIN32)
/// @brief 作っている間だけ Dx12Device に WARP を選ばせない
struct HardwareOnlyScope
{
	HardwareOnlyScope() noexcept { compat::hardwareOnlySlot() = true; }
	~HardwareOnlyScope() { compat::hardwareOnlySlot() = false; }
	HardwareOnlyScope(const HardwareOnlyScope&) = delete;
	HardwareOnlyScope& operator=(const HardwareOnlyScope&) = delete;
};

/// @brief 自動の順の 2 番目。D3D12 のハードウェアを作れなかった PC を、Dawn の D3D11 (ハードウェアのアダプタだけ) で拾う
/// @details ソフトウェア (WARP) は 3 番目なので、ここでは選ばない。MITIRU_AUTO_DAWN=0 で飛ばせる。
///          makeDawn が nullptr を返す (窓が無いなど) か投げたら nullptr を返し、呼び手が WARP へ進む。
///          窓のある起動と窓の無い起動が同じ順と同じ知らせの文になるよう、Dawn の作り方だけを呼び手から受ける。
template <class MakeDawn>
[[nodiscard]] inline std::unique_ptr<IDevice> tryDawnD3D11AfterDx12Failure(MakeDawn&& makeDawn, const std::string& why)
{
	if (env::value("MITIRU_AUTO_DAWN") == "0") { return nullptr; }
	try
	{
		std::unique_ptr<IDevice> device = makeDawn();
		if (!device) { return nullptr; }
		debug::warnOnce("gfx.dawn.autoFallback", "D3D12 のハードウェアを使えないので、Dawn の D3D11 で描きます");
		compat::appendReport(std::string("\n結果の訂正: D3D12 のハードウェアを使えず、Dawn の D3D11 に切り替えた。\n") + why + "\n");
		return device;
	}
	catch (const std::exception& e)
	{
		debug::warnOnce("gfx.dawn.autoFail", std::string("Dawn の D3D11 も作れなかったので、WARP を試します (") + e.what() + ")");
		compat::appendReport(std::string("\nDawn の D3D11 も作れなかった。\n") + e.what() + "\n");
		return nullptr;
	}
}

/// @brief D3D12 をハードウェアだけで作る。作れなければ Dawn の D3D11 を試し、どちらも駄目なら nullptr (WARP は呼び手が試す)
template <class MakeDx12, class MakeDawn>
[[nodiscard]] inline std::unique_ptr<IDevice> createDx12HardwareThenDawn(MakeDx12&& makeDx12, MakeDawn&& makeDawn)
{
	const HardwareOnlyScope hardwareOnly;
	try
	{
		return makeDx12();
	}
	catch (const std::exception& e)
	{
		return tryDawnD3D11AfterDx12Failure(makeDawn, e.what());
	}
}

/// @brief ハードウェアは調べ済みなので、WARP を直接選んで D3D12 を作る。失敗は呼び手へ投げる
template <class MakeDx12>
[[nodiscard]] inline std::unique_ptr<IDevice> createDx12Warp(MakeDx12&& makeDx12)
{
	compat::retryWithWarpSlot() = true;
	try
	{
		return makeDx12();
	}
	catch (...)
	{
		compat::retryWithWarpSlot() = false;
		throw;
	}
}

/// @brief 窓のある起動の自動の順 (DX12 のハードウェア → Dawn の D3D11 → WARP)
[[nodiscard]] inline std::unique_ptr<IDevice> createDx12AutoOrder(Win32Window* window)
{
	const auto makeDx12 = [&]() -> std::unique_ptr<IDevice> { return std::make_unique<Dx12Device>(window); };
	const auto makeDawn = [&]() -> std::unique_ptr<IDevice>
	{
		if (window->nativeHandle() == 0) { return nullptr; }
		return std::make_unique<DawnDevice>(window->nativeHandle(), window->width(), window->height(), DawnApi::D3D11, true);
	};
	if (auto device = createDx12HardwareThenDawn(makeDx12, makeDawn)) { return device; }
	return createDx12Warp(makeDx12);
}

/// @brief 窓の無い起動の自動の順。窓のある起動と同じ順と同じ知らせで、Dawn だけ窓の無い作り方にする
[[nodiscard]] inline std::unique_ptr<IDevice> createWindowlessDx12AutoOrder(int width, int height)
{
	const auto makeDx12 = [&]() -> std::unique_ptr<IDevice> { return std::make_unique<Dx12Device>(width, height); };
	const auto makeDawn = [&]() -> std::unique_ptr<IDevice> { return std::make_unique<DawnDevice>(width, height, DawnApi::D3D11); };
	if (auto device = createDx12HardwareThenDawn(makeDx12, makeDawn)) { return device; }
	return createDx12Warp(makeDx12);
}
#endif

#ifdef _WIN32
/// @brief 最初の Dx12Device が WARP を選んでよいか。Dawn を組み込んだ Auto は、ハードウェアだけで試し、WARP は最後にする。
///        host が D3D12 のデバイス作りを早く始める (Dx12EarlyDevice.hpp) とき、後で受け取る側と同じ条件で始めるために使う
[[nodiscard]] inline constexpr bool firstDx12AllowsWarp([[maybe_unused]] Backend backend) noexcept
{
#ifdef MITIRU_HAS_DAWN
	return backend != Backend::Auto;
#else
	return true;
#endif
}

/// @brief DX12 のデバイスを作る。選んだ GPU の準備 (キュー、スワップチェーンなど) が済まなかったときは、WARP で一度だけやり直す
/// @details DX11 は 3D の世界を描けない (ADR 0023)。3D のゲームは DX11 へ落ちると終了するので、遅くても描ける WARP を先に試す。
///          WARP を作りに行って失敗したとき、MITIRU_D3D12_WARP を自分で指定したときは、やり直さずそのまま投げる。
[[nodiscard]] inline std::unique_ptr<IDevice> createDx12WithWarpRetry(Win32Window* window)
{
	try
	{
		return std::make_unique<Dx12Device>(window);
	}
	catch (const std::exception& e)
	{
		if (compat::warpUsedSlot() || env::flag("MITIRU_D3D12_WARP")) { throw; }
		debug::warnOnce("gfx.dx12.warpRetry",
			std::string("選んだ GPU の準備に失敗したので、WARP (ソフトウェア描画) でやり直します (") + e.what() + ")");
		compat::appendReport(std::string("\n結果の訂正: 選んだ GPU の準備に失敗したので、WARP でやり直す。\n") + e.what() + "\n");
		compat::retryWithWarpSlot() = true;
		try
		{
			return std::make_unique<Dx12Device>(window);
		}
		catch (...)
		{
			compat::retryWithWarpSlot() = false;
			throw;
		}
	}
}
#endif

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
#ifdef MITIRU_HAS_DAWN
				// D3D12 のハードウェア → Dawn の D3D11 → WARP の順。D3D12 を作れる PC は、今までどおり最初の 1 回で終わる
				return createDx12AutoOrder(win32Window);
#else
				return createDx12WithWarpRetry(win32Window);
#endif
			}
			catch (const std::exception& e)
			{
				debug::warnOnce("gfx.dx12.fallback",
					std::string("DX12 のデバイス作成に失敗したので、代わりに DX11 で描きます (") + e.what() + ")");
				compat::dx11FallbackSlot() = true;
				compat::appendReport(std::string("\n結果の訂正: DX12 で始められず、DX11 に切り替えた (3D の絵は出ない)。\n") + e.what() + "\n");
				return std::make_unique<Dx11Device>(win32Window);
			}
			catch (...)
			{
				debug::warnOnce("gfx.dx12.fallback",
					"DX12 のデバイス作成に失敗したので、代わりに DX11 で描きます");
				compat::dx11FallbackSlot() = true;
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
		debug::verboseOnce("gfx.dx11.mismatch",
			"Backend::Dx11 には Win32Window が要るので、代わりに描画方式を自動で選びます。"
			"Win32Window を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<Dx11Device>(win32Window);
	}
#else
		debug::verboseOnce("gfx.dx11.platform",
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
		return createDx12WithWarpRetry(win32Window);
	}
#else
		return std::make_unique<NullDevice>();
#endif

	case Backend::Vulkan:
#ifdef MITIRU_HAS_VULKAN
	{
		if (!window)
		{
		debug::verboseOnce("gfx.vulkan.mismatch",
			"Backend::Vulkan には窓が要るので、代わりに描画方式を自動で選びます。"
			"GlfwWindow を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
#ifdef MITIRU_HAS_GLFW
		auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
		if (!glfwWindow)
		{
		debug::verboseOnce("gfx.vulkan.mismatch",
			"Backend::Vulkan には GlfwWindow が要るので、代わりに描画方式を自動で選びます。"
			"GlfwWindow を渡すか、Backend::Auto を指定してください。");
			return createDevice(Backend::Auto, window);
		}
		return std::make_unique<VulkanDevice>(glfwWindow);
#else
		debug::verboseOnce("gfx.vulkan.platform",
			"このビルドには GLFW が入っていないため Backend::Vulkan を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif
	}
#else
		debug::verboseOnce("gfx.vulkan.platform",
			"このビルドには Vulkan が入っていないため Backend::Vulkan を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGL:
#ifdef __EMSCRIPTEN__
		return std::make_unique<WebGLDevice>();
#else
		debug::verboseOnce("gfx.webgl.platform",
			"Backend::WebGL は Emscripten でビルドしたときだけ使えるので、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::WebGPU:
#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
		return std::make_unique<WebGPUDevice>();
#else
		debug::verboseOnce("gfx.webgpu.platform",
			"Backend::WebGPU は Emscripten で MITIRU_ENABLE_WEBGPU=ON にしてビルドしたときだけ使えるので、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif

	case Backend::Dawn:
#ifdef MITIRU_HAS_DAWN
	{
		if (window == nullptr || window->nativeHandle() == 0)
		{
			debug::verboseOnce("gfx.dawn.nowindow", "Backend::Dawn には窓が要るので、代わりに描画方式を自動で選びます。");
			return createDevice(Backend::Auto, window);
		}
		try
		{
			return std::make_unique<DawnDevice>(window->nativeHandle(), window->width(), window->height(), dawnApiFromEnv(), true);
		}
		catch (const std::exception& e)
		{
			debug::warnOnce("gfx.dawn.fail",
				std::string("Dawn のデバイスを作れなかったので、代わりに描画方式を自動で選びます (") + e.what() + ")");
			return createDevice(Backend::Auto, window);
		}
	}
#else
		debug::verboseOnce("gfx.dawn.platform",
			"このビルドには Dawn が入っていないため Backend::Dawn を使えず、代わりに描画方式を自動で選びます。"
			"MITIRU_WITH_DAWN=ON で組み直すか、Backend::Auto を指定してください。");
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
		debug::verboseOnce("gfx.opengl.mismatch",
			"Backend::OpenGL には GlfwWindow か Sdl2Window が要るので、代わりに描画方式を自動で選びます。"
			"どちらかの窓を渡すか、Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
	}
#else
		debug::verboseOnce("gfx.opengl.platform",
			"このビルドには OpenGL が入っていないため Backend::OpenGL を使えず、代わりに描画方式を自動で選びます。"
			"Backend::Auto を指定してください。");
		return createDevice(Backend::Auto, window);
#endif
	}

	throw std::runtime_error("Unknown graphics backend");
}

/// @brief window を持たず、3D の描画結果を readPixels() で読み戻せるデバイスを生成する
/// @details createDevice の headless 分岐とは別の経路。Auto と Dx11 は単一のオフスクリーン RT を持つ Dx11Device、Dx12 は同型の Dx12Device を使い、トリプルバッファリングを使わず、beginFrame と endFrame のたびに GPU の完了を待つ。Dawn の入ったビルドの Dx12 は、窓のある起動の Auto と同じ順 (D3D12 のハードウェア → Dawn の D3D11 → WARP) と同じ知らせで選ぶ。対応しないバックエンドでは NullDevice を返し、gfx.headless3d.unsupported を 1 回だけ警告する。
/// @param backend 要求するバックエンド
/// @param width オフスクリーン RT の論理幅
/// @param height オフスクリーン RT の論理高さ
[[nodiscard]] inline std::unique_ptr<IDevice> createWindowlessDevice3D(
	[[maybe_unused]] Backend backend, [[maybe_unused]] int width, [[maybe_unused]] int height)
{
#ifdef MITIRU_HAS_DAWN
	if (backend == Backend::Dawn)
	{
		try
		{
			return std::make_unique<DawnDevice>(width, height, dawnApiFromEnv());
		}
		catch (const std::exception& e)
		{
			debug::warnOnce("gfx.headless3d.dawn.fail", std::string("Dawn のデバイスを作れなかったので、絵は出ません (") + e.what() + ")");
			return std::make_unique<NullDevice>();
		}
	}
#endif
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
				std::string("窓なしの 3D 描画で DX11 のデバイス作成に失敗したので、GPU ドライバーを更新してください (") + e.what() + ")");
			return std::make_unique<NullDevice>();
		}
	}
	if (backend == Backend::Dx12)
	{
		try
		{
#ifdef MITIRU_HAS_DAWN
			return createWindowlessDx12AutoOrder(width, height);
#else
			return std::make_unique<Dx12Device>(width, height);
#endif
		}
		catch (const std::exception& e)
		{
			debug::warnOnce("gfx.headless3d.dx12.fail",
				std::string("窓なしの 3D 描画で DX12 のデバイス作成に失敗したので、GPU ドライバーを更新してください (") + e.what() + ")");
			return std::make_unique<NullDevice>();
		}
	}
	debug::verboseOnce("gfx.headless3d.unsupported",
		"窓なしの 3D 描画は Backend::Auto / Dx11 / Dx12 でしか使えないため、絵は出ません。"
		"Backend::Auto を指定してください。");
	return std::make_unique<NullDevice>();
#else
	debug::verboseOnce("gfx.headless3d.platform",
		"窓なしの 3D 描画は Windows でしか使えないため、絵は出ません。Windows で実行してください。");
	return std::make_unique<NullDevice>();
#endif
}

} // namespace mitiru::gfx
