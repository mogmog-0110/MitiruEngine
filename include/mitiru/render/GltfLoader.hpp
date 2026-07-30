#pragma once

/// @file GltfLoader.hpp
/// @brief glTF 2.0 ローダー (.gltf / .glb)
/// @details cgltfを使用してglTFファイルをパースし、エンジンのMeshオブジェクトに変換する。
///          ObjLoader.hppと同じパターン（optional返却）を踏襲する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

/// cgltf の実装は src/cgltf_impl.cpp に集約（vendor glue、#16）。ここは宣言のみ include
/// するので、複数 TU から本ヘッダを include しても重複定義リンクエラーにならない。
#include <cgltf.h>

/// stb_image は宣言のみ（実装は src/stb_impl.cpp）。埋め込みテクスチャの decode に使う（#17）。
#include <stb_image.h>

namespace mitiru::render
{

namespace detail
{

/// @brief 埋め込み（buffer_view）画像を stb_image で RGBA8 にデコードする（#17）。
/// @details glb の埋め込みテクスチャのみ対応（外部 URI / data URI は未対応 → 空 CpuTexture）。
///          cgltf_load_buffers 済み前提（buffer->data が埋まっている）。
[[nodiscard]] inline CpuTexture decodeEmbeddedImage(const cgltf_image* img)
{
	CpuTexture tex;
	if (img == nullptr || img->buffer_view == nullptr) { return tex; }
	const cgltf_buffer_view* bv = img->buffer_view;
	if (bv->buffer == nullptr || bv->buffer->data == nullptr || bv->size == 0) { return tex; }

	const auto* bytes = static_cast<const unsigned char*>(bv->buffer->data) + bv->offset;
	int w = 0, h = 0, comp = 0;
	unsigned char* pixels =
		stbi_load_from_memory(bytes, static_cast<int>(bv->size), &w, &h, &comp, 4);
	if (pixels == nullptr) { return tex; }

	tex.width = w;
	tex.height = h;
	tex.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
	stbi_image_free(pixels);
	return tex;
}

/// @brief cgltfアクセサから浮動小数点値を安全に読み取る
[[nodiscard]] inline float readFloat(const cgltf_accessor* accessor, cgltf_size index, cgltf_size component)
{
	float value = 0.0f;
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, &value + 0, 1);
		/// 複数コンポーネントの場合、一時バッファを使う
		if (component > 0)
		{
			float buf[4] = {0};
			const auto numComponents = cgltf_num_components(accessor->type);
			if (component < static_cast<cgltf_size>(numComponents))
			{
				cgltf_accessor_read_float(accessor, index, buf, static_cast<cgltf_size>(numComponents));
				value = buf[component];
			}
		}
		else
		{
			float buf[4] = {0};
			cgltf_accessor_read_float(accessor, index, buf, 1);
			value = buf[0];
		}
	}
	return value;
}

/// @brief cgltfアクセサからVec3を読み取る
[[nodiscard]] inline sgc::Vec3f readVec3(const cgltf_accessor* accessor, cgltf_size index)
{
	float buf[3] = {0, 0, 0};
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, buf, 3);
	}
	return {buf[0], buf[1], buf[2]};
}

/// @brief cgltfアクセサからVec2を読み取る
[[nodiscard]] inline sgc::Vec2f readVec2(const cgltf_accessor* accessor, cgltf_size index)
{
	float buf[2] = {0, 0};
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, buf, 2);
	}
	return {buf[0], buf[1]};
}

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

/// @brief cgltfアクセサからインデックスを読み取る
[[nodiscard]] inline uint32_t readIndex(const cgltf_accessor* accessor, cgltf_size index)
{
	if (!accessor || index >= accessor->count) { return 0; }
	return static_cast<uint32_t>(cgltf_accessor_read_index(accessor, index));
}

