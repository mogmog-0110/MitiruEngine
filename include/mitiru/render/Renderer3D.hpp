#pragma once

/// @file Renderer3D.hpp
/// @brief DX11 3Dレンダラー
/// @details Phongシェーディングによる3Dメッシュ描画を行うレンダラー。
///          シェーダーコンパイル・定数バッファ管理・深度バッファ・ラスタライザ状態を
///          統合的に管理し、drawMesh()一発でメッシュを描画できる。

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/DefaultShaders3D.hpp>
#include <mitiru/render/ToonShaders3D.hpp>
#include <mitiru/render/NPRShaders3D.hpp>
#include <mitiru/render/MultiLightShaders3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/LightArrayCB.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/Skybox.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/RenderState3D.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/IRenderer3D.hpp>

#include <mitiru/debug/TracyZones.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>

#endif // _WIN32

namespace mitiru::render
{

// ShaderMode3D は IRenderer3D.hpp で定義済み

/// @brief トランスフォーム用定数バッファ（CbTransform: register(b0)）
/// @details ワールド・ビュー・射影行列をGPUに転送する。
struct alignas(16) CbTransform
{
	float world[4][4]{};       ///< ワールド行列
	float view[4][4]{};        ///< ビュー行列
	float projection[4][4]{};  ///< 射影行列
};

/// @brief ライティング用定数バッファ（CbLighting: register(b1)）
/// @details ライト・マテリアル・カメラ位置の情報をGPUに転送する。
struct alignas(16) CbLighting
{
	float lightDir[4]{};          ///< ライト方向 (xyz) + パディング
	float lightColor[4]{};        ///< ライト色 (xyz) + パディング
	float ambientColor[4]{};      ///< アンビエント色 (xyz) + パディング
	float cameraPos[4]{};         ///< カメラ位置 (xyz) + パディング
	float materialDiffuse[4]{};   ///< マテリアル拡散色 (rgba)
	float materialSpecular[4]{};  ///< マテリアル鏡面色 (rgba)
	float materialShininess = 32.0f;  ///< マテリアル光沢度
	float _pad[3]{};              ///< パディング
};

/// @brief Renderer3D設定
/// @details 初期化時に渡す設定パラメータ。
struct Renderer3DConfig
{
	float viewportWidth = 1280.0f;   ///< ビューポート幅
	float viewportHeight = 720.0f;   ///< ビューポート高さ
	bool enableDepthBuffer = true;   ///< 深度バッファ有効
	sgc::Colorf defaultAmbient{0.15f, 0.15f, 0.15f, 1.0f};  ///< デフォルトアンビエント色
};

/// @brief DX11 3Dレンダラー
/// @details Camera3Dとリアルタイムライトを使い、メッシュをPhong照明で描画する。
///          内部でシェーダーコンパイル・入力レイアウト・定数バッファ・深度バッファ・
///          ラスタライザステートを管理する。
///
/// @code
/// // 初期化
/// mitiru::render::Renderer3D renderer;
/// renderer.initialize(dx11Device, {1280, 720});
///
/// // 毎フレーム
/// renderer.beginFrame({0.2f, 0.3f, 0.4f, 1.0f});
/// renderer.setCamera(camera);
/// renderer.setLight(Light::directional({0, -1, 0.5f}));
/// renderer.drawMesh(cubeMesh, worldMat, material);
/// renderer.endFrame();
/// @endcode
class Renderer3D : public IRenderer3D
{
public:
	/// @brief デフォルトコンストラクタ
	Renderer3D() noexcept = default;

	/// @brief 描画統計情報を取得する
	/// @return 直前フレームのドローコール数
	[[nodiscard]] int drawCallCount() const noexcept override
	{
		return m_drawCallCount;
	}

	/// @brief 初期化済みかどうかを取得する
	/// @return GPUリソースが構築済みならtrue
	[[nodiscard]] bool isInitialized() const noexcept override
	{
		return m_initialized;
	}

	/// @brief 現在の描画状態を取得する
	/// @return 描画状態の定数参照
	[[nodiscard]] const RenderState3D& renderState() const noexcept
	{
		return m_renderState;
	}

	/// @brief 描画状態を設定する
	/// @param state 新しい描画状態
	void setRenderState(const RenderState3D& state) noexcept
	{
		m_renderState = state;
		m_rasterizerDirty = true;
	}

	/// @brief 設定を取得する
	/// @return 設定の定数参照
	[[nodiscard]] const Renderer3DConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief シェーダーモードを設定する
	/// @param mode 新しいシェーダーモード
	/// @details 初期化済みの場合はシェーダーを再コンパイルする。
	void setShaderMode(ShaderMode3D mode) override
	{
		if (m_shaderMode == mode)
		{
			return;
		}
		m_shaderMode = mode;
#ifdef _WIN32
		if (m_initialized)
		{
			recompileShaders();
		}
#endif
	}

	/// @brief 現在のシェーダーモードを取得する
	/// @return 現在のシェーダーモード
	[[nodiscard]] ShaderMode3D shaderMode() const noexcept
	{
		return m_shaderMode;
	}

#ifdef _WIN32

	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief レンダラーを初期化する
	/// @param device DX11デバイスへのポインタ
	/// @param cfg 設定パラメータ
	/// @param mode シェーダーモード（初期化時に直接設定 — setShaderModeの再コンパイルを回避）
	void initialize(gfx::Dx11Device* device,
		const Renderer3DConfig& cfg = {},
		ShaderMode3D mode = ShaderMode3D::Phong)
	{
		if (!device)
		{
			return;
		}

		m_device = device;
		m_config = cfg;
		m_sceneAmbient = cfg.defaultAmbient;
		m_shaderMode = mode;
		m_d3dDevice = device->getD3DDevice();
		m_d3dContext = device->getD3DContext();

		compileShaders();
		createInputLayout();
		createConstantBuffers();
		createDepthBuffer();
		createRasterizerState();
		createDepthStencilState();
		createDefaultWhiteTexture();
		createSamplerState();

		m_initialized = true;
	}

