// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// GPU instancing。1 インスタンス = 行列 4 行 + 色 (80 byte) を slot1 に per-instance で流す。
// VS の出力はメインの VS と同形なので PS はメインと同じものを使い、陰影の種類と両面の別ごとに PSO を持つ。
// 影も落とす: 影のパスは前フレームの caster を描くので、インスタンスの行列も 1 フレーム持ち越す。

/// @brief 1 インスタンス分 (行列の 4 行と色)
struct InstanceDataDx12
{
	float row0[4];
	float row1[4];
	float row2[4];
	float row3[4];
	float tint[4];
};

/// @brief 1 回の instanced draw でまとめられるインスタンス数の上限
static constexpr std::size_t kInstanceBatchMaxDx12 = 1024;

/// @brief instanced 用 VS。出力はメインの VS (DX12_DEFAULT_VS_3D) と同形で、頂点色にインスタンスの色を掛ける
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
    float4 InstTint : TEXCOORD7;
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

// 頂点色は書いた sRGB (ColorSpace.hpp の linearRgb と同じ: 1 を超える色は 1 に収めて線形にし、倍率を戻す)
float3 srgbToLinear(float3 c)
{
    float m = max(max(c.r, c.g), max(c.b, 1.0));
    c /= m;
    return ((c <= 0.04045) ? c / 12.92 : pow((max(c, 0.04045) + 0.055) / 1.055, 2.4)) * m;
}
float4 srgbToLinear(float4 c) { return float4(srgbToLinear(c.rgb), c.a); }

// DX12_DEFAULT_VS_3D と同じ: 非一様スケールでも面に垂直なまま運ぶ余因子行列
float3 transformNormal(float3x3 m, float3 n)
{
    float3x3 cof = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    float det = dot(m[0], cof[0]);
    return normalize(mul(cof, n) * (det < 0.0 ? -1.0 : 1.0));
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4x4 instWorld = float4x4(input.InstRow0, input.InstRow1, input.InstRow2, input.InstRow3);
    float4 worldPos = mul(instWorld, float4(input.Position, 1.0));
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = transformNormal((float3x3)instWorld, input.Normal);
    output.Position = mul(Projection, mul(View, worldPos));
    output.LightSpacePos = mul(LightViewProj, worldPos);
    output.TexCoord = input.TexCoord;
    output.Color = srgbToLinear(input.Color) * srgbToLinear(input.InstTint);
    return output;
}
)hlsl";

/// @brief インスタンス描画の PSO の陰影の種類 (selectMainPSO と同じ並び)
enum InstancedShadeDx12 : int { InstToon = 0, InstPhong, InstUnlit, InstFlat, InstPbr, kInstancedShadeCount };

std::optional<gfx::Dx12Shader> m_instancedVSDx12;
ComPtr<ID3D12PipelineState>    m_instancedPso[kInstancedShadeCount][2];   ///< [陰影][0 = 片面, 1 = 両面]
ComPtr<ID3D12PipelineState>    m_instancedShadowPso[3];                   ///< [0 = 片面, 1 = 両面, 2 = スポット (透視)]
bool                           m_instancedPipelineFailedDx12 = false;
std::vector<InstanceDataDx12>  m_instanceScratchDx12;
CullAABB                       m_instanceBoundsDx12{};   ///< m_instanceScratchDx12 の全部を包むワールドの箱 (影の caster 用)
std::vector<MeshInstance>      m_instanceInputScratch;   ///< 行列だけの旧 API と glTF の節点行列の合成用
std::vector<InstanceDataDx12>  m_shadowInstances;        ///< このフレームに描いたインスタンス (影の caster)
std::vector<InstanceDataDx12>  m_shadowInstancesPrev;    ///< 影のパスが読む前フレーム分

