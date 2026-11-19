#pragma once

/// @file ModelCheck.hpp
/// @brief glb / glTF の中身 (クリップ、骨の数、大きさ、スキンの上限に触れる所) を描画せずに調べる。

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cgltf.h>

#include <sgc/math/Vec3.hpp>

#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/GltfLoader.hpp>

namespace mitiru::animation
{

/// Renderer3D_DX12::Config::maxSkinJoints の既定値と同じ。超えたスキンは剛体で描かれる
inline constexpr int kModelCheckMaxSkinJoints = 256;
/// 重みの和がこれ未満の頂点では、5 本目以降の影響が失われている。エンジンは JOINTS_0 / WEIGHTS_0 の 4 本だけを読む
inline constexpr float kModelCheckMinWeightSum = 0.99f;

struct ModelClipInfo
{
	std::string name;
	float durationSec = 0.0f;
};

struct ModelCheck
{
	std::string path;
	std::string error;                  ///< 空でなければ読めていない (他のフィールドは空のまま)
	std::string requestError;           ///< 頼まれたクリップが無いなど、読めてはいるが頼みに応えられない理由
	std::vector<ModelClipInfo> clips;
	int nodeCount = 0;
	int skinCount = 0;
	int maxJoints = 0;
	int meshCount = 0;
	int vertexCount = 0;
	int triangleCount = 0;
	int materialCount = 0;
	int texturedMaterialCount = 0;
	int skinnedVertexCount = 0;
	int unweightedVertexCount = 0;
	int underweightedVertexCount = 0;
	int badJointIndexCount = 0;         ///< 骨の番号が、最大のスキンの骨の数にも収まらない頂点 (複数のスキンは区別しない)
	int extraInfluencePrimitiveCount = 0;
	sgc::Vec3f boundsMin{0.0f, 0.0f, 0.0f};   ///< 頂点の包含箱 (バインド姿勢、メッシュの座標のまま)
	sgc::Vec3f boundsMax{0.0f, 0.0f, 0.0f};
	std::vector<std::string> warnings;

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
	[[nodiscard]] bool failed() const noexcept { return !error.empty() || !requestError.empty(); }
	[[nodiscard]] int findClip(std::string_view name) const noexcept
	{
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (clips[i].name == name) { return static_cast<int>(i); }
		}
		return -1;
	}
};

namespace detail
{

inline int countExtraInfluencePrimitives(const std::vector<std::uint8_t>& bytes)
{
	cgltf_options options{};
	cgltf_data* data = nullptr;
	if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success || data == nullptr)
	{
		if (data != nullptr) { cgltf_free(data); }
		return 0;
	}
	int count = 0;
	for (cgltf_size m = 0; m < data->meshes_count; ++m)
	{
		for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
		{
			const auto& prim = data->meshes[m].primitives[p];
			bool extra = false;
			for (cgltf_size a = 0; a < prim.attributes_count; ++a)
			{
				const auto& attr = prim.attributes[a];
				const bool skinSet = attr.type == cgltf_attribute_type_joints || attr.type == cgltf_attribute_type_weights;
				extra = extra || (skinSet && attr.index >= 1);
			}
			count += extra ? 1 : 0;
		}
	}
	cgltf_free(data);
	return count;
}

