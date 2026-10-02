/// @file fbx_import_impl.cpp
/// @brief FBX → glb 変換 (asset/FbxImport.hpp の実装、ufbx)
/// @details ufbx に右手系 Y-up・1 単位 1 m へそろえさせ、ノード・メッシュ・スキン・動作・材質を
///          GltfSceneData に移して GlbWriter で書く。動作は ufbx_bake_anim で線形キーに焼く
///          (FBX のオイラー角補間とレイヤーをエンジンの補間で再現しなくて済む)。

#include <mitiru/asset/FbxImport.hpp>

#include <ufbx.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace mitiru::asset
{
namespace
{

namespace fs = std::filesystem;
using render::GltfAnimationChannel;
using render::GltfAnimPath;

/// glb の generator に埋め、変換器の出力が変わったら古い cache を作り直す目印にする
constexpr const char* kGenerator = "mitiru fbx import 1 (ufbx 0.23.1)";

/// ufbx_generate_indices がバイト列で比較するので、詰め物の無い float / uint32 だけで組む
struct PackedVertex
{
	float pos[3];
	float nrm[3];
	float uv[2];
	float col[4];
	std::uint32_t joints[4];
	float weights[4];
};
static_assert(sizeof(PackedVertex) == 20 * 4);

[[nodiscard]] std::string str(const ufbx_string& s) { return std::string(s.data, s.length); }

[[nodiscard]] fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

[[nodiscard]] std::string u8string(const fs::path& p)
{
	const auto u8 = p.generic_u8string();
	return std::string(u8.begin(), u8.end());
}

[[nodiscard]] ufbx_vec4 white()
{
	ufbx_vec4 v = {};
	v.x = v.y = v.z = v.w = 1.0;
	return v;
}

[[nodiscard]] sgc::Mat4f toMat4(const ufbx_matrix& m)
{
	sgc::Mat4f r = sgc::Mat4f::identity();
	for (int c = 0; c < 4; ++c)
	{
		r.m[0][c] = static_cast<float>(m.cols[c].x);
		r.m[1][c] = static_cast<float>(m.cols[c].y);
		r.m[2][c] = static_cast<float>(m.cols[c].z);
	}
	return r;
}

[[nodiscard]] bool isIdentity(const ufbx_transform& t)
{
	constexpr double e = 1e-6;
	const auto near = [](double a, double b) { return std::abs(a - b) < e; };
	return near(t.translation.x, 0) && near(t.translation.y, 0) && near(t.translation.z, 0) &&
	       near(t.rotation.x, 0) && near(t.rotation.y, 0) && near(t.rotation.z, 0) && near(t.rotation.w, 1) &&
	       near(t.scale.x, 1) && near(t.scale.y, 1) && near(t.scale.z, 1);
}

[[nodiscard]] const char* sniffMime(const ufbx_blob& b)
{
	const auto* p = static_cast<const unsigned char*>(b.data);
	if (b.size >= 4 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') { return "image/png"; }
	if (b.size >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) { return "image/jpeg"; }
	return nullptr;
}

/// Blender は `Armature|Run` の形で AnimStack を書く。glTF 出力では `Run` なので、名前をそろえる
[[nodiscard]] std::string clipBaseName(const std::string& full)
{
	const auto bar = full.find_last_of('|');
	return bar == std::string::npos ? full : full.substr(bar + 1);
}

[[nodiscard]] ufbx_load_opts loadOptions()
{
	ufbx_load_opts o = {};
	o.target_axes = ufbx_axes_right_handed_y_up;
	o.target_unit_meters = 1.0;
	o.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
	o.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
	o.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
	o.generate_missing_normals = true;
	o.clean_skin_weights = true;
	o.use_blender_pbr_material = true;
	o.ignore_missing_external_files = true;
	return o;
}

class Converter
{
public:
	Converter(const ufbx_scene& scene, std::string sourceDir, std::string fileName)
		: m_scene(scene), m_sourceDir(u8path(sourceDir)), m_fileName(std::move(fileName))
	{
		m_doc.generator = kGenerator;
	}

	[[nodiscard]] std::optional<render::GlbDocument> run(std::string& error)
	{
		convertNodes();
		convertMaterials();
		if (!convertMeshes(error)) { return std::nullopt; }
		convertAnimations();
		if (m_doc.scene.meshes.empty())
		{
			error = "fbx: メッシュがありません";
			return std::nullopt;
		}
		return std::move(m_doc);
	}

private:
	// ── ノード ──

	void convertNodes()
	{
		m_nodeOf.assign(m_scene.nodes.count, -1);
		for (std::size_t i = 0; i < m_scene.nodes.count; ++i)
		{
			const ufbx_node& n = *m_scene.nodes.data[i];
			// 軸と単位の変換は子へ押し込まれるので、根は普通は単位行列。残っていたらノードとして書く
			if (n.is_root && isIdentity(n.local_transform)) { continue; }
			m_nodeOf[n.typed_id] = static_cast<int>(m_doc.scene.nodes.size());
			render::GltfNode g;
			g.name = str(n.name);
			const ufbx_transform& t = n.local_transform;
			g.translation = {static_cast<float>(t.translation.x), static_cast<float>(t.translation.y),
			                 static_cast<float>(t.translation.z)};
			g.rotation = {static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y),
			              static_cast<float>(t.rotation.z), static_cast<float>(t.rotation.w)};
			g.scale = {static_cast<float>(t.scale.x), static_cast<float>(t.scale.y), static_cast<float>(t.scale.z)};
			m_doc.scene.nodes.push_back(std::move(g));
		}
		for (std::size_t i = 0; i < m_scene.nodes.count; ++i)
		{
			const ufbx_node& n = *m_scene.nodes.data[i];
			const int self = m_nodeOf[n.typed_id];
			const int parent = n.parent != nullptr ? m_nodeOf[n.parent->typed_id] : -1;
			if (self < 0 || parent < 0) { continue; }
			m_doc.scene.nodes[static_cast<std::size_t>(self)].parent = parent;
			m_doc.scene.nodes[static_cast<std::size_t>(parent)].children.push_back(self);
		}
	}

	// ── 材質と画像 ──

	void convertMaterials()
	{
		m_materialOf.assign(m_scene.materials.count, -1);
		for (std::size_t i = 0; i < m_scene.materials.count; ++i)
		{
			const ufbx_material& m = *m_scene.materials.data[i];
			const ufbx_texture* baseTex = m.pbr.base_color.texture != nullptr ? m.pbr.base_color.texture
			                                                                  : m.fbx.diffuse_color.texture;
			render::GlbMaterialImages imgs;
			imgs.baseColor = imageFor(baseTex);
			imgs.normal = imageFor(m.pbr.normal_map.texture);

			render::GltfMaterialData g;
			g.name = str(m.name);
			const ufbx_vec4 c = m.pbr.base_color.has_value ? m.pbr.base_color.value_vec4 : white();
			const double f = m.pbr.base_factor.has_value ? m.pbr.base_factor.value_real : 1.0;
			const double a = m.pbr.opacity.has_value ? m.pbr.opacity.value_real : 1.0;
			// Blender は画像をつないだ材質でも、つながっていない色の値を DiffuseColor に書く。
			// それを掛けると画像が染まるので、画像があるときは色を白にする
			const bool textured = imgs.baseColor >= 0;
			g.baseColor = {textured ? 1.0f : static_cast<float>(c.x * f), textured ? 1.0f : static_cast<float>(c.y * f),
			               textured ? 1.0f : static_cast<float>(c.z * f), static_cast<float>(a)};
			g.metallic = m.pbr.metalness.has_value ? static_cast<float>(m.pbr.metalness.value_real) : 0.0f;
			g.roughness = m.pbr.roughness.has_value ? static_cast<float>(m.pbr.roughness.value_real) : 1.0f;
			g.alphaMode = a < 0.999 ? render::GltfAlphaMode::Blend : render::GltfAlphaMode::Opaque;
			m_materialOf[m.typed_id] = static_cast<int>(m_doc.scene.materials.size());
			m_doc.scene.materials.push_back(std::move(g));
			m_doc.materialImages.push_back(imgs);
		}
	}

	[[nodiscard]] int imageFor(const ufbx_texture* tex)
	{
		if (tex == nullptr) { return -1; }
		if (tex->type != UFBX_TEXTURE_FILE && tex->file_textures.count > 0) { tex = tex->file_textures.data[0]; }
		if (const auto it = m_imageOf.find(tex); it != m_imageOf.end()) { return it->second; }
		render::GlbImage img;
		img.name = str(tex->name);
		const char* mime = tex->content.size > 0 ? sniffMime(tex->content) : nullptr;
		if (mime != nullptr)
		{
			const auto* p = static_cast<const std::uint8_t*>(tex->content.data);
			img.bytes.assign(p, p + tex->content.size);
			img.mimeType = mime;
		}
		else
		{
			img.uri = tex->content.size > 0 ? writeEmbedded(*tex) : externalUri(*tex);
		}
		const int idx = static_cast<int>(m_doc.images.size());
		m_doc.images.push_back(std::move(img));
		m_imageOf.emplace(tex, idx);
		return idx;
	}

	/// PNG / JPEG 以外 (TGA など) の埋め込み画像は glb に入れられないので、隣のファイルにして参照する
	[[nodiscard]] std::string writeEmbedded(const ufbx_texture& tex)
	{
		if (m_fileName.empty()) { return externalUri(tex); }
		const std::string ext = u8string(u8path(str(tex.filename)).extension());
		const std::string name = m_fileName + ".tex" + std::to_string(m_embeddedCount++) + ext;
		std::ofstream f(m_sourceDir / u8path(name), std::ios::binary | std::ios::trunc);
		f.write(static_cast<const char*>(tex.content.data), static_cast<std::streamsize>(tex.content.size));
		return name;
	}

	/// FBX の画像パスは書き出した機械の絶対パスのことが多い。FBX の隣、相対パス、絶対パスの順に探す
	[[nodiscard]] std::string externalUri(const ufbx_texture& tex) const
	{
		const fs::path rel = u8path(str(tex.relative_filename));
		const fs::path abs = u8path(str(tex.absolute_filename));
		const fs::path named = u8path(str(tex.filename));
		const fs::path candidates[] = {m_sourceDir / rel, m_sourceDir / named.filename(), abs, named};
		std::error_code ec;
		for (const auto& c : candidates)
		{
			if (c.empty() || !fs::is_regular_file(c, ec)) { continue; }
			const fs::path r = fs::relative(c, m_sourceDir, ec);
			if (!ec && !r.empty()) { return u8string(r); }
		}
		std::fprintf(stderr, "[fbx] テクスチャが見つからない (名前だけ残す): %s\n", str(tex.filename).c_str());
		return u8string(named.filename());
	}

	// ── メッシュとスキン ──

	[[nodiscard]] bool convertMeshes(std::string& error)
	{
		m_meshOf.assign(m_scene.meshes.count, -1);
		for (std::size_t i = 0; i < m_scene.nodes.count; ++i)
		{
			const ufbx_node& n = *m_scene.nodes.data[i];
			const int self = m_nodeOf[n.typed_id];
			if (n.mesh == nullptr || self < 0) { continue; }
			auto& g = m_doc.scene.nodes[static_cast<std::size_t>(self)];
			g.mesh = meshFor(*n.mesh, error);
			if (!error.empty()) { return false; }
			const ufbx_skin_deformer* skin = skinOf(*n.mesh);
			if (g.mesh >= 0 && skin != nullptr) { g.skin = skinFor(*skin); }
		}
		return true;
	}

	/// 頂点を 1 つも動かさないスキンは無いものとして扱う (glTF の skin は joint を 1 つ以上要る)
	[[nodiscard]] const ufbx_skin_deformer* skinOf(const ufbx_mesh& mesh)
	{
		if (mesh.skin_deformers.count == 0) { return nullptr; }
		const ufbx_skin_deformer* skin = mesh.skin_deformers.data[0];
		const auto& joints = jointMap(*skin);
		return std::any_of(joints.begin(), joints.end(), [](int j) { return j >= 0; }) ? skin : nullptr;
	}

	/// @return glTF のメッシュの添字。三角形が 1 枚も無ければ -1 (glTF のメッシュは primitive を 1 つ以上要る)
	[[nodiscard]] int meshFor(const ufbx_mesh& mesh, std::string& error)
	{
		constexpr int kEmpty = -2;
		if (m_meshOf[mesh.typed_id] != -1) { return m_meshOf[mesh.typed_id] == kEmpty ? -1 : m_meshOf[mesh.typed_id]; }
		if (mesh.skin_deformers.count > 1)
		{
			std::fprintf(stderr, "[fbx] %s: スキンが複数ある — 最初の 1 つだけ使う\n", str(mesh.name).c_str());
		}
		const ufbx_skin_deformer* skin = skinOf(mesh);
		const std::vector<int>* joints = skin != nullptr ? &jointMap(*skin) : nullptr;
		render::GltfMeshData out;
		out.name = str(mesh.name);
		for (std::size_t p = 0; p < mesh.material_parts.count; ++p)
		{
			const ufbx_mesh_part& part = mesh.material_parts.data[p];
			if (part.num_triangles == 0) { continue; }
			auto prim = convertPart(mesh, part.face_indices.data, part.face_indices.count, {skin, joints}, error);
			if (!error.empty()) { return -1; }
			const bool hasMat = part.index < mesh.materials.count && mesh.materials.data[part.index] != nullptr;
			prim.materialIndex = hasMat ? m_materialOf[mesh.materials.data[part.index]->typed_id] : -1;
			out.primitives.push_back(std::move(prim));
		}
		if (out.primitives.empty())
		{
			m_meshOf[mesh.typed_id] = kEmpty;
			return -1;
		}
		const int idx = static_cast<int>(m_doc.scene.meshes.size());
		m_doc.scene.meshes.push_back(std::move(out));
		m_meshOf[mesh.typed_id] = idx;
		return idx;
	}

	/// 変換中のスキンと、その cluster → joint の対応 (jointMap)。スキンが無ければ両方 null
	struct SkinRef
	{
		const ufbx_skin_deformer* skin;
		const std::vector<int>* joints;
	};

	[[nodiscard]] render::GltfMeshPrimitive convertPart(const ufbx_mesh& mesh, const std::uint32_t* faces,
	                                                    std::size_t faceCount, SkinRef skin, std::string& error) const
	{
		std::vector<std::uint32_t> tri(mesh.max_face_triangles * 3);
		std::vector<PackedVertex> corners;
		for (std::size_t f = 0; f < faceCount; ++f)
		{
			const std::uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), &mesh, mesh.faces.data[faces[f]]);
			for (std::uint32_t k = 0; k < n * 3; ++k) { corners.push_back(packVertex(mesh, tri[k], skin)); }
		}
		std::vector<std::uint32_t> indices(corners.size());
		const ufbx_vertex_stream stream = {corners.data(), corners.size(), sizeof(PackedVertex)};
		ufbx_error err = {};
		const std::size_t unique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &err);
		if (err.type != UFBX_ERROR_NONE)
		{
			error = "fbx: 頂点の統合に失敗: " + str(mesh.name);
			return {};
		}
		corners.resize(unique);
		return unpack(corners, std::move(indices), skin.skin != nullptr);
	}

	[[nodiscard]] static PackedVertex packVertex(const ufbx_mesh& mesh, std::uint32_t index, SkinRef skin)
	{
		PackedVertex v = {};
		const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh.vertex_position, index);
		v.pos[0] = static_cast<float>(p.x); v.pos[1] = static_cast<float>(p.y); v.pos[2] = static_cast<float>(p.z);
		if (mesh.vertex_normal.exists)
		{
			const ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh.vertex_normal, index);
			v.nrm[0] = static_cast<float>(n.x); v.nrm[1] = static_cast<float>(n.y); v.nrm[2] = static_cast<float>(n.z);
		}
		if (mesh.vertex_uv.exists)
		{
			const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
			v.uv[0] = static_cast<float>(uv.x);
			v.uv[1] = 1.0f - static_cast<float>(uv.y);   // FBX の V は上向き、glTF は下向き
		}
		const ufbx_vec4 c = mesh.vertex_color.exists ? ufbx_get_vertex_vec4(&mesh.vertex_color, index) : white();
		v.col[0] = static_cast<float>(c.x); v.col[1] = static_cast<float>(c.y);
		v.col[2] = static_cast<float>(c.z); v.col[3] = static_cast<float>(c.w);
		if (skin.skin != nullptr) { packWeights(skin, mesh.vertex_indices.data[index], v); }
		return v;
	}

	/// 影響の大きい順に並んでいるので先頭 4 つを取り、和を 1 に直す
	static void packWeights(SkinRef ref, std::uint32_t vertex, PackedVertex& v)
	{
		const ufbx_skin_deformer& skin = *ref.skin;
		if (vertex >= skin.vertices.count) { return; }
		const ufbx_skin_vertex& sv = skin.vertices.data[vertex];
		const std::uint32_t count = std::min<std::uint32_t>(sv.num_weights, 4);
		float sum = 0.0f;
		for (std::uint32_t k = 0; k < count; ++k)
		{
			const ufbx_skin_weight& w = skin.weights.data[sv.weight_begin + k];
			v.joints[k] = static_cast<std::uint32_t>((*ref.joints)[w.cluster_index]);
			v.weights[k] = static_cast<float>(w.weight);
			sum += v.weights[k];
		}
		if (sum <= 0.0f) { return; }
		for (std::uint32_t k = 0; k < count; ++k) { v.weights[k] /= sum; }
	}

	[[nodiscard]] static render::GltfMeshPrimitive unpack(const std::vector<PackedVertex>& verts,
	                                                      std::vector<std::uint32_t> indices, bool skinned)
	{
		render::GltfMeshPrimitive prim;
		prim.indices = std::move(indices);
		prim.vertices.resize(verts.size());
		if (skinned) { prim.skin.resize(verts.size()); }
		for (std::size_t i = 0; i < verts.size(); ++i)
		{
			const PackedVertex& s = verts[i];
			auto& d = prim.vertices[i];
			d.position = {s.pos[0], s.pos[1], s.pos[2]};
			d.normal = {s.nrm[0], s.nrm[1], s.nrm[2]};
			d.texCoord = {s.uv[0], s.uv[1]};
			d.color = {s.col[0], s.col[1], s.col[2], s.col[3]};
			if (!skinned) { continue; }
			std::copy(s.joints, s.joints + 4, prim.skin[i].joints);
			std::copy(s.weights, s.weights + 4, prim.skin[i].weights);
		}
		return prim;
	}

	/// 頂点が使う cluster (重みの大きい 4 つに入るもの) だけを joint にし、cluster の順に番号を振る。
	/// 影響の無い骨まで cluster にする資産が多く、そのままでは joint 数の上限 (maxSkinJoints) を超えやすい
	[[nodiscard]] const std::vector<int>& jointMap(const ufbx_skin_deformer& skin)
	{
		if (const auto it = m_jointOf.find(&skin); it != m_jointOf.end()) { return it->second; }
		std::vector<int> map(skin.clusters.count, -1);
		for (std::size_t v = 0; v < skin.vertices.count; ++v)
		{
			const ufbx_skin_vertex& sv = skin.vertices.data[v];
			for (std::uint32_t k = 0; k < std::min<std::uint32_t>(sv.num_weights, 4); ++k)
			{
				map[skin.weights.data[sv.weight_begin + k].cluster_index] = 0;
			}
		}
		int next = 0;
		for (auto& m : map) { m = m == 0 ? next++ : -1; }
		return m_jointOf.emplace(&skin, std::move(map)).first->second;
	}

	[[nodiscard]] int skinFor(const ufbx_skin_deformer& skin)
	{
		if (const auto it = m_skinOf.find(&skin); it != m_skinOf.end()) { return it->second; }
		const std::vector<int>& joints = jointMap(skin);
		render::GltfSkinData s;
		s.name = str(skin.name);
		for (std::size_t c = 0; c < skin.clusters.count; ++c)
		{
			if (joints[c] < 0) { continue; }
			const ufbx_skin_cluster& cl = *skin.clusters.data[c];
			s.joints.push_back(cl.bone_node != nullptr ? m_nodeOf[cl.bone_node->typed_id] : -1);
			s.inverseBindMatrices.push_back(toMat4(cl.geometry_to_bone));
		}
		const bool valid = std::none_of(s.joints.begin(), s.joints.end(), [](int j) { return j < 0; });
		const int idx = valid ? static_cast<int>(m_doc.scene.skins.size()) : -1;
		if (valid) { m_doc.scene.skins.push_back(std::move(s)); }
		else { std::fprintf(stderr, "[fbx] %s: 骨の無い cluster がある — 剛体で描く\n", str(skin.name).c_str()); }
		m_skinOf.emplace(&skin, idx);
		return idx;
	}

	// ── 動作 ──

	void convertAnimations()
	{
		std::vector<std::string> bases;
		for (std::size_t i = 0; i < m_scene.anim_stacks.count; ++i)
		{
			bases.push_back(clipBaseName(str(m_scene.anim_stacks.data[i]->name)));
		}
		for (std::size_t i = 0; i < m_scene.anim_stacks.count; ++i)
		{
			const ufbx_anim_stack& stack = *m_scene.anim_stacks.data[i];
			const bool unique = std::count(bases.begin(), bases.end(), bases[i]) == 1;
			render::GltfAnimationClip clip;
			clip.name = unique ? bases[i] : str(stack.name);
			if (bakeClip(stack, clip)) { m_doc.scene.animations.push_back(std::move(clip)); }
		}
	}

	[[nodiscard]] bool bakeClip(const ufbx_anim_stack& stack, render::GltfAnimationClip& clip) const
	{
		ufbx_bake_opts opts = {};
		opts.trim_start_time = true;
		opts.resample_rate = 30.0;
		opts.key_reduction_enabled = true;
		opts.key_reduction_rotation = true;
		ufbx_error err = {};
		ufbx_baked_anim* baked = ufbx_bake_anim(&m_scene, stack.anim, &opts, &err);
		if (baked == nullptr)
		{
			std::fprintf(stderr, "[fbx] 動作 %s を焼けない — 飛ばす\n", str(stack.name).c_str());
			return false;
		}
		float duration = static_cast<float>(baked->playback_duration);
		for (std::size_t n = 0; n < baked->nodes.count; ++n)
		{
			const ufbx_baked_node& bn = baked->nodes.data[n];
			const int node = bn.typed_id < m_nodeOf.size() ? m_nodeOf[bn.typed_id] : -1;
			if (node < 0) { continue; }
			addChannel(clip, node, GltfAnimPath::Translation, bn.translation_keys.data, bn.translation_keys.count);
			addChannel(clip, node, GltfAnimPath::Rotation, bn.rotation_keys.data, bn.rotation_keys.count);
			addChannel(clip, node, GltfAnimPath::Scale, bn.scale_keys.data, bn.scale_keys.count);
		}
		ufbx_free_baked_anim(baked);
		for (const auto& ch : clip.channels) { duration = std::max(duration, ch.times.back()); }
		holdUntil(clip, duration);
		clip.durationSec = duration;
		return !clip.channels.empty();
	}

	template <typename Key>
	static void addChannel(render::GltfAnimationClip& clip, int node, GltfAnimPath path, const Key* keys,
	                       std::size_t count)
	{
		if (count == 0) { return; }
		GltfAnimationChannel ch;
		ch.nodeIndex = node;
		ch.path = path;
		ch.interpolation = render::GltfAnimInterp::Linear;
		for (std::size_t k = 0; k < count; ++k)
		{
			ch.times.push_back(static_cast<float>(keys[k].time));
			ch.values.push_back(keyValue(keys[k].value));
		}
		clip.channels.push_back(std::move(ch));
	}

	[[nodiscard]] static sgc::Vec4f keyValue(const ufbx_vec3& v)
	{
		return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 0.0f};
	}

	[[nodiscard]] static sgc::Vec4f keyValue(const ufbx_quat& q)
	{
		return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
	}

	/// glb には長さの欄が無く、読み手は最後のキーの時刻を長さにする。静止したチャンネルだけの動作でも
	/// 長さが残るよう、最後の値を終端まで伸ばす
	static void holdUntil(render::GltfAnimationClip& clip, float duration)
	{
		for (auto& ch : clip.channels)
		{
			if (ch.times.back() + 1e-5f >= duration) { continue; }
			ch.times.push_back(duration);
			ch.values.push_back(ch.values.back());
		}
	}

	const ufbx_scene& m_scene;
	fs::path m_sourceDir;
	std::string m_fileName;
	render::GlbDocument m_doc;
	std::vector<int> m_nodeOf;       ///< ufbx の node typed_id → glTF のノード (-1 = 書かない根)
	std::vector<int> m_meshOf;       ///< ufbx の mesh typed_id → glTF のメッシュ
	std::vector<int> m_materialOf;   ///< ufbx の material typed_id → glTF の材質
	std::map<const ufbx_skin_deformer*, int> m_skinOf;
	std::map<const ufbx_skin_deformer*, std::vector<int>> m_jointOf;   ///< cluster → joint (-1 = 使わない)
	std::map<const ufbx_texture*, int> m_imageOf;
	int m_embeddedCount = 0;
};

