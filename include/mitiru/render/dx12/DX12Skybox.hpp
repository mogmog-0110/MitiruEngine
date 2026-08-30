#pragma once

/// @file DX12Skybox.hpp
/// @brief Renderer3D_DX12 の skybox 描画実装（部分ヘッダ）
/// @details Renderer3D_DX12 のクラス内部から `.inl` 形式で include される。
///          外から直接 include しない。
///
///          責務:
///            - キューブマップ GPU リソース（TextureCube + SRV）の構築
///            - skybox 専用ルートシグネチャ / PSO / 立方体メッシュ
///            - frame ごとの skybox 描画（depth=1.0 / LESS_EQUAL / no write）
///            - skybox 描画後にメインパイプラインの root sig / PSO に戻す

#include <array>
#include <cstring>

#include <mitiru/render/SkyboxShaders.hpp>

/// @brief skybox の TextureCube + SRV + upload buffer を準備する
/// @details GPU リソースは faceSize が変わらない限り **再利用** する。
///          variant 切替（1/2/3）は memcpy → CopyTextureRegion だけで済み、
///          CreateCommittedResource は走らない（軽い）。
void ensureSkyboxTextureDx12()
{
	if (!m_d3dDevice) return;
	if (!m_skyboxCubemap.valid()) return;
	if (m_skyboxTextureReady) return;

	const int faceSize = m_skyboxCubemap.faceSize();

	// row pitch / face stride の計算
	const UINT rawRowBytes = static_cast<UINT>(faceSize) * 4u;
	const UINT alignedRow =
		(rawRowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
		& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
	const UINT facePadded = alignedRow * static_cast<UINT>(faceSize);
	const UINT faceStride =
		(facePadded + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u)
		& ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
	const UINT uploadSize = faceStride * static_cast<UINT>(kCubemapFaceCount);

	// faceSize 変化（または初回）のみ GPU リソースを作り直す
	const bool sizeChanged =
		!m_skyboxTexture
		|| !m_skyboxUpload
		|| !m_skyboxSrvHeap
		|| m_skyboxFaceSize != faceSize;

	if (sizeChanged)
	{
		// ── 1. TextureCube リソース ────────────────────────────
		D3D12_HEAP_PROPERTIES texHp = {};
		texHp.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width            = static_cast<UINT64>(faceSize);
		texDesc.Height           = static_cast<UINT>(faceSize);
		texDesc.DepthOrArraySize = kCubemapFaceCount;
		texDesc.MipLevels        = 1;
		// 色テクスチャなので sRGB で置く。UNORM で置くとトーンマップ側のガンマと二重にかかり、
		// 彩度の高い色ほど白へ寄る。
		texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		texDesc.SampleDesc.Count = 1;
		texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		m_skyboxTexture.Reset();
		if (FAILED(m_d3dDevice->CreateCommittedResource(
				&texHp, D3D12_HEAP_FLAG_NONE, &texDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(m_skyboxTexture.GetAddressOf()))))
		{
			return;
		}
		// 新しく作った直後は COPY_DEST 状態
		m_skyboxTextureInPSR = false;

		// ── 2. Upload buffer（6 面分）──────────────────────────
		D3D12_HEAP_PROPERTIES upHp = {};
		upHp.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC upDesc = {};
		upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		upDesc.Width            = uploadSize;
		upDesc.Height           = 1;
		upDesc.DepthOrArraySize = 1;
		upDesc.MipLevels        = 1;
		upDesc.Format           = DXGI_FORMAT_UNKNOWN;
		upDesc.SampleDesc.Count = 1;
		upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		m_skyboxUpload.Reset();
		if (FAILED(m_d3dDevice->CreateCommittedResource(
				&upHp, D3D12_HEAP_FLAG_NONE, &upDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(m_skyboxUpload.GetAddressOf()))))
		{
			return;
		}

		// ── 3. SRV ヒープと SRV ───────────────────────────────
		D3D12_DESCRIPTOR_HEAP_DESC srvHd = {};
		srvHd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHd.NumDescriptors = 1;
		srvHd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_skyboxSrvHeap.Reset();
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(
				&srvHd, IID_PPV_ARGS(m_skyboxSrvHeap.GetAddressOf()))))
		{
			return;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		srv.ViewDimension               = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.TextureCube.MipLevels       = 1;
		srv.TextureCube.MostDetailedMip = 0;
		m_d3dDevice->CreateShaderResourceView(
			m_skyboxTexture.Get(), &srv,
			m_skyboxSrvHeap->GetCPUDescriptorHandleForHeapStart());

		m_skyboxFaceStride  = faceStride;
		m_skyboxAlignedRow  = alignedRow;
		m_skyboxFaceSize    = faceSize;
	}

	// テクスチャは PIXEL_SHADER_RESOURCE で常駐している可能性があるので
	// CopyTextureRegion 前に COPY_DEST に戻す（不要なら uploadSkyboxTextureDx12 で no-op）。
	// 6 面のピクセルを毎回 upload buffer に書き込む（軽量 memcpy のみ）
	void* mapped = nullptr;
	D3D12_RANGE readRange = {0, 0};
	if (FAILED(m_skyboxUpload->Map(0, &readRange, &mapped)))
	{
		return;
	}
	auto* uploadBase = static_cast<std::uint8_t*>(mapped);
	for (int face = 0; face < kCubemapFaceCount; ++face)
	{
		const auto& faceTex = m_skyboxCubemap.face(face);
		const auto& px      = faceTex.pixels();
		auto* dst = uploadBase + face * m_skyboxFaceStride;
		const auto* src = px.data();
		for (int row = 0; row < faceSize; ++row)
		{
			std::memcpy(dst + row * m_skyboxAlignedRow,
			            src + row * rawRowBytes, rawRowBytes);
		}
	}
	const D3D12_RANGE wroteAll = {0, uploadSize};
	m_skyboxUpload->Unmap(0, &wroteAll);

	m_skyboxNeedsUpload  = true;
	m_skyboxTextureReady = true;
}

