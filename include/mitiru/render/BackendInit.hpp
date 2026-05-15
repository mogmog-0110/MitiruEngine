#pragma once

/// @file BackendInit.hpp
/// @brief 3D renderer / 2D pipeline backend dispatch helpers.
/// @details
/// Engine.hpp used to do `dynamic_cast<Dx11Device*>` / `dynamic_cast<Dx12Device*>`
/// to pick the right concrete factory for `Renderer3D` / `RenderPipeline2D`.
/// That violates the "no backend type leakage in engine-internal code"
/// guideline.
///
/// Rather than adding render-layer types to `IDevice` (which would create a
/// circular dependency: IDevice -> Renderer3D -> IDevice), this header sits
/// in the render layer and dispatches on the device-reported `Backend` enum.
/// The static_cast that follows is safe because the enum is canonical for the
/// concrete IDevice subclass.
///
/// Usage:
/// @code
/// auto pipeline = render::createPipeline2DFor(device, w, h);
/// auto renderer = render::createRenderer3DFor(device, cfg, w, h);
/// @endcode

#include <memory>
#include <optional>

#include <mitiru/core/Config.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/Renderer3D.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>

#ifdef _WIN32
#include <fstream>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/render/PostProcessIntegration.hpp>
#include <mitiru/render/Renderer3D_DX12.hpp>
#endif

namespace mitiru::render
{

/// @brief Result of `createPipeline2DFor` - pipeline plus optional
///        post-process manager that the caller is expected to wire into
///        the device.
struct Pipeline2DResult
{
	/// 2D render pipeline (`valid()` reports backend support).
	std::optional<RenderPipeline2D> pipeline;

