// Class-body chunk for Renderer3D_DX12 - included via DX12PipelineStates.hpp

// ─────────────────────────────────────────────────────────────
//  コマンドリソース生成
// ─────────────────────────────────────────────────────────────

void createCommandResources()
{
	/// フレーム毎のコマンドアロケータを生成する
	for (uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		HRESULT hr = m_d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_commandAllocators[i].GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Renderer3D_DX12: CreateCommandAllocator failed");
		}
	}

	/// グラフィクスコマンドリストを生成する
	HRESULT hr = m_d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocators[0].Get(),
		nullptr,
		IID_PPV_ARGS(m_graphicsCmdList.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateCommandList failed");
	}

	/// 初期状態はクローズしておく（beginFrameでリセットする）
	m_graphicsCmdList->Close();
}

// ─────────────────────────────────────────────────────────────
//  シェーダーコンパイル
// ─────────────────────────────────────────────────────────────

/// @brief シェーダーをコンパイルする
void compileShaders()
{
	// DX12 メインパスは LightSpacePos を出力する独自 VS を使う
	// (shadow サンプル用; CbShadow b3 を読む)
	m_toonVS = gfx::Dx12Shader::createVertexShader(DX12_DEFAULT_VS_3D, "VSMain");
	// DX12 メインパスは MRT + t0 albedo を扱う独自 Toon PS を使う
	m_toonPS = gfx::Dx12Shader::createPixelShader(DX12_TOON_PS_3D, "PSMain");
	m_outlinePostVS = gfx::Dx12Shader::createVertexShader(OUTLINE_POST_VS, "VSMain");
	m_outlinePostPS = gfx::Dx12Shader::createPixelShader(OUTLINE_POST_PS, "PSMain");

	// アウトラインモード用の追加シェーダー
	m_outlinePostPS_Laplacian = gfx::Dx12Shader::createPixelShader(
		OUTLINE_POST_PS_LAPLACIAN, "PSMain");
	m_outlinePostPS_DepthNdotV = gfx::Dx12Shader::createPixelShader(
		OUTLINE_POST_PS_DEPTH_NDOTV, "PSMain");
	m_outlinePostPS_ColorEdge = gfx::Dx12Shader::createPixelShader(
		OUTLINE_POST_PS_COLOR_EDGE, "PSMain");
	m_outlinePostPS_DepthColor = gfx::Dx12Shader::createPixelShader(
		OUTLINE_POST_PS_DEPTH_COLOR, "PSMain");
	// Fresnel も DX12 VS の出力 signature (LightSpacePos 含む) に合わせた MRT 変種を使う
	m_fresnelToonPS = gfx::Dx12Shader::createPixelShader(
		DX12_TOON_PS_3D_FRESNEL, "PSMain");

	// FXAA ポストプロセス PS (ENG-104) — VS は OUTLINE_POST_VS を流用
	m_fxaaPS = gfx::Dx12Shader::createPixelShader(DX12_FXAA_PS_3D, "PSMain");

	// Tonemap (ENG-106) — HDR FP16 → backbuffer LDR
	m_tonemapVS = gfx::Dx12Shader::createVertexShader(DX12_TONEMAP_VS, "VSMain");
	m_tonemapPS = gfx::Dx12Shader::createPixelShader(DX12_TONEMAP_PS, "PSMain");

	// マルチライト Phong PS（VS は TOON_VS_3D を流用）
	m_multiLightPS = gfx::Dx12Shader::createPixelShader(
		DX12_MULTI_LIGHT_PS_3D, "PSMain");

	// ShaderMode 別 PS（MRT 互換、b1 = CbLighting）
	m_phongPS = gfx::Dx12Shader::createPixelShader(
		DX12_PHONG_PS_3D, "PSMain");
	m_unlitPS = gfx::Dx12Shader::createPixelShader(
		DX12_UNLIT_PS_3D, "PSMain");
	m_flatPS = gfx::Dx12Shader::createPixelShader(
		DX12_FLAT_PS_3D, "PSMain");

	// 2Dオーバーレイ用シェーダー
	m_overlay2DVS = gfx::Dx12Shader::createVertexShader(OVERLAY2D_VS, "VSMain");
	m_overlay2DPS = gfx::Dx12Shader::createPixelShader(OVERLAY2D_PS, "PSMain");
}

// ─────────────────────────────────────────────────────────────
//  ルートシグネチャ
// ─────────────────────────────────────────────────────────────

