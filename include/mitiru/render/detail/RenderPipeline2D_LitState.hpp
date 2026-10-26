#pragma once
// RenderPipeline2D.hpp からだけ include する。2D ライト (R6) が RenderPipeline2D の外に持つ状態。
// RenderPipeline2D の末尾に unique_ptr 1 本だけを足し、光を使わないゲームはこれを作らない。

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include <mitiru/render/Light2DMath.hpp>

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#endif

namespace mitiru::render::detail
{

/// 色のテクスチャに対応づけた法線マップ。pixels の先頭は SpriteCache が読み直すと変わるので、使う直前に host が登録し直す
struct Lit2DNormalRef
{
	const void* key = nullptr;
	int w = 0;
	int h = 0;
	const std::uint8_t* rgba = nullptr;
};

/// シェーダーの定数バッファ (b0)。DefaultShaders2DLit.hpp の LitConstants と同じ並び
struct Lit2DConstants
{
	float mode[4];
	float map[4];
	float ambient[4];
	float lights[kMaxLights2D * light2d::kVec4PerLight][4];
};
static_assert(sizeof(Lit2DConstants) == 16 * (3 + kMaxLights2D * light2d::kVec4PerLight));

struct Lit2DState
{
	Lighting2D lighting;
	light2d::PackedLight packed[kMaxLights2D] = {};
	std::unordered_map<const void*, Lit2DNormalRef> normals;
	std::unordered_set<std::uint64_t> warnedMismatch;   ///< 大きさが色と違う法線マップを知らせた組

#ifdef _WIN32
	static constexpr int kRing = 3;   // RenderPipeline2D::kDx12Ring と同じ (本体側で static_assert)

	struct HeapEntry
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;   ///< [色, 法線] の 2 スロット
		ID3D12Resource* color = nullptr;
		ID3D12Resource* normal = nullptr;
	};

	bool dx12Ready = false;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> classic;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> classicMsaa;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> quad;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> quadMsaa;
	gfx::GpuResource cb[kRing];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nullHeap;   ///< テクスチャなしの描画用。2 スロットとも空
	std::unordered_map<std::uint64_t, HeapEntry> heaps;      ///< (色のハンドル << 32 | 法線のハンドル)
#endif
};

}  // namespace mitiru::render::detail
