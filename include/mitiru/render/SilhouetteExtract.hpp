#pragma once

/// @file SilhouetteExtract.hpp
/// @brief object-space シルエットエッジ抽出（隣接面の表裏が切り替わる稜線）
/// @details `ContourDetect`(#2) は screen-space で深度/法線の不連続を拾うが、本ファイルは
///          メッシュの幾何から「片側が表・片側が裏を向く稜線」＝真のシルエットを object-space で
///          抽出する。境界エッジ（隣接面が 1 枚しかない開メッシュの縁）も常にシルエット扱い。
///          抽出した稜線は world-space の線分なので、頂点 ID と紐づけてフレーム間追跡しやすい
///          （screen-space より素直に persistent ID を振れる、というのが object-space の利点）。
///
/// createCube のような per-face 法線で頂点が分割されたメッシュでも拾えるよう、隣接判定は
/// 頂点インデックスでなく **頂点位置の溶接（weld）** で行う。
///
/// @code
/// auto edges = mitiru::render::extractSilhouette(mesh, worldMat, camera.position());
/// for (const auto& e : edges) { drawLine3D(e.a, e.b); }
/// @endcode

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Mesh.hpp>

namespace mitiru::render
{

/// @brief シルエット稜線（world-space の線分）
struct SilhouetteEdge
{
	sgc::Vec3f a{};   ///< 端点 A（world）
	sgc::Vec3f b{};   ///< 端点 B（world）
};

namespace detail
{
/// 位置を量子化して溶接キーにする（1e-4 グリッド）。
[[nodiscard]] inline std::array<long long, 3> weldKey(const sgc::Vec3f& p) noexcept
{
	constexpr float kScale = 10000.0f;
	return {static_cast<long long>(std::lround(p.x * kScale)),
	        static_cast<long long>(std::lround(p.y * kScale)),
	        static_cast<long long>(std::lround(p.z * kScale))};
}

struct EdgeAccum
{
	int faces = 0;        ///< この稜線を共有する面の数
	int frontCount = 0;   ///< そのうち表向きの面の数
	sgc::Vec3f a{}, b{};  ///< world 端点（最初に見たもの）
};
}  // namespace detail

/// @brief メッシュから object-space シルエット稜線を抽出する。
/// @param mesh 対象メッシュ（三角形リスト。indices 空なら 3 頂点ずつ）
/// @param world ワールド変換
/// @param cameraPos カメラのワールド位置
/// @return シルエット稜線の配列（world-space）
[[nodiscard]] inline std::vector<SilhouetteEdge> extractSilhouette(
	const Mesh& mesh, const sgc::Mat4f& world, const sgc::Vec3f& cameraPos)
{
	const auto& verts = mesh.vertices();
	const auto& indices = mesh.indices();
	std::vector<SilhouetteEdge> out;
	if (verts.size() < 3) { return out; }

	// 頂点を位置で溶接して canonical id を振る。
	std::map<std::array<long long, 3>, int> canon;
	std::vector<int> vidToCanon(verts.size());
	std::vector<sgc::Vec3f> canonWorld;   // canonical id → world 位置
	for (std::size_t i = 0; i < verts.size(); ++i)
	{
		const sgc::Vec3f wp = world.transformPoint(verts[i].position);
		const auto key = detail::weldKey(verts[i].position);
		const auto it = canon.find(key);
		if (it == canon.end())
		{
			const int id = static_cast<int>(canonWorld.size());
			canon.emplace(key, id);
			canonWorld.push_back(wp);
			vidToCanon[i] = id;
		}
		else { vidToCanon[i] = it->second; }
	}

	// 三角形を走査して稜線ごとに面数 / 表向き数を集計する。
	std::map<std::pair<int, int>, detail::EdgeAccum> edges;
	auto triCount = indices.empty() ? verts.size() : indices.size();
	auto vertAt = [&](std::size_t k) -> std::size_t {
		return indices.empty() ? k : static_cast<std::size_t>(indices[k]);
	};

	for (std::size_t t = 0; t + 2 < triCount; t += 3)
	{
		const std::size_t i0 = vertAt(t), i1 = vertAt(t + 1), i2 = vertAt(t + 2);
		const sgc::Vec3f w0 = world.transformPoint(verts[i0].position);
		const sgc::Vec3f w1 = world.transformPoint(verts[i1].position);
		const sgc::Vec3f w2 = world.transformPoint(verts[i2].position);

		// 幾何法線（world）と表裏判定。
		const sgc::Vec3f faceN = (w1 - w0).cross(w2 - w0);
		const sgc::Vec3f center{(w0.x + w1.x + w2.x) / 3.0f,
		                        (w0.y + w1.y + w2.y) / 3.0f,
		                        (w0.z + w1.z + w2.z) / 3.0f};
		const bool front = faceN.dot(cameraPos - center) > 0.0f;

		const int c0 = vidToCanon[i0], c1 = vidToCanon[i1], c2 = vidToCanon[i2];
		const std::array<std::pair<int, int>, 3> triEdges{
			std::minmax(c0, c1), std::minmax(c1, c2), std::minmax(c2, c0)};
		for (const auto& e : triEdges)
		{
			auto& acc = edges[e];
			if (acc.faces == 0) { acc.a = canonWorld[e.first]; acc.b = canonWorld[e.second]; }
			++acc.faces;
			if (front) { ++acc.frontCount; }
		}
	}

	// シルエット判定: 境界(面1枚) か、表裏が混在(面2枚で front が 0<count<faces)。
	for (const auto& [key, acc] : edges)
	{
		const bool boundary = (acc.faces == 1);
		const bool frontBackMix = (acc.frontCount > 0 && acc.frontCount < acc.faces);
		if (boundary || frontBackMix) { out.push_back(SilhouetteEdge{acc.a, acc.b}); }
	}
	return out;
}

} // namespace mitiru::render
