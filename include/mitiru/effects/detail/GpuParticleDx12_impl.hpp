#pragma once

/// @file GpuParticleDx12_impl.hpp
/// @brief GpuParticleDx12 の GPU リソース・パイプライン構築の実装本体（GpuParticleDx12.hpp から機械的分割）

#include <mitiru/effects/GpuParticleDx12.hpp>

#ifdef _WIN32

namespace mitiru::effects
{

/// @brief コマンドアロケータとコマンドリストを生成する
inline void GpuParticleDx12::createCommandResources()
{
	HRESULT hr = m_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_commandAllocator.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateCommandAllocator failed");
	}

	hr = m_device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocator.Get(), nullptr,
		IID_PPV_ARGS(m_commandList.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateCommandList failed");
	}

	m_commandList->Close();
}

/// @brief 構造化バッファとアップロードバッファを生成する
inline void GpuParticleDx12::createBuffers()
{
	const auto particleBufferSize = static_cast<UINT64>(
		m_maxParticles * sizeof(GpuParticle));

	/// パーティクル構造化バッファ（ピンポン、UAV対応）
	for (int i = 0; i < 2; ++i)
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = particleBufferSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(m_particleBuffer[i].GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (particle) failed");
		}
	}

	/// アップロードバッファ（CPU→GPU転送用）
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = particleBufferSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_uploadBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (upload) failed");
		}
	}

	/// Indirect Argsバッファ（DrawInstanced引数）
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = sizeof(D3D12_DRAW_ARGUMENTS);
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			nullptr,
			IID_PPV_ARGS(m_indirectArgsBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (indirect) failed");
		}
	}

	/// 定数バッファ（アップロードヒープ）
	{
		const UINT64 cbSize = 256; ///< 256バイトアライメント

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = cbSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(m_simConstantUpload.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (sim CB) failed");
		}

		hr = m_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(m_renderConstantUpload.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (render CB) failed");
		}
	}

	/// リードバックバッファ（コンパクション用）
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_READBACK;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = particleBufferSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT hr = m_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(m_readbackBuffer.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommittedResource (readback) failed");
		}
	}

	/// SRV/UAVデスクリプタヒープを生成する
	createDescriptorHeap();
}

/// @brief SRV/UAVデスクリプタヒープを生成する
/// @details レイアウト:
///   0: SRV パーティクルバッファ[0]
///   1: SRV パーティクルバッファ[1]
///   2: UAV パーティクルバッファ[0]
///   3: UAV パーティクルバッファ[1]
///   4: UAV IndirectArgsバッファ
inline void GpuParticleDx12::createDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 5;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = m_device->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(m_srvUavHeap.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateDescriptorHeap failed");
	}

	m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto cpuHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

	/// SRV[0], SRV[1]
	for (int i = 0; i < 2; ++i)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping =
			D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_maxParticles;
		srvDesc.Buffer.StructureByteStride = sizeof(GpuParticle);

		D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
		handle.ptr += static_cast<SIZE_T>(i) * m_descriptorSize;
		m_device->CreateShaderResourceView(
			m_particleBuffer[i].Get(), &srvDesc, handle);
	}

	/// UAV[0], UAV[1]
	for (int i = 0; i < 2; ++i)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = m_maxParticles;
		uavDesc.Buffer.StructureByteStride = sizeof(GpuParticle);

		D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
		handle.ptr += static_cast<SIZE_T>(2 + i) * m_descriptorSize;
		m_device->CreateUnorderedAccessView(
			m_particleBuffer[i].Get(), nullptr, &uavDesc, handle);
	}

	/// UAV IndirectArgs
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = 1;
		uavDesc.Buffer.StructureByteStride = sizeof(D3D12_DRAW_ARGUMENTS);

		D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
		handle.ptr += 4 * m_descriptorSize;
		m_device->CreateUnorderedAccessView(
			m_indirectArgsBuffer.Get(), nullptr, &uavDesc, handle);
	}
}