/// @brief instanced 用の入力レイアウト（slot0=頂点、slot1=インスタンス行列 4 行 + 色）を返す
static void getInstancedInputLayoutDx12(std::array<D3D12_INPUT_ELEMENT_DESC, 9>& desc)
{
	UINT vertexCount = 0;
	D3D12_INPUT_ELEMENT_DESC vertexLayout[4] = {};
	getInputLayoutInternal(vertexLayout, vertexCount);
	for (UINT i = 0; i < vertexCount; ++i) { desc[i] = vertexLayout[i]; }
	for (UINT i = 0; i < 5; ++i)
	{
		D3D12_INPUT_ELEMENT_DESC& e = desc[vertexCount + i];
		e.SemanticName = "TEXCOORD";
		e.SemanticIndex = 3 + i;   // TEXCOORD3..7
		e.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		e.InputSlot = 1;
		e.AlignedByteOffset = i * sizeof(float) * 4;
		e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
		e.InstanceDataStepRate = 1;
	}
}

/// @brief MeshInstance を instanced VS の per-instance 属性へ。VS は 4 本を行として組むので、行優先の行列をそのまま渡す
[[nodiscard]] static InstanceDataDx12 toInstanceDataDx12(const MeshInstance& inst) noexcept
{
	InstanceDataDx12 out;
	std::memcpy(out.row0, inst.world + 0, sizeof(out.row0));
	std::memcpy(out.row1, inst.world + 4, sizeof(out.row1));
	std::memcpy(out.row2, inst.world + 8, sizeof(out.row2));
	std::memcpy(out.row3, inst.world + 12, sizeof(out.row3));
	std::memcpy(out.tint, inst.tint, sizeof(out.tint));
	return out;
}

/// @brief instanced 用 VS/PSO を（未生成なら）作る。失敗したら drawMeshEx の繰り返しへ恒久的に切り替える
void ensureInstancedPipelineDx12()
{
	if (m_instancedPso[InstToon][0] || m_instancedPipelineFailedDx12) { return; }
	try
	{
		m_instancedVSDx12 = gfx::Dx12Shader::createVertexShader(kInstancedVS3DDx12, "VSMain");
	}
	catch (const std::exception&)
	{
		m_instancedPipelineFailedDx12 = true;
		return;
	}
	std::array<D3D12_INPUT_ELEMENT_DESC, 9> layout{};
	getInstancedInputLayoutDx12(layout);
	const std::optional<gfx::Dx12Shader>* ps[kInstancedShadeCount] = {&m_toonPS, &m_phongPS, &m_unlitPS, &m_flatPS, &m_pbrPS};
	for (int cull = 0; cull < 2; ++cull)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		fillForwardPsoState(pd);
		pd.VS = m_instancedVSDx12->shaderBytecode();
		pd.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
		pd.RasterizerState.CullMode = (cull == 0) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
		for (int s = 0; s < kInstancedShadeCount; ++s)
		{
			if (!*ps[s]) { continue; }
			pd.PS = (*ps[s])->shaderBytecode();
			(void)m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_instancedPso[s][cull].ReleaseAndGetAddressOf()));
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC sd = {};
		fillShadowPsoState(sd);
		sd.VS = m_instancedVSDx12->shaderBytecode();
		sd.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
		sd.RasterizerState.CullMode = (cull == 0) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
		(void)m_d3dDevice->CreateGraphicsPipelineState(&sd, IID_PPV_ARGS(m_instancedShadowPso[cull].ReleaseAndGetAddressOf()));
		if (cull == 0)
		{
			setPerspectiveShadowBias(sd);
			(void)m_d3dDevice->CreateGraphicsPipelineState(&sd, IID_PPV_ARGS(m_instancedShadowPso[2].ReleaseAndGetAddressOf()));
		}
	}
	m_instancedPipelineFailedDx12 = !m_instancedPso[InstToon][0];
}

/// @brief 今の陰影の種類と両面の別に合う instanced PSO (無ければ Toon)
[[nodiscard]] ID3D12PipelineState* selectInstancedPso(bool doubleSided) const noexcept
{
	int shade = InstToon;
	switch (m_shaderMode)
	{
	case ShaderMode3D::Phong: shade = InstPhong; break;
	case ShaderMode3D::Unlit: shade = InstUnlit; break;
	case ShaderMode3D::Flat:  shade = InstFlat; break;
	case ShaderMode3D::PBR:   shade = InstPbr; break;
	default: break;
	}
	const int cull = doubleSided ? 1 : 0;
	if (m_instancedPso[shade][cull]) { return m_instancedPso[shade][cull].Get(); }
	return m_instancedPso[InstToon][cull] ? m_instancedPso[InstToon][cull].Get() : m_instancedPso[InstToon][0].Get();
}