[[nodiscard]] std::string lowerExt(std::string_view path)
{
	const auto dot = path.find_last_of('.');
	if (dot == std::string_view::npos) { return {}; }
	std::string ext(path.substr(dot));
	for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	return ext;
}

/// 相対パスの解決は vfs::readGlobal / clod の import cache と同じ (cwd → MITIRU_ASSET_ROOT)
[[nodiscard]] fs::path resolveSource(const std::string& path)
{
	fs::path src(path);
	std::error_code ec;
	if (src.is_relative() && !fs::exists(src, ec))
	{
		if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0')
		{
			const fs::path rooted = fs::path(root) / src;
			if (fs::exists(rooted, ec)) { return rooted; }
		}
	}
	return src;
}

[[nodiscard]] bool isCurrentCache(const fs::path& cache, const fs::path& src)
{
	std::error_code ec;
	const auto cacheTime = fs::last_write_time(cache, ec);
	if (ec || cacheTime < fs::last_write_time(src, ec) || ec) { return false; }
	std::ifstream f(cache, std::ios::binary);
	std::string head(1024, '\0');
	f.read(head.data(), static_cast<std::streamsize>(head.size()));
	head.resize(static_cast<std::size_t>(f.gcount()));
	return head.find(kGenerator) != std::string::npos;
}