/// @brief 三角形の頂点からフラット法線を計算する
[[nodiscard]] inline sgc::Vec3f computeFlatNormal(
	const sgc::Vec3f& v0, const sgc::Vec3f& v1, const sgc::Vec3f& v2)
{
	const sgc::Vec3f edge1 = v1 - v0;
	const sgc::Vec3f edge2 = v2 - v0;
	sgc::Vec3f n{
		edge1.y * edge2.z - edge1.z * edge2.y,
		edge1.z * edge2.x - edge1.x * edge2.z,
		edge1.x * edge2.y - edge1.y * edge2.x
	};
	const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (len > 1e-7f)
	{
		n.x /= len; n.y /= len; n.z /= len;
	}
	return n;
}

/// @brief cgltf プリミティブを GltfMeshPrimitive に変換する
[[nodiscard]] inline GltfMeshPrimitive convertPrimitive(const cgltf_primitive& prim)
{
	GltfMeshPrimitive result;

	/// 三角形以外はスキップ（空で返す）
	if (prim.type != cgltf_primitive_type_triangles)
	{
		return result;
	}

	/// アクセサを検索する
	const cgltf_accessor* posAccessor = nullptr;
	const cgltf_accessor* normAccessor = nullptr;
	const cgltf_accessor* uvAccessor = nullptr;
	const cgltf_accessor* jointsAccessor = nullptr;   ///< JOINTS_0 (#23a)
	const cgltf_accessor* weightsAccessor = nullptr;  ///< WEIGHTS_0 (#23a)

	for (cgltf_size i = 0; i < prim.attributes_count; ++i)
	{
		const auto& attr = prim.attributes[i];
		/// JOINTS_0 / WEIGHTS_0 は index 0 のみ採用 (4 ボーン束縛、glTF 標準)。
		switch (attr.type)
		{
		case cgltf_attribute_type_position: posAccessor = attr.data; break;
		case cgltf_attribute_type_normal:   normAccessor = attr.data; break;
		case cgltf_attribute_type_texcoord: uvAccessor = attr.data; break;
		case cgltf_attribute_type_joints:
			if (attr.index == 0) { jointsAccessor = attr.data; }
			break;
		case cgltf_attribute_type_weights:
			if (attr.index == 0) { weightsAccessor = attr.data; }
			break;
		default: break;
		}
	}

	if (!posAccessor || posAccessor->count == 0)
	{
		return result;
	}

	/// 頂点を読み取る
	const auto vertexCount = posAccessor->count;
	result.vertices.resize(vertexCount);

	for (cgltf_size i = 0; i < vertexCount; ++i)
	{
		auto& v = result.vertices[i];
		v.position = readVec3(posAccessor, i);
		v.normal = normAccessor ? readVec3(normAccessor, i) : sgc::Vec3f{0, 0, 0};
		v.texCoord = uvAccessor ? readVec2(uvAccessor, i) : sgc::Vec2f{0, 0};
		v.color = sgc::Colorf{1, 1, 1, 1};
	}

	/// スキン束縛 (JOINTS_0 / WEIGHTS_0) を読み取る (#23a)。両方揃っている時のみ。
	if (jointsAccessor && weightsAccessor &&
	    jointsAccessor->count == vertexCount && weightsAccessor->count == vertexCount)
	{
		result.skin.resize(vertexCount);
		for (cgltf_size i = 0; i < vertexCount; ++i)
		{
			cgltf_uint j[4] = {0, 0, 0, 0};
			float w[4] = {0, 0, 0, 0};
			cgltf_accessor_read_uint(jointsAccessor, i, j, 4);
			cgltf_accessor_read_float(weightsAccessor, i, w, 4);
			auto& s = result.skin[i];
			for (int k = 0; k < 4; ++k)
			{
				s.joints[k] = static_cast<std::uint32_t>(j[k]);
				s.weights[k] = w[k];
			}
		}
	}

	/// インデックスを読み取る
	if (prim.indices)
	{
		const auto indexCount = prim.indices->count;
		result.indices.resize(indexCount);
		for (cgltf_size i = 0; i < indexCount; ++i)
		{
			result.indices[i] = readIndex(prim.indices, i);
		}
	}
	else
	{
		/// インデックスなし → 連番生成
		result.indices.resize(vertexCount);
		for (cgltf_size i = 0; i < vertexCount; ++i)
		{
			result.indices[i] = static_cast<uint32_t>(i);
		}
	}

	/// 法線がない場合、フラット法線を生成する
	if (!normAccessor && result.indices.size() >= 3)
	{
		for (std::size_t i = 0; i + 2 < result.indices.size(); i += 3)
		{
			const auto i0 = result.indices[i];
			const auto i1 = result.indices[i + 1];
			const auto i2 = result.indices[i + 2];
			if (i0 < result.vertices.size() &&
				i1 < result.vertices.size() &&
				i2 < result.vertices.size())
			{
				const auto n = computeFlatNormal(
					result.vertices[i0].position,
					result.vertices[i1].position,
					result.vertices[i2].position);
				result.vertices[i0].normal = n;
				result.vertices[i1].normal = n;
				result.vertices[i2].normal = n;
			}
		}
	}

	/// モーフターゲット (blend shape) のデルタを読む (#24)。各 target の POSITION/NORMAL delta。
	for (cgltf_size t = 0; t < prim.targets_count; ++t)
	{
		const auto& target = prim.targets[t];
		const cgltf_accessor* posDelta = nullptr;
		const cgltf_accessor* normDelta = nullptr;
		for (cgltf_size a = 0; a < target.attributes_count; ++a)
		{
			const auto& attr = target.attributes[a];
			if (attr.type == cgltf_attribute_type_position) { posDelta = attr.data; }
			else if (attr.type == cgltf_attribute_type_normal) { normDelta = attr.data; }
		}
		GltfMorphTarget gt;
		if (posDelta && posDelta->count == vertexCount)
		{
			gt.positionDelta.resize(vertexCount);
			for (cgltf_size i = 0; i < vertexCount; ++i) { gt.positionDelta[i] = readVec3(posDelta, i); }
		}
		if (normDelta && normDelta->count == vertexCount)
		{
			gt.normalDelta.resize(vertexCount);
			for (cgltf_size i = 0; i < vertexCount; ++i) { gt.normalDelta[i] = readVec3(normDelta, i); }
		}
		result.morphTargets.push_back(std::move(gt));
	}

	/// マテリアルインデックス
	if (prim.material)
	{
		/// cgltf_material ポインタからインデックスを逆算する
		/// （cgltf はポインタベースなので、data->materials からのオフセットで取得）
		result.materialIndex = -1; // 後で外側で設定
	}

	return result;
}

} // namespace detail

