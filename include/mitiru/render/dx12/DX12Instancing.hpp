// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// GPU instancing (drawMeshInstanced 用)。DX11 の per-instance vertex buffer 方式と
// 同じ設計: 4x float4 (ワールド行列の行) を slot1 に per-instance で渡す。
// PSInput は DX12_DEFAULT_VS_3D と同一形状のため、Toon / MultiLight の既存 PS を
// そのまま再利用できる（VS は 1 種類、PSO は Toon・MultiLight の 2 種類のみ。
// DX11 版と同じ機能範囲で、ShaderMode / doubleSided の instanced 版は無い）。

/// @brief 1 インスタンス分のワールド行列（`float4x4(row0..row3)` として VS 側で再構成する）
struct InstanceDataDx12
{
	float row0[4];
	float row1[4];
	float row2[4];
	float row3[4];
};

/// @brief 1 回の instanced draw でまとめられるインスタンス数の上限
static constexpr std::size_t kInstanceBatchMaxDx12 = 1024;

/// @brief instanced 用 VS。DX12_DEFAULT_VS_3D と PSInput 形状を揃えることで
///        既存の Toon / MultiLight PS をそのまま bind できる。
static constexpr const char* kInstancedVS3DDx12 = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;      // instancing 経路では未使用 (View/Projection のみ使う)
    float4x4 View;
    float4x4 Projection;
};

cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
    float4 InstRow0 : TEXCOORD3;
    float4 InstRow1 : TEXCOORD4;
    float4 InstRow2 : TEXCOORD5;
    float4 InstRow3 : TEXCOORD6;
};

struct VSOutput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4x4 instWorld = float4x4(input.InstRow0, input.InstRow1, input.InstRow2, input.InstRow3);

    float4 worldPos = mul(instWorld, float4(input.Position, 1.0));
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul((float3x3)instWorld, input.Normal));

    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);

    output.LightSpacePos = mul(LightViewProj, worldPos);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief instanced 用の入力レイアウト（slot0=頂点、slot1=インスタンス行列 4 行）を返す
static void getInstancedInputLayoutDx12(std::array<D3D12_INPUT_ELEMENT_DESC, 8>& desc)
{
	UINT vertexCount = 0;
	D3D12_INPUT_ELEMENT_DESC vertexLayout[4] = {};
	getInputLayoutInternal(vertexLayout, vertexCount);
	for (UINT i = 0; i < vertexCount; ++i) { desc[i] = vertexLayout[i]; }

	static const char* kNames[4] = {"TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD"};
	for (UINT i = 0; i < 4; ++i)
	{
		D3D12_INPUT_ELEMENT_DESC& e = desc[vertexCount + i];
		e.SemanticName = kNames[i];
		e.SemanticIndex = 3 + i;   // TEXCOORD3..6
		e.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		e.InputSlot = 1;
		e.AlignedByteOffset = i * sizeof(float) * 4;
		e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
		e.InstanceDataStepRate = 1;
	}
}

/// @brief world 行列を instanced VS の per-instance 属性へ変換する（column-major で渡す）
[[nodiscard]] static InstanceDataDx12 toInstanceDataDx12(const sgc::Mat4f& world) noexcept
{
	float colMajor[4][4];
	toColumnMajor(colMajor, toGlm(world));
	InstanceDataDx12 out;
	std::memcpy(out.row0, colMajor[0], sizeof(out.row0));
	std::memcpy(out.row1, colMajor[1], sizeof(out.row1));
	std::memcpy(out.row2, colMajor[2], sizeof(out.row2));
	std::memcpy(out.row3, colMajor[3], sizeof(out.row3));
	return out;
}

/// @brief instanced 用 VS/PSO を（未生成なら）遅延生成する
/// @details 失敗したら m_instancedPipelineFailedDx12 を立ててループ描画へ
///          恒久的にフォールバックする（毎フレーム再試行しない）。
void ensureInstancedPipelineDx12()
{
	if (m_instancedPSODx12 || m_instancedPipelineFailedDx12) { return; }
	if (!m_toonPS || !m_rootSignature)
	{
		m_instancedPipelineFailedDx12 = true;
		return;
	}

	try
	{
		m_instancedVSDx12 = gfx::Dx12Shader::createVertexShader(kInstancedVS3DDx12, "VSMain");
	}
	catch (const std::exception&)
	{
		m_instancedPipelineFailedDx12 = true;
		return;
	}

	std::array<D3D12_INPUT_ELEMENT_DESC, 8> layout{};
	getInstancedInputLayoutDx12(layout);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = m_instancedVSDx12->shaderBytecode();
	psoDesc.PS = m_toonPS->shaderBytecode();
	psoDesc.InputLayout.pInputElementDescs = layout.data();
	psoDesc.InputLayout.NumElements = static_cast<UINT>(layout.size());

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	setNormalTargetOpaque(psoDesc.BlendState);

	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 2;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_instancedPSODx12.GetAddressOf()))))
	{
		m_instancedPipelineFailedDx12 = true;
		return;
	}

	if (m_multiLightPS)
	{
		psoDesc.PS = m_multiLightPS->shaderBytecode();
		m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_instancedMultiLightPSODx12.GetAddressOf()));
	}
}