	/// @brief フレーム描画を開始する
	/// @param clearColor 画面クリア色
	/// @brief フレームアクティブフラグをリセットする（Engine::run()から毎フレーム呼ばれる）
	void resetFrameActive() noexcept override { m_frameActive = false; }

	/// @brief 今フレームで3D描画が行われたか
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }

	/// @brief フレーム描画を開始する
	/// @param clearColor 画面クリア色
	void beginFrame(const sgc::Colorf& clearColor) override
	{
		MITIRU_ZONE_NAMED("Render::Dx11::BeginFrame");
		if (!m_initialized)
		{
			return;
		}

		m_frameActive = true;
		m_drawCallCount = 0;
		m_outlineQueue.clear();
		m_skyboxDrawnThisFrame = false;

		/// レンダーターゲットと深度バッファを設定する
		auto* swapChain = m_device->getSwapChain();
		if (!swapChain)
		{
			return;
		}

		auto* rtv = swapChain->getRenderTargetView();
		if (rtv)
		{
			const float color[4] = {
				clearColor.r, clearColor.g, clearColor.b, clearColor.a
			};
			m_d3dContext->ClearRenderTargetView(rtv, color);

			if (m_depthStencilView)
			{
				m_d3dContext->ClearDepthStencilView(
					m_depthStencilView.Get(),
					D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
					1.0f, 0);
			}

			m_d3dContext->OMSetRenderTargets(
				1, &rtv, m_depthStencilView.Get());
		}

		/// ビューポートを設定する
		D3D11_VIEWPORT vp = {};
		vp.Width = m_config.viewportWidth;
		vp.Height = m_config.viewportHeight;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		m_d3dContext->RSSetViewports(1, &vp);

		/// シェーダーを設定する
		m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
		m_d3dContext->IASetInputLayout(m_inputLayout.Get());
		m_d3dContext->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// ラスタライザが変更されていれば再作成する
		if (m_rasterizerDirty)
		{
			createRasterizerState();
			m_rasterizerDirty = false;
		}
		m_d3dContext->RSSetState(m_rasterizerState.Get());

		/// 深度ステンシルステートを設定する
		m_d3dContext->OMSetDepthStencilState(
			m_depthStencilState.Get(), 0);

		/// ブレンドステートを設定する
		if (m_renderState.blendEnabled)
		{
			const float blendFactor[4] = {0, 0, 0, 0};
			m_d3dContext->OMSetBlendState(
				m_blendState.Get(), blendFactor, 0xFFFFFFFF);
		}
		else
		{
			m_d3dContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		}
	}

	/// @brief カメラを設定する
	/// @param camera 3Dカメラ
	/// @details DX11 でも projection を **DX 規約 Z[0,1]** にする。
	///          sgc::Mat4f::perspective は OpenGL 規約 (Z[-1,1]) のため、
	///          DX で使うと near 側半分の depth が clip され precision が半減する。
	///          camera の fov / aspect / near / far を使い直して DX 友好的な
	///          RH perspective を組む。view は sgc RH のまま。
	void setCamera(const Camera3D& camera) override
	{
		m_viewMatrix = camera.viewMatrix();
		m_projMatrix = makePerspectiveRH_ZO_DX(
			camera.fov(), camera.aspectRatio(),
			camera.nearClip(), camera.farClip());
		m_cameraPosition = camera.position();
	}

	/// @brief DX 用 RH perspective (Z[0,1])
	/// @details column-major で GPU に流すと前提で sgc 行レイアウトに合わせて作る。
	[[nodiscard]] static sgc::Mat4f makePerspectiveRH_ZO_DX(
		float fovY, float aspect, float nearZ, float farZ) noexcept
	{
		const float tanHalf = std::tan(fovY * 0.5f);
		const float range   = nearZ - farZ;   // < 0
		// 標準 RH-ZO 透視行列（OpenGL gluPerspective を DX Z[0,1] に書き換えたもの）
		return {
			1.0f / (aspect * tanHalf), 0.0f,            0.0f,                0.0f,
			0.0f,                      1.0f / tanHalf,  0.0f,                0.0f,
			0.0f,                      0.0f,            farZ / range,        (farZ * nearZ) / range,
			0.0f,                      0.0f,           -1.0f,                0.0f,
		};
	}

	/// @brief ディレクショナルライトを設定する
	/// @param light ライトデータ
	void setLight(const Light& light) override
	{
		m_light = light;
	}

	/// @brief 複数ライトを設定する
	/// @param lights ライト配列（kMaxLights を超える分は切り詰め）
	/// @details `setUseMultiLight(true)` 時は GPU 側 b2 にこのライト配列が
	///          毎フレーム drawMesh で送られる。`setUseMultiLight(false)` 時
	///          （デフォルト）は `m_light = lights[0]` の後方互換動作のみ。
	void setLights(std::span<const Light> lights) override
	{
		m_lights.assign(lights.begin(), lights.end());
		if (m_lights.size() > static_cast<std::size_t>(IRenderer3D::kMaxLights))
		{
			m_lights.resize(IRenderer3D::kMaxLights);
		}
		if (!m_lights.empty())
		{
			m_light = m_lights.front();
		}
	}

