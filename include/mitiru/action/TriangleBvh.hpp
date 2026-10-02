#pragma once

/// @file TriangleBvh.hpp
/// @brief 三角形の BVH。binned SAH で 1 度だけ作り、以後は読むだけ。
/// @details 地形は遊んでいる間に変わらないデータなので、GameMemory の外 (DLL の static など) に
///          std::vector で持つ。読み込みとホットリロードで作り直し、遊んでいる間は書き換えない。
///          構築は決定論にしてある。分割は元の順序を保つ安定な分け方で、同点は軸と分割位置の小さい方を選ぶ。
///          走査は再帰せず固定長のスタックで回すので、問い合わせはヒープを使わない。

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <mitiru/action/TriangleTests.hpp>

namespace mitiru::action
{

/// @brief BVH の三角形。id は呼び出し側が決める元の番号で、当たりの結果にそのまま返る
struct BvhTriangle
{
	Vec3          a{}, b{}, c{};
	std::uint32_t id    = 0;
	std::uint32_t layer = 0; ///< 0-31。問い合わせの mask の (1 << layer) と照らす
};

/// @brief count > 0 なら葉 (三角形 [index, index + count))、0 なら子は index と index + 1
struct BvhNode
{
	Aabb          box{};
	std::uint32_t index = 0;
	std::uint32_t count = 0;
};

class TriangleBvh
{
public:
	static constexpr std::uint32_t kMaxLeafSize = 4;
	static constexpr int           kStackSize   = 128;

	TriangleBvh() = default;

	/// @brief 潰れた三角形と有限でない頂点は捨てる
	explicit TriangleBvh(std::vector<BvhTriangle> tris)
	{
		std::erase_if(tris, [](const BvhTriangle& t) {
			return !isFinite(t.a) || !isFinite(t.b) || !isFinite(t.c) || geom::isDegenerateTriangle(t.a, t.b, t.c);
		});
		build(std::move(tris));
	}

	[[nodiscard]] bool empty() const noexcept { return m_tris.empty(); }
	[[nodiscard]] const std::vector<BvhTriangle>& triangles() const noexcept { return m_tris; }
	[[nodiscard]] const std::vector<BvhNode>& nodes() const noexcept { return m_nodes; }
	[[nodiscard]] Aabb bounds() const noexcept { return m_nodes.empty() ? Aabb{} : m_nodes[0].box; }

	/// @brief レイ (o + d t、t は 0..tMax) に沿って葉の三角形を近い順に渡す
	/// @param inflate ノード箱を各軸この量だけ広げる (掃引する形の半サイズ)
	/// @param f float f(const BvhTriangle&, float tMax) が返す値で tMax を縮めて枝を刈る
	template <class F>
	void traverseRay(const Vec3& o, const Vec3& d, float tMax, const Vec3& inflate, F&& f) const
	{
		if (m_nodes.empty()) { return; }
		struct Entry { std::uint32_t node; float t; };
		const Vec3 inv = geom::safeInverse(d);
		Entry      stack[kStackSize];
		int        sp = 0;
		float      t0 = 0.0f;
		if (!geom::rayAabb(o, inv, m_nodes[0].box.inflated(inflate), tMax, t0)) { return; }
		stack[sp++] = {0u, t0};
		while (sp > 0)
		{
			const Entry e = stack[--sp];
			if (e.t > tMax) { continue; }
			const BvhNode& node = m_nodes[e.node];
			if (node.count > 0)
			{
				for (std::uint32_t i = 0; i < node.count; ++i) { tMax = minf(tMax, f(m_tris[node.index + i], tMax)); }
				continue;
			}
			const std::uint32_t l = node.index, r = node.index + 1;
			float               tl = 0.0f, tr = 0.0f;
			const bool hl = geom::rayAabb(o, inv, m_nodes[l].box.inflated(inflate), tMax, tl);
			const bool hr = geom::rayAabb(o, inv, m_nodes[r].box.inflated(inflate), tMax, tr);
			// 近い子を後に積んで先に取り出す。同時刻なら左を先に
			const bool rightFirst = hl && hr && tr < tl;
			if (rightFirst) { stack[sp++] = {l, tl}; stack[sp++] = {r, tr}; }
			else
			{
				if (hr) { stack[sp++] = {r, tr}; }
				if (hl) { stack[sp++] = {l, tl}; }
			}
		}
	}

