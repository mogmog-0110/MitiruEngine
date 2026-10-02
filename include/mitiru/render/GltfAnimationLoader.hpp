#pragma once

/// @file GltfAnimationLoader.hpp
/// @brief glTF のノード階層・スキン・アニメーションだけを読む。
/// @details 描画の GltfLoader とアニメーションのランタイム (ゲーム DLL 側でも動く) が同じ関数で
///          骨格とクリップを取り出す。ここが 1 本なので、host と DLL が組む骨格は同じになる。
///          メッシュとテクスチャは読まない (stb の decode を通らない)。

#include <algorithm>
#include <cstddef>
#include <optional>

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/GltfTypes.hpp>

/// cgltf の実装は src/cgltf_impl.cpp にある。ここは宣言だけを使う。
#include <cgltf.h>

namespace mitiru::render
{

namespace detail
{

/// @brief アクセサから 16 float (列優先) を読み、row-major sgc::Mat4f に転置して返す (#23a)。
/// @details glTF の行列は列優先で格納される。sgc::Mat4f は行優先 (m[row][col]) なので転置する。
[[nodiscard]] inline sgc::Mat4f readMat4ColumnMajor(const cgltf_accessor* accessor, cgltf_size index)
{
	float buf[16] = {0};
	sgc::Mat4f out = sgc::Mat4f::identity();
	if (accessor && index < accessor->count &&
	    cgltf_accessor_read_float(accessor, index, buf, 16))
	{
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				out.m[r][c] = buf[c * 4 + r];  // 列優先 buf[col*4+row] → 行優先 m[row][col]
			}
		}
	}
	return out;
}

/// @brief ノード階層 (ボーン) を抽出する (#23a)。index は gltfData->nodes 配列基準。
inline void extractGltfNodes(const cgltf_data& data, GltfSceneData& scene)
{
	scene.nodes.resize(data.nodes_count);
	for (cgltf_size i = 0; i < data.nodes_count; ++i)
	{
		const auto& n = data.nodes[i];
		auto& gn = scene.nodes[i];
		gn.name = n.name ? n.name : "";
		gn.parent = n.parent ? static_cast<int>(n.parent - data.nodes) : -1;
		gn.mesh = n.mesh ? static_cast<int>(n.mesh - data.meshes) : -1;   // #25
		gn.skin = n.skin ? static_cast<int>(n.skin - data.skins) : -1;   // #25
		if (n.has_translation) { gn.translation = {n.translation[0], n.translation[1], n.translation[2]}; }
		if (n.has_rotation) { gn.rotation = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]}; }
		if (n.has_scale) { gn.scale = {n.scale[0], n.scale[1], n.scale[2]}; }
		gn.children.reserve(n.children_count);
		for (cgltf_size c = 0; c < n.children_count; ++c)
		{
			gn.children.push_back(static_cast<int>(n.children[c] - data.nodes));
		}
	}
}

/// @brief スキン (joints + inverseBindMatrices) を抽出する (#23a)。
inline void extractGltfSkins(const cgltf_data& data, GltfSceneData& scene)
{
	scene.skins.resize(data.skins_count);
	for (cgltf_size i = 0; i < data.skins_count; ++i)
	{
		const auto& sk = data.skins[i];
		auto& gs = scene.skins[i];
		gs.name = sk.name ? sk.name : "";
		gs.skeletonRoot = sk.skeleton ? static_cast<int>(sk.skeleton - data.nodes) : -1;
		gs.joints.reserve(sk.joints_count);
		for (cgltf_size j = 0; j < sk.joints_count; ++j)
		{
			gs.joints.push_back(static_cast<int>(sk.joints[j] - data.nodes));
		}
		if (sk.inverse_bind_matrices)
		{
			gs.inverseBindMatrices.resize(sk.joints_count);
			for (cgltf_size j = 0; j < sk.joints_count; ++j)
			{
				gs.inverseBindMatrices[j] = readMat4ColumnMajor(sk.inverse_bind_matrices, j);
			}
		}
	}
}