inline void growBounds(ModelCheck& out, const sgc::Vec3f& p, bool first)
{
	const sgc::Vec3f lo = out.boundsMin, hi = out.boundsMax;
	out.boundsMin = first ? p : sgc::Vec3f{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
	out.boundsMax = first ? p : sgc::Vec3f{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}

inline void tallyMesh(const render::GltfSceneData& scene, ModelCheck& out, std::size_t jointLimit)
{
	bool first = true;
	for (const auto& mesh : scene.meshes)
	{
		++out.meshCount;
		for (const auto& prim : mesh.primitives)
		{
			out.vertexCount += static_cast<int>(prim.vertices.size());
			out.triangleCount += static_cast<int>(prim.indices.size() / 3);
			for (const auto& v : prim.vertices)
			{
				growBounds(out, v.position, first);
				first = false;
			}
			for (const auto& s : prim.skin)
			{
				const float sum = s.weights[0] + s.weights[1] + s.weights[2] + s.weights[3];
				++out.skinnedVertexCount;
				out.unweightedVertexCount += (sum <= 1e-6f) ? 1 : 0;
				out.underweightedVertexCount += (sum > 1e-6f && sum < kModelCheckMinWeightSum) ? 1 : 0;
				bool outOfRange = false;
				for (int k = 0; k < 4; ++k) { outOfRange = outOfRange || (s.weights[k] > 0.0f && s.joints[k] >= jointLimit); }
				out.badJointIndexCount += outOfRange ? 1 : 0;
			}
		}
	}
}

inline void addWarnings(ModelCheck& out)
{
	const auto add = [&out](const std::string& text) { out.warnings.push_back(text); };
	if (out.maxJoints > kModelCheckMaxSkinJoints)
	{
		add("骨が " + std::to_string(out.maxJoints) + " 本で、上限の " + std::to_string(kModelCheckMaxSkinJoints) +
		    " を超えている (剛体で描かれる)");
	}
	if (out.extraInfluencePrimitiveCount > 0)
	{
		add("JOINTS_1 以降を持つ primitive が " + std::to_string(out.extraInfluencePrimitiveCount) +
		    " 個ある (5 本目以降の影響は読まれない。骨ごとの影響を 4 本までに絞って書き出し直す)");
	}
	if (out.underweightedVertexCount > 0)
	{
		add("重みの和が 0.99 未満の頂点が " + std::to_string(out.underweightedVertexCount) + " 個ある (正規化して書き出し直す)");
	}
	if (out.unweightedVertexCount > 0)
	{
		add("重みが全部 0 の頂点が " + std::to_string(out.unweightedVertexCount) + " 個ある");
	}
	if (out.badJointIndexCount > 0)
	{
		add("スキンの骨の数に収まらない骨の番号を持つ頂点が " + std::to_string(out.badJointIndexCount) + " 個ある");
	}
	if (out.skinnedVertexCount > 0 && out.clips.empty()) { add("スキンはあるがクリップが無い"); }
}

} // namespace detail

[[nodiscard]] inline ModelCheck checkModelFile(std::string_view path)
{
	ModelCheck out;
	out.path = std::string(path);
	std::string error;
	const auto source = animModelSourcePath(path, error);
	const auto bytes = source ? vfs::readGlobal(*source) : std::nullopt;
	if (!bytes) { out.error = error.empty() ? "読めない: " + out.path : error; return out; }

	const auto dirEnd = source->find_last_of("/\\");
	const std::string baseDir = dirEnd == std::string::npos ? std::string() : source->substr(0, dirEnd + 1);
	const auto scene = render::loadGltfFromMemory(bytes->data(), bytes->size(), baseDir);
	const auto asset = loadAnimAssetFile(path, &out.warnings);
	if (!scene || !asset) { out.error = "glTF として読めない (メッシュが無いか、壊れている): " + out.path; return out; }

	out.nodeCount = asset->nodeCount();
	out.skinCount = static_cast<int>(asset->skins.size());
	std::size_t jointLimit = 0;
	for (const auto& skin : asset->skins) { jointLimit = std::max(jointLimit, skin.joints.size()); }
	out.maxJoints = static_cast<int>(jointLimit);
	for (const auto& c : asset->clips) { out.clips.push_back({c.name, c.durationSec}); }
	out.materialCount = static_cast<int>(scene->materials.size());
	for (const auto& m : scene->materials)
	{
		out.texturedMaterialCount += (m.baseColorTexture.valid() || !m.baseColorTexturePath.empty()) ? 1 : 0;
	}
	out.extraInfluencePrimitiveCount = detail::countExtraInfluencePrimitives(*bytes);
	detail::tallyMesh(*scene, out, jointLimit);
	detail::addWarnings(out);
	return out;
}

/// @brief --clip で指定した名前がないとき、存在する名前を並べて requestError に入れる。レスト姿勢は表示しない。
inline bool requireClip(ModelCheck& check, std::string_view clipName)
{
	if (clipName.empty() || !check.ok() || check.findClip(clipName) >= 0) { return check.ok(); }
	std::string names;
	for (const auto& c : check.clips) { names += (names.empty() ? "" : ", ") + c.name; }
	check.requestError = "クリップ " + std::string(clipName) + " が無い (あるのは: " + (names.empty() ? "無し" : names) + ")";
	return false;
}

[[nodiscard]] inline std::string formatModelCheck(const ModelCheck& c)
{
	std::string text = "モデル: " + c.path + "\n";
	if (!c.ok()) { return text + "  エラー: " + c.error + "\n"; }
	char line[320];
	std::snprintf(line, sizeof line, "  大きさ: 幅 %.3f x 高さ %.3f x 奥行 %.3f (x %.3f..%.3f, y %.3f..%.3f, z %.3f..%.3f)\n",
	              c.boundsMax.x - c.boundsMin.x, c.boundsMax.y - c.boundsMin.y, c.boundsMax.z - c.boundsMin.z,
	              c.boundsMin.x, c.boundsMax.x, c.boundsMin.y, c.boundsMax.y, c.boundsMin.z, c.boundsMax.z);
	text += line;
	std::snprintf(line, sizeof line, "  メッシュ %d / 頂点 %d / 三角形 %d / 材質 %d (テクスチャあり %d)\n", c.meshCount, c.vertexCount,
	              c.triangleCount, c.materialCount, c.texturedMaterialCount);
	text += line;
	std::snprintf(line, sizeof line, "  ノード %d / スキン %d / 骨 %d (上限 %d)\n", c.nodeCount, c.skinCount, c.maxJoints,
	              kModelCheckMaxSkinJoints);
	text += line;
	text += "  クリップ " + std::to_string(c.clips.size()) + ":";
	for (const auto& clip : c.clips)
	{
		std::snprintf(line, sizeof line, " %s (%.2f 秒)", clip.name.c_str(), static_cast<double>(clip.durationSec));
		text += line;
	}
	text += "\n";
	for (const auto& w : c.warnings) { text += "  警告: " + w + "\n"; }
	if (!c.requestError.empty()) { text += "  エラー: " + c.requestError + "\n"; }
	return text;
}

} // namespace mitiru::animation