	/// Win32 / DX11 specific post-process manager. nullptr on other backends.
	std::shared_ptr<PostProcessManager> postProcess;
};

/// @brief Construct the 2D pipeline appropriate for the device backend.
/// @details Replaces the engine-internal dynamic_cast chain. Dispatches on
///          `device->backend()` (the canonical concrete type tag) instead.
/// @param device GPU device
/// @param screenWidth Target screen width
/// @param screenHeight Target screen height
/// @return Pipeline2DResult with the constructed pipeline. `pipeline` is
///         `std::nullopt` for Null / unsupported backends.
[[nodiscard]] inline Pipeline2DResult createPipeline2DFor(
	gfx::IDevice* device, int screenWidth, int screenHeight)
{
	Pipeline2DResult result;
	if (!device)
	{
		return result;
	}

	const auto backend = device->backend();
#ifdef _WIN32
	if (backend == gfx::Backend::Dx11)
	{
		auto* dx11 = static_cast<gfx::Dx11Device*>(device);
		result.pipeline = RenderPipeline2D::createFromDx11(
			dx11,
			static_cast<float>(screenWidth),
			static_cast<float>(screenHeight));

		/// ポストプロセスマネージャーを初期化する
		result.postProcess = std::make_shared<PostProcessManager>();
		result.postProcess->init(
			dx11->getD3DDevice(), screenWidth, screenHeight);
		/// デフォルトは無効 (ユーザーが明示的にエフェクトを有効化する)
		result.postProcess->setEnabled(false);
		return result;
	}

	if (backend == gfx::Backend::Dx12)
	{
		auto* dx12 = static_cast<gfx::Dx12Device*>(device);
		/// 汎用 createFromDevice は PSO/root sig が bind されず silent no-op に
		/// なるため、MitiruCefTexture と同等スタイルの専用パスを使う。
		result.pipeline = RenderPipeline2D::createFromDx12(
			dx12,
			static_cast<float>(screenWidth),
			static_cast<float>(screenHeight));
		return result;
	}
#endif

	/// OpenGL/WebGL/その他のバックエンド: 汎用IDevice経由でパイプラインを構築する
	if (backend != gfx::Backend::Null)
	{
		result.pipeline = RenderPipeline2D::createFromDevice(
			device,
			static_cast<float>(screenWidth),
			static_cast<float>(screenHeight));
	}

	/// NullDevice 等の場合はパイプラインなし (ヘッドレス動作)
	return result;
}

/// @brief Construct the 3D renderer appropriate for the device backend.
/// @details Replaces the engine-internal dynamic_cast chain in
///          `Engine::create3DRenderer`. DX12 is preferred when available
///          (its toon outline PSO is the reference path); DX11 is the
///          Win32 fallback. Other backends return nullptr (3D not yet
///          implemented).
/// @param device GPU device
/// @param screenWidth Logical screen width  (DX11 fallback viewport)
/// @param screenHeight Logical screen height (DX11 fallback viewport)
/// @param windowWidth Physical window width  (DX12 viewport, DPI aware)
/// @param windowHeight Physical window height (DX12 viewport, DPI aware)
/// @return Renderer instance, or nullptr when the backend has no 3D path.
[[nodiscard]] inline std::unique_ptr<IRenderer3D> createRenderer3DFor(
	gfx::IDevice* device,
	int screenWidth, int screenHeight,
	int windowWidth, int windowHeight)
{
	if (!device)
	{
		return nullptr;
	}

	const auto backend = device->backend();
#ifdef _WIN32
	// DX12を優先的に使用 (アウトラインがPSOで正しく動く)
	if (backend == gfx::Backend::Dx12)
	{
		auto* dx12 = static_cast<gfx::Dx12Device*>(device);
		Renderer3D_DX12::Config cfg;
		// 実際のウィンドウ物理ピクセルサイズを使用する (DPIスケーリング対応)
		cfg.viewportWidth = static_cast<float>(windowWidth);
		cfg.viewportHeight = static_cast<float>(windowHeight);
		cfg.defaultAmbient = {0.5f, 0.5f, 0.5f, 1.0f};
		cfg.enableOutline = true;
		auto dx12Renderer = std::make_unique<Renderer3D_DX12>();
		try
		{
			dx12Renderer->initialize(dx12, cfg);
		}
		catch (const std::exception& e)
		{
			std::ofstream log("mitiru_engine_init_error.log",
				std::ios::out | std::ios::trunc);
			if (log)
			{
				log << "Renderer3D_DX12::initialize threw: "
				    << e.what() << std::endl;
				// D3D12 info queue から具体エラーを吸い出す
				Microsoft::WRL::ComPtr<ID3D12InfoQueue> iq;
				if (SUCCEEDED(dx12->nativeDevice()->QueryInterface(
					IID_PPV_ARGS(iq.GetAddressOf()))))
				{
					const UINT64 n = iq->GetNumStoredMessages();
					log << "InfoQueue messages: " << n << std::endl;
					for (UINT64 i = 0; i < n; ++i)
					{
						SIZE_T sz = 0;
						iq->GetMessage(i, nullptr, &sz);
						std::vector<char> buf(sz);
						auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
						if (SUCCEEDED(iq->GetMessage(i, msg, &sz)))
						{
							log << "[" << i << "] severity="
							    << static_cast<int>(msg->Severity)
							    << " id=" << static_cast<int>(msg->ID)
							    << " desc=" << msg->pDescription << std::endl;
						}
					}
				}
				else
				{
					log << "ID3D12InfoQueue unavailable (debug layer off?)"
					    << std::endl;
				}
			}
			throw;
		}
		return dx12Renderer;
	}

	// DX11フォールバック
	if (backend == gfx::Backend::Dx11)
	{
		auto* dx11 = static_cast<gfx::Dx11Device*>(device);
		Renderer3DConfig cfg;
		cfg.viewportWidth = static_cast<float>(screenWidth);
		cfg.viewportHeight = static_cast<float>(screenHeight);
		cfg.defaultAmbient = {0.5f, 0.5f, 0.5f, 1.0f};
		auto dx11Renderer = std::make_unique<Renderer3D>();
		dx11Renderer->initialize(dx11, cfg, ShaderMode3D::Toon);
		return dx11Renderer;
	}
#else
	(void)screenWidth;
	(void)screenHeight;
	(void)windowWidth;
	(void)windowHeight;
#endif

	// 非Win32 / Null / OpenGL: 3D renderer not yet implemented
	return nullptr;
}

} // namespace mitiru::render
