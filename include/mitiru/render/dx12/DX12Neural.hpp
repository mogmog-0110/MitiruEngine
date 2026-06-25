#pragma once

/// @file DX12Neural.hpp
/// @brief Renderer3D_DX12 のニューラル現像 (M3) 実装 (部分ヘッダ、.inl)。
/// @details Renderer3D_DX12 のクラス内部から include される (DX12Splat.hpp と同じ流儀)。
///          安全境界 (engine フレーム頭、backbuffer = PRESENT) で tickDevelop が
///          直前フレームを readPixels → NeuralStyle (ORT + DirectML, GPU 推論) で
///          2D 絵画へ変換 → 保持する。ゲームは Screen 経由で styleReady/styleImage を
///          取り、drawPixelGrid で表示する。3D⇄2D の「現像」ギミックの心臓部。

#include <string>
#include <vector>

/// @brief 現像を要求する (実際の readback + 推論は次の tickDevelop で行う)。
void requestDevelopDx12(const char* modelPath)
{
	m_developModel   = (modelPath != nullptr) ? modelPath : "";
	m_developRequest = !m_developModel.empty();
}

/// @brief 安全境界で呼ぶ: 要求があれば直前フレームを読み出して style 変換する。
/// @details m_device->readPixels は backbuffer が PRESENT 状態であることを前提とするので、
///          engine フレーム頭 (device->beginFrame 後・game.draw 前) から呼ぶこと。
///          推論は GPU (DirectML EP) で走る。1 回の現像で完結するので毎フレーム負荷はない。
void tickDevelopDx12()
{
	// 焼き込み / リセット要求は安全境界で splat buffer を書き換える (GPU idle 後)。
	if (m_bakeRequest)
	{
		m_bakeRequest = false;
		if (m_styleReady && m_device) { m_device->waitForGpu(); bakeStyleToSplatsDx12(); }
	}
	if (m_resetRequest)
	{
		m_resetRequest = false;
		if (m_device) { m_device->waitForGpu(); resetSplatColorsDx12(); }
	}

	if (!m_developRequest || m_device == nullptr) { return; }
	m_developRequest = false;

	const int w = static_cast<int>(m_config.viewportWidth);
	const int h = static_cast<int>(m_config.viewportHeight);
	if (w <= 0 || h <= 0) { return; }

	if (!m_neuralStyle.ensure(m_developModel)) { return; }   // ORT+DML セッション構築失敗

	// フレームの GPU 描画完了を待つ (3D は別キューで合成されるため、待たないと
	// backbuffer が未完成 = 黒のまま読めてしまう。auto-test キャプチャと同じ作法)。
	m_device->waitForGpu();
	std::vector<std::uint8_t> frame = m_device->readPixels(w, h);   // 直前フレーム RGBA8
	if (static_cast<int>(frame.size()) < w * h * 4) { return; }

	std::vector<std::uint8_t> styled;
	if (m_neuralStyle.stylize(frame.data(), w, h, styled))
	{
		m_styleImage.swap(styled);
		m_styleW       = w;
		m_styleH       = h;
		m_styleReady   = true;
		m_styleTexDirty = true;   // blit 用テクスチャを次フレームで再アップロード
		m_developView  = m_viewMatrix;   // 焼き込み射影用に現像視点を保存
		m_developProj  = m_projMatrix;
	}
}

