#pragma once

/// @file Renderer3D_DX12_PBR.hpp
/// @brief Renderer3D_DX12 の PBR / IBL 描画の宣言
/// @details Renderer3D_DX12 のクラス内から include する部分ヘッダ。外から直接 include しない。

#include <cstring>
#include <vector>

#include <mitiru/render/dx12/DX12PBRShaders.hpp>

/// @brief 環境 Cubemap が変わるまで、CPU 畳み込み結果と GPU リソースを再利用する。
/// irradiance は 1 枚、prefiltered は roughness 別の mip 連鎖 (kPbrPrefilterMipCount 枚)、
/// 環境 BRDF 表は環境に依らないので初回だけ焼く。
void ensurePBREnvironmentTexturesDx12()
{
	if (!m_d3dDevice) return;
	if (!m_pbrEnvironmentCubemap.valid()) return;
	if (m_pbrEnvironmentTextureReady) return;

	const int faceSize = m_pbrEnvironmentCubemap.faceSize();
	const int convSize = std::min(faceSize, 64);  // 畳み込み結果は高解像度が要らない
	m_pbrIrradianceCubemap = m_pbrEnvironmentCubemap.irradiance(convSize);
	if (!m_pbrIrradianceCubemap.valid()) { return; }

	m_pbrPrefilteredChain.clear();
	m_pbrPrefilterSizes.clear();
	for (int mip = 0; mip < kPbrPrefilterMipCount; ++mip)
	{
		const int   size      = std::max(convSize >> mip, 4);
		const float roughness = static_cast<float>(mip) / static_cast<float>(kPbrPrefilterMipCount - 1);
		Cubemap level = m_pbrEnvironmentCubemap.prefilterSpecular(size, roughness);
		if (!level.valid()) { return; }
		m_pbrPrefilteredChain.push_back(std::move(level));
		m_pbrPrefilterSizes.push_back(size);
	}

	const auto alignRow = [](UINT bytes) {
		return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
	};
	const auto alignPlace = [](UINT bytes) {
		return (bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
	};

	// irradiance (mip 1 枚)
	const int  outSize    = m_pbrIrradianceCubemap.faceSize();
	const UINT rawRowBytes = static_cast<UINT>(outSize) * 4u;
	const UINT alignedRow  = alignRow(rawRowBytes);
	const UINT faceStride  = alignPlace(alignedRow * static_cast<UINT>(outSize));
	const UINT uploadSize  = faceStride * static_cast<UINT>(kCubemapFaceCount);

	// prefiltered (mip 連鎖): [mip][face] の順に置く
	m_pbrPrefilterMipOffsets.clear();
	m_pbrPrefilterFaceStrides.clear();
	m_pbrPrefilterAlignedRows.clear();
	UINT prefilterUploadSize = 0;
	for (int mip = 0; mip < kPbrPrefilterMipCount; ++mip)
	{
		const UINT size   = static_cast<UINT>(m_pbrPrefilterSizes[static_cast<std::size_t>(mip)]);
		const UINT row    = alignRow(size * 4u);
		const UINT stride = alignPlace(row * size);
		m_pbrPrefilterMipOffsets.push_back(prefilterUploadSize);
		m_pbrPrefilterFaceStrides.push_back(stride);
		m_pbrPrefilterAlignedRows.push_back(row);
		prefilterUploadSize += stride * static_cast<UINT>(kCubemapFaceCount);
	}

	const bool sizeChanged =
		!m_pbrIrradianceTexture || !m_pbrPrefilteredTexture
		|| !m_pbrEnvironmentSrvHeap || m_pbrEnvironmentFaceSize != outSize;

	if (sizeChanged)
	{

		const auto makeCube = [&](int size, UINT mips, gfx::GpuResource& out) -> bool {
			D3D12_RESOURCE_DESC d = {};
			d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			d.Width            = static_cast<UINT64>(size);
			d.Height           = static_cast<UINT>(size);
			d.DepthOrArraySize = kCubemapFaceCount;
			d.MipLevels        = static_cast<UINT16>(mips);
			d.Format           = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			d.SampleDesc.Count = 1;
			d.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			return SUCCEEDED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, d,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, out));
		};
		const auto makeUpload = [&](UINT bytes, gfx::GpuResource& out) -> bool {
			return SUCCEEDED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_UPLOAD, bytes,
				D3D12_RESOURCE_STATE_GENERIC_READ, out));
		};

		if (!makeCube(outSize, 1, m_pbrIrradianceTexture)) { return; }
		if (!makeCube(m_pbrPrefilterSizes[0], static_cast<UINT>(kPbrPrefilterMipCount), m_pbrPrefilteredTexture)) { return; }
		if (!makeUpload(uploadSize, m_pbrIrradianceUpload)) { return; }
		if (!makeUpload(prefilterUploadSize, m_pbrPrefilteredUpload)) { return; }

		// 環境 BRDF 表 (環境に依らない。sizeChanged のたびに作り直しても安いが、初回だけ焼く)
		if (!m_pbrBrdfLutTexture)
		{
			D3D12_RESOURCE_DESC d = {};
			d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			d.Width            = static_cast<UINT64>(kPbrBrdfLutSize);
			d.Height           = static_cast<UINT>(kPbrBrdfLutSize);
			d.DepthOrArraySize = 1;
			d.MipLevels        = 1;
			d.Format           = DXGI_FORMAT_R32G32_FLOAT;
			d.SampleDesc.Count = 1;
			d.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, d,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, m_pbrBrdfLutTexture)))
			{
				return;
			}
			const UINT lutRow = alignRow(static_cast<UINT>(kPbrBrdfLutSize) * 8u);
			if (!makeUpload(lutRow * static_cast<UINT>(kPbrBrdfLutSize), m_pbrBrdfLutUpload)) { return; }
			const auto lut = computeBrdfLut(kPbrBrdfLutSize, 128);
			void* mapped = nullptr;
			D3D12_RANGE readRange = {0, 0};
			if (FAILED(m_pbrBrdfLutUpload->Map(0, &readRange, &mapped))) { return; }
			for (int y = 0; y < kPbrBrdfLutSize; ++y)
			{
				std::memcpy(static_cast<std::uint8_t*>(mapped) + static_cast<std::size_t>(y) * lutRow,
				            lut.rg.data() + static_cast<std::size_t>(y) * kPbrBrdfLutSize * 2,
				            static_cast<std::size_t>(kPbrBrdfLutSize) * 8u);
			}
			m_pbrBrdfLutUpload->Unmap(0, nullptr);
			m_pbrBrdfLutInPSR = false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC srvHd = {};
		srvHd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHd.NumDescriptors = 3;  // t0=irradiance, t1=prefiltered (mip 連鎖), t2=BRDF LUT
		srvHd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		// 前の環境マップで描いているフレームがまだ読んでいるので、ヒープは GPU 完了まで預ける
		if (m_device) { m_device->deferRelease(m_pbrEnvironmentSrvHeap); }
		m_pbrEnvironmentSrvHeap.Reset();
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(
				&srvHd, IID_PPV_ARGS(m_pbrEnvironmentSrvHeap.GetAddressOf()))))
		{
			return;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		srv.ViewDimension               = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.TextureCube.MipLevels       = 1;
		srv.TextureCube.MostDetailedMip = 0;

		const UINT srvIncrement = m_d3dDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE handle =
			m_pbrEnvironmentSrvHeap->GetCPUDescriptorHandleForHeapStart();
		m_d3dDevice->CreateShaderResourceView(m_pbrIrradianceTexture.Get(), &srv, handle);
		handle.ptr += srvIncrement;
		srv.TextureCube.MipLevels = static_cast<UINT>(kPbrPrefilterMipCount);
		m_d3dDevice->CreateShaderResourceView(m_pbrPrefilteredTexture.Get(), &srv, handle);
		handle.ptr += srvIncrement;
		D3D12_SHADER_RESOURCE_VIEW_DESC lutSrv = {};
		lutSrv.Format                    = DXGI_FORMAT_R32G32_FLOAT;
		lutSrv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
		lutSrv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		lutSrv.Texture2D.MipLevels       = 1;
		m_d3dDevice->CreateShaderResourceView(m_pbrBrdfLutTexture.Get(), &lutSrv, handle);

		m_pbrEnvironmentFaceStride = faceStride;
		m_pbrEnvironmentAlignedRow = alignedRow;
		m_pbrEnvironmentFaceSize   = outSize;
		m_pbrEnvironmentTextureInPSR = false;
	}

	const auto copyFaces = [](std::uint8_t* dstBase, const Cubemap& cm, UINT faceStrideBytes, UINT rowPitch)
	{
		const int  size    = cm.faceSize();
		const UINT rowRaw  = static_cast<UINT>(size) * 4u;
		for (int face = 0; face < kCubemapFaceCount; ++face)
		{
			const auto& px = cm.face(face).pixels();
			auto* dst = dstBase + face * faceStrideBytes;
			for (int row = 0; row < size; ++row)
			{
				std::memcpy(dst + row * rowPitch, px.data() + row * rowRaw, rowRaw);
			}
		}
	};
	{
		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		if (FAILED(m_pbrIrradianceUpload->Map(0, &readRange, &mapped))) { return; }
		copyFaces(static_cast<std::uint8_t*>(mapped), m_pbrIrradianceCubemap, m_pbrEnvironmentFaceStride, m_pbrEnvironmentAlignedRow);
		const D3D12_RANGE wroteAll = {0, uploadSize};
		m_pbrIrradianceUpload->Unmap(0, &wroteAll);
	}
	{
		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		if (FAILED(m_pbrPrefilteredUpload->Map(0, &readRange, &mapped))) { return; }
		for (int mip = 0; mip < kPbrPrefilterMipCount; ++mip)
		{
			const auto m = static_cast<std::size_t>(mip);
			copyFaces(static_cast<std::uint8_t*>(mapped) + m_pbrPrefilterMipOffsets[m], m_pbrPrefilteredChain[m],
			          m_pbrPrefilterFaceStrides[m], m_pbrPrefilterAlignedRows[m]);
		}
		const D3D12_RANGE wroteAll = {0, prefilterUploadSize};
		m_pbrPrefilteredUpload->Unmap(0, &wroteAll);
	}

	m_pbrEnvironmentNeedsUpload  = true;
	m_pbrEnvironmentTextureReady = true;
}

