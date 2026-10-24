// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。
// 的を隠す面を網点で抜く (ADR 0077)。抜くかどうかは前方の描画の PS と clod のラスタが、CbCluster と clod の CB の末尾に載せた
// 同じ定数で決める。影のパスと DDGI はこの定数を読まないので、抜いた面も影を落とし、光を遮る。
// ここは定数の用意と、的として描いた物の影絵 (不透明と clod の後に、隠れた所だけ上から塗る)。

public:

void setOccluderFade(const OccluderFade& fade) override { flushDrawRun(); m_occluderFade = fade; }
void setFadeTarget(bool enabled) override { flushDrawRun(); m_fadeTargetDraw = enabled; }

private:

struct SilhouetteDraw
{
	const Mesh* mesh = nullptr;
	sgc::Mat4f world;
};

OccluderFade m_occluderFade{};
bool m_fadeTargetDraw = false;
std::vector<SilhouetteDraw> m_silhouetteDraws;   ///< 的として描いた物 (主ビューだけ)。容量はフレームを越えて使い回す
ComPtr<ID3D12PipelineState> m_silhouettePSO;
ComPtr<ID3D12PipelineState> m_silhouetteNearestPSO;   ///< 的のいちばん手前の深度を書く (MSAA の R32、MIN)
ComPtr<ID3D12RootSignature> m_silhouetteRootSig;      ///< b0 = 変換、b1 = 影絵、t0 = いちばん手前の深度
ComPtr<ID3D12DescriptorHeap> m_silhouetteNearestRtv;
gfx::GpuResource            m_silhouetteNearest;      ///< 内部解像度・主パスと同じ標本数の R32_FLOAT。影絵を描くフレームだけ使う
int m_silhouetteNearestW = 0;
int m_silhouetteNearestH = 0;

void beginFrameOccluderFade() noexcept
{
	m_occluderFade = OccluderFade{};
	m_fadeTargetDraw = false;
	m_silhouetteDraws.clear();
}

/// @brief 主ビューの透かしの定数。TAA か TAAU の間は網点の閾値をフレームごとにずらし、履歴で混ざって半透明に見せる
[[nodiscard]] OccluderFadeGpu occluderFadeGpu() const noexcept
{
	const bool temporal = m_aaMode == AntiAliasing3D::Taa || upscaleActive();
	const float shift = temporal ? static_cast<float>(m_frameCounter % 64u) * 0.618034f : 0.0f;
	return makeOccluderFadeGpu(m_occluderFade, m_clodCamera.position(), m_clodCamera.fov(), shift);
}

void recordFadeTargetDraw(const Mesh& mesh, const sgc::Mat4f& world)
{
	if (m_fadeTargetDraw && m_activeView == nullptr) { m_silhouetteDraws.push_back({&mesh, world}); }
}

[[nodiscard]] bool createSilhouetteRootSig()
{
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER prm[3] = {};
	for (UINT i = 0; i < 2; ++i)
	{
		prm[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		prm[i].Descriptor.ShaderRegister = i;
		prm[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}
	prm[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	prm[2].DescriptorTable.NumDescriptorRanges = 1;
	prm[2].DescriptorTable.pDescriptorRanges = &range;
	prm[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 3;
	rsd.pParameters = prm;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) { return false; }
	return SUCCEEDED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	                                                  IID_PPV_ARGS(m_silhouetteRootSig.ReleaseAndGetAddressOf())));
}

void createSilhouettePso()
{
	try
	{
		if (!createSilhouetteRootSig()) { return; }
		const auto vs = gfx::Dx12Shader::createVertexShader(DX12_OCCLUDER_SILHOUETTE_HLSL, "VSMain");
		const auto ps = gfx::Dx12Shader::createPixelShader(DX12_OCCLUDER_SILHOUETTE_HLSL, "PSMain");
		const auto nearestPs = gfx::Dx12Shader::createPixelShader(DX12_OCCLUDER_SILHOUETTE_HLSL, "PSNearest");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		fillForwardPsoState(pd);
		pd.pRootSignature = m_silhouetteRootSig.Get();
		pd.VS = vs.shaderBytecode();
		pd.PS = ps.shaderBytecode();
		D3D12_INPUT_ELEMENT_DESC layout[4] = {};
		UINT count = 0;
		getInputLayout(layout, count);
		pd.InputLayout = {layout, count};
		pd.NumRenderTargets = 1;
		pd.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
		pd.BlendState.IndependentBlendEnable = FALSE;
		pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
		if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_silhouettePSO.ReleaseAndGetAddressOf()))))
		{
			m_silhouettePSO.Reset();
			return;
		}
		pd.PS = nearestPs.shaderBytecode();
		pd.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
		pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
		pd.DepthStencilState.DepthEnable = FALSE;
		auto& rt = pd.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_MIN;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_MIN;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
		auto* nearestOut = m_silhouetteNearestPSO.ReleaseAndGetAddressOf();
		if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(nearestOut))))
		{
			m_silhouettePSO.Reset();
		}
	}
	catch (const std::exception&)
	{
		m_silhouettePSO.Reset();
	}
}