/// @brief 直前の現像 2D を、その現像視点から見えるスプラットへ色として焼き込む (2D→3D)。
/// @details 各スプラットを現像視点の view/proj で画面へ射影し、現像画像の該当ピクセル色を
///          そのスプラットの rgb に書き込む。複数視点から焼くと全周が絵画化する。
void bakeStyleToSplatsDx12()
{
	if (m_styleImage.empty() || m_splatCount == 0 || !m_splatBuffer) { return; }
	const int W = m_styleW, H = m_styleH;
	const std::size_t WH = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);

	// 各 splat を現像視点へ射影 (CPU)。clip.w = 視空間奥行き (小さい=手前)。
	std::vector<int>   sxB(m_splatCount, -1), syB(m_splatCount, -1);
	std::vector<float> wB(m_splatCount, -1.0f);
	std::vector<float> depth(WH, 1e30f);   // pass1: ピクセル毎の最前面奥行き
	for (UINT i = 0; i < m_splatCount; ++i)
	{
		const glm::vec4 clip = m_developProj * m_developView *
			glm::vec4(m_splatPos[i*3+0], m_splatPos[i*3+1], m_splatPos[i*3+2], 1.0f);
		if (clip.w <= 0.0f) { continue; }
		const float nx = clip.x / clip.w, ny = clip.y / clip.w;
		if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f) { continue; }
		const int sx = static_cast<int>((nx * 0.5f + 0.5f) * static_cast<float>(W));
		const int sy = static_cast<int>((0.5f - 0.5f * ny) * static_cast<float>(H));  // NDC y↑→screen y↓
		if (sx < 0 || sx >= W || sy < 0 || sy >= H) { continue; }
		sxB[i] = sx; syB[i] = sy; wB[i] = clip.w;
		float& d = depth[static_cast<std::size_t>(sy) * W + sx];
		if (clip.w < d) { d = clip.w; }
	}

	// pass2: 各ピクセルの最前面シェル (手前 4%) の splat だけ焼く = 裏側を塗らない。
	void* p = nullptr; D3D12_RANGE rr = {0, 0};
	if (FAILED(m_splatBuffer->Map(0, &rr, &p))) { return; }
	auto* splats = static_cast<SplatGPU*>(p);
	for (UINT i = 0; i < m_splatCount; ++i)
	{
		if (wB[i] < 0.0f) { continue; }
		const std::size_t pix = static_cast<std::size_t>(syB[i]) * W + sxB[i];
		if (wB[i] > depth[pix] * 1.04f) { continue; }   // 最前面シェルのみ
		const std::uint8_t* px = &m_styleImage[pix * 4];
		splats[i].rgb[0] = static_cast<float>(px[0]) / 255.0f;
		splats[i].rgb[1] = static_cast<float>(px[1]) / 255.0f;
		splats[i].rgb[2] = static_cast<float>(px[2]) / 255.0f;
		if (i < m_splatBaked.size() && !m_splatBaked[i]) { m_splatBaked[i] = 1; ++m_bakedTotal; }
	}
	const D3D12_RANGE wr = {0, static_cast<SIZE_T>(m_splatCount) * sizeof(SplatGPU)};
	m_splatBuffer->Unmap(0, &wr);
}

/// @brief スプラット色を元の写実色へ戻す (焼き込み解除)。
void resetSplatColorsDx12()
{
	if (m_splatOrigRgb.empty() || m_splatCount == 0 || !m_splatBuffer) { return; }
	void* p = nullptr; D3D12_RANGE rr = {0, 0};
	if (FAILED(m_splatBuffer->Map(0, &rr, &p))) { return; }
	auto* splats = static_cast<SplatGPU*>(p);
	for (UINT i = 0; i < m_splatCount; ++i)
	{
		splats[i].rgb[0] = m_splatOrigRgb[i*3+0];
		splats[i].rgb[1] = m_splatOrigRgb[i*3+1];
		splats[i].rgb[2] = m_splatOrigRgb[i*3+2];
	}
	const D3D12_RANGE wr = {0, static_cast<SIZE_T>(m_splatCount) * sizeof(SplatGPU)};
	m_splatBuffer->Unmap(0, &wr);
	m_splatBaked.assign(m_splatBaked.size(), static_cast<std::uint8_t>(0));
	m_bakedTotal = 0;
}

/// @brief 現在の現像 2D を「お題」として保存する (現像合わせゲーム)。
void captureTargetDx12()
{
	if (m_styleImage.empty()) { return; }
	m_targetImage = m_styleImage;
	m_targetReady = true;
}