/// @brief インスタンスバッチ 1 個分を upload ring から切り出して描画する
void drawInstanceBatchDx12(ID3D12Resource* vb, ID3D12Resource* ib, UINT vbSize,
                           UINT vertexCount, UINT indexCount)
{
	const UINT instanceBytes =
		static_cast<UINT>(m_instanceScratchDx12.size() * sizeof(InstanceDataDx12));
	const auto instAlloc = m_uploadRing.upload(m_instanceScratchDx12.data(), instanceBytes, 256);
	if (!instAlloc.valid()) { return; }

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vb->GetGPUVirtualAddress();
	vbv.SizeInBytes = vbSize;
	vbv.StrideInBytes = sizeof(Vertex3D);

	D3D12_VERTEX_BUFFER_VIEW instVbv = {};
	instVbv.BufferLocation = instAlloc.gpuAddr;
	instVbv.SizeInBytes = instanceBytes;
	instVbv.StrideInBytes = sizeof(InstanceDataDx12);

	D3D12_VERTEX_BUFFER_VIEW views[2] = {vbv, instVbv};
	m_graphicsCmdList->IASetVertexBuffers(0, 2, views);

	const auto instanceCount = static_cast<UINT>(m_instanceScratchDx12.size());
	if (ib)
	{
		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = ib->GetGPUVirtualAddress();
		ibv.SizeInBytes = indexCount * sizeof(uint32_t);
		ibv.Format = DXGI_FORMAT_R32_UINT;
		m_graphicsCmdList->IASetIndexBuffer(&ibv);
		m_graphicsCmdList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
	}
	else
	{
		m_graphicsCmdList->DrawInstanced(vertexCount, instanceCount, 0, 0);
	}
	++m_drawCallCount;
}

/// @brief 同一メッシュを複数のワールド行列で GPU instancing 描画する（DX12）
/// @details PSO/VS 生成に失敗した環境では drawMesh ループへフォールバックする。
///          視錐台カリングはインスタンス単位（バッチ内のみを instance buffer へ書く）。
void drawMeshInstancedDx12(const Mesh& mesh, std::span<const sgc::Mat4f> worlds,
                           const Material* material)
{
	if (!m_initialized || !m_graphicsCmdList || mesh.vertexCount() == 0 || worlds.empty())
	{
		return;
	}

	ensureInstancedPipelineDx12();
	const Material& mat = material ? *material : Material{};
	const bool useMulti = m_useMultiLight && !m_lights.empty() && m_instancedMultiLightPSODx12;
	auto* pso = useMulti ? m_instancedMultiLightPSODx12.Get() : m_instancedPSODx12.Get();
	if (!pso)
	{
		// instancing 未対応環境（shader compile / PSO 生成失敗）はループ描画へフォールバック
		for (const auto& world : worlds) { drawMesh(mesh, world, mat); }
		return;
	}

	if (m_skyboxEnabled && !m_skyboxDrawnThisFrame && m_skyboxCubemap.valid())
	{
		ensureSkyboxPipelineDx12();
		ensureSkyboxTextureDx12();
		drawSkyboxIfNeededDx12();
	}

	const auto cbTransformAddr = uploadTransformCB(sgc::Mat4f::identity());
	const auto cbLightingAddr = uploadLightingCB(mat);
	if (cbTransformAddr == 0 || cbLightingAddr == 0) { return; }

	D3D12_GPU_VIRTUAL_ADDRESS cbLightArrayAddr = 0;
	if (useMulti)
	{
		cbLightArrayAddr = uploadLightArrayCB();
		if (cbLightArrayAddr == 0) { return; }
	}

	const auto& verts = mesh.vertices();
	const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex3D));
	auto* vb = acquireMeshBuffer(m_meshVBCache, mesh, verts.data(), vbSize);
	if (!vb) { return; }

	const auto& indices = mesh.indices();
	ID3D12Resource* ib = nullptr;
	if (!indices.empty())
	{
		const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));
		ib = acquireMeshBuffer(m_meshIBCache, mesh, indices.data(), ibSize);
		if (!ib) { return; }
	}

	m_graphicsCmdList->SetPipelineState(pso);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbTransformAddr);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbLightingAddr);
	if (useMulti) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(2, cbLightArrayAddr); }
	const auto cbShadowAddr = uploadShadowCB();
	if (cbShadowAddr != 0) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(3, cbShadowAddr); }

	ID3D12DescriptorHeap* heaps[] = {m_albedoSrvHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	const auto srvGpu = writeMainSrvTable(mat.albedoTexture);
	if (srvGpu.ptr != 0) { m_graphicsCmdList->SetGraphicsRootDescriptorTable(4, srvGpu); }

	for (std::size_t offset = 0; offset < worlds.size(); offset += kInstanceBatchMaxDx12)
	{
		const std::size_t batchCount = std::min(kInstanceBatchMaxDx12, worlds.size() - offset);
		m_instanceScratchDx12.clear();
		for (std::size_t i = 0; i < batchCount; ++i)
		{
			const auto& world = worlds[offset + i];
			if (m_frustumCullingEnabled && !m_frustum.isMeshVisible(mesh.localAABB(), world))
			{
				++m_culledCount;
				continue;
			}
			if (m_occlusionCullingEnabled && m_occlusionCuller.hasDepth() &&
			    m_occlusionCuller.isOccluded(worldOcclusionAABB(mesh.localAABB(), world),
			                                 occlusionViewProj().data()))
			{
				++m_occludedCount;
				continue;
			}
			m_instanceScratchDx12.push_back(toInstanceDataDx12(world));
		}
		if (!m_instanceScratchDx12.empty())
		{
			drawInstanceBatchDx12(vb, ib, vbSize, static_cast<UINT>(verts.size()),
			                      static_cast<UINT>(indices.size()));
		}
	}

	// 次の drawMesh のためにメイン PSO/root sig へ復元する
	restoreMainState();
}
