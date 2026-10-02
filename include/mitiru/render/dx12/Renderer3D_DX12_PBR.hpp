#pragma once

/// @file Renderer3D_DX12_PBR.hpp
/// @brief Renderer3D_DX12 の IBL 用テクスチャ (環境キューブマップの畳み込みと GPU への転送)
/// @details Renderer3D_DX12 のクラス内から include する部分ヘッダ。外から直接 include しない。
///          描画はメインの PBR PS (DX12LitShaders.hpp) が場面の表 (t8〜t10) から読む。

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
		|| m_pbrEnvironmentFaceSize != outSize;

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
