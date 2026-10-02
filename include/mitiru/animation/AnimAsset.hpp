#pragma once

/// @file AnimAsset.hpp
/// @brief アニメーションランタイムの読み取り専用データ (骨格・クリップ・イベント・マスク・ソケット)。
/// @details glTF から組み立てたあとは書き換えない。ゲーム DLL も host も同じ glTF と同じ sidecar から
///          同じ関数で組むので、同じ入力 (AnimPoseParams) から同じ姿勢が出る。
///          ノードの添字は glTF の nodes 配列そのまま。描画側のメッシュ・スキンの添字と一致する。

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

/// AnimLayer::clip / mask は int16、AnimEventKey::nameId は uint16 なので、組み立て時にこの数で打ち切る。
inline constexpr int kAnimMaxClips = 32767;
inline constexpr int kAnimMaxMasks = 32767;
inline constexpr std::size_t kAnimMaxEventNames = 65535;

/// @brief findClip / findNode / findMask の int を POD の int16 の欄へ入れる。収まらない値は「無し」の -1 にする
[[nodiscard]] constexpr std::int16_t animIndex16(int index) noexcept
{
	return (index >= 0 && index <= 32767) ? static_cast<std::int16_t>(index) : std::int16_t{-1};
}

/// @brief クリップ上の名前付きの時刻 (足音、攻撃判定の開始など)。nameId は AnimAsset::eventNames の添字。
struct AnimEventKey
{
	std::uint16_t nameId = 0;
	float timeSec = 0.0f;
};

/// @brief ルートモーションを計算するための、クリップ先頭でのルート骨のモデル空間の姿勢。
struct RootStart
{
	sgc::Vec3f t{0, 0, 0};
	sgc::Vec4f twist{0, 0, 0, 1};
};

struct AnimClip
{
	std::string name;
	float durationSec = 0.0f;
	std::vector<render::GltfAnimationChannel> channels;
	std::vector<AnimEventKey> events;       ///< 時刻の昇順
	std::vector<NodeTRS> refPose;           ///< t=0 の姿勢。加算レイヤの基準とルートモーションの起点
	RootStart rootStart;
};

/// @brief ノードごとの効き (0..1)。上半身だけ、下半身だけのレイヤに使う。
struct AnimMask
{
	std::string name;
	std::vector<float> weights;
};

/// @brief 骨に固定した取り付け点 (武器を持つ手など)。offset は骨の座標系での変換。
struct AnimSocket
{
	std::string name;
	int node = -1;
	sgc::Mat4f offset = sgc::Mat4f::identity();
};

struct AnimAsset
{
	std::vector<render::GltfNode> nodes;
	std::vector<int> evalOrder;               ///< 親が子より先に来る順。循環で届かないノードは含まない
	std::vector<render::GltfSkinData> skins;
	std::vector<AnimClip> clips;
	std::vector<AnimMask> masks;
	std::vector<AnimSocket> sockets;
	std::vector<std::string> eventNames;
	int rootMotionNode = -1;
	NodeTRS rootParentRest;                   ///< ルート骨の親までのモデル空間の変換 (レスト姿勢)

	[[nodiscard]] int nodeCount() const noexcept { return static_cast<int>(nodes.size()); }
	[[nodiscard]] int findNode(std::string_view name) const noexcept { return findByName(nodes, name); }
	[[nodiscard]] int findClip(std::string_view name) const noexcept { return findByName(clips, name); }
	[[nodiscard]] int findMask(std::string_view name) const noexcept { return findByName(masks, name); }
	[[nodiscard]] int findSocket(std::string_view name) const noexcept { return findByName(sockets, name); }
	[[nodiscard]] int findEventName(std::string_view name) const noexcept
	{
		for (std::size_t i = 0; i < eventNames.size(); ++i)
		{
			if (eventNames[i] == name) { return static_cast<int>(i); }
		}
		return -1;
	}
	[[nodiscard]] const AnimClip* clip(int index) const noexcept
	{
		return (index >= 0 && index < static_cast<int>(clips.size())) ? &clips[static_cast<std::size_t>(index)]
		                                                               : nullptr;
	}

private:
	template <class T>
	[[nodiscard]] static int findByName(const std::vector<T>& items, std::string_view name) noexcept
	{
		for (std::size_t i = 0; i < items.size(); ++i)
		{
			if (items[i].name == name) { return static_cast<int>(i); }
		}
		return -1;
	}
};

namespace detail
{

/// @brief 親から子へ辿る順を作る。ルート (parent == -1) を添字順に始点にし、子は glTF の並び順で積む。
[[nodiscard]] inline std::vector<int> buildEvalOrder(const std::vector<render::GltfNode>& nodes)
{
	std::vector<int> order;
	order.reserve(nodes.size());
	std::vector<char> visited(nodes.size(), 0);
	std::deque<int> queue;
	for (std::size_t i = 0; i < nodes.size(); ++i)
	{
		if (nodes[i].parent == -1) { queue.push_back(static_cast<int>(i)); }
	}
	while (!queue.empty())
	{
		const int idx = queue.front();
		queue.pop_front();
		const auto ui = static_cast<std::size_t>(idx);
		if (visited[ui] != 0) { continue; }
		visited[ui] = 1;
		order.push_back(idx);
		for (const int child : nodes[ui].children)
		{
			if (child >= 0 && static_cast<std::size_t>(child) < nodes.size() &&
			    visited[static_cast<std::size_t>(child)] == 0)
			{
				queue.push_back(child);
			}
		}
	}
	return order;
}

/// @brief 既定のルートモーション骨。skin の skeleton 指定、無ければ親が joint でない最初の joint。
[[nodiscard]] inline int defaultRootMotionNode(const std::vector<render::GltfNode>& nodes,
                                               const std::vector<render::GltfSkinData>& skins)
{
	if (skins.empty()) { return -1; }
	const auto& skin = skins.front();
	if (skin.skeletonRoot >= 0 && static_cast<std::size_t>(skin.skeletonRoot) < nodes.size())
	{
		return skin.skeletonRoot;
	}
	for (const int j : skin.joints)
	{
		if (j < 0 || static_cast<std::size_t>(j) >= nodes.size()) { continue; }
		const int parent = nodes[static_cast<std::size_t>(j)].parent;
		bool parentIsJoint = false;
		for (const int k : skin.joints) { parentIsJoint = parentIsJoint || (k == parent); }
		if (!parentIsJoint) { return j; }
	}
	return -1;
}

} // namespace detail

} // namespace mitiru::animation
