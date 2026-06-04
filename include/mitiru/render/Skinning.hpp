#pragma once

/// @file Skinning.hpp
/// @brief CPU 線形ブレンドスキニング (LBS) — ボーンポーズで base メッシュ頂点を変形する (#23b)
/// @details glTF/VRM の JOINTS_0 / WEIGHTS_0 (`GltfTypes::SkinVertexBinding`) と
///          inverseBindMatrices、そしてボーンの **ワールドポーズ palette** を受け取り、
///          base (バインドポーズ) 頂点の位置 + 法線を変形した頂点列を返す。
///
///          各 joint のスキニング行列 = worldPose[j] * inverseBind[j]。
///          頂点 v の変形 = Σ_k weight_k * (skinMat[joint_k] · v)  (位置は点変換、法線はベクタ変換)。
///
///          出力の変形済み頂点列はそのまま `MotionVectorPass::drawMeshDeforming` (#21a) /
///          `DeferredPipeline::prevMesh` (#18) の cur / prev ストリームに流せる ＝ VMD など
///          実モーションを「提示 MV」研究 (sim×描画の継ぎ目) に接続できる。
///
///          アニメ再生 (VMD 解釈・ボーン名マップ・ワールドポーズ palette の構築) は呼び出し側の
///          責務。本関数は「palette が与えられたら頂点を変形する」純関数 (GPU 非依存・テスト可能)。

#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/GltfTypes.hpp>
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

} // namespace mitiru::render