/// @brief 的のいちばん手前の深度を置く R32 を内部解像度で用意する。前と同じ大きさなら作り直さない
[[nodiscard]] bool ensureSilhouetteNearest()
{
	const int w = static_cast<int>(m_config.viewportWidth);
	const int h = static_cast<int>(m_config.viewportHeight);
	if (m_silhouetteNearest && w == m_silhouetteNearestW && h == m_silhouetteNearestH) { return true; }
	m_silhouetteNearest.Reset();
	if (!m_silhouetteNearestRtv)
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = 1;
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_silhouetteNearestRtv.GetAddressOf()))))
		{
			return false;
		}
	}
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = static_cast<UINT64>(std::max(w, 1));
	desc.Height = static_cast<UINT>(std::max(h, 1));
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_R32_FLOAT;
	clear.Color[0] = 1.0f;
	constexpr auto kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, kRead, &clear, m_silhouetteNearest)))
	{
		return false;
	}
	m_d3dDevice->CreateRenderTargetView(m_silhouetteNearest.Get(), nullptr,
	                                    m_silhouetteNearestRtv->GetCPUDescriptorHandleForHeapStart());
	m_silhouetteNearestW = w;
	m_silhouetteNearestH = h;
	return true;
}

static void silhouetteBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
                              D3D12_RESOURCE_STATES to)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &b);
}

void drawSilhouetteTargets(D3D12_GPU_VIRTUAL_ADDRESS cb)
{
	auto* cmd = m_graphicsCmdList.Get();
	cmd->SetGraphicsRootConstantBufferView(1, cb);
	for (const SilhouetteDraw& d : m_silhouetteDraws)
	{
		const auto transform = uploadTransformCB(d.world);
		if (transform == 0) { continue; }
		cmd->SetGraphicsRootConstantBufferView(0, transform);
		if (drawMeshBuffers(*d.mesh)) { ++m_drawCallCount; }
	}
}

/// @brief 的を寄せた後の深度を画素ごとに一番手前だけ残す。影絵はこれより奥の的の面を塗らない
[[nodiscard]] bool writeSilhouetteNearest(D3D12_GPU_VIRTUAL_ADDRESS cb, D3D12_GPU_DESCRIPTOR_HANDLE& srvOut)
{
	if (!m_silhouetteNearestPSO || !ensureSilhouetteNearest()) { return false; }
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
	if (!reserveSrvSlots(1, srvCpu, srvOut)) { return false; }
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_d3dDevice->CreateShaderResourceView(m_silhouetteNearest.Get(), &srv, srvCpu);
	auto* cmd = m_graphicsCmdList.Get();
	constexpr auto kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	constexpr auto kWrite = D3D12_RESOURCE_STATE_RENDER_TARGET;
	silhouetteBarrier(cmd, m_silhouetteNearest.Get(), kRead, kWrite);
	const auto rtv = m_silhouetteNearestRtv->GetCPUDescriptorHandleForHeapStart();
	const float far1[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmd->ClearRenderTargetView(rtv, far1, 0, nullptr);
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	cmd->SetPipelineState(m_silhouetteNearestPSO.Get());
	drawSilhouetteTargets(cb);
	silhouetteBarrier(cmd, m_silhouetteNearest.Get(), kWrite, kRead);
	return true;
}

/// @brief 影絵の色と寄せる距離 (的の半径の一番大きいもの) を積む。描かない時は 0
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadSilhouetteCB()
{
	struct alignas(256) CbSilhouette
	{
		float color[4];
		float params[4];
	};
	const float* s = m_occluderFade.silhouette;
	const sgc::Colorf c = linearColor(sgc::Colorf{s[0], s[1], s[2], 1.0f});
	CbSilhouette cb{{c.r, c.g, c.b, std::clamp(s[3], 0.0f, 1.0f)}, {0.0f, 0.0f, 0.0f, 0.0f}};
	const int n = std::clamp(m_occluderFade.targetCount, 0, OccluderFade::kMaxTargets);
	for (int i = 0; i < n; ++i) { cb.params[0] = std::max(cb.params[0], m_occluderFade.targets[i].radius); }
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief 不透明と clod の後、空と半透明の前に呼ぶ。描く先は MSAA の色と深度 (深度は書かない)
void drawOccluderSilhouettes()
{
	if (m_silhouetteDraws.empty() || !(m_occluderFade.silhouette[3] > 0.0f) || m_occluderFade.targetCount <= 0) { return; }
	if (!m_silhouettePSO)
	{
		debug::verboseOnce("dx12.occluderFade.silhouette", "透かしの影絵のシェーダーを作れなかったので、影絵を描きません。");
		return;
	}
	const auto cb = uploadSilhouetteCB();
	if (cb == 0) { return; }
	auto* cmd = m_graphicsCmdList.Get();
	const D3D12_VIEWPORT vp = {0.0f, 0.0f, m_config.viewportWidth, m_config.viewportHeight, 0.0f, 1.0f};
	const D3D12_RECT sc = {0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight)};
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	ID3D12DescriptorHeap* heaps[] = {m_albedoSrvHeap.Get()};
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootSignature(m_silhouetteRootSig.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	D3D12_GPU_DESCRIPTOR_HANDLE nearest{};
	if (!writeSilhouetteNearest(cb, nearest))
	{
		debug::verboseOnce("dx12.occluderFade.nearest", "的の手前の深度を置く場所を作れなかったので、影絵を描きません。");
		return;
	}
	const auto rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	cmd->SetPipelineState(m_silhouettePSO.Get());
	cmd->SetGraphicsRootDescriptorTable(2, nearest);
	drawSilhouetteTargets(cb);
}
