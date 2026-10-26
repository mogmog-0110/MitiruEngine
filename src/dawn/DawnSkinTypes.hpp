#pragma once

// DawnModelStore (CPU) と DawnSkinning (GPU) が受け渡すスキンの型。どちらも相手のヘッダを読まずに済ませる。

#include <atomic>
#include <cstdint>
#include <vector>

#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/MorphTargets.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render::dawn_detail
{

/// 変形の元になる 1 段ぶんの頂点列。モデルが持つ実体を指すだけで、GPU への転送は DawnSkinning が id で覚える
struct DawnSkinGeometry
{
	std::uint64_t id = 0;   ///< 0 は空。モデルを読み直すと別の id になる (アドレスの使い回しで古い転送を引かないため)
	const std::vector<Vertex3D>* base = nullptr;
	const std::vector<SkinVertexBinding>* binding = nullptr;
	const std::vector<std::uint32_t>* indices = nullptr;
	const GltfMorphSet* morph = nullptr;   ///< モーフの差分 (この段の頂点の並びで詰めたもの)。あれば最初の転送で源の buffer に付ける
};

[[nodiscard]] inline std::uint64_t nextSkinGeometryId() noexcept
{
	static std::atomic<std::uint64_t> counter{1};
	return counter.fetch_add(1);
}

} // namespace mitiru::render::dawn_detail