	/// @brief 直近に設定されたライト配列を取得する
	[[nodiscard]] std::span<const Light> lights() const noexcept
	{
		return { m_lights.data(), m_lights.size() };
	}

	/// @brief マルチライト経路を有効/無効にする
	/// @details `true` にすると Phong シェーダーが MULTI_LIGHT 変種に切り替わり、
	///          b2 = `CbLightArray` を消費するようになる。Toon / Unlit / 他の
	///          NPR モードは現状マルチライト未対応で、本フラグは無視される。
	///          初期化済み状態で値が変わった場合のみ recompileShaders を呼ぶ。
	void setUseMultiLight(bool useMulti) override
	{
		if (m_useMultiLight == useMulti) return;
		m_useMultiLight = useMulti;
		if (m_initialized)
		{
			recompileShaders();
		}
	}

	/// @brief 現在マルチライト経路かを返す
	[[nodiscard]] bool useMultiLight() const noexcept override { return m_useMultiLight; }

	/// @brief キューブマップ skybox をセットする (DX11)
	void setSkybox(const Cubemap& cubemap) override
	{
		m_skyboxImpl.setCubemap(cubemap);
		m_skyboxNeedsInit = cubemap.valid();
		if (cubemap.valid())
		{
			m_skyboxEnabled = true;
		}
	}

	/// @brief skybox 描画の有効/無効
	void setSkyboxEnabled(bool enabled) override
	{
		m_skyboxEnabled = enabled;
	}

	/// @brief skybox 描画が有効か
	[[nodiscard]] bool isSkyboxEnabled() const noexcept override
	{
		return m_skyboxEnabled && m_skyboxImpl.hasValidCubemap();
	}

	/// @brief 内部の DX11 デバイスを返す（拡張機能用 — Skybox など）
	/// @details Skybox / IBL / experimental エフェクトのように Renderer3D の
	///          外で DX11 リソースを直接構築する consumer 向け。
	///          `initialize()` 前は nullptr。
	[[nodiscard]] gfx::Dx11Device* getDx11Device() const noexcept { return m_device; }

	/// @brief 内部の ID3D11DeviceContext を返す（拡張機能用）
	/// @details ID3D11DeviceContext を直接触る必要があるとき（例: Skybox の
	///          drawDx11 にコンテキストを渡す）に使う。`beginFrame()` で
	///          RTV/DSV を設定済みの context を返すため、追加の draw を
	///          そのままインジェクトできる。
	[[nodiscard]] ID3D11DeviceContext* getD3DContext() const noexcept { return m_d3dContext; }

	/// @brief シーンのアンビエント色を設定する
	/// @param color RGB アンビエント色
	void setAmbientColor(const sgc::Colorf& color) override
	{
		m_sceneAmbient = color;
	}

	/// @brief 現在のシーンアンビエント色を返す
	[[nodiscard]] sgc::Colorf ambientColor() const noexcept override
	{
		return m_sceneAmbient;
	}

	/// @brief テクスチャを設定する
	/// @param tex 適用するテクスチャ
	void setTexture(const Texture& tex)
	{
		if (!tex.valid())
		{
			return;
		}
		uploadTexture(tex);
	}

	/// @brief テクスチャをクリアしてデフォルト（白）に戻す
	void clearTexture()
	{
		m_currentSRV = m_defaultWhiteSRV;
	}

	/// @brief メッシュを描画する
	/// @param mesh 描画するメッシュ
	/// @param worldTransform ワールド変換行列
	/// @param material マテリアル
	void drawMesh(const Mesh& mesh,
	              const sgc::Mat4f& worldTransform,
	              const Material& material) override
	{
		if (!m_initialized || mesh.vertexCount() == 0)
		{
			return;
		}

		// アウトラインは無効（ポストプロセス方式で別途実装予定）

		// skybox が必要なら最初の drawMesh の前に描画する
		drawSkyboxIfNeeded();

		/// トランスフォーム定数バッファを更新する
		updateTransformCB(worldTransform);

		/// ライティング定数バッファを更新する
		updateLightingCB(material);

		/// マルチライト経路ならライト配列 CB (b2) も毎フレーム更新する
		if (m_useMultiLight)
		{
			updateLightArrayCB();
		}

		/// テクスチャとサンプラーをバインドする
		/// material.albedoTexture が優先。null なら setTexture / clearTexture で
		/// 設定された m_currentSRV を使う（後方互換）。
		ID3D11ShaderResourceView* srv = nullptr;
		if (material.albedoTexture)
		{
			srv = getOrUploadAlbedoSrv(material.albedoTexture);
		}
		if (!srv && m_currentSRV)
		{
			srv = m_currentSRV.Get();
		}
		if (!srv && m_defaultWhiteSRV)
		{
			srv = m_defaultWhiteSRV.Get();
		}
		if (srv)
		{
			m_d3dContext->PSSetShaderResources(0, 1, &srv);
		}
		if (m_samplerState)
		{
			ID3D11SamplerState* sampler = m_samplerState.Get();
			m_d3dContext->PSSetSamplers(0, 1, &sampler);
		}

		/// 頂点バッファを作成してバインドする
		const auto& verts = mesh.vertices();
		const auto vbSize = static_cast<UINT>(
			verts.size() * sizeof(Vertex3D));

		auto vb = createDynamicVertexBuffer(verts.data(), vbSize);
		if (!vb)
		{
			return;
		}

		const UINT stride = sizeof(Vertex3D);
		const UINT offset = 0;
		m_d3dContext->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);