/// @brief skybox 用 root sig / PSO / VS / PS / cube VB / IB を構築する
/// @details cubemap の内容には依存しない。renderer 寿命中に一度だけ作って
///          流用する。
void ensureSkyboxPipelineDx12()
{
	if (!m_d3dDevice) return;
	if (m_skyboxPipelineReady) return;

	// ── 4. シェーダー & PSO 用ルートシグネチャ ───────────────
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors                    = 1;
	srvRange.BaseShaderRegister                = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER skyParams[2] = {};
	skyParams[0].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV;
	skyParams[0].Descriptor.ShaderRegister = 0;
	skyParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	skyParams[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	skyParams[1].DescriptorTable.NumDescriptorRanges = 1;
	skyParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
	skyParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister   = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters     = 2;
	rsd.pParameters       = skyParams;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers   = &sampler;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> sigBlob;
	ComPtr<ID3DBlob> errBlob;
	if (FAILED(D3D12SerializeRootSignature(
			&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
			sigBlob.GetAddressOf(), errBlob.GetAddressOf())))
	{
		return;
	}
	if (FAILED(m_d3dDevice->CreateRootSignature(
			0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
			IID_PPV_ARGS(m_skyboxRootSig.GetAddressOf()))))
	{
		return;
	}

	// ── 5. VS / PS をコンパイル ──────────────────────────────
	ComPtr<ID3DBlob> vsBlob, psBlob, compileErr;
	if (FAILED(D3DCompile(
			SKYBOX_VS_HLSL, std::strlen(SKYBOX_VS_HLSL),
			nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
			0, 0, vsBlob.GetAddressOf(), compileErr.GetAddressOf())))
	{
		return;
	}
	if (FAILED(D3DCompile(
			SKYBOX_PS_HLSL, std::strlen(SKYBOX_PS_HLSL),
			nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
			0, 0, psBlob.GetAddressOf(), compileErr.GetAddressOf())))
	{
		return;
	}

	// ── 6. PSO 構築 ─────────────────────────────────────────
	D3D12_INPUT_ELEMENT_DESC inputLayout[1] = {};
	inputLayout[0].SemanticName    = "POSITION";
	inputLayout[0].SemanticIndex   = 0;
	inputLayout[0].Format          = DXGI_FORMAT_R32G32B32_FLOAT;
	inputLayout[0].AlignedByteOffset = 0;
	inputLayout[0].InputSlotClass  = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_skyboxRootSig.Get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements        = 1;

	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.DepthStencilState.DepthEnable    = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 1;
	// ENG-106 HDR: skybox は MSAA color FP16 に書き込む
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
	// 4x MSAA。共有 depth と sample count を揃える (ENG-105 v2)
	psoDesc.SampleDesc.Count      = 4;

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_skyboxPSO.GetAddressOf()))))
	{
		return;
	}

	// ── 7. 立方体 VB / IB（IMMUTABLE）────────────────────────
	const float cubeVerts[8 * 3] = {
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
	};
	const std::uint32_t cubeIdx[36] = {
		0, 2, 1,  0, 3, 2,
		4, 5, 6,  4, 6, 7,
		0, 4, 7,  0, 7, 3,
		1, 2, 6,  1, 6, 5,
		3, 7, 6,  3, 6, 2,
		0, 1, 5,  0, 5, 4,
	};

	auto makeUploadBuffer = [&](UINT64 sz, const void* data,
	                            ComPtr<ID3D12Resource>& out) -> bool
	{
		D3D12_HEAP_PROPERTIES uph = {}; uph.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC d = {};
		d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		d.Width = sz; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
		d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(m_d3dDevice->CreateCommittedResource(
				&uph, D3D12_HEAP_FLAG_NONE, &d,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(out.GetAddressOf()))))
			return false;
		void* p = nullptr;
		D3D12_RANGE r = {0, 0};
		if (FAILED(out->Map(0, &r, &p))) return false;
		std::memcpy(p, data, sz);
		D3D12_RANGE wr = {0, sz};
		out->Unmap(0, &wr);
		return true;
	};

	if (!makeUploadBuffer(sizeof(cubeVerts), cubeVerts, m_skyboxVB)) return;
	if (!makeUploadBuffer(sizeof(cubeIdx), cubeIdx, m_skyboxIB)) return;

	// transform CB は m_uploadRing から毎フレーム切り出す (専用リソース不要)

	m_skyboxPipelineReady = true;
}