/// @brief ルートシグネチャを生成する
/// @details 5 パラメータ:
///   - b0: CbTransform（VS）
///   - b1: CbLighting（VS/PS 共通）
///   - b2: CbLightArray（マルチライト PS。それ以外は参照しないだけで OK）
///   - b3: CbShadow（light view*proj, PS）
///   - SRV table { t0=albedo, t1=shadow }（PS）
///   静的サンプラ s0: linear + repeat / s1: comparison(less)（PS）
void createRootSignature()
{
	D3D12_ROOT_PARAMETER rootParams[5] = {};

	/// b0: CbTransform -- 頂点シェーダーで使用
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	/// b1: CbLighting -- VS/PSの両方で使用するためALL
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[1].Descriptor.ShaderRegister = 1;
	rootParams[1].Descriptor.RegisterSpace = 0;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	/// b2: CbLightArray -- マルチライトパスのPSで使用
	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[2].Descriptor.ShaderRegister = 2;
	rootParams[2].Descriptor.RegisterSpace = 0;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	/// b3: CbShadow (lightViewProj) -- VS (lightSpacePos 計算) + PS (PCF サンプル)
	rootParams[3].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[3].Descriptor.ShaderRegister = 3;
	rootParams[3].Descriptor.RegisterSpace  = 0;
	rootParams[3].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

	/// SRV table: { t0=albedo, t1=shadow }
	static D3D12_DESCRIPTOR_RANGE srvRanges[2] = {};
	srvRanges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRanges[0].NumDescriptors                    = 1;
	srvRanges[0].BaseShaderRegister                = 0; // t0
	srvRanges[0].RegisterSpace                     = 0;
	srvRanges[0].OffsetInDescriptorsFromTableStart = 0;
	srvRanges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRanges[1].NumDescriptors                    = 1;
	srvRanges[1].BaseShaderRegister                = 1; // t1
	srvRanges[1].RegisterSpace                     = 0;
	srvRanges[1].OffsetInDescriptorsFromTableStart = 1;
	rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[4].DescriptorTable.NumDescriptorRanges = 2;
	rootParams[4].DescriptorTable.pDescriptorRanges   = srvRanges;
	rootParams[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	/// s0: linear + repeat / s1: comparison(less) for PCF
	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	// s0
	samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[0].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	samplers[0].MinLOD           = 0.0f;
	samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
	samplers[0].ShaderRegister   = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// s1 — PCF 比較サンプラ
	samplers[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	samplers[1].MinLOD           = 0.0f;
	samplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
	samplers[1].ShaderRegister   = 1;
	samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters     = 5;
	rootSigDesc.pParameters       = rootParams;
	rootSigDesc.NumStaticSamplers = 2;
	rootSigDesc.pStaticSamplers   = samplers;
	rootSigDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr))
	{
		std::string msg = "Renderer3D_DX12: SerializeRootSignature failed";
		if (errorBlob)
		{
			msg += ": ";
			msg += static_cast<const char*>(errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(msg);
	}

	hr = m_d3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateRootSignature failed");
	}
}

// ─────────────────────────────────────────────────────────────
//  入力レイアウト
// ─────────────────────────────────────────────────────────────

/// @brief Vertex3D用の入力レイアウトを取得する（内部用）
/// @param desc 出力先の配列（4要素）
/// @param count 出力先の要素数
static void getInputLayoutInternal(D3D12_INPUT_ELEMENT_DESC* desc, UINT& count)
{
	count = 4;

	/// POSITION: float3 (offset 0)
	desc[0].SemanticName = "POSITION";
	desc[0].SemanticIndex = 0;
	desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	desc[0].InputSlot = 0;
	desc[0].AlignedByteOffset = 0;
	desc[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	desc[0].InstanceDataStepRate = 0;

	/// NORMAL: float3 (offset 12)
	desc[1].SemanticName = "NORMAL";
	desc[1].SemanticIndex = 0;
	desc[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	desc[1].InputSlot = 0;
	desc[1].AlignedByteOffset = 12;
	desc[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	desc[1].InstanceDataStepRate = 0;

	/// TEXCOORD: float2 (offset 24)
	desc[2].SemanticName = "TEXCOORD";
	desc[2].SemanticIndex = 0;
	desc[2].Format = DXGI_FORMAT_R32G32_FLOAT;
	desc[2].InputSlot = 0;
	desc[2].AlignedByteOffset = 24;
	desc[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	desc[2].InstanceDataStepRate = 0;

	/// COLOR: float4 (offset 32)
	desc[3].SemanticName = "COLOR";
	desc[3].SemanticIndex = 0;
	desc[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc[3].InputSlot = 0;
	desc[3].AlignedByteOffset = 32;
	desc[3].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	desc[3].InstanceDataStepRate = 0;
}