		/// インデックスバッファを作成してバインドする（あれば）
		const auto& indices = mesh.indices();
		if (!indices.empty())
		{
			const auto ibSize = static_cast<UINT>(
				indices.size() * sizeof(uint32_t));

			auto ib = createDynamicIndexBuffer(indices.data(), ibSize);
			if (!ib)
			{
				return;
			}

			m_d3dContext->IASetIndexBuffer(
				ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			m_d3dContext->DrawIndexed(
				static_cast<UINT>(indices.size()), 0, 0);
		}
		else
		{
			m_d3dContext->Draw(
				static_cast<UINT>(verts.size()), 0);
		}

		++m_drawCallCount;

		// アウトラインはdrawMesh内で完結（endFrame不要）
	}

	/// @brief フレーム描画を終了する
	/// @note Use ToonPipeline for outline rendering.
	void endFrame() override
	{
		m_outlineQueue.clear();
	}

private:
	/// @brief HLSLシェーダーをコンパイルする
	void compileShaders()
	{
		/// シェーダーモードに応じたソースを選択する
		const char* vsSource = DEFAULT_VS_3D;
		const char* psSource = DEFAULT_PS_3D;

		switch (m_shaderMode)
		{
		case ShaderMode3D::Toon:        vsSource = TOON_VS_3D;  psSource = TOON_PS_3D;         break;
		case ShaderMode3D::Unlit:       vsSource = UNLIT_VS_3D; psSource = UNLIT_PS_3D;        break;
		case ShaderMode3D::Flat:                                psSource = FLAT_PS_3D;          break;
		case ShaderMode3D::Posterize:                           psSource = POSTERIZE_PS_3D;     break;
		case ShaderMode3D::Halftone:                            psSource = HALFTONE_PS_3D;      break;
		case ShaderMode3D::Hatching:                            psSource = HATCHING_PS_3D;      break;
		case ShaderMode3D::GradientMap:                         psSource = GRADIENT_MAP_PS_3D;  break;
		case ShaderMode3D::Silhouette:                          psSource = SILHOUETTE_PS_3D;    break;
		case ShaderMode3D::Watercolor:                          psSource = WATERCOLOR_PS_3D;    break;
		default: break;
		}

		// マルチライト経路は現状 Phong だけ差し替える。
		// （Toon / NPR は単一光源モデルの色味設計と密結合なので変更しない）
		if (m_useMultiLight && m_shaderMode == ShaderMode3D::Phong)
		{
			vsSource = MULTI_LIGHT_VS_3D;
			psSource = MULTI_LIGHT_PS_3D;
		}

		/// 頂点シェーダーをコンパイルする
		auto vsBlob = compileHLSL(
			vsSource, "VSMain", "vs_5_0");

		HRESULT hr = m_d3dDevice->CreateVertexShader(
			vsBlob->GetBufferPointer(),
			vsBlob->GetBufferSize(),
			nullptr,
			m_vertexShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateVertexShader failed");
		}

		/// バイトコードを保存する（InputLayout用）
		m_vsBytecode.resize(vsBlob->GetBufferSize());
		std::memcpy(m_vsBytecode.data(),
		            vsBlob->GetBufferPointer(),
		            vsBlob->GetBufferSize());

		/// ピクセルシェーダーをコンパイルする
		auto psBlob = compileHLSL(
			psSource, "PSMain", "ps_5_0");

		hr = m_d3dDevice->CreatePixelShader(
			psBlob->GetBufferPointer(),
			psBlob->GetBufferSize(),
			nullptr,
			m_pixelShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreatePixelShader failed");
		}

		/// アウトラインコンパイル無効（ポストプロセス方式で別途実装予定）
		if (false)
		{
			auto outVsBlob = compileHLSL(OUTLINE_VS_3D, "VSMain", "vs_5_0");
			m_d3dDevice->CreateVertexShader(
				outVsBlob->GetBufferPointer(),
				outVsBlob->GetBufferSize(),
				nullptr,
				m_outlineVS.GetAddressOf());

			/// アウトライン用入力レイアウト（メインVSと同じフォーマット）
			std::vector<uint8_t> outlineBytecode(outVsBlob->GetBufferSize());
			std::memcpy(outlineBytecode.data(),
			            outVsBlob->GetBufferPointer(),
			            outVsBlob->GetBufferSize());

			const D3D11_INPUT_ELEMENT_DESC outlineLayout[] =
			{
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
			};
			m_d3dDevice->CreateInputLayout(
				outlineLayout, 4,
				outlineBytecode.data(),
				static_cast<UINT>(outlineBytecode.size()),
				m_outlineInputLayout.GetAddressOf());

			auto outPsBlob = compileHLSL(OUTLINE_PS_3D, "PSMain", "ps_5_0");
			m_d3dDevice->CreatePixelShader(
				outPsBlob->GetBufferPointer(),
				outPsBlob->GetBufferSize(),
				nullptr,
				m_outlinePS.GetAddressOf());

			/// フロントフェースカリング用ラスタライザ（アウトライン描画用）
			D3D11_RASTERIZER_DESC rd = {};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_FRONT;
			rd.FrontCounterClockwise = FALSE;
			rd.DepthClipEnable = TRUE;
			m_d3dDevice->CreateRasterizerState(
				&rd, m_outlineFrontCull.GetAddressOf());
		}
	}