/// @brief インスタンスバッチ 1 個分を upload ring から切り出して描画する
void drawInstanceBatchDx12(ID3D12Resource* vb, ID3D12Resource* ib, UINT vbSize, UINT vertexCount, UINT indexCount)
{
	const UINT instanceBytes = static_cast<UINT>(m_instanceScratchDx12.size() * sizeof(InstanceDataDx12));
	const auto instAlloc = m_uploadRing.upload(m_instanceScratchDx12.data(), instanceBytes, 256);
	if (!instAlloc.valid()) { return; }

	const D3D12_VERTEX_BUFFER_VIEW views[2] = {
		{vb->GetGPUVirtualAddress(), vbSize, sizeof(Vertex3D)},
		{instAlloc.gpuAddr, instanceBytes, sizeof(InstanceDataDx12)},
	};
	m_graphicsCmdList->IASetVertexBuffers(0, 2, views);

	const auto instanceCount = static_cast<UINT>(m_instanceScratchDx12.size());
	if (ib)
	{
		const D3D12_INDEX_BUFFER_VIEW ibv = {ib->GetGPUVirtualAddress(), indexCount * static_cast<UINT>(sizeof(uint32_t)),
		                                     DXGI_FORMAT_R32_UINT};
		m_graphicsCmdList->IASetIndexBuffer(&ibv);
		m_graphicsCmdList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
	}
	else
	{
		m_graphicsCmdList->DrawInstanced(vertexCount, instanceCount, 0, 0);
	}
	++m_drawCallCount;
}

/// @brief 描いたバッチを影の caster として残す (影のパスは次のフレームにこれを読む)
void recordShadowInstances(const Mesh& mesh)
{
	if (!wantsShadowCasters() || m_instanceScratchDx12.empty()) { return; }
	const auto first = static_cast<uint32_t>(m_shadowInstances.size());
	m_shadowInstances.insert(m_shadowInstances.end(), m_instanceScratchDx12.begin(), m_instanceScratchDx12.end());
	if (!addShadowCaster({&mesh, sgc::Mat4f::identity(), first, static_cast<uint32_t>(m_instanceScratchDx12.size()),
	                      m_instanceBoundsDx12}))
	{
		m_shadowInstances.resize(first);
	}
}

/// @brief 影のパスでインスタンスの caster を張る (PSO と slot1)。PSO が無ければ false
/// @param psoIndex m_instancedShadowPso の番号 (0 片面、1 両面、2 スポット)
[[nodiscard]] bool bindShadowInstances(uint32_t first, uint32_t count, int psoIndex)
{
	ID3D12PipelineState* pso = m_instancedShadowPso[psoIndex].Get();
	if (pso == nullptr || static_cast<std::size_t>(first) + count > m_shadowInstancesPrev.size())
	{
		return false;
	}
	const UINT bytes = count * static_cast<UINT>(sizeof(InstanceDataDx12));
	const auto alloc = m_uploadRing.upload(m_shadowInstancesPrev.data() + first, bytes, 256);
	if (!alloc.valid()) { return false; }
	const D3D12_VERTEX_BUFFER_VIEW view = {alloc.gpuAddr, bytes, sizeof(InstanceDataDx12)};
	m_graphicsCmdList->IASetVertexBuffers(1, 1, &view);
	m_graphicsCmdList->SetPipelineState(pso);
	return true;
}

/// @brief beginFrame で呼ぶ。このフレームの影のインスタンスを前フレーム分へ回す (確保し直さない)
void rotateShadowInstances()
{
	std::swap(m_shadowInstances, m_shadowInstancesPrev);
	m_shadowInstances.clear();
}

/// @brief 半透明 (OIT) で描くか。材質かどれかのインスタンスの色が透けていれば、まとめて描かずに 1 個ずつ OIT へ回す
[[nodiscard]] static bool needsBlend(const Material& material, const MeshInstance* instances, std::size_t count) noexcept
{
	if (material.alphaMode == Material::AlphaMode::Blend) { return true; }
	if (material.alphaMode == Material::AlphaMode::Mask) { return false; }
	if (material.diffuse.a < 1.0f) { return true; }
	for (std::size_t i = 0; i < count; ++i)
	{
		if (instances[i].tint[3] < 1.0f) { return true; }
	}
	return false;
}

