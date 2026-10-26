#pragma once

/// @file Dx12PipelineCreateCounter.hpp
/// @brief D3D12 のパイプライン作成の回数を、device の関数表を差し替えて数える (計測用の opt-in)
/// @details PSO の作成は 89 か所に散っていて、最初の使用で作る物 (instanced の組など) もある。1 か所ずつ数える代わりに
///          ID3D12Device の CreateGraphicsPipelineState / CreateComputePipelineState / CreatePipelineState の
///          関数表の項を、数えてから元を呼ぶ関数へ替える。関数表は D3D12 の実行時が持つ物でプロセスに 1 つ、
///          差し替えは MITIRU_COUNT_PIPELINES=1 のとき (host の --perf-log) だけ。通常の実行では何も触らない。
///          項の番号は d3d12.h の宣言順で、IUnknown 3、ID3D12Object 4、ID3D12Device の先頭 3 つ
///          (GetNodeCount、CreateCommandQueue、CreateCommandAllocator) の次が 10。
///          ID3D12Device2::CreatePipelineState は、Device1 の 44〜46 の次の 47。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/ShaderCompileCount.hpp>

namespace mitiru::gfx
{

namespace detail
{

constexpr std::size_t kVtblCreateGraphicsPso = 10;
constexpr std::size_t kVtblCreateComputePso = 11;
constexpr std::size_t kVtblCreatePipelineState = 47;

using CreateGraphicsPsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using CreateComputePsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
using CreatePipelineStateFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

struct PipelineHookOriginals
{
	CreateGraphicsPsoFn graphics = nullptr;
	CreateComputePsoFn compute = nullptr;
	CreatePipelineStateFn stream = nullptr;
};

[[nodiscard]] inline PipelineHookOriginals& pipelineHookOriginals() noexcept
{
	static PipelineHookOriginals o;
	return o;
}

/// @brief 作成の呼び出しの時間を pipelineCreateNanos に足す (スコープを出るときに)
struct PipelineTimer
{
	PipelineTimer() noexcept { QueryPerformanceCounter(&begin); }
	~PipelineTimer()
	{
		LARGE_INTEGER end{}, freq{};
		QueryPerformanceCounter(&end);
		QueryPerformanceFrequency(&freq);
		notePipelineCreateNanos(static_cast<std::uint64_t>((end.QuadPart - begin.QuadPart) * 1000000000LL / freq.QuadPart));
	}
	LARGE_INTEGER begin{};
};

inline HRESULT STDMETHODCALLTYPE countedCreateGraphicsPso(ID3D12Device* self, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** out)
{
	notePipelineCreated();
	const PipelineTimer timer;
	return pipelineHookOriginals().graphics(self, desc, riid, out);
}

inline HRESULT STDMETHODCALLTYPE countedCreateComputePso(ID3D12Device* self, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** out)
{
	notePipelineCreated();
	const PipelineTimer timer;
	return pipelineHookOriginals().compute(self, desc, riid, out);
}

inline HRESULT STDMETHODCALLTYPE countedCreatePipelineState(ID3D12Device2* self, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** out)
{
	notePipelineCreated();
	const PipelineTimer timer;
	return pipelineHookOriginals().stream(self, desc, riid, out);
}

/// @brief 関数表の slot を hook に替え、元の関数を orig へ残す。すでに替えてあれば何もしない
inline bool patchVtableSlot(void* object, std::size_t slot, void* hook, void** orig) noexcept
{
	void** vtbl = *static_cast<void***>(object);
	if (vtbl[slot] == hook) { return true; }
	DWORD old = 0;
	if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_READWRITE, &old)) { return false; }
	*orig = vtbl[slot];
	vtbl[slot] = hook;
	DWORD ignored = 0;
	VirtualProtect(&vtbl[slot], sizeof(void*), old, &ignored);
	return true;
}

} // namespace detail

/// @brief device の PSO 作成を数える。数えられなかった項があれば false (その分は数に入らない)
inline bool installPipelineCreateCounter(ID3D12Device* device) noexcept
{
	if (device == nullptr) { return false; }
	auto& o = detail::pipelineHookOriginals();
	bool ok = detail::patchVtableSlot(device, detail::kVtblCreateGraphicsPso,
	                                  reinterpret_cast<void*>(&detail::countedCreateGraphicsPso), reinterpret_cast<void**>(&o.graphics));
	ok = detail::patchVtableSlot(device, detail::kVtblCreateComputePso,
	                             reinterpret_cast<void*>(&detail::countedCreateComputePso), reinterpret_cast<void**>(&o.compute)) && ok;
	Microsoft::WRL::ComPtr<ID3D12Device2> device2;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf()))))
	{
		ok = detail::patchVtableSlot(device2.Get(), detail::kVtblCreatePipelineState,
		                             reinterpret_cast<void*>(&detail::countedCreatePipelineState), reinterpret_cast<void**>(&o.stream)) && ok;
	}
	return ok;
}

} // namespace mitiru::gfx

#endif // _WIN32