	/// @brief 箱と重なる葉の三角形を渡す。f(const BvhTriangle&)
	template <class F>
	void traverseBox(const Aabb& box, F&& f) const
	{
		if (m_nodes.empty()) { return; }
		std::uint32_t stack[kStackSize];
		int           sp = 0;
		stack[sp++]      = 0;
		while (sp > 0)
		{
			const BvhNode& node = m_nodes[stack[--sp]];
			if (!node.box.overlaps(box)) { continue; }
			if (node.count > 0)
			{
				for (std::uint32_t i = 0; i < node.count; ++i) { f(m_tris[node.index + i]); }
				continue;
			}
			stack[sp++] = node.index + 1;
			stack[sp++] = node.index;
		}
	}

	/// @brief 点に近い順に三角形を渡す。f(const BvhTriangle&, float bestDistSq) が新しい bestDistSq を返す
	template <class F>
	void traverseClosest(const Vec3& p, float maxDistSq, F&& f) const
	{
		if (m_nodes.empty()) { return; }
		std::uint32_t stack[kStackSize];
		int           sp   = 0;
		float         best = maxDistSq;
		stack[sp++]        = 0;
		while (sp > 0)
		{
			const BvhNode& node = m_nodes[stack[--sp]];
			if (boxDistSq(node.box, p) > best) { continue; }
			if (node.count > 0)
			{
				for (std::uint32_t i = 0; i < node.count; ++i) { best = minf(best, f(m_tris[node.index + i], best)); }
				continue;
			}
			const std::uint32_t l = node.index, r = node.index + 1;
			const bool rightNearer = boxDistSq(m_nodes[r].box, p) < boxDistSq(m_nodes[l].box, p);
			stack[sp++] = rightNearer ? l : r;
			stack[sp++] = rightNearer ? r : l;
		}
	}

	[[nodiscard]] static float boxDistSq(const Aabb& b, const Vec3& p) noexcept
	{
		const float dx = maxf(maxf(b.min.x - p.x, 0.0f), p.x - b.max.x);
		const float dy = maxf(maxf(b.min.y - p.y, 0.0f), p.y - b.max.y);
		const float dz = maxf(maxf(b.min.z - p.z, 0.0f), p.z - b.max.z);
		return dx * dx + dy * dy + dz * dz;
	}

private:
	static constexpr int kBins = 16;

	struct Split
	{
		int   axis  = -1;
		int   bin   = 0;
		float cost  = 1e38f;
	};

	struct Work
	{
		std::vector<Aabb>          boxes;
		std::vector<Vec3>          centroids;
		std::vector<std::uint32_t> order;
		std::vector<std::uint32_t> temp;
	};

	void build(std::vector<BvhTriangle> tris)
	{
		const auto n = static_cast<std::uint32_t>(tris.size());
		if (n == 0) { return; }
		Work  work;
		float maxAbs = 0.0f;
		work.boxes.resize(n);
		work.centroids.resize(n);
		for (std::uint32_t i = 0; i < n; ++i)
		{
			Aabb b;
			b.expand(tris[i].a);
			b.expand(tris[i].b);
			b.expand(tris[i].c);
			work.boxes[i]     = b;
			work.centroids[i] = b.center();
			const Vec3 m      = vmax(vmax(Vec3{absf(b.min.x), absf(b.min.y), absf(b.min.z)},
			                              Vec3{absf(b.max.x), absf(b.max.y), absf(b.max.z)}), Vec3{});
			maxAbs = maxf(maxAbs, maxf(m.x, maxf(m.y, m.z)));
		}
		// 丸め誤差で境界ぎりぎりの当たりを落とさないよう、ノード箱を座標の大きさに比例して少し広げる
		m_pad = maxAbs * 4.0f * 1.1920929e-7f + 1e-6f;
		work.order.resize(n);
		for (std::uint32_t i = 0; i < n; ++i) { work.order[i] = i; }
		work.temp.resize(n);
		m_nodes.reserve(static_cast<std::size_t>(n) * 2 / kMaxLeafSize + 4);
		m_nodes.push_back({});
		buildNode(work, 0, 0, n, 0);
		m_tris.resize(n);
		for (std::uint32_t i = 0; i < n; ++i) { m_tris[i] = tris[work.order[i]]; }
	}