	/// @brief シェーダーを再コンパイルする
	/// @details シェーダーモード変更時に呼び出される。
	void recompileShaders()
	{
		m_vertexShader.Reset();
		m_pixelShader.Reset();
		m_inputLayout.Reset();
		m_vsBytecode.clear();
		m_outlineVS.Reset();
		m_outlinePS.Reset();
		m_outlineInputLayout.Reset();
		m_outlineFrontCull.Reset();
		compileShaders();
		createInputLayout();

		// フレーム最中（beginFrame と endFrame の間）に setShaderMode や
		// setUseMultiLight でこの関数が呼ばれた場合、device context には
		// 旧シェーダー / 旧 InputLayout がバインドされたままになる。
		// 新シェーダーを使うため、context が存在すれば即座に再バインドする。
		if (m_d3dContext)
		{
			m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
			m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
			m_d3dContext->IASetInputLayout(m_inputLayout.Get());
		}
	}

	/// @brief HLSL文字列をコンパイルする
	/// @param source HLSL文字列
	/// @param entryPoint エントリーポイント
	/// @param target コンパイルターゲット
	/// @return コンパイル済みBlob
	[[nodiscard]] ComPtr<ID3DBlob> compileHLSL(
		const char* source,
		const char* entryPoint,
		const char* target)
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		HRESULT hr = D3DCompile(
			source,
			std::strlen(source),
			nullptr, nullptr, nullptr,
			entryPoint, target,
			flags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string msg = "Renderer3D: D3DCompile failed";
			if (errorBlob)
			{
				msg += ": ";
				msg += static_cast<const char*>(
					errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(msg);
		}

		return shaderBlob;
	}

	/// @brief Vertex3D用の入力レイアウトを作成する
	void createInputLayout()
	{
		/// Vertex3D: position(float3) + normal(float3) + texCoord(float2) + color(float4)
		const D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{
				"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
				0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
				0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
				0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
				0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
		};

		HRESULT hr = m_d3dDevice->CreateInputLayout(
			layout,
			static_cast<UINT>(std::size(layout)),
			m_vsBytecode.data(),
			m_vsBytecode.size(),
			m_inputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateInputLayout failed");
		}
	}

	/// @brief 定数バッファを作成する
	void createConstantBuffers()
	{
		/// トランスフォーム定数バッファ (b0)
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbTransform);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_d3dDevice->CreateBuffer(
			&desc, nullptr, m_cbTransform.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateBuffer(CbTransform) failed");
		}

		/// ライティング定数バッファ (b1)
		desc.ByteWidth = sizeof(CbLighting);
		hr = m_d3dDevice->CreateBuffer(
			&desc, nullptr, m_cbLighting.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateBuffer(CbLighting) failed");
		}

		/// マルチライト定数バッファ (b2)。
		/// useMultiLight が false でも作成しておく（小容量だしフラグ切替に
		/// 追従して遅延作成するより常時用意したほうがハンドル管理がシンプル）。
		desc.ByteWidth = sizeof(LightArrayCB);
		hr = m_d3dDevice->CreateBuffer(
			&desc, nullptr, m_cbLightArray.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateBuffer(CbLightArray) failed");
		}
	}

	/// @brief マルチライト CB を更新して b2 にバインドする
	/// @details `useMultiLight()` が true のとき drawMesh から呼ばれる。
	void updateLightArrayCB()
	{
		const auto cb = LightArrayCB::fromLights(
			std::span<const Light>(m_lights.data(), m_lights.size()),
			m_sceneAmbient);

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT hr = m_d3dContext->Map(
			m_cbLightArray.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			m_d3dContext->Unmap(m_cbLightArray.Get(), 0);
		}

		ID3D11Buffer* buf = m_cbLightArray.Get();
		m_d3dContext->PSSetConstantBuffers(2, 1, &buf);
	}

	/// @brief 深度バッファを作成する
	void createDepthBuffer()
	{
		if (!m_config.enableDepthBuffer)
		{
			return;
		}

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = static_cast<UINT>(m_config.viewportWidth);
		texDesc.Height = static_cast<UINT>(m_config.viewportHeight);
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		ComPtr<ID3D11Texture2D> depthTex;
		HRESULT hr = m_d3dDevice->CreateTexture2D(
			&texDesc, nullptr, depthTex.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateTexture2D(depth) failed");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = texDesc.Format;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;

		hr = m_d3dDevice->CreateDepthStencilView(
			depthTex.Get(), &dsvDesc,
			m_depthStencilView.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateDepthStencilView failed");
		}
	}

	/// @brief ラスタライザステートを作成する
	void createRasterizerState()
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = m_renderState.wireframe
			? D3D11_FILL_WIREFRAME
			: D3D11_FILL_SOLID;

		switch (m_renderState.cullMode)
		{
		case CullMode::None:
			desc.CullMode = D3D11_CULL_NONE;
			break;
		case CullMode::Back:
			desc.CullMode = D3D11_CULL_BACK;
			break;
		case CullMode::Front:
			desc.CullMode = D3D11_CULL_FRONT;
			break;
		}

		desc.FrontCounterClockwise = FALSE;
		desc.DepthClipEnable = TRUE;

		m_rasterizerState.Reset();
		HRESULT hr = m_d3dDevice->CreateRasterizerState(
			&desc, m_rasterizerState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateRasterizerState failed");
		}
	}

