#pragma once

/// @file Dx12TextureReadback.hpp
/// @brief 2D テクスチャ 1 枚 (single-sample、mip 0) を CPU へ読む。診断とテスト用
/// @details 自前のコマンドリストを積んで完了まで待つので、フレームの途中では呼ばない。
///          テクスチャは restState で待っている前提で、読んだ後も restState へ戻す。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>

namespace mitiru::gfx
{

namespace detail
{

[[nodiscard]] inline bool submitAndWait(ID3D12Device* device, ID3D12CommandQueue* queue,
                                        ID3D12GraphicsCommandList* list)
{
	ID3D12CommandList* lists[] = {list};
	queue->ExecuteCommandLists(1, lists);
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))) { return false; }
	HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (ev == nullptr) { return false; }
	queue->Signal(fence.Get(), 1);
	bool done = fence->GetCompletedValue() >= 1;
	if (!done && SUCCEEDED(fence->SetEventOnCompletion(1, ev)))
	{
		done = WaitForSingleObject(ev, 10000) == WAIT_OBJECT_0;
	}
	CloseHandle(ev);
	return done;
}

inline void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* res, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after)
{
	if (before == after) { return; }
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	list->ResourceBarrier(1, &b);
}

} // namespace detail

/// @return 行を詰めた画素の並び (幅 × 高さ × texelBytes)。失敗時は空
[[nodiscard]] inline std::vector<std::uint8_t> readbackTexture2D(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                                 ID3D12Resource* texture,
                                                                 D3D12_RESOURCE_STATES restState,
                                                                 std::uint32_t texelBytes)
{
	if (device == nullptr || queue == nullptr || texture == nullptr || texelBytes == 0) { return {}; }
	const D3D12_RESOURCE_DESC desc = texture->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
	UINT64 total = 0;
	device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &total);

	GpuResource staging;
	if (FAILED(createGpuBuffer(device, D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_STATE_COPY_DEST, staging)))
	{
		return {};
	}
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.GetAddressOf()))) ||
	    FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
	                                     IID_PPV_ARGS(list.GetAddressOf()))))
	{
		return {};
	}
	detail::transition(list.Get(), texture, restState, D3D12_RESOURCE_STATE_COPY_SOURCE);
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = texture;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = staging.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = fp;
	list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	detail::transition(list.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, restState);
	list->Close();
	if (!detail::submitAndWait(device, queue, list.Get())) { return {}; }

	const std::size_t rowBytes = static_cast<std::size_t>(desc.Width) * texelBytes;
	std::vector<std::uint8_t> out(rowBytes * desc.Height);
	void* mapped = nullptr;
	const D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
	if (FAILED(staging->Map(0, &range, &mapped)) || mapped == nullptr) { return {}; }
	const auto* bytes = static_cast<const std::uint8_t*>(mapped);
	for (UINT y = 0; y < desc.Height; ++y)
	{
		std::memcpy(out.data() + y * rowBytes, bytes + fp.Offset + static_cast<std::size_t>(y) * fp.Footprint.RowPitch,
		            rowBytes);
	}
	const D3D12_RANGE none{0, 0};
	staging->Unmap(0, &none);
	return out;
}

/// @brief IEEE 754 の半精度を float へ直す (R16G16_FLOAT を読み戻して数で比べる用)
[[nodiscard]] inline float halfToFloat(std::uint16_t h) noexcept
{
	const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
	std::uint32_t exponent = (h >> 10) & 0x1Fu;
	std::uint32_t mantissa = h & 0x3FFu;
	std::uint32_t bits = sign;
	if (exponent == 31)
	{
		bits = sign | 0x7F800000u | (mantissa << 13);
	}
	else if (exponent != 0)
	{
		bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
	}
	else if (mantissa != 0)
	{
		exponent = 127 - 15 + 1;
		while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
		bits = sign | (exponent << 23) | ((mantissa & 0x3FFu) << 13);
	}
	float f = 0.0f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

} // namespace mitiru::gfx

#endif // _WIN32