/// @brief glTFメモリデータからシーンを読み込む
/// @param data データバッファ
/// @param size データサイズ
/// @return パース成功時はGltfSceneData、失敗時はnullopt
[[nodiscard]] inline std::optional<GltfSceneData> loadGltfFromMemory(
	const void* data, std::size_t size)
{
	if (!data || size == 0) { return std::nullopt; }

	cgltf_options options{};
	cgltf_data* gltfData = nullptr;

	cgltf_result result = cgltf_parse(&options, data, size, &gltfData);
	if (result != cgltf_result_success || !gltfData)
	{
		if (gltfData) { cgltf_free(gltfData); }
		return std::nullopt;
	}

	/// バッファデータをロードする（メモリ内glbの場合はバッファが埋め込まれている）
	result = cgltf_load_buffers(&options, gltfData, nullptr);
	if (result != cgltf_result_success)
	{
		cgltf_free(gltfData);
		return std::nullopt;
	}

	GltfSceneData scene;

	/// マテリアルを抽出する
	for (cgltf_size i = 0; i < gltfData->materials_count; ++i)
	{
		const auto& mat = gltfData->materials[i];
		GltfMaterialData gmat;
		gmat.name = mat.name ? mat.name : "";

		if (mat.has_pbr_metallic_roughness)
		{
			const auto& pbr = mat.pbr_metallic_roughness;
			gmat.baseColor = {
				pbr.base_color_factor[0],
				pbr.base_color_factor[1],
				pbr.base_color_factor[2],
				pbr.base_color_factor[3]
			};
			gmat.metallic = pbr.metallic_factor;
			gmat.roughness = pbr.roughness_factor;

			if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
			{
				const auto* img = pbr.base_color_texture.texture->image;
				gmat.baseColorTexturePath = img->uri ? img->uri : "";
				gmat.baseColorTexture = detail::decodeEmbeddedImage(img);   // #17: 埋め込みを decode
			}
		}

		if (mat.normal_texture.texture && mat.normal_texture.texture->image)
		{
			const auto* img = mat.normal_texture.texture->image;
			gmat.normalTexturePath = img->uri ? img->uri : "";
		}

		scene.materials.push_back(std::move(gmat));
	}

	/// メッシュを抽出する
	for (cgltf_size i = 0; i < gltfData->meshes_count; ++i)
	{
		const auto& mesh = gltfData->meshes[i];
		GltfMeshData gMesh;
		gMesh.name = mesh.name ? mesh.name : "";

		/// モーフ名 (VRM は日本語) を抽出する (#24)。primitive.morphTargets と同順。
		for (cgltf_size t = 0; t < mesh.target_names_count; ++t)
		{
			gMesh.morphTargetNames.push_back(mesh.target_names[t] ? mesh.target_names[t] : "");
		}

		for (cgltf_size j = 0; j < mesh.primitives_count; ++j)
		{
			auto prim = detail::convertPrimitive(mesh.primitives[j]);
			if (prim.vertices.empty()) { continue; }

			/// マテリアルインデックスを設定する
			if (mesh.primitives[j].material)
			{
				prim.materialIndex = static_cast<int>(
					mesh.primitives[j].material - gltfData->materials);
			}

			gMesh.primitives.push_back(std::move(prim));
		}

		if (!gMesh.primitives.empty())
		{
			scene.meshes.push_back(std::move(gMesh));
		}
	}

	/// ノード階層 (ボーン) を抽出する (#23a)。index は gltfData->nodes 配列基準。
	scene.nodes.resize(gltfData->nodes_count);
	for (cgltf_size i = 0; i < gltfData->nodes_count; ++i)
	{
		const auto& n = gltfData->nodes[i];
		auto& gn = scene.nodes[i];
		gn.name = n.name ? n.name : "";
		gn.parent = n.parent ? static_cast<int>(n.parent - gltfData->nodes) : -1;
		gn.mesh = n.mesh ? static_cast<int>(n.mesh - gltfData->meshes) : -1;   // #25
		gn.skin = n.skin ? static_cast<int>(n.skin - gltfData->skins) : -1;   // #25
		if (n.has_translation) { gn.translation = {n.translation[0], n.translation[1], n.translation[2]}; }
		if (n.has_rotation) { gn.rotation = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]}; }
		if (n.has_scale) { gn.scale = {n.scale[0], n.scale[1], n.scale[2]}; }
		gn.children.reserve(n.children_count);
		for (cgltf_size c = 0; c < n.children_count; ++c)
		{
			gn.children.push_back(static_cast<int>(n.children[c] - gltfData->nodes));
		}
	}

	/// スキン (joints + inverseBindMatrices) を抽出する (#23a)。
	scene.skins.resize(gltfData->skins_count);
	for (cgltf_size i = 0; i < gltfData->skins_count; ++i)
	{
		const auto& sk = gltfData->skins[i];
		auto& gs = scene.skins[i];
		gs.name = sk.name ? sk.name : "";
		gs.skeletonRoot = sk.skeleton ? static_cast<int>(sk.skeleton - gltfData->nodes) : -1;
		gs.joints.reserve(sk.joints_count);
		for (cgltf_size j = 0; j < sk.joints_count; ++j)
		{
			gs.joints.push_back(static_cast<int>(sk.joints[j] - gltfData->nodes));
		}
		if (sk.inverse_bind_matrices)
		{
			gs.inverseBindMatrices.resize(sk.joints_count);
			for (cgltf_size j = 0; j < sk.joints_count; ++j)
			{
				gs.inverseBindMatrices[j] = detail::readMat4ColumnMajor(sk.inverse_bind_matrices, j);
			}
		}
	}

	/// アニメーションクリップを抽出する (ADR 0028)。T/R/S チャンネルのみ (morph weights は skip)。
	scene.animations.reserve(gltfData->animations_count);
	for (cgltf_size i = 0; i < gltfData->animations_count; ++i)
	{
		const auto& anim = gltfData->animations[i];
		GltfAnimationClip clip;
		clip.name = anim.name ? anim.name : "";

		for (cgltf_size c = 0; c < anim.channels_count; ++c)
		{
			const auto& ch = anim.channels[c];
			if (ch.target_node == nullptr || ch.sampler == nullptr) { continue; }
			if (ch.sampler->input == nullptr || ch.sampler->output == nullptr) { continue; }

			GltfAnimationChannel gc;
			gc.nodeIndex = static_cast<int>(ch.target_node - gltfData->nodes);
			int comps = 3;
			switch (ch.target_path)
			{
			case cgltf_animation_path_type_translation: gc.path = GltfAnimPath::Translation; break;
			case cgltf_animation_path_type_rotation:    gc.path = GltfAnimPath::Rotation; comps = 4; break;
			case cgltf_animation_path_type_scale:       gc.path = GltfAnimPath::Scale; break;
			default: continue;  // weights (morph) 等は v1 対象外
			}

			/// CUBICSPLINE は 3 値/キー (in-tangent, 値, out-tangent)。中央値のみ Linear として読む。
			const bool cubic = (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline);
			gc.interpolation = (ch.sampler->interpolation == cgltf_interpolation_type_step)
			                       ? GltfAnimInterp::Step
			                       : GltfAnimInterp::Linear;
			if (cubic)
			{
				debug::warnOnce("gltf.anim.cubicspline",
				                "glTF CUBICSPLINE 補間は Linear へ縮退します (ADR 0028 v1)");
			}

			const cgltf_size keyCount = ch.sampler->input->count;
			const cgltf_size valueCount = ch.sampler->output->count;
			const cgltf_size expected = cubic ? keyCount * 3 : keyCount;
			if (keyCount == 0 || valueCount != expected) { continue; }  // 不整合は捨てる

			gc.times.resize(keyCount);
			gc.values.resize(keyCount);
			for (cgltf_size k = 0; k < keyCount; ++k)
			{
				float t = 0.0f;
				cgltf_accessor_read_float(ch.sampler->input, k, &t, 1);
				gc.times[k] = t;

				float buf[4] = {0, 0, 0, 0};
				const cgltf_size vi = cubic ? (k * 3 + 1) : k;
				cgltf_accessor_read_float(ch.sampler->output, vi, buf,
				                          static_cast<cgltf_size>(comps));
				gc.values[k] = {buf[0], buf[1], buf[2], buf[3]};
			}
			clip.durationSec = std::max(clip.durationSec, gc.times.back());
			clip.channels.push_back(std::move(gc));
		}

		if (!clip.channels.empty())
		{
			scene.animations.push_back(std::move(clip));
		}
	}

	cgltf_free(gltfData);

	if (scene.meshes.empty()) { return std::nullopt; }
	return scene;
}

