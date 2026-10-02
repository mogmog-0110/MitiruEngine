#pragma once

/// @file BackendInit.hpp
/// @brief 3D renderer / 2D pipeline の backend dispatch ヘルパー。
/// @details
/// Engine.hpp はかつて `Renderer3D` / `RenderPipeline2D` の適切な具象 factory を
/// 選ぶために `dynamic_cast<Dx11Device*>` / `dynamic_cast<Dx12Device*>` を
/// 行っていた。これは「engine 内部コードに backend 型を漏らさない」指針に反する。
///
/// `IDevice` に render 層の型を足す (IDevice -> Renderer3D -> IDevice の循環依存に
/// なる) のではなく、本ヘッダを render 層に置き、device が報告する `Backend` enum で
/// dispatch する。この enum は具象 IDevice 派生クラスと canonical に対応するので、
/// 続く static_cast は安全である。
///
/// 使い方は次のとおり。
/// @code
/// auto pipeline = render::createPipeline2DFor(device, w, h);
/// auto renderer = render::createRenderer3DFor(device, cfg, w, h);
/// @endcode

#include <memory>
#include <optional>

#include <mitiru/core/Config.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/NullRenderer3D.hpp>
#ifdef __EMSCRIPTEN__
#include <mitiru/render/Renderer3D_WebGL.hpp>
#endif
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

#ifndef _WIN32
// PostProcessManager は Win32 専用ヘッダ (PostProcessIntegration.hpp) にある。
// Pipeline2DResult は他 backend でもこの型の「空の shared_ptr」を持つので、
// 前方宣言だけあれば足りる (incomplete type の shared_ptr は合法)。
class PostProcessManager;
#endif

/// @brief `createPipeline2DFor` の結果。pipeline と、呼び出し側が device に
///        繋ぎ込むことが期待される optional な post-process manager。
struct Pipeline2DResult
{
	/// 2D render pipeline (`valid()` が backend 対応の有無を返す)。
	std::optional<RenderPipeline2D> pipeline;

	/// Win32 / DX11 専用の post-process manager。他 backend では nullptr。
	std::shared_ptr<PostProcessManager> postProcess;
};

/// @brief device の backend に適した 2D pipeline を構築する。
/// @details engine 内部の dynamic_cast チェーンを置き換える。代わりに
///          `device->backend()` (canonical な具象型タグ) で dispatch する。
/// @param device GPU device
/// @param screenWidth 対象 screen の幅
/// @param screenHeight 対象 screen の高さ
/// @return 構築した pipeline を含む Pipeline2DResult。Null / 非対応 backend では
///         `pipeline` は `std::nullopt`。
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
		/// 汎用 createFromDevice では PSO/root sig が bind されず、何もしないまま終わる
		/// (silent no-op) ため、専用パスを使う。
		result.pipeline = RenderPipeline2D::createFromDx12(
			dx12,
			static_cast<float>(screenWidth),
			static_cast<float>(screenHeight));
		return result;
	}
#endif

	/// OpenGL/WebGL/その他のバックエンド: 汎用 IDevice 経由でパイプラインを構築する
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

/// @brief device の backend に適した 3D renderer を構築する。
/// @details `Engine::create3DRenderer` 内の engine 内部 dynamic_cast チェーンを
///          置き換える。利用可能なら DX12 を優先 (toon outline PSO が reference
///          path)。DX11 は Win32 の明示 fallback。ここは device が既に
///          確定した後の dispatch なので、fallback 発動の通知は GfxFactory 側で行う
///          (明示 Dx11 指定と区別できるのは生成時のみ)。他 backend は nullptr を
///          返す (3D は未実装)。
/// @param device GPU device
/// @param screenWidth 論理 screen 幅  (DX11 fallback の viewport)
/// @param screenHeight 論理 screen 高さ (DX11 fallback の viewport)
/// @param windowWidth 物理 window 幅  (DX12 viewport、DPI 対応)
/// @param windowHeight 物理 window 高さ (DX12 viewport、DPI 対応)
/// @param antiAliasing 3D の AA の方式 (DX12 だけが受け取る)
/// @param motionBlur 3D の動きのぼけのシャッターの割合 (DX12 だけが受け取る)
/// @return renderer インスタンス。backend に 3D path が無ければ nullptr。
[[nodiscard]] inline std::unique_ptr<IRenderer3D> createRenderer3DFor(
	gfx::IDevice* device,
	int screenWidth, int screenHeight,
	int windowWidth, int windowHeight,
	AntiAliasing3D antiAliasing = AntiAliasing3D::MsaaFxaa,
	float motionBlur = 0.0f)
{
	if (!device)
	{
		return nullptr;
	}

	const auto backend = device->backend();
#ifdef _WIN32
	// DX12 を優先的に使用 (アウトラインが PSO で正しく動く)
	if (backend == gfx::Backend::Dx12)
	{
		auto* dx12 = static_cast<gfx::Dx12Device*>(device);
		Renderer3D_DX12::Config cfg;
		// 実際のウィンドウ物理ピクセルサイズを使用する (DPI スケーリング対応)
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
				// D3D12 info queue から具体的なエラーを取り出す
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
		dx12Renderer->setAntiAliasing(antiAliasing);
		dx12Renderer->setMotionBlur(motionBlur);
		return dx12Renderer;
	}

	// DX11 (明示指定 or GfxFactory で fallback 済みの device)。
	// WBOIT/HDR/MSAA/FXAA/影なしの Renderer3D 経路。
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
	(void)antiAliasing;
	(void)motionBlur;
#endif

#ifdef __EMSCRIPTEN__
	if (backend == gfx::Backend::WebGL)
	{
		auto webgl = std::make_unique<Renderer3D_WebGL>();
		webgl->initialize(windowWidth, windowHeight);
		if (webgl->isInitialized()) { return webgl; }
		return nullptr;
	}
#endif

	// NullDevice: headless 動作。描画は行わず drawCallCount だけ数える実体を返す。
	if (backend == gfx::Backend::Null)
	{
		return std::make_unique<NullRenderer3D>();
	}

	// Vulkan / OpenGL: 3D renderer 未実装。初回のみ警告し nullptr を返す
	// (呼び出し側は 2D のみで動作を継続する)。
	if (backend == gfx::Backend::Vulkan)
	{
		mitiru::debug::warnOnce("render3d.vulkan.unsupported",
			"Vulkan backend has no 3D renderer yet (docs/3D_RENDERING.md)");
	}
	else if (backend == gfx::Backend::OpenGL)
	{
		mitiru::debug::warnOnce("render3d.opengl.unsupported",
			"OpenGL backend has no 3D renderer yet (docs/3D_RENDERING.md)");
	}

	return nullptr;
}

} // namespace mitiru::render
