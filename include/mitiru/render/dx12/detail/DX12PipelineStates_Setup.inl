// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される

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
	m_graphicsCmdList->SetName(dx12::kRenderer3DListName);
	dx12::registerPass3DOrder();

	/// 初期状態はクローズしておく（beginFrame でリセットする）
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
	// Toon / Phong / PBR は材質のマップと局所光を共有する (DX12LitShaders.hpp)
	m_toonPS = gfx::Dx12Shader::createPixelShader(dx12LitPixelShader(LitShade::Toon), "PSMain");
	m_phongPS = gfx::Dx12Shader::createPixelShader(dx12LitPixelShader(LitShade::Phong), "PSMain");
	m_pbrPS = gfx::Dx12Shader::createPixelShader(dx12LitPixelShader(LitShade::Pbr), "PSMain");
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

	// FXAA ポストプロセス PS (ENG-104)。VS は OUTLINE_POST_VS を流用
	m_fxaaPS = gfx::Dx12Shader::createPixelShader(DX12_FXAA_PS_3D, "PSMain");

	// Tonemap (ENG-106)。HDR FP16 → backbuffer LDR
	m_tonemapVS = gfx::Dx12Shader::createVertexShader(DX12_TONEMAP_VS, "VSMain");
	m_tonemapPS = gfx::Dx12Shader::createPixelShader(DX12_TONEMAP_PS, "PSMain");

	// SSAO (v40)。VS は OUTLINE_POST_VS を流用。cbuffer/入力の宣言は 2 本で共通
	m_ssaoPS = gfx::Dx12Shader::createPixelShader(
		std::string(DX12_SSAO_COMMON_HLSL) + DX12_SSAO_PS_BODY, "PSMain");
	m_ssaoBlurPS = gfx::Dx12Shader::createPixelShader(
		std::string(DX12_SSAO_COMMON_HLSL) + DX12_SSAO_BLUR_PS_BODY, "PSMain");

	// bloom (v41)。VS と root sig は tonemap のものを流用
	m_bloomDownPS = gfx::Dx12Shader::createPixelShader(
		std::string(DX12_BLOOM_COMMON_HLSL) + DX12_BLOOM_DOWN_PS_BODY, "PSMain");
	m_bloomUpPS = gfx::Dx12Shader::createPixelShader(
		std::string(DX12_BLOOM_COMMON_HLSL) + DX12_BLOOM_UP_PS_BODY, "PSMain");

	// 被写界深度 (v44)。VS と root sig は tonemap のものを流用
	m_dofPS = gfx::Dx12Shader::createPixelShader(DX12_DOF_PS, "PSMain");

	// ShaderMode 別 PS（MRT 互換、b1 = CbLighting）
	m_unlitPS = gfx::Dx12Shader::createPixelShader(
		DX12_UNLIT_PS_3D, "PSMain");
	m_flatPS = gfx::Dx12Shader::createPixelShader(
		DX12_FLAT_PS_3D, "PSMain");
}

// ─────────────────────────────────────────────────────────────
//  ルートシグネチャ
// ─────────────────────────────────────────────────────────────

/// @brief メインのルートシグネチャを生成する
/// @details 引数とレジスタ (DX12LitShaders.hpp の PS がこの割り当てで読む):
///   - 0 b0 CbTransform (VS)、1 b1 CbLighting (全段)、2 b2 CbDrawEx (PS)、3 b3 CbShadow (全段)
///   - 4 材質の表 { t0 基本色, t3 法線, t4 金属・粗さ, t5 自発光 } (PS、描画ごと)
///   - 5 場面の表 { t1 影 (カスケード 0), t2 影 (カスケード 1/2 のアトラス), t8 irradiance, t9 prefiltered, t10 BRDF 表,
///     t11 スポットの影のアトラス, t35 デカール, t36 デカールの froxel のビット集合, t37/t38 VFX テクスチャ (色/データ) }
///     (PS、フレームに 1 枚)
///   - 6 b4 CbCluster、7 t6 局所光、8 t7 froxel のビット集合 (PS、root descriptor)
///   静的サンプラ s0 異方性 + repeat / s1 影の比較 / s2 最近傍 + repeat / s3 線形 + clamp (IBL)
void createRootSignature()
{
	D3D12_ROOT_PARAMETER rootParams[9] = {};
	const auto cbv = [&](int i, UINT reg, D3D12_SHADER_VISIBILITY vis) {
		rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[i].Descriptor.ShaderRegister = reg;
		rootParams[i].ShaderVisibility = vis;
	};
	cbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	cbv(1, 1, D3D12_SHADER_VISIBILITY_ALL);
	cbv(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
	cbv(3, 3, D3D12_SHADER_VISIBILITY_ALL);
	cbv(6, 4, D3D12_SHADER_VISIBILITY_PIXEL);

	// 材質の表 { t0, t3..t5 } と場面の表 { t1..t2, t8..t11, t35..t38 }
	static D3D12_DESCRIPTOR_RANGE materialRanges[2] = {};
	materialRanges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
	materialRanges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 3, 0, 1};
	static D3D12_DESCRIPTOR_RANGE sceneRanges[3] = {};
	sceneRanges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0};
	sceneRanges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 8, 0, 2};
	sceneRanges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 35, 0, 6};
	rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[4].DescriptorTable = {2, materialRanges};
	rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[5].DescriptorTable = {3, sceneRanges};
	rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	for (int i = 7; i <= 8; ++i)
	{
		rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[i].Descriptor.ShaderRegister = static_cast<UINT>(i - 1);
		rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	/// s0 が異方性なのは、寝た面 (床・台) が三線形だと片方向だけ過剰にぼけ、残った方向の
	/// 細かい階調が段の境で点々になるため。倍率は 4 (16 は差が見えず帯域を使うだけ)。
	/// s2 は glTF が NEAREST を宣言した資産用。ドット絵を線形補間でぼかさない。
	D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
	const auto sampler = [&](int i, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address) {
		samplers[i].Filter           = filter;
		samplers[i].AddressU         = address;
		samplers[i].AddressV         = address;
		samplers[i].AddressW         = address;
		samplers[i].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
		samplers[i].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samplers[i].MaxLOD           = D3D12_FLOAT32_MAX;
		samplers[i].ShaderRegister   = static_cast<UINT>(i);
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	};
	sampler(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	samplers[0].MaxAnisotropy = 4;
	sampler(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplers[1].BorderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	sampler(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	sampler(3, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters     = 9;
	rootSigDesc.pParameters       = rootParams;
	rootSigDesc.NumStaticSamplers = 4;
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

/// @brief Vertex3D 用の入力レイアウトを取得する（内部用）
/// @param desc 出力先の配列（4 要素）
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
