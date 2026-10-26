#pragma once

/// @file RenderPipeline2D_Lit.hpp
/// @brief 2D ライト (R6、ADR 0099) の DX12 の実装と、光の状態の持ち方
// RenderPipeline2D.hpp からだけ include する。光を使わない描画は DefaultShaders の PS・root signature・PSO をそのまま使い、
// 光が有効なフレームだけ、ここで作る lit の PS と PSO に差し替える (RenderPipeline2D_Dx12.hpp / QuadBatch.hpp / TexturedBatch.hpp が bindLit を呼ぶ)。

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::render
{

inline bool RenderPipeline2D::supportsLighting2D() const noexcept
{
	if (!m_valid || !m_lit2D) { return false; }
	if (m_batchSink) { return m_batchSink->supportsLighting2D(); }
#ifdef _WIN32
	return m_useDx12Path && m_lit2D->dx12Ready;
#else
	return false;
#endif
}

inline void RenderPipeline2D::clearLighting2D() noexcept
{
	if (!m_lit2DActive) { return; }
	m_lit2DActive = false;
	m_lit2D->lighting = Lighting2D{};
	if (m_batchSink) { m_batchSink->setLighting2D(nullptr); }
}

inline void RenderPipeline2D::addLight2D(const Light2DPod& light)
{
	if (!supportsLighting2D()) { return; }
	Lighting2D& l = m_lit2D->lighting;
	if (l.count >= static_cast<std::uint32_t>(kMaxLights2D)) { return; }
	m_lit2D->packed[l.count] = light2d::pack(light);
	l.lights[l.count++] = light;
	m_lit2DActive = true;
	if (m_batchSink) { m_batchSink->setLighting2D(&l); }
}

inline void RenderPipeline2D::setAmbient2D(float r, float g, float b)
{
	if (!supportsLighting2D()) { return; }
	Lighting2D& l = m_lit2D->lighting;
	l.ambient[0] = r; l.ambient[1] = g; l.ambient[2] = b;
	l.ambientSet = true;
	m_lit2DActive = true;
	if (m_batchSink) { m_batchSink->setLighting2D(&l); }
}

inline void RenderPipeline2D::setSpriteNormalMap(const void* colorKey, const void* normalKey, int w, int h, const std::uint8_t* rgba)
{
	if (!m_lit2DActive || colorKey == nullptr) { return; }
	if (m_batchSink) { m_batchSink->setNormalMap(colorKey, normalKey, w, h, rgba); return; }
	m_lit2D->normals[colorKey] = detail::Lit2DNormalRef{normalKey, w, h, rgba};
}

#ifdef _WIN32

namespace detail
{

constexpr D3D12_INPUT_ELEMENT_DESC kLitClassicLayout[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

constexpr D3D12_INPUT_ELEMENT_DESC kLitQuadLayout[] = {
	{ "QUADRECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
	{ "QUADUV",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
	{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
};

/// 色 (s0 = linear) と法線 (同じ s0 / s1) を読む lit の root signature。
/// 0: VS CBV b0、1: PS CBV b0 (Lit2DConstants)、2: PS SRV table t0..t1
inline Microsoft::WRL::ComPtr<ID3D12RootSignature> createLitRootSignature(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;
	srvRange.BaseShaderRegister = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER params[3] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].Descriptor.ShaderRegister = 0;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges = &srvRange;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	for (int i = 0; i < 2; ++i)
	{
		samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].ShaderRegister = static_cast<UINT>(i);
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 3;
	rsd.pParameters = params;
	rsd.NumStaticSamplers = 2;
	rsd.pStaticSamplers = samplers;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) || !sigBlob) { return {}; }
	if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) { return {}; }
	return rootSig;
}

inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> createLitHeap(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 2;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
	if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)))) { return {}; }
	return heap;
}

inline void writeLitSrv(ID3D12Device* device, ID3D12DescriptorHeap* heap, UINT index, ID3D12Resource* resource)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.Texture2D.MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	device->CreateShaderResourceView(resource, &desc, h);   // resource が null なら空の記述子
}

}  // namespace detail