/// @brief アップロードバッファから TextureCube へ 6 面コピーし
///        PIXEL_SHADER_RESOURCE 状態へ遷移する
/// @details m_graphicsCmdList が recording 中（beginFrame 内）に呼ぶ。
///          一度成功すれば m_skyboxNeedsUpload = false にして冪等化する。
void uploadSkyboxTextureDx12()
{
	if (!m_skyboxNeedsUpload) return;
	if (!m_skyboxTexture || !m_skyboxUpload) return;

	// 既に PIXEL_SHADER_RESOURCE 状態なら COPY_DEST に戻す（2 回目以降の再 upload 経路）
	if (m_skyboxTextureInPSR)
	{
		D3D12_RESOURCE_BARRIER pre = {};
		pre.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		pre.Transition.pResource   = m_skyboxTexture.Get();
		pre.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		pre.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
		pre.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_graphicsCmdList->ResourceBarrier(1, &pre);
		m_skyboxTextureInPSR = false;
	}

	for (int face = 0; face < kCubemapFaceCount; ++face)
	{
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource        = m_skyboxTexture.Get();
		dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = static_cast<UINT>(face);

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource        = m_skyboxUpload.Get();
		src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = static_cast<UINT64>(face) * m_skyboxFaceStride;
		src.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		src.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_skyboxFaceSize);
		src.PlacedFootprint.Footprint.Height    = static_cast<UINT>(m_skyboxFaceSize);
		src.PlacedFootprint.Footprint.Depth     = 1;
		src.PlacedFootprint.Footprint.RowPitch  = m_skyboxAlignedRow;

		m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}

	D3D12_RESOURCE_BARRIER b = {};
	b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource   = m_skyboxTexture.Get();
	b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);

	m_skyboxNeedsUpload = false;
	m_skyboxTextureInPSR = true;
}

/// @brief skybox を描画する（beginFrame 内、メッシュ描画の前）
/// @details MRT (MSAA color + normal) → single RTV (MSAA color) に切替えて
///          skybox を描画し、終わったら MRT に戻す。
void drawSkyboxIfNeededDx12()
{
	if (!m_skyboxEnabled) return;
	if (!m_skyboxPipelineReady || !m_skyboxTextureReady) return;
	if (m_skyboxDrawnThisFrame) return;
	if (!m_skyboxPSO || !m_skyboxRootSig) return;

	// 必要なら先にコピーする
	if (m_skyboxNeedsUpload)
	{
		uploadSkyboxTextureDx12();
	}

	// CB を更新する: viewNoTranslation + projection
	struct alignas(16) SkyCb
	{
		float viewNoTrans[4][4]{};
		float projection[4][4]{};
	};
	SkyCb cb{};

	// m_viewMatrix / m_projMatrix は既に glm::mat4
	glm::mat4 view = m_viewMatrix;
	view[3][0] = 0.0f; view[3][1] = 0.0f; view[3][2] = 0.0f;
	const glm::mat4 proj = m_projMatrix;
	toHLSL(cb.viewNoTrans, view);
	toHLSL(cb.projection,  proj);

	// CB は uploadRing から per-frame 切り出す (in-flight 前フレームの読取と競合しない)
	const auto skyCbAlloc = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!skyCbAlloc.valid()) return;

	// MSAA HDR color のみ + 深度に RT を切り替える (PSO の FP16 / 4x MSAA と一致)
	auto msaaRtv   = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &msaaRtv, FALSE, &dsvHandle);

	// skybox 用 root sig / PSO / バインディング
	m_graphicsCmdList->SetGraphicsRootSignature(m_skyboxRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_skyboxPSO.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, skyCbAlloc.gpuAddr);

	ID3D12DescriptorHeap* heaps[] = { m_skyboxSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(
		1, m_skyboxSrvHeap->GetGPUDescriptorHandleForHeapStart());

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_skyboxVB->GetGPUVirtualAddress();
	vbv.StrideInBytes  = sizeof(float) * 3;
	vbv.SizeInBytes    = sizeof(float) * 3 * 8;
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_skyboxIB->GetGPUVirtualAddress();
	ibv.SizeInBytes    = sizeof(std::uint32_t) * 36;
	ibv.Format         = DXGI_FORMAT_R32_UINT;
	m_graphicsCmdList->IASetIndexBuffer(&ibv);

	m_graphicsCmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

	// MRT (MSAA color + normal + DSV) を元に戻し、main root sig / main PSO に戻す
	auto normalRtv = m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = { msaaRtv, normalRtv };
	m_graphicsCmdList->OMSetRenderTargets(2, rtvHandles, FALSE, &dsvHandle);

	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(m_mainPSO.Get());

	m_skyboxDrawnThisFrame = true;
}
