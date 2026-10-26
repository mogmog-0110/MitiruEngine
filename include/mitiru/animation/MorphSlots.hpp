#pragma once

/// @file MorphSlots.hpp
/// @brief モデルごとのモーフのスロット表。(ノード, モーフの番号) を、重みの配列の添字へ決める。
/// @details スロットはノードの並び (glTF の nodes 配列) の順に、モーフを持つ mesh を参照するノードへ連番で振る。
///          同じ mesh を参照するノードは、ノードごとに別のスロットの範囲を持つ (node.weights がノードごとに違うため)。
///          ゲーム DLL と host は同じ glTF から同じ関数で組むので、同じ表になる。描画に渡す重みの配列の添字は、この表のスロット。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/AnimNameKey.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

struct MorphSlot
{
	int node = -1;                 ///< glTF の nodes の番号
	int target = 0;                ///< そのノードの mesh の何番目のモーフか
	std::string name;              ///< モーフの名前 (extras.targetNames)。無ければ空
	std::uint32_t nameKey = 0;     ///< name の鍵 (animNameKey)。名前が無ければ 0
	float defaultWeight = 0.0f;    ///< node.weights、無ければ mesh.weights、無ければ 0
};

struct MorphSlotTable
{
	std::vector<MorphSlot> slots;
	std::vector<int> nodeFirst;     ///< ノードごとの最初のスロット。モーフが無いノードは -1
	std::vector<int> nodeCount;     ///< ノードごとのスロットの数

	[[nodiscard]] int slotCount() const noexcept { return static_cast<int>(slots.size()); }

	/// @brief 名前のモーフの、最初のスロット。無ければ -1
	[[nodiscard]] int findByName(std::string_view name) const noexcept
	{
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			if (!slots[i].name.empty() && slots[i].name == name) { return static_cast<int>(i); }
		}
		return -1;
	}

	/// @brief (ノード, モーフの番号) のスロット。無ければ -1
	[[nodiscard]] int find(int node, int target) const noexcept
	{
		if (node < 0 || static_cast<std::size_t>(node) >= nodeFirst.size() || target < 0) { return -1; }
		const auto n = static_cast<std::size_t>(node);
		return (nodeFirst[n] >= 0 && target < nodeCount[n]) ? nodeFirst[n] + target : -1;
	}
};

/// @brief scene のノードと meshMorphs からスロット表を組む。mesh の番号が範囲外のノードは飛ばす
[[nodiscard]] inline MorphSlotTable buildMorphSlotTable(const render::GltfSceneData& scene)
{
	MorphSlotTable table;
	table.nodeFirst.assign(scene.nodes.size(), -1);
	table.nodeCount.assign(scene.nodes.size(), 0);
	for (std::size_t n = 0; n < scene.nodes.size(); ++n)
	{
		const auto& node = scene.nodes[n];
		if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= scene.meshMorphs.size()) { continue; }
		const auto& mesh = scene.meshMorphs[static_cast<std::size_t>(node.mesh)];
		if (mesh.targetCount == 0) { continue; }
		table.nodeFirst[n] = static_cast<int>(table.slots.size());
		table.nodeCount[n] = static_cast<int>(mesh.targetCount);
		const auto& weights = node.morphWeights.empty() ? mesh.defaultWeights : node.morphWeights;
		for (std::uint32_t t = 0; t < mesh.targetCount; ++t)
		{
			MorphSlot slot;
			slot.node = static_cast<int>(n);
			slot.target = static_cast<int>(t);
			if (t < mesh.names.size() && !mesh.names[t].empty())
			{
				slot.name = mesh.names[t];
				slot.nameKey = animNameKey(slot.name);
			}
			if (t < weights.size()) { slot.defaultWeight = weights[t]; }
			table.slots.push_back(slot);
		}
	}
	return table;
}

} // namespace mitiru::animation