inline void RenderPipeline2D::buildDx12LitResources(ID3D12Device* device)
{
	static_assert(detail::Lit2DState::kRing == kDx12Ring);
	m_lit2D = std::make_unique<detail::Lit2DState>();
	detail::Lit2DState& lit = *m_lit2D;

	lit.rootSig = detail::createLitRootSignature(device);
	Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> quadVsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> err;
	(void)gfx::compileDx12Shader(LIT_PS_2D, "PSMain", "ps_5_0", 0, &psBlob, &err);
	const bool wantsQuad = quadPipelineReady();
	if (wantsQuad) { (void)gfx::compileDx12Shader(DEFAULT_QUAD_VS_2D, "VSMain", "vs_5_0", 0, &quadVsBlob, &err); }
	if (!lit.rootSig || !psBlob || !m_dx12VsBlob || (wantsQuad && !quadVsBlob))
	{
		MITIRU_LOG_WARN("RenderPipeline2D", "2D light shader or root signature build failed; 2D lights are disabled");
		return;
	}

	const UINT classicCount = static_cast<UINT>(std::size(detail::kLitClassicLayout));
	const UINT quadCount = static_cast<UINT>(std::size(detail::kLitQuadLayout));
	try
	{
		lit.classic = buildDx12Pso(device, lit.rootSig.Get(), m_dx12VsBlob.Get(), psBlob.Get(), detail::kLitClassicLayout, classicCount);
		if (wantsQuad)
		{
			lit.quad = buildDx12Pso(device, lit.rootSig.Get(), quadVsBlob.Get(), psBlob.Get(), detail::kLitQuadLayout, quadCount);
		}
	}
	catch (...)
	{
		lit.classic.Reset();
		lit.quad.Reset();
	}
	if (!lit.classic || (wantsQuad && !lit.quad))
	{
		MITIRU_LOG_WARN("RenderPipeline2D", "2D light PSO build failed; 2D lights are disabled");
		return;
	}
	// 8x MSAA の変種。作れない環境は光なしの経路も MSAA の変種を持たないので、bindLit は MSAA の描画先では使わない
	lit.classicMsaa = tryBuildDx12PsoMsaa(device, lit.rootSig.Get(), m_dx12VsBlob.Get(), psBlob.Get(), detail::kLitClassicLayout, classicCount);
	if (wantsQuad)
	{
		lit.quadMsaa = tryBuildDx12PsoMsaa(device, lit.rootSig.Get(), quadVsBlob.Get(), psBlob.Get(), detail::kLitQuadLayout, quadCount);
	}

	constexpr std::uint32_t kCbBytes = (sizeof(detail::Lit2DConstants) + 255u) & ~255u;
	for (int i = 0; i < kDx12Ring; ++i) { lit.cb[i] = createUploadBufferDx12(device, kCbBytes); }
	lit.nullHeap = detail::createLitHeap(device);
	if (!lit.nullHeap)
	{
		MITIRU_LOG_WARN("RenderPipeline2D", "2D light descriptor heap build failed; 2D lights are disabled");
		return;
	}
	detail::writeLitSrv(device, lit.nullHeap.Get(), 0, nullptr);
	detail::writeLitSrv(device, lit.nullHeap.Get(), 1, nullptr);
	lit.dx12Ready = true;
}

inline ID3D12DescriptorHeap* RenderPipeline2D::litHeap(std::uint32_t colorHandle, std::uint32_t normalHandle)
{
	detail::Lit2DState& lit = *m_lit2D;
	ID3D12Resource* color = m_dx12SpriteTextures[colorHandle - 1].tex.Get();
	ID3D12Resource* normal = normalHandle != 0 ? m_dx12SpriteTextures[normalHandle - 1].tex.Get() : nullptr;
	const std::uint64_t key = (static_cast<std::uint64_t>(colorHandle) << 32) | normalHandle;
	const auto it = lit.heaps.find(key);
	if (it != lit.heaps.end())
	{
		if (it->second.color == color && it->second.normal == normal) { return it->second.heap.Get(); }
		// テクスチャが読み直されて資源が替わった。古い heap を使うサブミットが残っているかもしれないので、全部終わってから捨てる
		waitDx12Fence();
		lit.heaps.erase(it);
	}
	auto heap = detail::createLitHeap(m_dx12NativeDevice.Get());
	if (!heap) { return nullptr; }
	detail::writeLitSrv(m_dx12NativeDevice.Get(), heap.Get(), 0, color);
	detail::writeLitSrv(m_dx12NativeDevice.Get(), heap.Get(), 1, normal);
	ID3D12DescriptorHeap* raw = heap.Get();
	lit.heaps.emplace(key, detail::Lit2DState::HeapEntry{std::move(heap), color, normal});
	return raw;
}