/// @brief 同一メッシュをインスタンスごとの行列と色で描く (DX12)
/// @details 視錐台カリングはインスタンス単位。半透明の材質と PSO を作れない環境は drawMeshEx の繰り返しで描く
void drawMeshInstancesDx12(const Mesh& mesh, const MeshInstance* instances, std::size_t count, const Material& material,
                           const MaterialMaps* maps)
{
	if (!m_initialized || !m_graphicsCmdList || mesh.vertexCount() == 0 || instances == nullptr || count == 0) { return; }
	ensureInstancedPipelineDx12();
	if (m_instancedPipelineFailedDx12 || needsBlend(material, instances, count))
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			DrawTint tint;
			std::memcpy(tint.mul, instances[i].tint, sizeof(tint.mul));
			drawMeshEx(mesh, instanceWorld(instances[i]), material, maps, tint);
		}
		return;
	}
	drawSkyboxBeforeFirstDraw();
	m_graphicsCmdList->SetPipelineState(selectInstancedPso(material.doubleSided));
	if (!bindForwardDraw(sgc::Mat4f::identity(), material, maps, DrawTint{})) { return; }

	const auto& verts = mesh.vertices();
	const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex3D));
	auto* vb = acquireMeshBuffer(m_meshVBCache, mesh, verts.data(), vbSize);
	const auto& indices = mesh.indices();
	ID3D12Resource* ib = indices.empty() ? nullptr
		: acquireMeshBuffer(m_meshIBCache, mesh, indices.data(), static_cast<UINT>(indices.size() * sizeof(uint32_t)));
	if (!vb || (!indices.empty() && !ib)) { return; }

	for (std::size_t offset = 0; offset < count; offset += kInstanceBatchMaxDx12)
	{
		const std::size_t batchCount = std::min(kInstanceBatchMaxDx12, count - offset);
		m_instanceScratchDx12.clear();
		m_instanceBoundsDx12 = emptyCullAABB();
		const bool wantBounds = wantsShadowCasters();
		for (std::size_t i = 0; i < batchCount; ++i)
		{
			const MeshInstance& inst = instances[offset + i];
			const sgc::Mat4f world = instanceWorld(inst);
			const bool culled = cullMesh(mesh, world);
			// TAA・FSR・動きのぼけが動く物として扱えるよう、インスタンスも 1 個ずつ前フレームと対にする
			recordMotionDraw(mesh, world, !culled);
			if (culled) { continue; }
			m_instanceScratchDx12.push_back(toInstanceDataDx12(inst));
			if (wantBounds) { m_instanceBoundsDx12 = unionCullAABB(m_instanceBoundsDx12, worldOcclusionAABB(mesh.localAABB(), world)); }
		}
		if (m_instanceScratchDx12.empty()) { continue; }
		drawInstanceBatchDx12(vb, ib, vbSize, static_cast<UINT>(verts.size()), static_cast<UINT>(indices.size()));
		recordShadowInstances(mesh);
	}
}

/// @brief 行列だけのインスタンス (IRenderer3D::drawMeshInstanced) を色 1 のインスタンスとして描く
void drawMeshInstancedDx12(const Mesh& mesh, std::span<const sgc::Mat4f> worlds, const Material* material)
{
	const Material& mat = material ? *material : Material{};
	m_instanceInputScratch.clear();
	for (const auto& w : worlds) { m_instanceInputScratch.push_back(makeInstance(w)); }
	drawMeshInstancesDx12(mesh, m_instanceInputScratch.data(), m_instanceInputScratch.size(), mat, nullptr);
}

public:
/// @brief 同じメッシュをインスタンスごとの行列と色で描く (IRenderer3D)
void drawMeshInstances(const Mesh& mesh, const MeshInstance* instances, int count, const Material& material) override
{
	if (count > 0) { drawMeshInstancesDx12(mesh, instances, static_cast<std::size_t>(count), material, nullptr); }
}

private:
