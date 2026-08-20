#pragma once

/// @file ClodRenderer_Setup_impl.hpp
/// @brief ClodRenderer の初期化・資源生成 (caps gate / PSO / バッファ / heap)

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/dx12/clod/ClodImport.hpp>

namespace mitiru::render::clod
{

inline bool ClodRenderer::initialize(ID3D12Device* device, UINT frameCount)
{
	m_device = device;
	m_frameCount = frameCount;
	if (device == nullptr || !checkCaps(device)) { return false; }
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device2)))) { return false; }
	if (!createRootSignature() || !createPipelines() || !createCommandSignatures() ||
	    !createPersistentBuffers())
	{
		return false;
	}
	m_supported = true;
	return true;
}

inline const char* clodUnsupportedReason(ID3D12Device* device)
{
	if (device == nullptr) { return "clod: no device"; }

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7 = {};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7))) ||
	    opt7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
	{
		return "clod: mesh shaders unavailable - drawModel disabled";
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 opt1 = {};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &opt1, sizeof(opt1))) ||
	    !opt1.Int64ShaderOps)
	{
		return "clod: int64 shader ops unavailable - drawModel disabled";
	}
	D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_6 };
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) ||
	    sm.HighestShaderModel < D3D_SHADER_MODEL_6_6)
	{
		return "clod: shader model 6.6 unavailable - drawModel disabled";
	}
	return nullptr;
}

inline bool ClodRenderer::checkCaps(ID3D12Device* device) const
{
	const char* why = clodUnsupportedReason(device);
	if (why != nullptr)
	{
		debug::warnOnce("clod.caps", why);
		return false;
	}
	return true;
}

