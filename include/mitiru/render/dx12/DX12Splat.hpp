#pragma once

/// @file DX12Splat.hpp
/// @brief Renderer3D_DX12 の 3D Gaussian Splatting 描画実装 (部分ヘッダ、.inl)。
/// @details Renderer3D_DX12 のクラス内部から include される (DX12Skybox.hpp と同じ流儀)。
///          責務: .splat シーンの GPU アップロード / 専用 root sig・PSO・カメラCB /
///          フレーム内描画 (MSAA color FP16 へインスタンス化矩形、プリマルチプライ合成)。
///          ロードマップ: oscar-rythm/docs/splatting-dx12.md

#include <cstring>

#include <mitiru/render/SplatScene.hpp>
#include <mitiru/render/dx12/DX12SplatShaders.hpp>

/// @brief スプラット用 root sig / PSO / カメラ CB を一度だけ構築する。
void ensureSplatPipelineDx12()
{
	if (!m_d3dDevice || m_splatPipelineReady) { return; }

	// root sig: b0 = カメラCB(CBV) / t0=splat, t1=order (SRV table 2 連続)
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors     = 2;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
	params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
	params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 2;
	rsd.pParameters   = params;
	rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;   // 頂点入力なし (SV_VertexID)

	ComPtr<ID3DBlob> sigBlob, errBlob;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
	        sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) { return; }
	if (FAILED(m_d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(),
	        sigBlob->GetBufferSize(), IID_PPV_ARGS(m_splatRootSig.GetAddressOf())))) { return; }

	// VS / PS
	ComPtr<ID3DBlob> vsBlob, psBlob, cErr;
	if (FAILED(D3DCompile(SPLAT_VS_HLSL, std::strlen(SPLAT_VS_HLSL), nullptr, nullptr,
	        nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), cErr.GetAddressOf()))) { return; }
	if (FAILED(D3DCompile(SPLAT_PS_HLSL, std::strlen(SPLAT_PS_HLSL), nullptr, nullptr,
	        nullptr, "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), cErr.GetAddressOf()))) { return; }

	// PSO: 入力レイアウトなし / 深度オフ / プリマルチプライ over / MSAA FP16
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
	pso.pRootSignature = m_splatRootSig.Get();
	pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	pso.InputLayout = { nullptr, 0 };
	pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
	pso.RasterizerState.DepthClipEnable = TRUE;
	pso.DepthStencilState.DepthEnable    = FALSE;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
	pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
	pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
	pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
	pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
	pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
	pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
	pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pso.SampleMask            = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets      = 1;
	pso.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;   // MSAA color (HDR)
	pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count      = 4;                                // 4x MSAA に合わせる
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_splatPSO.GetAddressOf())))) { return; }

	// カメラ CB (256 B)
	D3D12_HEAP_PROPERTIES uph = {}; uph.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	d.Width = 256; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
	d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(m_d3dDevice->CreateCommittedResource(&uph, D3D12_HEAP_FLAG_NONE, &d,
	        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_splatCb.GetAddressOf())))) { return; }

	if (!m_splatSort.init(m_d3dDevice)) { return; }   // GPU 深度ソート compute pipeline
	m_splatPipelineReady = true;
}

/// @brief .splat シーンを読み込み GPU (UPLOAD heap の StructuredBuffer) へ展開する。
bool loadSplatSceneDx12(const char* path)
{
	if (!m_d3dDevice) { return false; }
	ensureSplatPipelineDx12();
	if (!m_splatPipelineReady) { return false; }

	SplatScene scene;
	if (!loadSplatFile(path, scene)) { return false; }
	m_splatCount = static_cast<UINT>(scene.count());
	if (m_splatCount == 0) { return false; }
	m_splatCenter[0] = scene.center[0]; m_splatCenter[1] = scene.center[1]; m_splatCenter[2] = scene.center[2];
	m_splatRadius = scene.radius;   // シーンを自動フレーミングするための境界球
	const UINT64 bytes = static_cast<UINT64>(m_splatCount) * sizeof(SplatGPU);

	// CPU 位置 (neural 現像 DX12Neural.hpp が使用)。深度ソートは GPU 側 (m_splatSort)。
	m_splatPos.resize(static_cast<std::size_t>(m_splatCount) * 3);
	m_splatOrigRgb.resize(static_cast<std::size_t>(m_splatCount) * 3);   // 焼き込みリセット用
	m_splatBaked.assign(m_splatCount, 0);   // 達成率トラッキング
	m_bakedTotal = 0;
	m_splatSorted = false;   // 新シーン: 次の draw で必ず一度ソートさせる
	for (UINT i = 0; i < m_splatCount; ++i)
	{
		m_splatPos[i * 3 + 0] = scene.splats[i].pos[0];
		m_splatPos[i * 3 + 1] = scene.splats[i].pos[1];
		m_splatPos[i * 3 + 2] = scene.splats[i].pos[2];
		m_splatOrigRgb[i * 3 + 0] = scene.splats[i].rgb[0];
		m_splatOrigRgb[i * 3 + 1] = scene.splats[i].rgb[1];
		m_splatOrigRgb[i * 3 + 2] = scene.splats[i].rgb[2];
	}

	D3D12_HEAP_PROPERTIES uph = {}; uph.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
	d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	m_splatBuffer.Reset();
	if (FAILED(m_d3dDevice->CreateCommittedResource(&uph, D3D12_HEAP_FLAG_NONE, &d,
	        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_splatBuffer.GetAddressOf())))) { return false; }

	void* p = nullptr; D3D12_RANGE rr = {0, 0};
	if (FAILED(m_splatBuffer->Map(0, &rr, &p))) { return false; }
	std::memcpy(p, scene.splats.data(), static_cast<std::size_t>(bytes));
	D3D12_RANGE wr = {0, static_cast<SIZE_T>(bytes)};
	m_splatBuffer->Unmap(0, &wr);

	// 深度ソート order バッファ (GPU compute が毎フレーム生成する DEFAULT-heap UAV)。
	if (!m_splatSort.setCount(m_d3dDevice, m_splatCount)) { return false; }

	// shader-visible SRV ヒープ (2 SRV: t0=splat, t1=order)
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = 2;
	hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_splatSrvHeap.Reset();
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_splatSrvHeap.GetAddressOf())))) { return false; }
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_splatSrvHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format                     = DXGI_FORMAT_UNKNOWN;   // structured
	srv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
	srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Buffer.FirstElement        = 0;
	srv.Buffer.NumElements         = m_splatCount;
	srv.Buffer.StructureByteStride = sizeof(SplatGPU);
	srv.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
	m_d3dDevice->CreateShaderResourceView(m_splatBuffer.Get(), &srv, cpu);   // t0 = splats

	cpu.ptr += inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvO = srv;
	srvO.Buffer.StructureByteStride = sizeof(std::uint32_t);
	m_d3dDevice->CreateShaderResourceView(m_splatSort.orderBuffer(), &srvO, cpu);  // t1 = GPU order

	m_splatReady = true;
	return true;
}