	void buildNode(Work& w, std::uint32_t nodeIndex, std::uint32_t begin, std::uint32_t end, int depth)
	{
		const std::uint32_t count = end - begin;
		Aabb                box, cbox;
		for (std::uint32_t i = begin; i < end; ++i)
		{
			box.expand(w.boxes[w.order[i]]);
			cbox.expand(w.centroids[w.order[i]]);
		}
		box                    = box.inflated(Vec3{m_pad, m_pad, m_pad});
		m_nodes[nodeIndex].box = box;
		const auto makeLeaf    = [&] { m_nodes[nodeIndex].index = begin; m_nodes[nodeIndex].count = count; };
		if (count <= 2) { makeLeaf(); return; }

		// 深くなりすぎた枝は中央値で割ってスタックの深さを抑える
		const Split split = depth > 48 ? Split{} : findSplit(w, begin, end, cbox);
		if (split.axis >= 0 && count <= kMaxLeafSize &&
		    box.surfaceArea() * static_cast<float>(count) <= split.cost + box.surfaceArea())
		{
			makeLeaf();
			return;
		}
		std::uint32_t mid = split.axis >= 0 ? partition(w, begin, end, cbox, split) : begin;
		if (mid == begin || mid == end)
		{
			if (count <= kMaxLeafSize) { makeLeaf(); return; }
			mid = medianSplit(w, begin, end, cbox);
		}
		const auto left = static_cast<std::uint32_t>(m_nodes.size());
		m_nodes.push_back({});
		m_nodes.push_back({});
		m_nodes[nodeIndex].index = left;
		m_nodes[nodeIndex].count = 0;
		buildNode(w, left, begin, mid, depth + 1);
		buildNode(w, left + 1, mid, end, depth + 1);
	}

	[[nodiscard]] static float axisOf(const Vec3& v, int axis) noexcept
	{
		return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
	}

	[[nodiscard]] static int binOf(const Vec3& c, int axis, const Aabb& cbox) noexcept
	{
		const float ext = axisOf(cbox.extent(), axis);
		const float k   = static_cast<float>(kBins) * (1.0f - 1e-6f) / ext;
		const int   b   = static_cast<int>((axisOf(c, axis) - axisOf(cbox.min, axis)) * k);
		return b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
	}

	[[nodiscard]] static Split findSplit(const Work& w, std::uint32_t begin, std::uint32_t end, const Aabb& cbox)
	{
		Split best;
		for (int axis = 0; axis < 3; ++axis)
		{
			if (!(axisOf(cbox.extent(), axis) > 1e-12f)) { continue; }
			Aabb          binBox[kBins];
			std::uint32_t binCount[kBins] = {};
			for (std::uint32_t i = begin; i < end; ++i)
			{
				const int b = binOf(w.centroids[w.order[i]], axis, cbox);
				binBox[b].expand(w.boxes[w.order[i]]);
				++binCount[b];
			}
			float         rightArea[kBins]  = {};
			std::uint32_t rightCount[kBins] = {};
			Aabb          acc;
			std::uint32_t accN = 0;
			for (int b = kBins - 1; b > 0; --b)
			{
				acc.expand(binBox[b]);
				accN += binCount[b];
				rightArea[b]  = accN ? acc.surfaceArea() : 0.0f;
				rightCount[b] = accN;
			}
			Aabb          lacc;
			std::uint32_t lN = 0;
			for (int s = 1; s < kBins; ++s)
			{
				lacc.expand(binBox[s - 1]);
				lN += binCount[s - 1];
				if (lN == 0 || rightCount[s] == 0) { continue; }
				const float cost = lacc.surfaceArea() * static_cast<float>(lN) +
				                   rightArea[s] * static_cast<float>(rightCount[s]);
				if (cost < best.cost) { best = Split{axis, s, cost}; }
			}
		}
		return best;
	}

	/// @brief 分割位置より左の bin を前へ寄せる (元の順序を保つ)
	static std::uint32_t partition(Work& w, std::uint32_t begin, std::uint32_t end, const Aabb& cbox, const Split& s)
	{
		std::uint32_t front = begin, back = 0;
		for (std::uint32_t i = begin; i < end; ++i)
		{
			const std::uint32_t t = w.order[i];
			if (binOf(w.centroids[t], s.axis, cbox) < s.bin) { w.order[front++] = t; }
			else { w.temp[back++] = t; }
		}
		for (std::uint32_t i = 0; i < back; ++i) { w.order[front + i] = w.temp[i]; }
		return front;
	}

	/// @brief 最長軸の重心の (値, 番号) 順に並べて半分に分ける
	static std::uint32_t medianSplit(Work& w, std::uint32_t begin, std::uint32_t end, const Aabb& cbox)
	{
		const Vec3 ext  = cbox.extent();
		const int  axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
		std::stable_sort(w.order.begin() + begin, w.order.begin() + end, [&](std::uint32_t x, std::uint32_t y) {
			const float kx = axisOf(w.centroids[x], axis), ky = axisOf(w.centroids[y], axis);
			return kx < ky || (kx == ky && x < y);
		});
		return begin + (end - begin) / 2;
	}

	std::vector<BvhNode>     m_nodes;
	std::vector<BvhTriangle> m_tris;
	float                    m_pad = 0.0f;
};

} // namespace mitiru::action