inline bool ClodRenderer::createRootSignature()
{
	// b0 CBV / t0-t11 SRV / u0-u10 UAV / b1 constants / s0 static sampler。
	// SM6.6 dynamic resources (ResourceDescriptorHeap) を使うため
	// CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED を立てる
	D3D12_ROOT_PARAMETER prm[25] = {};
	prm[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	prm[0].Descriptor.ShaderRegister = 0;
	for (UINT i = 0; i < 6; ++i)
	{
		prm[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		prm[1 + i].Descriptor.ShaderRegister = i;
	}
	for (UINT i = 0; i < 7; ++i)
	{
		prm[7 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		prm[7 + i].Descriptor.ShaderRegister = i;
	}
	prm[14].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	prm[14].Constants.ShaderRegister = 1;
	prm[14].Constants.Num32BitValues = 4;
	prm[15].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	prm[15].Descriptor.ShaderRegister = 7;
	prm[16].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	prm[16].Descriptor.ShaderRegister = 8;
	for (UINT i = 0; i < 3; ++i)
	{
		prm[17 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		prm[17 + i].Descriptor.ShaderRegister = 6 + i;
	}
	prm[20].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	prm[20].Descriptor.ShaderRegister = 9;
	prm[21].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	prm[21].Descriptor.ShaderRegister = 10;
	prm[22].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	prm[22].Descriptor.ShaderRegister = 9;
	prm[23].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	prm[23].Descriptor.ShaderRegister = 10;
	prm[24].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	prm[24].Descriptor.ShaderRegister = 11;

	D3D12_STATIC_SAMPLER_DESC smp = {};
	smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	smp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	smp.MaxLOD = D3D12_FLOAT32_MAX;
	smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 25;
	rsd.pParameters = prm;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers = &smp;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

	ComPtr<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
	{
		return false;
	}
	return SUCCEEDED(m_device->CreateRootSignature(0, sig->GetBufferPointer(),
	                                               sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)));
}

inline ClodRenderer::ComPtr<ID3D12PipelineState>
ClodRenderer::makeComputePso(const uint8_t* dxil, size_t size) const
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_rootSig.Get();
	pd.CS = { dxil, size };
	ComPtr<ID3D12PipelineState> pso;
	if (FAILED(m_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) { return nullptr; }
	return pso;
}

namespace detail
{
/// @brief pipeline state stream 用サブオブジェクト (mesh shader PSO は stream 必須)
template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename Inner>
struct alignas(void*) ClodStreamSubobject
{
	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
	Inner inner;
};
} // namespace detail

inline bool ClodRenderer::createPipelines()
{
	m_cullPso = makeComputePso(kClodCull, kClodCullSize);
	m_prepPso = makeComputePso(kClodPrep, kClodPrepSize);
	m_clearPso = makeComputePso(kClodClear, kClodClearSize);
	m_resolvePso = makeComputePso(kClodResolve, kClodResolveSize);
	m_swPso = makeComputePso(kClodSwRaster, kClodSwRasterSize);
	m_icullPso = makeComputePso(kClodInstCull, kClodInstCullSize);
	m_seedPso = makeComputePso(kClodSeed, kClodSeedSize);
	m_travPso = makeComputePso(kClodTraverse, kClodTraverseSize);
	m_prepQPso = makeComputePso(kClodPrepQueue, kClodPrepQueueSize);
	m_hzbPso = makeComputePso(kClodHzb, kClodHzbSize);
	if (!m_cullPso || !m_prepPso || !m_clearPso || !m_resolvePso || !m_swPso ||
	    !m_icullPso || !m_seedPso || !m_travPso || !m_prepQPso || !m_hzbPso)
	{
		return false;
	}

	// mesh PSO: RT / depth 無し (PS が visbuffer へ atomic 書きのみ)
	D3D12_RASTERIZER_DESC rast = {};
	rast.FillMode = D3D12_FILL_MODE_SOLID;
	rast.CullMode = D3D12_CULL_MODE_BACK;
	rast.FrontCounterClockwise = TRUE;
	rast.DepthClipEnable = TRUE;
	D3D12_DEPTH_STENCIL_DESC ds = {};
	ds.DepthEnable = FALSE;

	struct Stream
	{
		detail::ClodStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
		                            ID3D12RootSignature*> rootSig;
		detail::ClodStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,
		                            D3D12_SHADER_BYTECODE> ms;
		detail::ClodStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
		                            D3D12_SHADER_BYTECODE> ps;
		detail::ClodStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
		                            D3D12_RASTERIZER_DESC> rast;
		detail::ClodStreamSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
		                            D3D12_DEPTH_STENCIL_DESC> ds;
	} stream;
	stream.rootSig.inner = m_rootSig.Get();
	stream.ms.inner = { kClodMS, kClodMSSize };
	stream.ps.inner = { kClodPS, kClodPSSize };
	stream.rast.inner = rast;
	stream.ds.inner = ds;

	D3D12_PIPELINE_STATE_STREAM_DESC sd = { sizeof(stream), &stream };
	return SUCCEEDED(m_device2->CreatePipelineState(&sd, IID_PPV_ARGS(&m_meshPso)));
}

inline bool ClodRenderer::createCommandSignatures()
{
	D3D12_INDIRECT_ARGUMENT_DESC arg = {};
	arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
	D3D12_COMMAND_SIGNATURE_DESC cd = {};
	cd.ByteStride = 12;
	cd.NumArgumentDescs = 1;
	cd.pArgumentDescs = &arg;
	if (FAILED(m_device->CreateCommandSignature(&cd, nullptr, IID_PPV_ARGS(&m_dispatchMeshSig))))
	{
		return false;
	}
	arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	return SUCCEEDED(m_device->CreateCommandSignature(&cd, nullptr, IID_PPV_ARGS(&m_dispatchSig)));
}

inline ClodRenderer::ComPtr<ID3D12Resource>
ClodRenderer::makeBuffer(uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                         D3D12_RESOURCE_FLAGS flags) const
{
	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = heap;
	D3D12_RESOURCE_DESC rd = {};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = bytes;
	rd.Height = 1;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rd.Flags = flags;
	ComPtr<ID3D12Resource> r;
	if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
	                                             IID_PPV_ARGS(&r))))
	{
		return nullptr;
	}
	return r;
}

