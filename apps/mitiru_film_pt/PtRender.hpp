#pragma once

// DXR 1.1 の inline ray query で PtScene を撮る。ウィンドウもスワップチェーンも持たない compute だけの経路で、
// 記述子ヒープを使わず (ルートの CBV / SRV / UAV だけ)、シェーダー テーブルも持たない。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "PtScene.hpp"
#include "PtShaderBlob.hpp"

namespace film_pt
{

using Microsoft::WRL::ComPtr;

struct PtOptions
{
	int           spp = 16;
	int           maxBounce = 3;
	std::uint32_t seed = 1;
	float         sunCone = 0.01f;
};

struct PtResult
{
	std::vector<float> color, albedo, normal;   // 画素ごとに 3 float。標本の平均
	double             traceMs = 0.0;           // 標本の dispatch の実測 (AS の構築を除く)
	double             buildMs = 0.0;           // BLAS / TLAS の構築
	std::string        adapter;
};

namespace gpu
{

inline void check(HRESULT hr, const char* what)
{
	if (FAILED(hr)) { throw std::runtime_error(std::string(what) + " が失敗しました (HRESULT 0x" + std::to_string(static_cast<unsigned>(hr)) + ")"); }
}

struct Context
{
	ComPtr<ID3D12Device5>              dev;
	ComPtr<ID3D12CommandQueue>         queue;
	ComPtr<ID3D12CommandAllocator>     alloc;
	ComPtr<ID3D12GraphicsCommandList4> cl;
	ComPtr<ID3D12Fence>                fence;
	HANDLE                             event = nullptr;
	UINT64                             fenceValue = 0;
	std::string                        adapter;

	~Context() { if (event != nullptr) { CloseHandle(event); } }

	void begin() { check(alloc->Reset(), "alloc reset"); check(cl->Reset(alloc.Get(), nullptr), "list reset"); }
	void submit()
	{
		check(cl->Close(), "list close");
		ID3D12CommandList* lists[] = {cl.Get()};
		queue->ExecuteCommandLists(1, lists);
		check(queue->Signal(fence.Get(), ++fenceValue), "signal");
		if (fence->GetCompletedValue() < fenceValue)
		{
			check(fence->SetEventOnCompletion(fenceValue, event), "wait setup");
			WaitForSingleObject(event, INFINITE);
		}
		if (const HRESULT removed = dev->GetDeviceRemovedReason(); FAILED(removed)) { check(removed, "GPU 実行 (デバイスが消えました)"); }
	}
};

inline std::string narrow(const wchar_t* w)
{
	std::string s;
	for (; *w != 0; ++w) { s.push_back(*w < 128 ? static_cast<char>(*w) : '?'); }
	return s;
}

inline void create(Context& g)
{
	ComPtr<IDXGIFactory6> factory;
	check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 d{};
		adapter->GetDesc1(&d);
		if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&g.dev))))
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
			g.dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
			if (o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) { g.adapter = narrow(d.Description); break; }
			g.dev.Reset();
		}
		adapter.Reset();
	}
	if (!g.dev) { throw std::runtime_error("DXR 1.1 (inline ray query) に対応した GPU が見つかりません"); }
	D3D12_COMMAND_QUEUE_DESC qd{};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	check(g.dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.queue)), "queue");
	check(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.alloc)), "allocator");
	check(g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.alloc.Get(), nullptr, IID_PPV_ARGS(&g.cl)), "command list");
	check(g.cl->Close(), "list close");
	check(g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)), "fence");
	g.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

inline D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE t)
{
	D3D12_HEAP_PROPERTIES h{};
	h.Type = t;
	return h;
}

inline ComPtr<ID3D12Resource> buffer(ID3D12Device* dev, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                                     D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
	D3D12_RESOURCE_DESC d{};
	d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	d.Width = size > 0 ? size : 16;
	d.Height = d.DepthOrArraySize = d.MipLevels = 1;
	d.SampleDesc.Count = 1;
	d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	d.Flags = flags;
	const D3D12_HEAP_PROPERTIES hp = heap(type);
	ComPtr<ID3D12Resource> r;
	check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)), "buffer");
	return r;
}