/// @brief キー列 (times / values、CUBICSPLINE なら接線も) を読む。不整合なら false。
[[nodiscard]] inline bool readGltfChannelKeys(const cgltf_animation_sampler& s, cgltf_size comps,
                                              GltfAnimationChannel& gc)
{
	/// CUBICSPLINE は 3 値/キー (in-tangent, 値, out-tangent)。
	const bool cubic = (s.interpolation == cgltf_interpolation_type_cubic_spline);
	gc.interpolation = cubic ? GltfAnimInterp::CubicSpline
	                   : (s.interpolation == cgltf_interpolation_type_step) ? GltfAnimInterp::Step
	                                                                        : GltfAnimInterp::Linear;
	const cgltf_size keyCount = s.input->count;
	const cgltf_size expected = cubic ? keyCount * 3 : keyCount;
	if (keyCount == 0 || s.output->count != expected) { return false; }

	gc.times.resize(keyCount);
	gc.values.resize(keyCount);
	if (cubic)
	{
		gc.inTangents.resize(keyCount);
		gc.outTangents.resize(keyCount);
	}
	const auto readVec = [&](cgltf_size at) {
		float buf[4] = {0, 0, 0, 0};
		cgltf_accessor_read_float(s.output, at, buf, comps);
		return sgc::Vec4f{buf[0], buf[1], buf[2], buf[3]};
	};
	for (cgltf_size k = 0; k < keyCount; ++k)
	{
		float t = 0.0f;
		cgltf_accessor_read_float(s.input, k, &t, 1);
		gc.times[k] = t;
		gc.values[k] = readVec(cubic ? (k * 3 + 1) : k);
		if (cubic)
		{
			gc.inTangents[k] = readVec(k * 3);
			gc.outTangents[k] = readVec(k * 3 + 2);
		}
	}
	return true;
}

/// @brief アニメーションクリップを抽出する。T/R/S チャンネルのみ (morph weights は読まない)。
inline void extractGltfAnimations(const cgltf_data& data, GltfSceneData& scene)
{
	scene.animations.reserve(data.animations_count);
	for (cgltf_size i = 0; i < data.animations_count; ++i)
	{
		const auto& anim = data.animations[i];
		GltfAnimationClip clip;
		clip.name = anim.name ? anim.name : "";
		for (cgltf_size c = 0; c < anim.channels_count; ++c)
		{
			const auto& ch = anim.channels[c];
			if (ch.target_node == nullptr || ch.sampler == nullptr) { continue; }
			if (ch.sampler->input == nullptr || ch.sampler->output == nullptr) { continue; }

			GltfAnimationChannel gc;
			gc.nodeIndex = static_cast<int>(ch.target_node - data.nodes);
			cgltf_size comps = 3;
			switch (ch.target_path)
			{
			case cgltf_animation_path_type_translation: gc.path = GltfAnimPath::Translation; break;
			case cgltf_animation_path_type_rotation:    gc.path = GltfAnimPath::Rotation; comps = 4; break;
			case cgltf_animation_path_type_scale:       gc.path = GltfAnimPath::Scale; break;
			default: continue;
			}
			if (!readGltfChannelKeys(*ch.sampler, comps, gc)) { continue; }
			clip.durationSec = std::max(clip.durationSec, gc.times.back());
			clip.channels.push_back(std::move(gc));
		}
		if (!clip.channels.empty()) { scene.animations.push_back(std::move(clip)); }
	}
}

/// @brief ノード・スキン・アニメーションをまとめて抽出する。
inline void extractGltfRig(const cgltf_data& data, GltfSceneData& scene)
{
	extractGltfNodes(data, scene);
	extractGltfSkins(data, scene);
	extractGltfAnimations(data, scene);
}

} // namespace detail

/// @brief glTF / glb のバイト列からノード・スキン・アニメーションだけを読む。
/// @details meshes / materials は空のまま返す。メッシュを持たないアニメーション専用の glb も読める。
///          外部 .bin を参照する .gltf は読めない (バイト列だけでは解決できない)。
[[nodiscard]] inline std::optional<GltfSceneData> loadGltfAnimationFromMemory(const void* data, std::size_t size)
{
	if (data == nullptr || size == 0) { return std::nullopt; }
	cgltf_options options{};
	cgltf_data* parsed = nullptr;
	if (cgltf_parse(&options, data, size, &parsed) != cgltf_result_success || parsed == nullptr)
	{
		if (parsed != nullptr) { cgltf_free(parsed); }
		return std::nullopt;
	}
	if (cgltf_load_buffers(&options, parsed, nullptr) != cgltf_result_success)
	{
		cgltf_free(parsed);
		return std::nullopt;
	}
	GltfSceneData scene;
	detail::extractGltfRig(*parsed, scene);
	cgltf_free(parsed);
	if (scene.nodes.empty()) { return std::nullopt; }
	return scene;
}

} // namespace mitiru::render
