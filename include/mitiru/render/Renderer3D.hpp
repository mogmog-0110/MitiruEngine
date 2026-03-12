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

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/DefaultShaders3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/RenderState3D.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/IDevice.hpp>

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
class Renderer3D
{
public:
	/// @brief デフォルトコンストラクタ
	Renderer3D() noexcept = default;

	/// @brief 描画統計情報を取得する
	/// @return 直前フレームのドローコール数
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 初期化済みかどうかを取得する
	/// @return GPUリソースが構築済みならtrue
	[[nodiscard]] bool isInitialized() const noexcept
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

#ifdef _WIN32

	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief レンダラーを初期化する
	/// @param device DX11デバイスへのポインタ
	/// @param cfg 設定パラメータ
	/// @details シェーダーコンパイル・入力レイアウト・定数バッファ・
	///          深度バッファ・ラスタライザステートを構築する。
	void initialize(gfx::Dx11Device* device, const Renderer3DConfig& cfg = {})
	{
		if (!device)
		{
			return;
		}

		m_device = device;
		m_config = cfg;
		m_d3dDevice = device->getD3DDevice();
		m_d3dContext = device->getD3DContext();

		compileShaders();
		createInputLayout();
		createConstantBuffers();
		createDepthBuffer();
		createRasterizerState();
		createDepthStencilState();

		m_initialized = true;
	}

	/// @brief フレーム描画を開始する
	/// @param clearColor 画面クリア色
	void beginFrame(const sgc::Colorf& clearColor)
	{
		if (!m_initialized)
		{
			return;
		}

		m_drawCallCount = 0;

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
	void setCamera(const Camera3D& camera)
	{
		m_viewMatrix = camera.viewMatrix();
		m_projMatrix = camera.projectionMatrix();
		m_cameraPosition = camera.position();
	}

	/// @brief ディレクショナルライトを設定する
	/// @param light ライトデータ
	void setLight(const Light& light)
	{
		m_light = light;
	}

	/// @brief メッシュを描画する
	/// @param mesh 描画するメッシュ
	/// @param worldTransform ワールド変換行列
	/// @param material マテリアル
	void drawMesh(const Mesh& mesh,
	              const sgc::Mat4f& worldTransform,
	              const Material& material)
	{
		if (!m_initialized || mesh.vertexCount() == 0)
		{
			return;
		}

		/// トランスフォーム定数バッファを更新する
		updateTransformCB(worldTransform);

		/// ライティング定数バッファを更新する
		updateLightingCB(material);

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
	}

	/// @brief フレーム描画を終了する
	void endFrame()
	{
		/// 何もしない（presentはデバイス側で行う）
	}

private:
	/// @brief HLSLシェーダーをコンパイルする
	void compileShaders()
	{
		/// 頂点シェーダーをコンパイルする
		auto vsBlob = compileHLSL(
			DEFAULT_VS_3D, "VSMain", "vs_5_0");

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
			DEFAULT_PS_3D, "PSMain", "ps_5_0");

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

	/// @brief Mat4fの内容をfloat[4][4]にコピーする
	/// @param dst コピー先
	/// @param src コピー元行列
	static void copyMatrix(float dst[4][4], const sgc::Mat4f& src) noexcept
	{
		std::memcpy(dst, src.m, sizeof(float) * 16);
	}

	/// @brief トランスフォーム定数バッファを更新する
	/// @param worldTransform ワールド行列
	void updateTransformCB(const sgc::Mat4f& worldTransform)
	{
		CbTransform cb;
		copyMatrix(cb.world, worldTransform);
		copyMatrix(cb.view, m_viewMatrix);
		copyMatrix(cb.projection, m_projMatrix);

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

		cb.ambientColor[0] = m_config.defaultAmbient.r;
		cb.ambientColor[1] = m_config.defaultAmbient.g;
		cb.ambientColor[2] = m_config.defaultAmbient.b;
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

	/// @brief 深度ステンシルビュー
	ComPtr<ID3D11DepthStencilView> m_depthStencilView;
	/// @brief 深度ステンシルステート
	ComPtr<ID3D11DepthStencilState> m_depthStencilState;
	/// @brief ラスタライザステート
	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	/// @brief ブレンドステート
	ComPtr<ID3D11BlendState> m_blendState;

	/// @brief 描画状態
	RenderState3D m_renderState;
	/// @brief ラスタライザ再作成フラグ
	bool m_rasterizerDirty = false;

	/// @brief ビュー行列
	sgc::Mat4f m_viewMatrix;
	/// @brief 射影行列
	sgc::Mat4f m_projMatrix;
	/// @brief カメラ位置
	sgc::Vec3f m_cameraPosition;
	/// @brief アクティブライト
	Light m_light;

	/// @brief 設定
	Renderer3DConfig m_config;
	/// @brief 初期化済みフラグ
	bool m_initialized = false;
	/// @brief ドローコール数
	int m_drawCallCount = 0;

#endif // _WIN32
};

} // namespace mitiru::render