/// @brief glTFファイルからシーンを読み込む
/// @param filePath glTF/GLBファイルパス
/// @return パース成功時はGltfSceneData、失敗時はnullopt
[[nodiscard]] inline std::optional<GltfSceneData> loadGltfSceneFromFile(const std::string& filePath)
{
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) { return std::nullopt; }

	const auto fileSize = file.tellg();
	if (fileSize <= 0) { return std::nullopt; }

	file.seekg(0);
	std::vector<char> buffer(static_cast<std::size_t>(fileSize));
	file.read(buffer.data(), fileSize);

	return loadGltfFromMemory(buffer.data(), buffer.size());
}

/// @brief glTFファイルから最初のメッシュを読み込む（便利関数）
/// @param filePath glTF/GLBファイルパス
/// @return パース成功時はMesh、失敗時はnullopt
[[nodiscard]] inline std::optional<Mesh> loadGltfMeshFromFile(const std::string& filePath)
{
	auto scene = loadGltfSceneFromFile(filePath);
	if (!scene || scene->meshes.empty() || scene->meshes[0].primitives.empty())
	{
		return std::nullopt;
	}

	/// 最初のメッシュの最初のプリミティブを返す
	const auto& prim = scene->meshes[0].primitives[0];
	Mesh mesh;
	mesh.setVertices(prim.vertices);
	mesh.setIndices(prim.indices);
	return mesh;
}

} // namespace mitiru::render