inline bool RenderPipeline2D::bindLit(LitBind& out, int slot, std::uint32_t texHandle, bool msaa, bool usePoint)
{
	if (!m_lit2D || !m_lit2D->dx12Ready) { return false; }
	detail::Lit2DState& lit = *m_lit2D;
	out.rootSig = lit.rootSig.Get();
	out.classic = msaa ? lit.classicMsaa.Get() : lit.classic.Get();
	out.quad = msaa ? lit.quadMsaa.Get() : lit.quad.Get();
	if (out.classic == nullptr || (quadPipelineReady() && out.quad == nullptr)) { return false; }

	detail::Lit2DConstants c{};
	std::uint32_t normalHandle = 0;
	if (texHandle == 0)
	{
		out.heap = lit.nullHeap.Get();
	}
	else
	{
		if (texHandle > m_dx12SpriteTextures.size()) { return false; }
		// ensureSpriteTexture が配列を伸ばすことがあるので、先に必要な値を写す
		const Dx12SpriteTexture& entry = m_dx12SpriteTextures[texHandle - 1];
		if (!entry.srvHeap || !entry.tex) { return false; }
		const void* colorKey = entry.key;
		const int colorW = entry.w;
		const int colorH = entry.h;
		const float range = entry.distanceRange;
		const bool distanceField = range > 0.0f;
		c.mode[0] = distanceField ? 2.0f : 1.0f;
		c.mode[1] = range;
		c.mode[3] = (!distanceField && usePoint) ? 1.0f : 0.0f;
		if (!distanceField)
		{
			const auto ref = lit.normals.find(colorKey);
			if (ref != lit.normals.end() && ref->second.rgba != nullptr)
			{
				const detail::Lit2DNormalRef nref = ref->second;
				if (nref.w == colorW && nref.h == colorH)
				{
					// 色の直前キャッシュを、法線の問い合わせで書き換えない
					const void* lastKey = m_lastSpriteTexKey;
					const int lastW = m_lastSpriteTexW;
					const int lastH = m_lastSpriteTexH;
					const std::uint8_t* lastSrc = m_lastSpriteTexSrc;
					const std::uint32_t lastHandle = m_lastSpriteTexHandle;
					normalHandle = ensureSpriteTextureSlow(nref.key, nref.w, nref.h, nref.rgba, false);
					m_lastSpriteTexKey = lastKey;
					m_lastSpriteTexW = lastW;
					m_lastSpriteTexH = lastH;
					m_lastSpriteTexSrc = lastSrc;
					m_lastSpriteTexHandle = lastHandle;
				}
				else if (lit.warnedMismatch.insert(reinterpret_cast<std::uintptr_t>(colorKey)).second)
				{
					MITIRU_LOG_WARN("RenderPipeline2D", "the normal map size differs from its sprite; drawing it without a normal map");
				}
			}
		}
		c.mode[2] = normalHandle != 0 ? 1.0f : 0.0f;
		out.heap = litHeap(texHandle, normalHandle);
		if (out.heap == nullptr) { return false; }
	}

	const float vpW = viewportWidth();
	const float vpH = viewportHeight();
	c.map[0] = vpW > 0.0f ? m_screenWidth / vpW : 1.0f;
	c.map[1] = vpH > 0.0f ? m_screenHeight / vpH : 1.0f;
	c.map[2] = viewportOffsetX();
	c.map[3] = viewportOffsetY();
	const Lighting2D& l = lit.lighting;
	c.ambient[0] = l.ambient[0]; c.ambient[1] = l.ambient[1]; c.ambient[2] = l.ambient[2];
	c.ambient[3] = static_cast<float>(l.count);
	for (std::uint32_t i = 0; i < l.count; ++i)
	{
		const light2d::PackedLight& p = lit.packed[i];
		std::memcpy(c.lights[i * 3 + 0], p.posHeightRadius, sizeof(float) * 4);
		std::memcpy(c.lights[i * 3 + 1], p.colorFalloff, sizeof(float) * 4);
		std::memcpy(c.lights[i * 3 + 2], p.coneDir, sizeof(float) * 4);
	}
	updateCbDx12(lit.cb[slot].Get(), &c, sizeof(c));
	out.cb = lit.cb[slot]->GetGPUVirtualAddress();
	return true;
}

#endif  // _WIN32

}  // namespace mitiru::render