/// @brief 現在の現像 2D とお題の一致度 (0..1)。32x18 グリッド平均の絶対差で構図+色を比較。
float matchScoreDx12() const
{
	if (!m_targetReady || m_styleImage.empty() || m_targetImage.size() != m_styleImage.size()) { return 0.0f; }
	const int W = m_styleW, H = m_styleH;
	if (W <= 0 || H <= 0) { return 0.0f; }
	long diff = 0; int n = 0;
	for (int gy = 0; gy < 18; ++gy)
	for (int gx = 0; gx < 32; ++gx)
	{
		const int sx = (gx * 2 + 1) * W / 64;
		const int sy = (gy * 2 + 1) * H / 36;
		const std::size_t idx = (static_cast<std::size_t>(sy) * W + sx) * 4;
		for (int c = 0; c < 3; ++c)
		{
			int d = static_cast<int>(m_styleImage[idx + c]) - static_cast<int>(m_targetImage[idx + c]);
			diff += (d < 0) ? -d : d;
			++n;
		}
	}
	const float avg = (n > 0) ? static_cast<float>(diff) / static_cast<float>(n) : 255.0f;  // 0..255
	float score = 1.0f - avg / 110.0f;   // 平均差 110 で 0 点
	return score < 0.0f ? 0.0f : (score > 1.0f ? 1.0f : score);
}

/// @brief 現像 2D の合成強度を設定する (0=3D / 1=完全 2D)。Screen::drawStyle が呼ぶ。
void setStyleStrengthDx12(float s)
{
	m_styleStrength = (s < 0.0f) ? 0.0f : (s > 1.0f ? 1.0f : s);
}

/// @brief 現像 blit 用 root sig / PSO を一度だけ構築する (全画面三角形 + テクスチャ + α合成)。
void ensureStyleBlitPipelineDx12()
{
	if (m_styleBlitReady || !m_d3dDevice) { return; }

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	params[0].DescriptorTable.pDescriptorRanges   = &srvRange;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;   // b0 = strength
	params[1].Constants.ShaderRegister = 0;
	params[1].Constants.Num32BitValues = 1;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samp = {};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 2; rsd.pParameters = params;
	rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &samp;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
	        sig.GetAddressOf(), err.GetAddressOf()))) { return; }
	if (FAILED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	        IID_PPV_ARGS(m_styleBlitRootSig.GetAddressOf())))) { return; }

	static const char* kVS = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
    return o;
})";
	static const char* kPS = R"(
cbuffer B : register(b0) { float gStrength; }
Texture2D gTex : register(t0);
SamplerState gSmp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 PSMain(VSOut i) : SV_Target {
    float3 c = gTex.Sample(gSmp, i.uv).rgb;
    return float4(c, gStrength);
})";
	ComPtr<ID3DBlob> vs, ps, ce;
	if (FAILED(D3DCompile(kVS, std::strlen(kVS), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vs.GetAddressOf(), ce.GetAddressOf()))) { return; }
	if (FAILED(D3DCompile(kPS, std::strlen(kPS), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, ps.GetAddressOf(), ce.GetAddressOf()))) { return; }

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
	pso.pRootSignature = m_styleBlitRootSig.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pso.DepthStencilState.DepthEnable = FALSE;
	pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
	pso.BlendState.RenderTarget[0].SrcBlend  = D3D12_BLEND_SRC_ALPHA;
	pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	pso.BlendState.RenderTarget[0].BlendOp   = D3D12_BLEND_OP_ADD;
	pso.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
	pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	pso.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
	pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;   // LDR backbuffer
	pso.SampleDesc.Count = 1;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_styleBlitPSO.GetAddressOf())))) { return; }

	m_styleBlitReady = true;
}

