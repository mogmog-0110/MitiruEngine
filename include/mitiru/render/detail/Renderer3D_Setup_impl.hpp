#pragma once

/// @file Renderer3D_Setup_impl.hpp
/// @brief Renderer3D の初期化・GPU リソース構築の実装本体（Renderer3D.hpp から機械的分割）

#include <mitiru/render/Renderer3D.hpp>

#ifdef _WIN32

namespace mitiru::render
{

/// @brief オクルージョン min-depth resolve 用のフルスクリーン三角形頂点シェーダー
/// @details `DefaultShaders3D.hpp` の `OCCLUSION_MIN_DEPTH_RESOLVE_PS_3D` 専用。
///          頂点バッファ不要 (`SV_VertexID` から3頂点を合成)。
constexpr const char* kOcclusionResolveVS_3D = R"hlsl(
struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
)hlsl";

/// @brief レンダラーを初期化する
/// @param device DX11デバイスへのポインタ
/// @param cfg 設定パラメータ
/// @param mode シェーダーモード（初期化時に直接設定。setShaderModeの再コンパイルを回避）
inline void Renderer3D::initialize(gfx::Dx11Device* device,
	const Renderer3DConfig& cfg,
	ShaderMode3D mode)
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

/// @brief HLSLシェーダーをコンパイルする
inline void Renderer3D::compileShaders()
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
inline void Renderer3D::recompileShaders()
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
inline Renderer3D::ComPtr<ID3DBlob> Renderer3D::compileHLSL(
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
inline void Renderer3D::createInputLayout()
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
inline void Renderer3D::createConstantBuffers()
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

/// @brief 深度バッファを作成する
inline void Renderer3D::createDepthBuffer()
{
	if (!m_config.enableDepthBuffer)
	{
		return;
	}

	// バックバッファの MSAA サンプル数に合わせる。OMSetRenderTargets は color/depth の
	// サンプル数が一致しないと深度が書き込まれない (実機で深度テストが無効化される)。
	UINT sampleCount = 1;
	UINT sampleQuality = 0;
	if (auto* swapChain = m_device->getSwapChain())
	{
		sampleCount = swapChain->sampleCount();
		sampleQuality = swapChain->sampleQuality();
	}
	m_depthIsMultisampled = sampleCount > 1;

	// R24G8_TYPELESS にすることで DSV (D24_UNORM_S8_UINT) と SRV
	// (R24_UNORM_X8_TYPELESS) を同じリソースから両方作れる。MSAA 深度を
	// min-depth resolve パス (updateOcclusionDepth) がピクセルシェーダーで
	// 読むために SRV が要る。非 MSAA 時は SRV を作らない（CopyResource で
	// staging へ直接読み戻す既存経路のみで足りる）。
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = static_cast<UINT>(m_config.viewportWidth);
	texDesc.Height = static_cast<UINT>(m_config.viewportHeight);
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	texDesc.SampleDesc.Count = sampleCount;
	texDesc.SampleDesc.Quality = sampleQuality;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (m_depthIsMultisampled)
	{
		texDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	}

	ComPtr<ID3D11Texture2D> depthTex;
	HRESULT hr = m_d3dDevice->CreateTexture2D(
		&texDesc, nullptr, depthTex.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D: CreateTexture2D(depth) failed");
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = m_depthIsMultisampled
		? D3D11_DSV_DIMENSION_TEXTURE2DMS
		: D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = m_d3dDevice->CreateDepthStencilView(
		depthTex.Get(), &dsvDesc,
		m_depthStencilView.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D: CreateDepthStencilView failed");
	}

	m_depthSRV.Reset();
	if (m_depthIsMultisampled)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		hr = m_d3dDevice->CreateShaderResourceView(
			depthTex.Get(), &srvDesc, m_depthSRV.GetAddressOf());
		if (FAILED(hr))
		{
			// SRV が作れない環境は resolve パスを諦める
			// (updateOcclusionDepth が warnOnce 付きでスキップする)
			m_depthSRV.Reset();
		}
	}
}

/// @brief ラスタライザステートを作成する
inline void Renderer3D::createRasterizerState()
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
inline void Renderer3D::createDepthStencilState()
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

/// @brief デフォルトの1x1白テクスチャを作成する
inline void Renderer3D::createDefaultWhiteTexture()
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
inline void Renderer3D::createSamplerState()
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

/// @brief MSAA 深度の min-depth resolve 用 VS/PS を（未生成なら）コンパイルする
inline void Renderer3D::ensureOcclusionResolvePipeline()
{
	if (m_occlusionResolveVS || m_occlusionResolvePipelineFailed)
	{
		return;
	}

	try
	{
		auto vsBlob = compileHLSL(kOcclusionResolveVS_3D, "VSMain", "vs_5_0");
		auto psBlob = compileHLSL(OCCLUSION_MIN_DEPTH_RESOLVE_PS_3D, "PSMain", "ps_5_0");

		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		HRESULT hr = m_d3dDevice->CreateVertexShader(
			vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.GetAddressOf());
		if (SUCCEEDED(hr))
		{
			hr = m_d3dDevice->CreatePixelShader(
				psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.GetAddressOf());
		}
		if (FAILED(hr))
		{
			m_occlusionResolvePipelineFailed = true;
			return;
		}
		m_occlusionResolveVS = vs;
		m_occlusionResolvePS = ps;
	}
	catch (const std::exception&)
	{
		m_occlusionResolvePipelineFailed = true;
	}
}

/// @brief オクルージョン resolve 用の単一サンプル R32_FLOAT RT + staging を
///        (未生成、または深度と違うサイズなら) 生成し直す
inline bool Renderer3D::ensureOcclusionResolveTargets(UINT width, UINT height)
{
	if (m_occlusionResolveRT && m_occlusionResolveW == width && m_occlusionResolveH == height)
	{
		return true;
	}

	D3D11_TEXTURE2D_DESC rtDesc = {};
	rtDesc.Width = width;
	rtDesc.Height = height;
	rtDesc.MipLevels = 1;
	rtDesc.ArraySize = 1;
	rtDesc.Format = DXGI_FORMAT_R32_FLOAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Usage = D3D11_USAGE_DEFAULT;
	rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

	ComPtr<ID3D11Texture2D> rtTex;
	if (FAILED(m_d3dDevice->CreateTexture2D(&rtDesc, nullptr, rtTex.GetAddressOf())))
	{
		return false;
	}

	ComPtr<ID3D11RenderTargetView> rtv;
	if (FAILED(m_d3dDevice->CreateRenderTargetView(rtTex.Get(), nullptr, rtv.GetAddressOf())))
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = rtDesc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ComPtr<ID3D11Texture2D> stagingTex;
	if (FAILED(m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, stagingTex.GetAddressOf())))
	{
		return false;
	}

	m_occlusionResolveRT = rtTex;
	m_occlusionResolveRTV = rtv;
	m_occlusionResolveStaging = stagingTex;
	m_occlusionResolveW = width;
	m_occlusionResolveH = height;
	return true;
}

} // namespace mitiru::render

#endif // _WIN32
