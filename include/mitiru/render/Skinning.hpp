#pragma once

/// @file Skinning.hpp
/// @brief 線形ブレンドスキニング (LBS) の CPU 版と、スキン後の包含箱 (#23b)
/// @details glTF/VRM の JOINTS_0 / WEIGHTS_0 (`GltfTypes::SkinVertexBinding`) と
///          inverseBindMatrices、そしてボーンの **ワールドポーズ palette** を受け取り、
///          base (バインドポーズ) 頂点の位置 + 法線を変形した頂点列を返す。
///
///          各 joint のスキニング行列 = worldPose[j] * inverseBind[j]。
///          頂点 v の変形 = Σ_k weight_k * (skinMat[joint_k] · v)  (位置は点変換、法線はベクタ変換)。
///
///          DX12 の描画は同じ式を compute (Dx12SkinningCompute) で回す。skinVertices はその
///          答え合わせの基準であり、変形済み頂点を CPU で使う研究用途
///          (`MotionVectorPass::drawMeshDeforming` #21a / `DeferredPipeline::prevMesh` #18) の入口。
///          skinnedBounds は頂点を CPU に持たない GPU スキンのカリング用の箱を、頂点を見ずに出す。
///          palette の構築 (クリップ再生・ボーン名マップ) は呼び出し側 (animation/AnimPose.hpp)。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief 線形ブレンドスキニングで base 頂点を変形する。
/// @param base        バインドポーズ頂点 (位置 + 法線)。texCoord/color はそのまま引き継ぐ。
/// @param binding     頂点ごとの joints/weights (base と同数。空 or サイズ不一致なら base をそのまま返す)
/// @param inverseBind joint ごとの逆バインド行列 (row-major)。joints palette と同じ添字。
/// @param worldPose   joint ごとのワールドポーズ行列 (row-major)。inverseBind と同数であること。
/// @return 変形済み頂点列 (base と同数・同順)。法線は正規化して返す。
[[nodiscard]] inline std::vector<Vertex3D> skinVertices(
	const std::vector<Vertex3D>& base,
	const std::vector<SkinVertexBinding>& binding,
	const std::vector<sgc::Mat4f>& inverseBind,
	const std::vector<sgc::Mat4f>& worldPose)
{
	// 束縛情報が無い / 不整合なら変形しない (剛体扱い)。
	if (binding.size() != base.size() || inverseBind.size() != worldPose.size() ||
	    inverseBind.empty())
	{
		return base;
	}

	// joint ごとのスキニング行列 (worldPose * inverseBind) を前計算。
	const std::size_t jointCount = inverseBind.size();
	std::vector<sgc::Mat4f> skinMat(jointCount);
	for (std::size_t j = 0; j < jointCount; ++j)
	{
		skinMat[j] = worldPose[j] * inverseBind[j];
	}

	std::vector<Vertex3D> out = base;
	for (std::size_t i = 0; i < base.size(); ++i)
	{
		const auto& b = binding[i];
		// 重み和 (正規化用)。glTF は ≈1 だが端数や 0 束縛に備える。
		float wsum = b.weights[0] + b.weights[1] + b.weights[2] + b.weights[3];
		if (wsum <= 1e-8f)
		{
			continue;  // 影響ボーン無し → バインドポーズのまま
		}

		// 4 影響ボーンの skinMat を重み付き合成 (LBS は行列をブレンドしてから変換)。
		sgc::Mat4f blended{};  // 全要素 0
		for (int k = 0; k < 4; ++k)
		{
			const float w = b.weights[k];
			if (w == 0.0f) { continue; }
			const std::uint32_t jid = b.joints[k];
			if (jid >= jointCount) { continue; }  // 範囲外 joint は無視
			blended = blended + skinMat[jid] * (w / wsum);
		}

		out[i].position = blended.transformPoint(base[i].position);
		sgc::Vec3f n = blended.transformVector(base[i].normal);
		const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		if (len > 1e-7f) { n = {n.x / len, n.y / len, n.z / len}; }
		out[i].normal = n;
	}
	return out;
}

/// @brief skinnedBounds の入力。読み込み時に 1 回だけ作る
struct SkinBoundsSource
{
	/// joint ごとに、その joint に重みを持つ頂点を inverseBind で joint 空間へ移した箱 (空 = 影響無し)
	std::vector<Mesh::AABB> joint;
	Mesh::AABB rest;            ///< 重みが全て 0 でバインドポーズのまま残る頂点の箱
	bool towardOrigin = false;  ///< 範囲外 joint に重みを持つ頂点がある (skinVertices はその分だけ原点へ縮める)
};

namespace detail
{

inline void growBox(Mesh::AABB& box, const sgc::Vec3f& p) noexcept
{
	box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
	box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
}

} // namespace detail

/// @brief skinVertices と同じ束縛から SkinBoundsSource を作る
[[nodiscard]] inline SkinBoundsSource skinBoundsSource(const std::vector<Vertex3D>& base,
                                                       const std::vector<SkinVertexBinding>& binding,
                                                       const std::vector<sgc::Mat4f>& inverseBind)
{
	SkinBoundsSource src;
	src.joint.resize(inverseBind.size());
	for (std::size_t i = 0; i < base.size() && i < binding.size(); ++i)
	{
		const auto& b = binding[i];
		if (b.weights[0] + b.weights[1] + b.weights[2] + b.weights[3] <= 1e-8f)
		{
			detail::growBox(src.rest, base[i].position);
			continue;
		}
		for (int k = 0; k < 4; ++k)
		{
			if (b.weights[k] == 0.0f) { continue; }
			if (b.joints[k] >= inverseBind.size()) { src.towardOrigin = true; continue; }
			detail::growBox(src.joint[b.joints[k]], inverseBind[b.joints[k]].transformPoint(base[i].position));
		}
	}
	return src;
}

/// @brief worldPose (skinVertices と同じ palette) で変形した頂点を必ず含む箱
/// @details 変形後の頂点は「重みを持つ joint の箱を worldPose で動かしたもの」の凸結合なので、
///          動かした箱の和に収まる。頂点数に依らず joint 数 x 8 点で済む。
[[nodiscard]] inline Mesh::AABB skinnedBounds(const SkinBoundsSource& src,
                                              const std::vector<sgc::Mat4f>& worldPose)
{
	Mesh::AABB out = src.rest;
	if (src.towardOrigin) { detail::growBox(out, {0.0f, 0.0f, 0.0f}); }
	for (std::size_t j = 0; j < src.joint.size() && j < worldPose.size(); ++j)
	{
		const Mesh::AABB& b = src.joint[j];
		if (b.min.x > b.max.x) { continue; }
		for (int c = 0; c < 8; ++c)
		{
			const sgc::Vec3f corner{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y,
			                        (c & 4) ? b.max.z : b.min.z};
			detail::growBox(out, worldPose[j].transformPoint(corner));
		}
	}
	return out;
}

} // namespace mitiru::render