/// @brief 現像画像 (CPU RGBA) を GPU テクスチャへアップロードする (dirty 時のみ)。
void uploadStyleTexDx12()
{
	if (!m_styleTexDirty || !m_d3dDevice) { return; }
	const std::vector<std::uint8_t>& img = (m_showTarget && m_targetReady) ? m_targetImage : m_styleImage;
	if (img.empty()) { return; }
	const UINT tw = static_cast<UINT>(m_styleW), th = static_cast<UINT>(m_styleH);

	if (!m_styleTex || m_styleTexW != m_styleW || m_styleTexH != m_styleH)
	{
		D3D12_HEAP_PROPERTIES dh = {}; dh.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC td = {};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = tw; td.Height = th; td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
		td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		m_styleTex.Reset();
		if (FAILED(m_d3dDevice->CreateCommittedResource(&dh, D3D12_HEAP_FLAG_NONE, &td,
		        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_styleTex.GetAddressOf())))) { return; }

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT64 total = 0;
		m_d3dDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
		D3D12_HEAP_PROPERTIES uh = {}; uh.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bd = {};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
		bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		m_styleUpload.Reset();
		if (FAILED(m_d3dDevice->CreateCommittedResource(&uh, D3D12_HEAP_FLAG_NONE, &bd,
		        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_styleUpload.GetAddressOf())))) { return; }

		if (!m_styleSrvHeap)
		{
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
			hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_styleSrvHeap.GetAddressOf())))) { return; }
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
		m_d3dDevice->CreateShaderResourceView(m_styleTex.Get(), &sv,
		        m_styleSrvHeap->GetCPUDescriptorHandleForHeapStart());
		m_styleTexW = m_styleW; m_styleTexH = m_styleH;
	}

	// upload バッファへ行ごとにコピー (row pitch を 256 整列)
	D3D12_RESOURCE_DESC td = m_styleTex->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT64 total = 0;
	m_d3dDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
	std::uint8_t* map = nullptr; D3D12_RANGE rr = {0, 0};
	if (FAILED(m_styleUpload->Map(0, &rr, reinterpret_cast<void**>(&map)))) { return; }
	const UINT srcPitch = tw * 4;
	for (UINT y = 0; y < th; ++y)
	{
		std::memcpy(map + fp.Offset + static_cast<UINT64>(y) * fp.Footprint.RowPitch,
		            img.data() + static_cast<std::size_t>(y) * srcPitch, srcPitch);
	}
	m_styleUpload->Unmap(0, nullptr);

	// upload → texture, COPY_DEST→PIXEL_SHADER_RESOURCE
	D3D12_RESOURCE_BARRIER toCopy = {};
	toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopy.Transition.pResource = m_styleTex.Get();
	toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	toCopy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
	toCopy.Transition.Subresource = 0;
	if (m_styleTexUploaded) { m_graphicsCmdList->ResourceBarrier(1, &toCopy); }

	D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = m_styleTex.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = m_styleUpload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
	m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	D3D12_RESOURCE_BARRIER toSrv = toCopy;
	toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	toSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	m_graphicsCmdList->ResourceBarrier(1, &toSrv);

	m_styleTexUploaded = true;
	m_styleTexDirty = false;
}

/// @brief 現像 2D 画像をバックバッファへ全画面 α合成する (post-process、overlay2D の前)。
void blitStyleDx12()
{
	const bool ready = m_showTarget ? m_targetReady : m_styleReady;
	if (!ready || m_styleStrength <= 0.0f || !m_graphicsCmdList) { return; }
	ensureStyleBlitPipelineDx12();
	if (!m_styleBlitReady) { return; }
	uploadStyleTexDx12();
	if (!m_styleTex) { return; }

	// backbuffer を RTV に、viewport/scissor を実バックバッファ解像度に明示設定する
	// (FXAA の状態に依存しない)。
	auto* bb  = static_cast<gfx::Dx12RenderTarget*>(m_device->getSwapChain()->backBuffer());
	auto  rtv = bb->rtvHandle();
	const auto bbDesc = bb->nativeResource()->GetDesc();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	D3D12_VIEWPORT vp = {}; vp.Width = static_cast<float>(bbDesc.Width);
	vp.Height = static_cast<float>(bbDesc.Height); vp.MaxDepth = 1.0f;
	D3D12_RECT scr = {0, 0, static_cast<LONG>(bbDesc.Width), static_cast<LONG>(bbDesc.Height)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &scr);

	m_graphicsCmdList->SetGraphicsRootSignature(m_styleBlitRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_styleBlitPSO.Get());
	ID3D12DescriptorHeap* heaps[] = { m_styleSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(0, m_styleSrvHeap->GetGPUDescriptorHandleForHeapStart());
	m_graphicsCmdList->SetGraphicsRoot32BitConstant(1, *reinterpret_cast<const UINT*>(&m_styleStrength), 0);
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);
}