inline bool ClodRenderer::createPersistentBuffers()
{
	constexpr auto kUav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	constexpr auto kUavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	const uint64_t instVisWords = (kClodMaxInstances + 31) / 32;
	m_bInstVis = makeBuffer(instVisWords * 4, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bVisListHw = makeBuffer(static_cast<uint64_t>(kClodListCap) * 4, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bVisListSw = makeBuffer(static_cast<uint64_t>(kClodListCap) * 4, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bMarked = makeBuffer(static_cast<uint64_t>(kClodListCap) * 4, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bCounters = makeBuffer(32, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bIndArgs = makeBuffer(80, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bStats = makeBuffer(48, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bQueueA = makeBuffer(static_cast<uint64_t>(kClodQueueCap) * 8, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	m_bQueueB = makeBuffer(static_cast<uint64_t>(kClodQueueCap) * 8, D3D12_HEAP_TYPE_DEFAULT, kUavState, kUav);
	if (!m_bInstVis || !m_bVisListHw || !m_bVisListSw || !m_bMarked || !m_bCounters ||
	    !m_bIndArgs || !m_bStats || !m_bQueueA || !m_bQueueB)
	{
		return false;
	}
	// CB ×2 slot + instance / mesh table で 1 フレーム 256KB あれば足りる
	return m_ring.initialize(m_device, m_frameCount, 512 * 1024);
}

inline void ClodRenderer::ensureScreenResources(uint32_t width, uint32_t height)
{
	if (width == m_width && height == m_height && m_visBuf) { return; }
	if (waitIdle) { waitIdle(); }
	m_width = width;
	m_height = height;
	m_hzbW = 1;
	while (m_hzbW < width) { m_hzbW <<= 1; }
	m_hzbH = 1;
	while (m_hzbH < height) { m_hzbH <<= 1; }
	m_hzbMips = 1;
	for (uint32_t m = m_hzbW > m_hzbH ? m_hzbW : m_hzbH; m > 1; m >>= 1) { ++m_hzbMips; }

	constexpr auto kUav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	constexpr auto kUavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	m_visBuf = makeBuffer(static_cast<uint64_t>(width) * height * 8, D3D12_HEAP_TYPE_DEFAULT,
	                      kUavState, kUav);
	m_overdraw = makeBuffer(static_cast<uint64_t>(width) * height * 4, D3D12_HEAP_TYPE_DEFAULT,
	                        kUavState, kUav);

	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC td = {};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = width;
	td.Height = height;
	td.DepthOrArraySize = 1;
	td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	td.SampleDesc.Count = 1;
	td.Flags = kUav;
	m_colorTex.Reset();
	m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, kUavState, nullptr,
	                                  IID_PPV_ARGS(&m_colorTex));

	td.Width = m_hzbW;
	td.Height = m_hzbH;
	td.MipLevels = static_cast<UINT16>(m_hzbMips);
	td.Format = DXGI_FORMAT_R32_FLOAT;
	m_hzb.Reset();
	m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, kUavState, nullptr,
	                                  IID_PPV_ARGS(&m_hzb));
	m_prevViewValid = false;
	rebuildDescriptorHeap();
}

/// @brief heap 配置: [0]=offscreen UAV, [1..mips]=HZB mip UAV, [1+mips+i]=texture SRV
inline void ClodRenderer::rebuildDescriptorHeap()
{
	if (!m_colorTex || !m_hzb) { return; }
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = 1 + m_hzbMips + static_cast<UINT>(m_textures.size());
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_heap.Reset();
	if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) { return; }

	const UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
	D3D12_UNORDERED_ACCESS_VIEW_DESC rv = {};
	rv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_colorTex.Get(), nullptr, &rv, cpu);
	for (uint32_t m = 0; m < m_hzbMips; ++m)
	{
		cpu.ptr += inc;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
		uv.Format = DXGI_FORMAT_R32_FLOAT;
		uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uv.Texture2D.MipSlice = m;
		m_device->CreateUnorderedAccessView(m_hzb.Get(), nullptr, &uv, cpu);
	}
	for (size_t ti = 0; ti < m_textures.size(); ++ti)
	{
		cpu.ptr += inc;
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = m_scene.textures()[ti].srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
		                                        : DXGI_FORMAT_R8G8B8A8_UNORM;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = static_cast<UINT>(-1);
		m_device->CreateShaderResourceView(m_textures[ti].Get(), &sv, cpu);
	}
}

inline int ClodRenderer::ensureModel(const char* path)
{
	if (const auto it = m_registry.find(path); it != m_registry.end()) { return it->second; }

	// OBJ / glTF / GLB は `<source>.clod` cache 経由で読む (初回にここで変換)。
	// pack 配布時は変換せず、同梱済みの cache を探すだけ。
	std::string clodPath(path);
	if (isImportableModelPath(clodPath))
	{
		if (vfs::hasGlobalMount())
		{
			clodPath += ".clod";
		}
		else
		{
			std::string err;
			if (const auto cached = ensureClodCache(clodPath, err)) { clodPath = *cached; }
			else
			{
				debug::warnOnce(std::string("clod.import.") + path,
				                ("clod: モデル変換に失敗: " + err).c_str());
				m_registry.emplace(path, -1);
				return -1;
			}
		}
	}

	const auto blob = vfs::readGlobal(clodPath);
	int idx = -1;
	if (blob && !blob->empty())
	{
		std::string dir(path);
		const size_t slash = dir.find_last_of("/\\");
		dir = slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
		idx = m_scene.appendModel(blob->data(), blob->size(), dir);
	}
	if (idx < 0)
	{
		debug::warnOnce(std::string("clod.model.") + path,
		                (std::string("clod: cannot load model '") + path + "'").c_str());
	}
	m_registry.emplace(path, idx);
	return idx;
}

} // namespace mitiru::render::clod