inline D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = r;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	return b;
}

inline D3D12_RESOURCE_BARRIER uavBarrier(ID3D12Resource* r)
{
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	b.UAV.pResource = r;
	return b;
}

/// CPU の配列から SRV 用の GPU バッファを作る。upload は submit が終わるまで残す
inline ComPtr<ID3D12Resource> upload(Context& g, const void* data, std::size_t bytes, std::vector<ComPtr<ID3D12Resource>>& keep)
{
	auto up = buffer(g.dev.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	void* p = nullptr;
	check(up->Map(0, nullptr, &p), "map");
	std::memcpy(p, data, bytes);
	up->Unmap(0, nullptr);
	auto dst = buffer(g.dev.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
	g.cl->CopyBufferRegion(dst.Get(), 0, up.Get(), 0, bytes);
	const auto b = transition(dst.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	g.cl->ResourceBarrier(1, &b);
	keep.push_back(up);
	return dst;
}

}  // namespace gpu

namespace detail
{

struct ParamsCb
{
	float         eye[3], tanHalf;
	float         right[3], aspect;
	float         up[3], focusDist;
	float         fwd[3], apertureR;
	float         ambient[3];
	std::uint32_t sampleIndex;
	std::uint32_t size[2], maxBounce, lightCount;
	float         sunCone;
	std::uint32_t seed, pad0, pad1;
};
static_assert(sizeof(ParamsCb) == 112, "HLSL の cbuffer Params と同じ並び");

struct Accel
{
	ComPtr<ID3D12Resource> blas, tlas, scratch, instances;
};

inline Accel buildAccel(gpu::Context& g, ID3D12Resource* verts, UINT vertexCount, ID3D12Resource* indices, UINT indexCount)
{
	Accel a;
	D3D12_RAYTRACING_GEOMETRY_DESC geo{};
	geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;   // 不透明にしない。alpha test を候補ごとに見るため
	geo.Triangles.VertexBuffer.StartAddress = verts->GetGPUVirtualAddress();
	geo.Triangles.VertexBuffer.StrideInBytes = 32;
	geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geo.Triangles.VertexCount = vertexCount;
	geo.Triangles.IndexBuffer = indices->GetGPUVirtualAddress();
	geo.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	geo.Triangles.IndexCount = indexCount;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
	in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	in.NumDescs = 1;
	in.pGeometryDescs = &geo;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo{}, tlasInfo{};
	g.dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &blasInfo);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tin{};
	tin.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tin.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	tin.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tin.NumDescs = 1;
	g.dev->GetRaytracingAccelerationStructurePrebuildInfo(&tin, &tlasInfo);

	const UINT64 scratchSize = std::max(blasInfo.ScratchDataSizeInBytes, tlasInfo.ScratchDataSizeInBytes);
	a.scratch = gpu::buffer(g.dev.Get(), scratchSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	a.blas = gpu::buffer(g.dev.Get(), blasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
	                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	a.tlas = gpu::buffer(g.dev.Get(), tlasInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
	                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	D3D12_RAYTRACING_INSTANCE_DESC inst{};
	inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;
	inst.InstanceMask = 0xFF;
	inst.AccelerationStructure = a.blas->GetGPUVirtualAddress();
	a.instances = gpu::buffer(g.dev.Get(), sizeof(inst), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	void* p = nullptr;
	gpu::check(a.instances->Map(0, nullptr, &p), "map instances");
	std::memcpy(p, &inst, sizeof(inst));
	a.instances->Unmap(0, nullptr);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
	bd.Inputs = in;
	bd.DestAccelerationStructureData = a.blas->GetGPUVirtualAddress();
	bd.ScratchAccelerationStructureData = a.scratch->GetGPUVirtualAddress();
	g.cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
	const auto barrier = gpu::uavBarrier(nullptr);   // BLAS の結果と、次の TLAS が同じ scratch を使うための順序
	g.cl->ResourceBarrier(1, &barrier);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC td{};
	td.Inputs = tin;
	td.Inputs.InstanceDescs = a.instances->GetGPUVirtualAddress();
	td.DestAccelerationStructureData = a.tlas->GetGPUVirtualAddress();
	td.ScratchAccelerationStructureData = a.scratch->GetGPUVirtualAddress();
	g.cl->BuildRaytracingAccelerationStructure(&td, 0, nullptr);
	const auto barrier2 = gpu::uavBarrier(a.tlas.Get());
	g.cl->ResourceBarrier(1, &barrier2);
	return a;
}

inline ComPtr<ID3D12RootSignature> makeRootSignature(ID3D12Device* dev)
{
	D3D12_ROOT_PARAMETER params[11]{};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor = {0, 0};
	for (UINT i = 0; i < 7; ++i)
	{
		params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		params[1 + i].Descriptor = {i, 0};
	}
	for (UINT i = 0; i < 3; ++i)
	{
		params[8 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		params[8 + i].Descriptor = {i, 0};
	}
	D3D12_ROOT_SIGNATURE_DESC rd{};
	rd.NumParameters = 11;
	rd.pParameters = params;
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &err)))
	{
		throw std::runtime_error(std::string("ルートシグネチャ: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : "?"));
	}
	ComPtr<ID3D12RootSignature> rs;
	gpu::check(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs)), "root signature");
	return rs;
}

}  // namespace detail

inline PtResult renderPt(const PtScene& s, const PtOptions& opt)
{
	using clock = std::chrono::steady_clock;
	const auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
	gpu::Context g;
	gpu::create(g);
	PtResult out;
	out.adapter = g.adapter;
	if (s.indices.empty()) { throw std::runtime_error("三角形がありません (モデルが 1 つも読めていません)"); }

	std::vector<ComPtr<ID3D12Resource>> keep;
	const auto t0 = clock::now();
	g.begin();
	auto verts = gpu::upload(g, s.verts.data(), s.verts.size() * sizeof(float), keep);
	auto indices = gpu::upload(g, s.indices.data(), s.indices.size() * 4, keep);
	auto triMat = gpu::upload(g, s.triMat.data(), s.triMat.size() * 4, keep);
	auto mats = gpu::upload(g, s.materials.data(), s.materials.size() * sizeof(PtMaterial), keep);
	const std::uint32_t emptyTex = 0;
	auto texels = s.texels.empty() ? gpu::upload(g, &emptyTex, 4, keep) : gpu::upload(g, s.texels.data(), s.texels.size() * 4, keep);
	auto lights = gpu::upload(g, s.lights.data(), s.lights.size() * sizeof(PtLight), keep);
	const detail::Accel accel = detail::buildAccel(g, verts.Get(), static_cast<UINT>(s.verts.size() / 8), indices.Get(), static_cast<UINT>(s.indices.size()));
	g.submit();
	out.buildMs = ms(t0, clock::now());

	const UINT64 pixels = static_cast<UINT64>(s.width) * static_cast<UINT64>(s.height);
	ComPtr<ID3D12Resource> accum[3];
	for (auto& a : accum)
	{
		a = gpu::buffer(g.dev.Get(), pixels * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	}

	const UINT64 stride = 256;
	auto cbuf = gpu::buffer(g.dev.Get(), stride * static_cast<UINT64>(opt.spp), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	std::uint8_t* cb = nullptr;
	gpu::check(cbuf->Map(0, nullptr, reinterpret_cast<void**>(&cb)), "map cbuffer");
	for (int i = 0; i < opt.spp; ++i)
	{
		detail::ParamsCb p{};
		std::memcpy(p.eye, s.camera.eye, 12); p.tanHalf = s.camera.tanHalf;
		std::memcpy(p.right, s.camera.right, 12); p.aspect = s.camera.aspect;
		std::memcpy(p.up, s.camera.up, 12); p.focusDist = s.camera.focusDist;
		std::memcpy(p.fwd, s.camera.fwd, 12); p.apertureR = s.camera.apertureR;
		std::memcpy(p.ambient, s.ambient, 12);
		p.sampleIndex = static_cast<std::uint32_t>(i);
		p.size[0] = static_cast<std::uint32_t>(s.width); p.size[1] = static_cast<std::uint32_t>(s.height);
		p.maxBounce = static_cast<std::uint32_t>(opt.maxBounce);
		p.lightCount = static_cast<std::uint32_t>(s.lights.size());
		p.sunCone = opt.sunCone;
		p.seed = opt.seed;
		std::memcpy(cb + stride * static_cast<UINT64>(i), &p, sizeof(p));
	}
	cbuf->Unmap(0, nullptr);

	auto rootSig = detail::makeRootSignature(g.dev.Get());
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
	pd.pRootSignature = rootSig.Get();
	pd.CS = {kPtTraceDxil, kPtTraceDxilSize};
	ComPtr<ID3D12PipelineState> pso;
	gpu::check(g.dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "compute pipeline (DXIL か GPU が SM 6.5 に対応していません)");

	// 1 回の submit に載せる標本の数。長い 1 回の実行が TDR (描画のタイムアウト) に触れないよう、短く区切る
	const int chunk = 4;
	const auto t1 = clock::now();
	for (int first = 0; first < opt.spp; first += chunk)
	{
		g.begin();
		g.cl->SetComputeRootSignature(rootSig.Get());
		g.cl->SetPipelineState(pso.Get());
		g.cl->SetComputeRootShaderResourceView(1, accel.tlas->GetGPUVirtualAddress());
		ID3D12Resource* srvs[] = {verts.Get(), indices.Get(), triMat.Get(), mats.Get(), texels.Get(), lights.Get()};
		for (UINT i = 0; i < 6; ++i) { g.cl->SetComputeRootShaderResourceView(2 + i, srvs[i]->GetGPUVirtualAddress()); }
		for (UINT i = 0; i < 3; ++i) { g.cl->SetComputeRootUnorderedAccessView(8 + i, accum[i]->GetGPUVirtualAddress()); }
		for (int i = first; i < std::min(first + chunk, opt.spp); ++i)
		{
			g.cl->SetComputeRootConstantBufferView(0, cbuf->GetGPUVirtualAddress() + stride * static_cast<UINT64>(i));
			g.cl->Dispatch((static_cast<UINT>(s.width) + 7) / 8, (static_cast<UINT>(s.height) + 7) / 8, 1);
			const auto b = gpu::uavBarrier(nullptr);
			g.cl->ResourceBarrier(1, &b);
		}
		g.submit();
	}
	out.traceMs = ms(t1, clock::now());

	g.begin();
	ComPtr<ID3D12Resource> readback[3];
	for (int i = 0; i < 3; ++i)
	{
		readback[i] = gpu::buffer(g.dev.Get(), pixels * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
		const auto b = gpu::transition(accum[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		g.cl->ResourceBarrier(1, &b);
		g.cl->CopyResource(readback[i].Get(), accum[i].Get());
	}
	g.submit();
	std::vector<float>* dst[3] = {&out.color, &out.albedo, &out.normal};
	for (int i = 0; i < 3; ++i)
	{
		const float* src = nullptr;
		gpu::check(readback[i]->Map(0, nullptr, reinterpret_cast<void**>(const_cast<float**>(&src))), "map readback");
		dst[i]->resize(static_cast<std::size_t>(pixels) * 3);
		for (std::size_t p = 0; p < static_cast<std::size_t>(pixels); ++p)
		{
			const float n = src[p * 4 + 3] > 0 ? src[p * 4 + 3] : 1.0f;
			for (int c = 0; c < 3; ++c) { (*dst[i])[p * 3 + static_cast<std::size_t>(c)] = src[p * 4 + static_cast<std::size_t>(c)] / n; }
		}
		readback[i]->Unmap(0, nullptr);
	}
	return out;
}

}  // namespace film_pt