/// @brief コンピュートパイプラインとレンダリングパイプラインを生成する
inline void GpuParticleDx12::createPipelines()
{
	createComputePipeline();
	createRenderPipeline();
	createCommandSignature();
}

/// @brief コンピュートパイプラインを生成する
inline void GpuParticleDx12::createComputePipeline()
{
	/// ルートシグネチャ: b0(CBV) + t0(SRV table) + u0(UAV table) + u1(UAV table)
	D3D12_ROOT_PARAMETER rootParams[4] = {};

	/// パラメータ0: CBV (b0)
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	/// パラメータ1: SRV テーブル (t0)
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	/// パラメータ2: UAV テーブル (u0)
	D3D12_DESCRIPTOR_RANGE uavRange0 = {};
	uavRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange0.NumDescriptors = 1;
	uavRange0.BaseShaderRegister = 0;
	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange0;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	/// パラメータ3: UAV テーブル (u1)
	D3D12_DESCRIPTOR_RANGE uavRange1 = {};
	uavRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange1.NumDescriptors = 1;
	uavRange1.BaseShaderRegister = 1;
	rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[3].DescriptorTable.pDescriptorRanges = &uavRange1;
	rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 4;
	rsDesc.pParameters = rootParams;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(
		&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		signature.GetAddressOf(), error.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: SerializeRootSignature (compute) failed");
	}

	hr = m_device->CreateRootSignature(
		0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(m_computeRootSignature.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateRootSignature (compute) failed");
	}

	/// コンピュートシェーダーをコンパイルする
	auto csBlob = compileHLSL(
		DX12_PARTICLE_COMPUTE_HLSL, "CSMain", "cs_5_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_computeRootSignature.Get();
	psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};

	hr = m_device->CreateComputePipelineState(
		&psoDesc,
		IID_PPV_ARGS(m_computePSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateComputePipelineState failed");
	}
}

/// @brief レンダリングパイプラインを生成する
inline void GpuParticleDx12::createRenderPipeline()
{
	/// ルートシグネチャ: b0(CBV) + t0(SRV table)
	D3D12_ROOT_PARAMETER rootParams[2] = {};

	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 2;
	rsDesc.pParameters = rootParams;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(
		&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		signature.GetAddressOf(), error.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: SerializeRootSignature (render) failed");
	}

	hr = m_device->CreateRootSignature(
		0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(m_renderRootSignature.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateRootSignature (render) failed");
	}

	/// シェーダーをコンパイルする
	auto vsBlob = compileHLSL(
		DX12_PARTICLE_VS_HLSL, "VSMain", "vs_5_0");
	auto psBlob = compileHLSL(
		DX12_PARTICLE_PS_HLSL, "PSMain", "ps_5_0");

	/// グラフィックスPSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = {nullptr, 0};
	psoDesc.pRootSignature = m_renderRootSignature.Get();
	psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
	psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

	/// ラスタライザステート（カリングなし）
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	/// 加算ブレンドステート
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	auto& rt = psoDesc.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_ONE;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	/// 深度ステンシル（深度読み取りのみ、書き込みなし）
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	hr = m_device->CreateGraphicsPipelineState(
		&psoDesc,
		IID_PPV_ARGS(m_renderPSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateGraphicsPipelineState failed");
	}
}

/// @brief ExecuteIndirect用のコマンドシグネチャを生成する
inline void GpuParticleDx12::createCommandSignature()
{
	D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
	argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

	D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
	sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
	sigDesc.NumArgumentDescs = 1;
	sigDesc.pArgumentDescs = &argDesc;

	HRESULT hr = m_device->CreateCommandSignature(
		&sigDesc, nullptr,
		IID_PPV_ARGS(m_commandSignature.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateCommandSignature failed");
	}
}

/// @brief フェンスを生成する
inline void GpuParticleDx12::createFence()
{
	HRESULT hr = m_device->CreateFence(
		0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(m_fence.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateFence failed");
	}

	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!m_fenceEvent)
	{
		throw std::runtime_error(
			"GpuParticleDx12: CreateEvent failed");
	}
}

} // namespace mitiru::effects

#endif // _WIN32
