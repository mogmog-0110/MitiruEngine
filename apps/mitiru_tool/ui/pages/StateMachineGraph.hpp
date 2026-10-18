#pragma once
// statemachine ページの図。状態を楕円の上に並べ、遷移の矢印を CPU で RGBA に塗る。
// 状態の箱と名前は RML の要素 (文字は RmlUi が描く) で、ここで塗るのは箱の下に敷く矢印の絵だけ。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::tool
{

/// 矢印の絵の座標は dp (ページ上の大きさ)。絵そのものはその scale 倍の画素で作る。
struct GraphBox
{
	float cx = 0.0f;   ///< 箱の中心
	float cy = 0.0f;
};

struct GraphLayout
{
	float width = 0.0f;
	float height = 0.0f;
	float boxW = 0.0f;
	float boxH = 0.0f;
	std::vector<GraphBox> boxes;
};

enum class EdgeLook : std::uint8_t
{
	Idle,       ///< 宣言にあるが、まだ起きていない
	Seen,       ///< 起きたことがある
	Outgoing,   ///< 今の状態から出ていく
	Recent,     ///< 直近に通った
	Undeclared, ///< 宣言に無いのに起きた
};

struct GraphEdge
{
	int from = 0;      ///< boxes の番号
	int to = 0;
	std::uint64_t count = 0;
	EdgeLook look = EdgeLook::Idle;
};

/// n 個の箱を、上から時計回りに楕円の上へ並べる。n が 1 なら中央。
[[nodiscard]] GraphLayout layoutEllipse(std::size_t n, float width, float height, float boxW, float boxH);

struct GraphImage
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;   ///< 不透明。地色で塗ってから矢印を重ねる
};

/// 矢印だけの絵。向かい合う 2 本は左右にずらし、線は箱の縁で止めて、先に矢じりを付ける。
/// Idle -> Seen -> Recent -> Outgoing -> Undeclared の順に重ねるので、目立つ線が下に隠れない。
[[nodiscard]] GraphImage renderGraphEdges(const GraphLayout& layout, const std::vector<GraphEdge>& edges, float scale);

} // namespace mitiru::tool