/// @brief drawMesh のコマンド記録中に呼ぶ。
void uploadPBREnvironmentTexturesDx12()
{
	if (!m_pbrEnvironmentNeedsUpload) return;
	if (!m_pbrIrradianceTexture || !m_pbrPrefilteredTexture || !m_pbrBrdfLutTexture) return;

	const auto transition = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = res;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter  = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_graphicsCmdList->ResourceBarrier(1, &b);
	};

	if (m_pbrEnvironmentTextureInPSR)
	{
		transition(m_pbrIrradianceTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		transition(m_pbrPrefilteredTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		m_pbrEnvironmentTextureInPSR = false;
	}

	const auto copyFace = [&](ID3D12Resource* tex, UINT subresource, ID3D12Resource* upload, UINT64 offset,
	                          UINT size, UINT rowPitch)
	{
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource        = tex;
		dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = subresource;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource        = upload;
		src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = offset;
		src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		src.PlacedFootprint.Footprint.Width    = size;
		src.PlacedFootprint.Footprint.Height   = size;
		src.PlacedFootprint.Footprint.Depth    = 1;
		src.PlacedFootprint.Footprint.RowPitch = rowPitch;
		m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	};

	for (int face = 0; face < kCubemapFaceCount; ++face)
	{
		copyFace(m_pbrIrradianceTexture.Get(), static_cast<UINT>(face), m_pbrIrradianceUpload.Get(),
		         static_cast<UINT64>(face) * m_pbrEnvironmentFaceStride,
		         static_cast<UINT>(m_pbrEnvironmentFaceSize), m_pbrEnvironmentAlignedRow);
	}
	// subresource = mip + face * mipCount (配列スライスごとに mip が並ぶ)
	for (int face = 0; face < kCubemapFaceCount; ++face)
	{
		for (int mip = 0; mip < kPbrPrefilterMipCount; ++mip)
		{
			const auto m = static_cast<std::size_t>(mip);
			copyFace(m_pbrPrefilteredTexture.Get(), static_cast<UINT>(mip + face * kPbrPrefilterMipCount),
			         m_pbrPrefilteredUpload.Get(),
			         static_cast<UINT64>(m_pbrPrefilterMipOffsets[m]) + static_cast<UINT64>(face) * m_pbrPrefilterFaceStrides[m],
			         static_cast<UINT>(m_pbrPrefilterSizes[m]), m_pbrPrefilterAlignedRows[m]);
		}
	}

	transition(m_pbrIrradianceTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	transition(m_pbrPrefilteredTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	if (!m_pbrBrdfLutInPSR)
	{
		const UINT lutRow = (static_cast<UINT>(kPbrBrdfLutSize) * 8u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource        = m_pbrBrdfLutTexture.Get();
		dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource        = m_pbrBrdfLutUpload.Get();
		src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R32G32_FLOAT;
		src.PlacedFootprint.Footprint.Width    = static_cast<UINT>(kPbrBrdfLutSize);
		src.PlacedFootprint.Footprint.Height   = static_cast<UINT>(kPbrBrdfLutSize);
		src.PlacedFootprint.Footprint.Depth    = 1;
		src.PlacedFootprint.Footprint.RowPitch = lutRow;
		m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		transition(m_pbrBrdfLutTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_pbrBrdfLutInPSR = true;
	}

	m_pbrEnvironmentNeedsUpload  = false;
	m_pbrEnvironmentTextureInPSR = true;
}

/// @brief IBL Cubemap 用の t0 / t1 と環境 BRDF 表 t2 を確保するため、Toon / Phong とは別の root signature を使う。
void ensurePBRPipelineDx12()
{
	if (!m_d3dDevice) return;
	if (m_pbrPipelineReady) return;

	D3D12_ROOT_PARAMETER params[4] = {};
	params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;  // CbTransform
	params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

	params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].Descriptor.ShaderRegister = 1;  // CbLighting（Toon/Phong と共通構造体）
	params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

	params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[2].Descriptor.ShaderRegister = 2;  // CbPBRExtra
	params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

	static D3D12_DESCRIPTOR_RANGE envRange = {};
	envRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	envRange.NumDescriptors                    = 3;  // t0=irradiance, t1=prefiltered, t2=BRDF LUT
	envRange.BaseShaderRegister                = 0;
	envRange.OffsetInDescriptorsFromTableStart = 0;
	params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[3].DescriptorTable.NumDescriptorRanges = 1;
	params[3].DescriptorTable.pDescriptorRanges   = &envRange;
	params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister   = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters     = 4;
	rsd.pParameters       = params;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers   = &sampler;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
	if (FAILED(D3D12SerializeRootSignature(
			&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
			sigBlob.GetAddressOf(), errBlob.GetAddressOf())))
	{
		return;
	}
	if (FAILED(m_d3dDevice->CreateRootSignature(
			0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
			IID_PPV_ARGS(m_pbrRootSig.GetAddressOf()))))
	{
		return;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, compileErr;
	if (FAILED(gfx::compileDx12Shader(PBR_VS_3D, "VSMain", "vs_5_0", 0,
		vsBlob.GetAddressOf(), compileErr.GetAddressOf())))
	{
		return;
	}
	if (FAILED(gfx::compileDx12Shader(PBR_IBL_PS_3D, "PSMain", "ps_5_0", 0,
		psBlob.GetAddressOf(), compileErr.GetAddressOf())))
	{
		return;
	}

	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayoutInternal(inputLayout, inputCount);  // Vertex3D と同一レイアウトを流用

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pbrRootSig.Get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements        = inputCount;

	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.DepthStencilState.DepthEnable    = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// MRT は color HDR と world normal の 2 枚にし、PBR メッシュも outline / occlusion の対象にする。
	psoDesc.NumRenderTargets      = 2;
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.RTVFormats[1]         = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count      = 4;  // 共有 depth/MSAA color と sample count を揃える

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_pbrPSO.GetAddressOf()))))
	{
		return;
	}

	m_pbrPipelineReady = true;
}

/// @brief PBR IBL でメッシュを描画する。
/// @return PBR で描画したときは true。環境 Cubemap が未設定、またはパイプラインを構築できないときは false。
[[nodiscard]] bool drawMeshPBRDx12(const Mesh& mesh, const sgc::Mat4f& worldTransform,
                                    const Material& material)
{
	if (!m_pbrEnvironmentCubemap.valid()) { return false; }

	ensurePBRPipelineDx12();
	ensurePBREnvironmentTexturesDx12();
	if (!m_pbrPipelineReady || !m_pbrEnvironmentTextureReady) { return false; }
	if (m_pbrEnvironmentNeedsUpload) { uploadPBREnvironmentTexturesDx12(); }

	const auto cbTransformAddr = uploadTransformCB(worldTransform);
	const auto cbLightingAddr  = uploadLightingCB(material);
	if (cbTransformAddr == 0 || cbLightingAddr == 0) { return false; }

	DX12CbPBRExtra extra;
	extra.metallicRoughnessAO[0] = material.metallic;
	extra.metallicRoughnessAO[1] = material.roughness;
	extra.metallicRoughnessAO[2] = 1.0f;
	extra.ambientIntensityHasIBL[0] = 1.0f;
	extra.ambientIntensityHasIBL[1] = 1.0f;
	const auto extraAlloc = m_uploadRing.upload(&extra, sizeof(extra), 256);
	if (!extraAlloc.valid()) { return false; }

	const auto& verts = mesh.vertices();
	const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex3D));
	auto* vb = acquireMeshBuffer(m_meshVBCache, mesh, verts.data(), vbSize);
	if (!vb) { return false; }

	m_graphicsCmdList->SetGraphicsRootSignature(m_pbrRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_pbrPSO.Get());

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vb->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbSize;
	vbv.StrideInBytes  = sizeof(Vertex3D);
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbTransformAddr);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbLightingAddr);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(2, extraAlloc.gpuAddr);

	ID3D12DescriptorHeap* heaps[] = { m_pbrEnvironmentSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(
		3, m_pbrEnvironmentSrvHeap->GetGPUDescriptorHandleForHeapStart());

	const auto& indices = mesh.indices();
	if (!indices.empty())
	{
		const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));
		auto* ib = acquireMeshBuffer(m_meshIBCache, mesh, indices.data(), ibSize);
		if (!ib)
		{
			// 頂点だけを描画済みにしないよう、メインの root signature と PSO に戻して抜ける。
			m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
			m_graphicsCmdList->SetPipelineState(m_mainPSO.Get());
			return false;
		}

		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = ib->GetGPUVirtualAddress();
		ibv.SizeInBytes    = ibSize;
		ibv.Format         = DXGI_FORMAT_R32_UINT;
		m_graphicsCmdList->IASetIndexBuffer(&ibv);
		m_graphicsCmdList->DrawIndexedInstanced(
			static_cast<UINT>(indices.size()), 1, 0, 0, 0);
	}
	else
	{
		m_graphicsCmdList->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
	}

	// 次の draw call に備え、メインの root signature と PSO に戻す
	// (drawSkyboxIfNeededDx12 と同じ規約)。
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(m_mainPSO.Get());

	return true;
}