[[nodiscard]] std::optional<std::vector<char>> readAll(const fs::path& path)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) { return std::nullopt; }
	const auto size = f.tellg();
	std::vector<char> bytes(static_cast<std::size_t>(size));
	f.seekg(0);
	f.read(bytes.data(), size);
	return f ? std::optional(std::move(bytes)) : std::nullopt;
}

[[nodiscard]] bool writeAtomically(const fs::path& path, const std::vector<std::uint8_t>& bytes, std::string& error)
{
	fs::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) { error = "fbx: cache を書けません: " + tmp.string(); return false; }
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	std::error_code ec;
	fs::rename(tmp, path, ec);
	if (ec) { error = "fbx: cache の rename に失敗: " + path.string(); }
	return !ec;
}

}  // namespace

bool isFbxPath(std::string_view path) noexcept
{
	return lowerExt(path) == ".fbx";
}

std::optional<render::GlbDocument> importFbx(const void* data, std::size_t size, const std::string& sourceDir,
                                             const std::string& fileName, std::string& error)
{
	ufbx_load_opts opts = loadOptions();
	// ufbx はこの名前を基準に画像の相対パスを解く (メモリから読むと元の置き場所を知らないため)
	const std::string sourceName = sourceDir.empty() || fileName.empty() ? std::string() : sourceDir + "/" + fileName;
	opts.filename = {sourceName.c_str(), sourceName.size()};
	ufbx_error err = {};
	ufbx_scene* scene = ufbx_load_memory(data, size, &opts, &err);
	if (scene == nullptr)
	{
		char buf[512];
		ufbx_format_error(buf, sizeof(buf), &err);
		error = std::string("fbx: 読めません: ") + buf;
		return std::nullopt;
	}
	auto doc = Converter(*scene, sourceDir, fileName).run(error);
	ufbx_free_scene(scene);
	return doc;
}

std::optional<std::string> ensureFbxGlbCache(const std::string& fbxPath, std::string& error)
{
	const fs::path src = resolveSource(fbxPath);
	fs::path cache = src;
	cache += ".glb";
	std::error_code ec;
	if (!fs::exists(src, ec))
	{
		error = "fbx: ファイルがありません: " + fbxPath;
		return std::nullopt;
	}
	if (isCurrentCache(cache, src)) { return cache.string(); }

	std::fprintf(stderr, "[fbx] importing %s -> %s (converts once)\n", src.filename().string().c_str(),
	             cache.filename().string().c_str());
	const auto bytes = readAll(src);
	if (!bytes)
	{
		error = "fbx: 読めません: " + src.string();
		return std::nullopt;
	}
	const auto doc = importFbx(bytes->data(), bytes->size(), u8string(src.parent_path()),
	                           u8string(src.filename()), error);
	if (!doc || !writeAtomically(cache, render::writeGlb(*doc), error)) { return std::nullopt; }
	return cache.string();
}

}  // namespace mitiru::asset