	/// @brief 深度ステンシルステートを作成する
	void createDepthStencilState()
	{
		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = m_renderState.depthTest ? TRUE : FALSE;
		desc.DepthWriteMask = m_renderState.depthWrite
			? D3D11_DEPTH_WRITE_MASK_ALL
			: D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_LESS;
		desc.StencilEnable = FALSE;

		HRESULT hr = m_d3dDevice->CreateDepthStencilState(
			&desc, m_depthStencilState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateDepthStencilState failed");
		}

		/// ブレンドステート（半透明用）
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = m_d3dDevice->CreateBlendState(
			&blendDesc, m_blendState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D: CreateBlendState failed");
		}
	}

	/// @brief トランスフォーム定数バッファを更新する
	/// @param worldTransform ワールド行列
	/// @details glmを経由してHLSL互換のrow-majorレイアウトに変換する。
	///          sgc::Mat4fのメモリレイアウトがHLSLと一致しない問題を回避。
	void updateTransformCB(const sgc::Mat4f& worldTransform)
	{
		CbTransform cb;
		// glm経由でHLSL互換レイアウトに変換
		glm::mat4 world = toGlm(worldTransform);
		glm::mat4 view  = toGlm(m_viewMatrix);
		glm::mat4 proj  = toGlm(m_projMatrix);
		toHLSL(cb.world, world);
		toHLSL(cb.view, view);
		toHLSL(cb.projection, proj);

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_d3dContext->Map(
			m_cbTransform.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			m_d3dContext->Unmap(m_cbTransform.Get(), 0);
		}

		ID3D11Buffer* buf = m_cbTransform.Get();
		m_d3dContext->VSSetConstantBuffers(0, 1, &buf);
	}

	/// @brief ライティング定数バッファを更新する
	/// @param material マテリアル
	void updateLightingCB(const Material& material)
	{
		CbLighting cb;
		cb.lightDir[0] = m_light.direction.x;
		cb.lightDir[1] = m_light.direction.y;
		cb.lightDir[2] = m_light.direction.z;
		cb.lightDir[3] = 0.0f;

		cb.lightColor[0] = m_light.color.r * m_light.intensity;
		cb.lightColor[1] = m_light.color.g * m_light.intensity;
		cb.lightColor[2] = m_light.color.b * m_light.intensity;
		cb.lightColor[3] = 1.0f;

		cb.ambientColor[0] = m_sceneAmbient.r;
		cb.ambientColor[1] = m_sceneAmbient.g;
		cb.ambientColor[2] = m_sceneAmbient.b;
		cb.ambientColor[3] = 1.0f;

		cb.cameraPos[0] = m_cameraPosition.x;
		cb.cameraPos[1] = m_cameraPosition.y;
		cb.cameraPos[2] = m_cameraPosition.z;
		cb.cameraPos[3] = 1.0f;

		cb.materialDiffuse[0] = material.diffuse.r;
		cb.materialDiffuse[1] = material.diffuse.g;
		cb.materialDiffuse[2] = material.diffuse.b;
		cb.materialDiffuse[3] = material.diffuse.a;

		cb.materialSpecular[0] = material.specular.r;
		cb.materialSpecular[1] = material.specular.g;
		cb.materialSpecular[2] = material.specular.b;
		cb.materialSpecular[3] = material.specular.a;

		cb.materialShininess = material.shininess;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_d3dContext->Map(
			m_cbLighting.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			m_d3dContext->Unmap(m_cbLighting.Get(), 0);
		}

		ID3D11Buffer* buf = m_cbLighting.Get();
		m_d3dContext->PSSetConstantBuffers(1, 1, &buf);
	}

	/// @brief 動的頂点バッファを作成する
	/// @param data 頂点データ
	/// @param sizeBytes データサイズ
	/// @return 作成されたバッファ
	[[nodiscard]] ComPtr<ID3D11Buffer> createDynamicVertexBuffer(
		const void* data, UINT sizeBytes)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeBytes;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = data;