/// @brief 読み込み済みスプラットを現在のカメラで MSAA color RT へ描画する。
/// @details beginFrame 後 (MSAA color RT がバインド済み) に呼ぶ。skybox と同様に
///          color-only RT へ切替→描画→MRT/main 復帰する。
void drawSplatsDx12()
{
	if (!m_splatReady || !m_splatPipelineReady || !m_graphicsCmdList) { return; }
	if (!m_splatPSO || !m_splatRootSig || m_splatCount == 0) { return; }

	// 深度ソート (GPU compute、奥→手前): camPos が動いたフレームだけ再ソートする。
	// キーは camPos からの距離² だけに依存するので、カメラ静止時は前フレームの
	// order をそのまま使う (compute dispatch を丸ごと省ける)。sort は m_splatBuffer
	// (StructuredBuffer<SplatGPU>) の pos を GPU 上で直接読み、order を生成する
	// (CPU 走査も PCIe 転送もゼロ)。生成後に order を UAV→SRV へ遷移して VS が読む。
	const float cx = m_cameraPosition.x, cy = m_cameraPosition.y, cz = m_cameraPosition.z;
	const float mdx = cx - m_splatSortCam.x, mdy = cy - m_splatSortCam.y, mdz = cz - m_splatSortCam.z;
	if (!m_splatSorted || (mdx * mdx + mdy * mdy + mdz * mdz) > 0.0f)
	{
		m_splatSortCam = m_cameraPosition;
		m_splatSorted = true;
		m_splatSort.encode(m_graphicsCmdList.Get(), m_splatBuffer->GetGPUVirtualAddress(),
		                   static_cast<UINT>(sizeof(SplatGPU)), 0u, cx, cy, cz);
	}
	m_splatSort.toShaderResource(m_graphicsCmdList.Get());   // order を VS が読める SRV 状態へ

	// カメラ CB を更新 (view/proj は skybox と同じ row-vector 規約で toHLSL)
	struct alignas(16) SplatCb { float view[4][4]; float proj[4][4]; float params[4]; };
	SplatCb cb{};
	toHLSL(cb.view, m_viewMatrix);
	toHLSL(cb.proj, m_projMatrix);
	// 焦点距離(px) = 0.5 * viewport * proj 対角 (EWA ヤコビアン用)。glm は列優先 [col][row]。
	cb.params[0] = m_config.viewportWidth;
	cb.params[1] = m_config.viewportHeight;
	cb.params[2] = 0.5f * m_config.viewportWidth  * m_projMatrix[0][0];   // focalX
	cb.params[3] = 0.5f * m_config.viewportHeight * m_projMatrix[1][1];   // focalY
	void* mp = nullptr; D3D12_RANGE rr = {0, 0};
	if (SUCCEEDED(m_splatCb->Map(0, &rr, &mp)))
	{
		std::memcpy(mp, &cb, sizeof(cb));
		D3D12_RANGE wr = {0, sizeof(cb)};
		m_splatCb->Unmap(0, &wr);
	}

	// MSAA color のみ + 深度 を bind
	auto msaaRtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto dsv     = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &msaaRtv, FALSE, &dsv);

	m_graphicsCmdList->SetGraphicsRootSignature(m_splatRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_splatPSO.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, m_splatCb->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { m_splatSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(1,
	        m_splatSrvHeap->GetGPUDescriptorHandleForHeapStart());

	// 頂点バッファなし: 4 頂点 (strip 矩形) × m_splatCount インスタンス
	m_graphicsCmdList->DrawInstanced(4, m_splatCount, 0, 0);
	++m_drawCallCount;

	// MRT (color + normal) + main state へ復帰
	auto normalRtv = m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { msaaRtv, normalRtv };
	m_graphicsCmdList->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(m_mainPSO.Get());
}