		ComPtr<ID3D11Buffer> buffer;
		m_d3dDevice->CreateBuffer(&desc, &initData, buffer.GetAddressOf());
		return buffer;
	}

	/// @brief 動的インデックスバッファを作成する
	/// @param data インデックスデータ
	/// @param sizeBytes データサイズ
	/// @return 作成されたバッファ
	[[nodiscard]] ComPtr<ID3D11Buffer> createDynamicIndexBuffer(
		const void* data, UINT sizeBytes)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeBytes;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = data;

		ComPtr<ID3D11Buffer> buffer;
		m_d3dDevice->CreateBuffer(&desc, &initData, buffer.GetAddressOf());
		return buffer;
	}

	/// @brief テクスチャデータをGPUにアップロードする
	/// @param tex アップロードするテクスチャ
	void uploadTexture(const Texture& tex)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(tex.width());
		desc.Height = static_cast<UINT>(tex.height());
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = tex.pixels().data();
		initData.SysMemPitch = static_cast<UINT>(tex.width()) * 4;

		ComPtr<ID3D11Texture2D> texture2D;
		HRESULT hr = m_d3dDevice->CreateTexture2D(
			&desc, &initData, texture2D.GetAddressOf());
		if (FAILED(hr))
		{
			return;
		}

		m_currentSRV.Reset();
		m_d3dDevice->CreateShaderResourceView(
			texture2D.Get(), nullptr, m_currentSRV.GetAddressOf());
	}

	/// @brief Material.albedoTexture 用の SRV を取得（必要なら upload + cache）
	/// @details `setTexture` の global state とは独立した per-Texture* キャッシュ。
	///          同じ `Texture*` は 1 度しかアップロードしない。
	[[nodiscard]] ID3D11ShaderResourceView* getOrUploadAlbedoSrv(const Texture* tex)
	{
		if (!tex || !tex->valid()) return nullptr;
		auto it = m_albedoSrvCache.find(tex);
		if (it != m_albedoSrvCache.end())
		{
			return it->second.Get();
		}
		// upload texture (mirroring uploadTexture but storing in the cache)
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width            = static_cast<UINT>(tex->width());
		desc.Height           = static_cast<UINT>(tex->height());
		desc.MipLevels        = 1;
		desc.ArraySize        = 1;
		desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage            = D3D11_USAGE_DEFAULT;
		desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem     = tex->pixels().data();
		initData.SysMemPitch = static_cast<UINT>(tex->width()) * 4;

		ComPtr<ID3D11Texture2D> texture2D;
		if (FAILED(m_d3dDevice->CreateTexture2D(
				&desc, &initData, texture2D.GetAddressOf())))
		{
			return nullptr;
		}
		ComPtr<ID3D11ShaderResourceView> srv;
		if (FAILED(m_d3dDevice->CreateShaderResourceView(
				texture2D.Get(), nullptr, srv.GetAddressOf())))
		{
			return nullptr;
		}
		auto* raw = srv.Get();
		m_albedoSrvCache.emplace(tex, std::move(srv));
		return raw;
	}

	/// @brief デフォルトの1x1白テクスチャを作成する
	void createDefaultWhiteTexture()
	{
		const std::uint8_t whitePixel[4] = {255, 255, 255, 255};

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = whitePixel;
		initData.SysMemPitch = 4;

		ComPtr<ID3D11Texture2D> texture2D;
		HRESULT hr = m_d3dDevice->CreateTexture2D(
			&desc, &initData, texture2D.GetAddressOf());
		if (FAILED(hr))
		{
			return;
		}

		m_d3dDevice->CreateShaderResourceView(
			texture2D.Get(), nullptr, m_defaultWhiteSRV.GetAddressOf());
		m_currentSRV = m_defaultWhiteSRV;
	}

	/// @brief テクスチャサンプラーステートを作成する
	void createSamplerState()
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		m_d3dDevice->CreateSamplerState(
			&desc, m_samplerState.GetAddressOf());
	}

	/// @brief アウトラインパスでメッシュを描画する
	/// @brief アウトラインパス（drawMesh内から呼ばれる、メイン描画の前に実行）
	/// シェーダー・ラスタライザを切替→描画→即座に復元
	void drawOutlinePass(const Mesh& mesh, const sgc::Mat4f& worldTransform)
	{
		// アウトラインシェーダーに切替
		m_d3dContext->VSSetShader(m_outlineVS.Get(), nullptr, 0);
		m_d3dContext->PSSetShader(m_outlinePS.Get(), nullptr, 0);
		m_d3dContext->IASetInputLayout(m_outlineInputLayout.Get());
		m_d3dContext->RSSetState(m_outlineFrontCull.Get());

		updateTransformCB(worldTransform);

		const auto& verts = mesh.vertices();
		auto vb = createDynamicVertexBuffer(
			verts.data(), static_cast<UINT>(verts.size() * sizeof(Vertex3D)));
		if (!vb) goto restore;

		{
			UINT stride = sizeof(Vertex3D), off = 0;
			m_d3dContext->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &off);

			const auto& indices = mesh.indices();
			if (!indices.empty())
			{
				auto ib = createDynamicIndexBuffer(
					indices.data(), static_cast<UINT>(indices.size() * sizeof(uint32_t)));
				if (ib)
				{
					m_d3dContext->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
					m_d3dContext->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
				}
			}
			else
			{
				m_d3dContext->Draw(static_cast<UINT>(verts.size()), 0);
			}
		}

		restore:
		// 即座にメインシェーダーに復元（次の行でメイン描画が行われるため）
		m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
		m_d3dContext->IASetInputLayout(m_inputLayout.Get());
		m_d3dContext->RSSetState(m_rasterizerState.Get());
	}

	/// @brief アウトライン描画（旧API、互換用）
	/// @param mesh 描画するメッシュ
	/// @param worldTransform ワールド変換行列
	void drawMeshOutline(const Mesh& mesh, const sgc::Mat4f& worldTransform)
	{
		if (!m_outlineVS || !m_outlinePS)
		{
			return;
		}

		/// アウトラインシェーダーに切り替える
		m_d3dContext->VSSetShader(m_outlineVS.Get(), nullptr, 0);
		m_d3dContext->PSSetShader(m_outlinePS.Get(), nullptr, 0);
		m_d3dContext->IASetInputLayout(m_outlineInputLayout.Get());
		m_d3dContext->RSSetState(m_outlineFrontCull.Get());

		/// トランスフォーム定数バッファを更新する（メインパスと同じ）
		updateTransformCB(worldTransform);

		/// 頂点バッファを作成してバインドする
		const auto& verts = mesh.vertices();
		auto vb = createDynamicVertexBuffer(
			verts.data(),
			static_cast<UINT>(verts.size() * sizeof(Vertex3D)));
		if (!vb)
		{
			return;
		}

		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		m_d3dContext->IASetVertexBuffers(
			0, 1, vb.GetAddressOf(), &stride, &offset);

		/// インデックスバッファを作成してバインドする（あれば）
		const auto& indices = mesh.indices();
		if (!indices.empty())
		{
			auto ib = createDynamicIndexBuffer(
				indices.data(),
				static_cast<UINT>(indices.size() * sizeof(uint32_t)));
			if (!ib)
			{
				return;
			}
			m_d3dContext->IASetIndexBuffer(
				ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			m_d3dContext->DrawIndexed(
				static_cast<UINT>(indices.size()), 0, 0);
		}
		else
		{
			m_d3dContext->Draw(
				static_cast<UINT>(verts.size()), 0);
		}
	}

	/// @brief DX11デバイス（非所有）
	gfx::Dx11Device* m_device = nullptr;
	/// @brief D3D11デバイス（非所有）
	ID3D11Device* m_d3dDevice = nullptr;
	/// @brief D3D11即時コンテキスト（非所有）
	ID3D11DeviceContext* m_d3dContext = nullptr;

	/// @brief 頂点シェーダー
	ComPtr<ID3D11VertexShader> m_vertexShader;
	/// @brief ピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_pixelShader;
	/// @brief VSバイトコード（InputLayout用）
	std::vector<uint8_t> m_vsBytecode;
	/// @brief 入力レイアウト
	ComPtr<ID3D11InputLayout> m_inputLayout;

	/// @brief トランスフォーム定数バッファ
	ComPtr<ID3D11Buffer> m_cbTransform;
	/// @brief ライティング定数バッファ
	ComPtr<ID3D11Buffer> m_cbLighting;
	/// @brief マルチライト定数バッファ (b2)
	ComPtr<ID3D11Buffer> m_cbLightArray;

	/// @brief 深度ステンシルビュー
	ComPtr<ID3D11DepthStencilView> m_depthStencilView;
	/// @brief 深度ステンシルステート
	ComPtr<ID3D11DepthStencilState> m_depthStencilState;
	/// @brief ラスタライザステート
	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	/// @brief ブレンドステート
	ComPtr<ID3D11BlendState> m_blendState;

	/// @brief アウトライン用頂点シェーダー
	ComPtr<ID3D11VertexShader> m_outlineVS;
	/// @brief アウトライン用ピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_outlinePS;
	/// @brief アウトライン用入力レイアウト
	ComPtr<ID3D11InputLayout> m_outlineInputLayout;
	/// @brief アウトライン用フロントフェースカリングラスタライザ
	ComPtr<ID3D11RasterizerState> m_outlineFrontCull;

	/// @brief デフォルト1x1白テクスチャSRV
	ComPtr<ID3D11ShaderResourceView> m_defaultWhiteSRV;

	/// Material.albedoTexture からの per-Texture* SRV キャッシュ（DX11）
	std::unordered_map<const Texture*, ComPtr<ID3D11ShaderResourceView>> m_albedoSrvCache;
	/// @brief 現在バインドされているテクスチャSRV
	ComPtr<ID3D11ShaderResourceView> m_currentSRV;
	/// @brief テクスチャサンプラーステート
	ComPtr<ID3D11SamplerState> m_samplerState;

	/// @brief ビュー行列
	sgc::Mat4f m_viewMatrix;
	/// @brief 射影行列
	sgc::Mat4f m_projMatrix;
	/// @brief カメラ位置
	sgc::Vec3f m_cameraPosition;
	/// @brief アクティブライト（後方互換の単一光源）
	Light m_light;

	/// @brief マルチライト経路用のライト配列（最大 kMaxLights）
	std::vector<Light> m_lights;

	/// @brief シーンアンビエント色（initialize 時に config.defaultAmbient で初期化）
	sgc::Colorf m_sceneAmbient{0.15f, 0.15f, 0.15f, 1.0f};

	/// @brief マルチライト経路フラグ（false = 単一光 Phong、true = b2 を消費）
	bool m_useMultiLight = false;

	/// @brief Skybox 実装（CPU+GPU リソースを持つ）
	Skybox m_skyboxImpl;
	/// @brief Skybox 描画フラグ
	bool m_skyboxEnabled = false;
	/// @brief 次フレーム以降に initializeDx11 を呼ぶ必要があるか
	bool m_skyboxNeedsInit = false;
	/// @brief このフレーム内で skybox を既に描画したか
	bool m_skyboxDrawnThisFrame = false;

	/// @brief skybox を描画する（drawMesh の最初の呼び出しで一度だけ）
	void drawSkyboxIfNeeded()
	{
		if (!m_skyboxEnabled) return;
		if (m_skyboxDrawnThisFrame) return;
		if (!m_skyboxImpl.hasValidCubemap()) return;

		if (m_skyboxNeedsInit)
		{
			m_skyboxImpl.initializeDx11(m_device);
			m_skyboxNeedsInit = false;
		}
		if (!m_skyboxImpl.isInitialized()) return;

		m_skyboxImpl.drawDx11(m_d3dContext, m_viewMatrix, m_projMatrix);
		m_skyboxDrawnThisFrame = true;
	}

#endif // _WIN32

	/// @brief 描画状態
	RenderState3D m_renderState;
	/// @brief ラスタライザ再作成フラグ
	bool m_rasterizerDirty = false;
	/// @brief 設定
	Renderer3DConfig m_config;
	/// @brief シェーダーモード
	ShaderMode3D m_shaderMode = ShaderMode3D::Phong;

	/// @brief アウトライン描画キュー（endFrameで一括描画）
	struct OutlineDrawCommand
	{
		const Mesh* mesh;
		sgc::Mat4f worldTransform;
	};
	std::vector<OutlineDrawCommand> m_outlineQueue;
	/// @brief 初期化済みフラグ
	bool m_initialized = false;
	bool m_frameActive = false;  ///< 今フレームでbeginFrame()が呼ばれたか
	/// @brief ドローコール数
	int m_drawCallCount = 0;
};

} // namespace mitiru::render
