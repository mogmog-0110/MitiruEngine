/// @file clod_import_impl.cpp
/// @brief drawModel の import cache 実装。OBJ / glTF / GLB / FBX →.clod (CLD6) 変換
/// @details clusterlod.h (meshoptimizer demo, MIT) の実装 TU はこのファイルだけに置く。
///          変換はソースの隣へ `<source>.clod` を書き、mtime と magic で再変換するかを決める。
///          テクスチャは各画像の隣へ BC 圧縮した `<画像>.dds` を作り、マテリアルはそれを指す。

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4456 4457 4100 4505)
#endif

#define CLUSTERLOD_IMPLEMENTATION
#include <meshoptimizer.h>
#include <clusterlod.h>

#include <tiny_obj_loader.h>
#include <cgltf.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <mitiru/asset/FbxImport.hpp>
#include <mitiru/level/LevelExtras.hpp>
#include <mitiru/render/TextureCompress.hpp>
#include <mitiru/util/ParallelFor.hpp>
#include <mitiru/render/dx12/clod/ClodFormat.hpp>
#include <mitiru/render/dx12/clod/ClodImport.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace mitiru::render::clod
{
namespace
{

struct ImportSubMesh
{
	uint32_t materialId = 0;
	std::vector<uint32_t> indices;
};

struct ImportModel
{
	std::vector<float> positions;   // xyz
	std::vector<float> uvs;         // uv (頂点数と同数)
	std::vector<float> normals;     // xyz (無ければ 0 → 後で面積重み計算)
	std::vector<ImportSubMesh> subs;
	std::vector<ClodFileMaterial> materials;
};

[[nodiscard]] std::string lowerExt(std::string_view path)
{
	const auto dot = path.find_last_of('.');
	if (dot == std::string_view::npos) { return {}; }
	std::string ext(path.substr(dot));
	for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	return ext;
}

void setMaterialPath(char (&dst)[120], const std::string& src)
{
	std::snprintf(dst, sizeof(dst), "%s", src.c_str());
	for (char* c = dst; *c; ++c) { if (*c == '\\') { *c = '/'; } }
}

// ── OBJ+MTL: (pos,uv,normal) の組で頂点を weld し、マテリアル別 submesh に分ける ──
bool loadObjModel(const std::string& path, ImportModel& out, std::string& error)
{
	std::string mtlDir;
	if (const auto slash = path.find_last_of("/\\"); slash != std::string::npos)
	{
		mtlDir = path.substr(0, slash + 1);
	}
	tinyobj::attrib_t at;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	if (!tinyobj::LoadObj(&at, &shapes, &materials, &warn, &err, path.c_str(),
	                      mtlDir.empty() ? nullptr : mtlDir.c_str(), true))
	{
		error = "obj: " + err;
		return false;
	}

	for (const tinyobj::material_t& m : materials)
	{
		ClodFileMaterial nm = {};
		nm.baseColor[0] = m.diffuse[0]; nm.baseColor[1] = m.diffuse[1];
		nm.baseColor[2] = m.diffuse[2]; nm.baseColor[3] = 1.0f;
		setMaterialPath(nm.albedo, m.diffuse_texname);
		// 法線マップ: normal → bump → displacement の順に採用
		const std::string& nrmTex = !m.normal_texname.empty() ? m.normal_texname
		                          : !m.bump_texname.empty() ? m.bump_texname
		                          : m.displacement_texname;
		setMaterialPath(nm.normal, nrmTex);
		out.materials.push_back(nm);
	}
	if (out.materials.empty())
	{
		ClodFileMaterial nm = {};
		nm.baseColor[0] = 0.78f; nm.baseColor[1] = 0.75f; nm.baseColor[2] = 0.70f; nm.baseColor[3] = 1.0f;
		out.materials.push_back(nm);
	}

	std::map<std::tuple<int, int, int>, uint32_t> weld;
	std::map<int, size_t> subOf;
	for (const tinyobj::shape_t& sh : shapes)
	{
		for (size_t f = 0; f < sh.mesh.num_face_vertices.size(); ++f)
		{
			int mat = f < sh.mesh.material_ids.size() ? sh.mesh.material_ids[f] : -1;
			if (mat < 0 || mat >= static_cast<int>(out.materials.size())) { mat = 0; }
			auto it = subOf.find(mat);
			if (it == subOf.end())
			{
				it = subOf.emplace(mat, out.subs.size()).first;
				out.subs.push_back({ static_cast<uint32_t>(mat), {} });
			}
			std::vector<uint32_t>& dst = out.subs[it->second].indices;
			for (int k = 0; k < 3; ++k)
			{
				const tinyobj::index_t ix = sh.mesh.indices[f * 3 + k];
				const auto key = std::make_tuple(ix.vertex_index, ix.texcoord_index, ix.normal_index);
				auto w = weld.find(key);
				uint32_t vi;
				if (w != weld.end()) { vi = w->second; }
				else
				{
					vi = static_cast<uint32_t>(out.positions.size() / 3);
					weld.emplace(key, vi);
					for (int a = 0; a < 3; ++a)
					{
						out.positions.push_back(at.vertices[static_cast<size_t>(ix.vertex_index) * 3 + a]);
					}
					if (ix.texcoord_index >= 0)
					{
						out.uvs.push_back(at.texcoords[static_cast<size_t>(ix.texcoord_index) * 2]);
						out.uvs.push_back(1.0f - at.texcoords[static_cast<size_t>(ix.texcoord_index) * 2 + 1]);   // OBJ は V 上向き
					}
					else { out.uvs.push_back(0.0f); out.uvs.push_back(0.0f); }
					if (ix.normal_index >= 0)
					{
						for (int a = 0; a < 3; ++a)
						{
							out.normals.push_back(at.normals[static_cast<size_t>(ix.normal_index) * 3 + a]);
						}
					}
					else { out.normals.push_back(0.0f); out.normals.push_back(0.0f); out.normals.push_back(0.0f); }
				}
				dst.push_back(vi);
			}
		}
	}
	if (out.positions.empty() || out.subs.empty()) { error = "obj: 三角形がありません"; return false; }
	return true;
}

// ── glTF/GLB: node のワールド変換を頂点へ焼き、マテリアル別 submesh に分ける ──

/// GLB 埋め込み画像はソースの隣へファイル化し、その名前を返す (uri 画像は名前をそのまま返す)
std::string gltfImageName(const cgltf_image* img,
                          std::map<const cgltf_image*, std::string>& cache,
                          const std::filesystem::path& sourcePath, int& texCounter)
{
	if (img == nullptr) { return {}; }
	if (const auto it = cache.find(img); it != cache.end()) { return it->second; }
	std::string name;
	if (img->uri != nullptr && std::strncmp(img->uri, "data:", 5) != 0)
	{
		name = img->uri;
		cgltf_decode_uri(name.data());
		name.resize(std::strlen(name.c_str()));
	}
	else if (img->buffer_view != nullptr && img->buffer_view->buffer->data != nullptr)
	{
		const auto* bytes = static_cast<const uint8_t*>(img->buffer_view->buffer->data)
		                  + img->buffer_view->offset;
		const char* ext = (img->mime_type != nullptr &&
		                   std::strcmp(img->mime_type, "image/jpeg") == 0) ? ".jpg" : ".png";
		name = sourcePath.filename().string() + ".tex" + std::to_string(texCounter++) + ext;
		std::ofstream f(sourcePath.parent_path() / name, std::ios::binary);
		f.write(reinterpret_cast<const char*>(bytes),
		        static_cast<std::streamsize>(img->buffer_view->size));
	}
	cache.emplace(img, name);
	return name;
}

bool loadGltfModel(const std::string& path, ImportModel& out, std::string& error)
{
	cgltf_options options = {};
	cgltf_data* data = nullptr;
	if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
	{
		error = "gltf: parse に失敗";
		return false;
	}
	if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
	{
		cgltf_free(data);
		error = "gltf: buffer (bin/base64) の読み込みに失敗";
		return false;
	}

	const std::filesystem::path srcPath(path);
	std::map<const cgltf_image*, std::string> imageCache;
	int texCounter = 0;

	std::map<const cgltf_material*, uint32_t> matOf;
	auto materialId = [&](const cgltf_material* m) -> uint32_t
	{
		if (const auto it = matOf.find(m); it != matOf.end()) { return it->second; }
		ClodFileMaterial nm = {};
		nm.baseColor[0] = nm.baseColor[1] = nm.baseColor[2] = nm.baseColor[3] = 1.0f;
		if (m != nullptr)
		{
			const cgltf_pbr_metallic_roughness& pbr = m->pbr_metallic_roughness;
			for (int k = 0; k < 4; ++k) { nm.baseColor[k] = pbr.base_color_factor[k]; }
			if (pbr.base_color_texture.texture != nullptr)
			{
				setMaterialPath(nm.albedo, gltfImageName(pbr.base_color_texture.texture->image,
				                                         imageCache, srcPath, texCounter));
			}
			if (m->normal_texture.texture != nullptr)
			{
				setMaterialPath(nm.normal, gltfImageName(m->normal_texture.texture->image,
				                                         imageCache, srcPath, texCounter));
			}
		}
		const auto id = static_cast<uint32_t>(out.materials.size());
		out.materials.push_back(nm);
		matOf.emplace(m, id);
		return id;
	};

	std::map<uint32_t, size_t> subOf;
	// 同じ node 内で複数 primitive が頂点 accessor を共有するのは普通なので、
	// (node, accessor 組) ごとに 1 回だけ頂点を積む
	std::map<std::tuple<const cgltf_node*, const cgltf_accessor*, const cgltf_accessor*,
	                    const cgltf_accessor*>, uint32_t> vertexBaseOf;
	for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
	{
		const cgltf_node& node = data->nodes[ni];
		// レベルの印 (mitiru_type: 当たり判定・ナビメッシュの元・配置) は描かない
		if (node.mesh == nullptr || !level::isRenderedNode(node.extras.data)) { continue; }
		float world[16];
		cgltf_node_transform_world(&node, world);

		for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi)
		{
			const cgltf_primitive& prim = node.mesh->primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles || prim.indices == nullptr) { continue; }

			const cgltf_accessor* aPos = nullptr;
			const cgltf_accessor* aUv = nullptr;
			const cgltf_accessor* aNrm = nullptr;
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
			{
				const cgltf_attribute& a = prim.attributes[ai];
				if (a.type == cgltf_attribute_type_position && a.index == 0) { aPos = a.data; }
				if (a.type == cgltf_attribute_type_texcoord && a.index == 0) { aUv = a.data; }
				if (a.type == cgltf_attribute_type_normal && a.index == 0) { aNrm = a.data; }
			}
			if (aPos == nullptr) { continue; }

			const auto vertexKey = std::make_tuple(&node, aPos, aUv, aNrm);
			const auto known = vertexBaseOf.find(vertexKey);
			const bool appendVerts = (known == vertexBaseOf.end());
			const auto base = appendVerts ? static_cast<uint32_t>(out.positions.size() / 3)
			                              : known->second;
			if (appendVerts) { vertexBaseOf.emplace(vertexKey, base); }
			const auto vc = appendVerts ? static_cast<size_t>(aPos->count) : 0;
			for (size_t v = 0; v < vc; ++v)
			{
				float p[3] = {0, 0, 0};
				cgltf_accessor_read_float(aPos, v, p, 3);
				// world (column-major) を適用
				const float wx = world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12];
				const float wy = world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13];
				const float wz = world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14];
				out.positions.push_back(wx);
				out.positions.push_back(wy);
				out.positions.push_back(wz);

				float uv[2] = {0, 0};
				if (aUv != nullptr) { cgltf_accessor_read_float(aUv, v, uv, 2); }
				out.uvs.push_back(uv[0]);
				out.uvs.push_back(uv[1]);   // glTF の UV 原点は左上 = そのまま

				float n[3] = {0, 0, 0};
				if (aNrm != nullptr)
				{
					cgltf_accessor_read_float(aNrm, v, n, 3);
					const float nx = world[0] * n[0] + world[4] * n[1] + world[8] * n[2];
					const float ny = world[1] * n[0] + world[5] * n[1] + world[9] * n[2];
					const float nz = world[2] * n[0] + world[6] * n[1] + world[10] * n[2];
					const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (l > 1e-20f) { n[0] = nx / l; n[1] = ny / l; n[2] = nz / l; }
				}
				out.normals.push_back(n[0]);
				out.normals.push_back(n[1]);
				out.normals.push_back(n[2]);
			}

			const uint32_t mat = materialId(prim.material);
			auto it = subOf.find(mat);
			if (it == subOf.end())
			{
				it = subOf.emplace(mat, out.subs.size()).first;
				out.subs.push_back({ mat, {} });
			}
			std::vector<uint32_t>& dst = out.subs[it->second].indices;
			for (cgltf_size i = 0; i < prim.indices->count; ++i)
			{
				dst.push_back(base + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
			}
		}
	}
	cgltf_free(data);

	if (out.positions.empty() || out.subs.empty())
	{
		error = "gltf: 三角形メッシュがありません";
		return false;
	}
	return true;
}

// ── 面積重み法線 (無い/ゼロの頂点のみ埋める) ──
void computeMissingNormals(ImportModel& m)
{
	const size_t vc = m.positions.size() / 3;
	if (m.normals.size() != vc * 3) { m.normals.assign(vc * 3, 0.0f); }
	std::vector<uint8_t> has(vc, 0);
	for (size_t v = 0; v < vc; ++v)
	{
		const float* n = &m.normals[v * 3];
		if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 1e-12f) { has[v] = 1; }
	}
	for (const ImportSubMesh& s : m.subs)
	{
		for (size_t t = 0; t + 2 < s.indices.size(); t += 3)
		{
			const uint32_t i0 = s.indices[t], i1 = s.indices[t + 1], i2 = s.indices[t + 2];
			if (has[i0] && has[i1] && has[i2]) { continue; }
			const float* a = &m.positions[static_cast<size_t>(i0) * 3];
			const float* b = &m.positions[static_cast<size_t>(i1) * 3];
			const float* c = &m.positions[static_cast<size_t>(i2) * 3];
			const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
			const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
			const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1],
			                     e1[2] * e2[0] - e1[0] * e2[2],
			                     e1[0] * e2[1] - e1[1] * e2[0] };
			const uint32_t vs[3] = { i0, i1, i2 };
			for (const uint32_t v : vs)
			{
				if (!has[v]) { for (int k = 0; k < 3; ++k) { m.normals[static_cast<size_t>(v) * 3 + k] += n[k]; } }
			}
		}
	}
	for (size_t v = 0; v < vc; ++v)
	{
		float* n = &m.normals[v * 3];
		const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (l > 1e-20f) { n[0] /= l; n[1] /= l; n[2] /= l; }
		else { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; }
	}
}

// ── テクスチャを BC 圧縮した `<画像>.dds` へ差し替える (圧縮できない画像は元の名前のまま) ──

constexpr std::string_view kDdsSuffix = ".dds";

struct TextureSlot
{
	char (*name)[120];
	TextureKind kind;
};

std::vector<TextureSlot> textureSlots(std::vector<ClodFileMaterial>& mats)
{
	std::vector<TextureSlot> slots;
	for (ClodFileMaterial& m : mats)
	{
		if (m.albedo[0] != '\0') { slots.push_back({ &m.albedo, TextureKind::Color }); }
		if (m.normal[0] != '\0') { slots.push_back({ &m.normal, TextureKind::Normal }); }
	}
	return slots;
}

/// 同じ画像を別の役割 (色と法線) で使うと同名の dds が衝突するので、先に出た役割だけ圧縮する
void compressMaterialTextures(std::vector<ClodFileMaterial>& mats, const std::filesystem::path& dir)
{
	const std::vector<TextureSlot> slots = textureSlots(mats);
	std::map<std::string, TextureKind> kindOf;
	for (const TextureSlot& s : slots) { kindOf.emplace(*s.name, s.kind); }
	std::vector<std::pair<std::string, TextureKind>> jobs(kindOf.begin(), kindOf.end());
	std::vector<char> compressed(jobs.size(), 0);

	util::parallelFor(jobs.size(), [&](size_t i) {
		const auto& job = jobs[i];
		std::string err;
		if (job.first.size() + kDdsSuffix.size() >= sizeof(ClodFileMaterial::albedo))
		{
			err = "名前が長すぎて .dds を付けられない";
		}
		else if (ensureCompressedTexture((dir / job.first).string(), job.second, err))
		{
			compressed[i] = 1;
			return;
		}
		std::fprintf(stderr, "[clod] texture を圧縮せずに使う: %s (%s)\n", job.first.c_str(), err.c_str());
	});

	for (const TextureSlot& s : slots)
	{
		const auto it = std::lower_bound(jobs.begin(), jobs.end(), std::string(*s.name),
		                                 [](const auto& j, const std::string& n) { return j.first < n; });
		if (compressed[static_cast<size_t>(it - jobs.begin())] != 0 && it->second == s.kind)
		{
			setMaterialPath(*s.name, it->first + std::string(kDdsSuffix));
		}
	}
}

/// cache が今の形式 (magic) で、ソースより新しいか
[[nodiscard]] bool isCurrentCache(const std::filesystem::path& cache, const std::filesystem::path& src)
{
	std::error_code ec;
	if (!std::filesystem::exists(cache, ec) ||
	    std::filesystem::last_write_time(cache, ec) < std::filesystem::last_write_time(src, ec))
	{
		return false;
	}
	std::ifstream f(cache, std::ios::binary);
	uint32_t magic = 0;
	f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	return f.good() && magic == kClodMagic;
}

/// cache が指す dds を、元画像が更新されていれば作り直す (cache 自体は書き換えない)
void refreshTextureSidecars(const std::filesystem::path& cache, const std::filesystem::path& dir)
{
	std::ifstream f(cache, std::ios::binary);
	ClodFileHeader h = {};
	if (!f.read(reinterpret_cast<char*>(&h), sizeof(h))) { return; }
	const auto matOffset = sizeof(ClodFileHeader) + static_cast<size_t>(h.vertexCount) * 32
		+ static_cast<size_t>(h.groupCount) * sizeof(ClodGroup)
		+ static_cast<size_t>(h.clusterCount) * sizeof(ClodCluster)
		+ static_cast<size_t>(h.vertIdxCount) * 4 + h.triIdxByteCount;
	// magic しか確かめていないので、壊れた cache の count で巨大な確保や範囲外の seek をしないよう、
	// 材質の表がファイルに収まるかを先に見る
	std::error_code ec;
	const auto fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(cache, ec));
	const std::uint64_t matBytes = static_cast<std::uint64_t>(h.materialCount) * sizeof(ClodFileMaterial);
	if (ec || matOffset > fileSize || matBytes > fileSize - matOffset) { return; }
	std::vector<ClodFileMaterial> mats(h.materialCount);
	f.seekg(static_cast<std::streamoff>(matOffset));
	if (!f.read(reinterpret_cast<char*>(mats.data()),
	            static_cast<std::streamsize>(mats.size() * sizeof(ClodFileMaterial))))
	{
		return;
	}
	for (const TextureSlot& s : textureSlots(mats))
	{
		const std::string name(*s.name);
		if (!name.ends_with(kDdsSuffix)) { continue; }
		std::string err;
		const auto source = dir / name.substr(0, name.size() - kDdsSuffix.size());
		if (!ensureCompressedTexture(source.string(), s.kind, err))
		{
			std::fprintf(stderr, "[clod] %s を作り直せない (%s)\n", name.c_str(), err.c_str());
		}
	}
}

// ── clusterlod で LOD DAG を組み、CLD6 バイト列へ直列化する ──
bool buildClodBytes(const ImportModel& m, std::vector<uint8_t>& out, std::string& error)
{
	const size_t vertexCount = m.positions.size() / 3;
	const clodConfig config = clodDefaultConfig(128);

	std::vector<ClodGroup> groups;
	std::vector<ClodCluster> clusters;
	std::vector<uint32_t> clusterVerts;
	std::vector<uint8_t> clusterTris;

	for (const ImportSubMesh& sub : m.subs)
	{
		clodMesh mesh = {};
		mesh.indices = sub.indices.data();
		mesh.index_count = sub.indices.size();
		mesh.vertex_count = vertexCount;
		mesh.vertex_positions = m.positions.data();
		mesh.vertex_positions_stride = sizeof(float) * 3;

		clodBuild(config, mesh,
			[&](clodGroup group, const clodCluster* cs, size_t count) -> int
			{
				ClodGroup g = {};
				std::memcpy(g.center, group.simplified.center, sizeof(g.center));
				g.radius = group.simplified.radius;
				g.error = group.simplified.error;
				const int groupId = static_cast<int>(groups.size());
				groups.push_back(g);

				for (size_t i = 0; i < count; ++i)
				{
					const clodCluster& c = cs[i];
					ClodCluster n = {};
					n.ownGroup = groupId;
					n.refined = c.refined;
					n.cull[0] = c.bounds.center[0]; n.cull[1] = c.bounds.center[1];
					n.cull[2] = c.bounds.center[2]; n.cull[3] = c.bounds.radius;
					n.vertOffset = static_cast<uint32_t>(clusterVerts.size());
					n.vertCount = static_cast<uint32_t>(c.vertex_count);
					n.triOffset = static_cast<uint32_t>(clusterTris.size());
					n.triCount = static_cast<uint32_t>(c.index_count / 3);
					n.lodDepth = static_cast<uint32_t>(group.depth);
					n.materialId = sub.materialId;

					clusterVerts.resize(clusterVerts.size() + c.vertex_count);
					clusterTris.resize(clusterTris.size() + c.index_count);
					clodLocalIndices(&clusterVerts[n.vertOffset], &clusterTris[n.triOffset],
					                 c.indices, c.index_count);
					clusters.push_back(n);
				}
				return groupId;
			});
	}

	// 誤差単調性 + 球包含の検証 (これが破れた DAG は描画で穴・重複になる)
	size_t violations = 0;
	for (const ClodCluster& c : clusters)
	{
		if (c.refined < 0) { continue; }
		const ClodGroup& fine = groups[static_cast<size_t>(c.refined)];
		const ClodGroup& coarse = groups[static_cast<size_t>(c.ownGroup)];
		if (coarse.error != FLT_MAX && fine.error > coarse.error) { ++violations; }
		const float dx = coarse.center[0] - fine.center[0];
		const float dy = coarse.center[1] - fine.center[1];
		const float dz = coarse.center[2] - fine.center[2];
		const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (d + fine.radius > coarse.radius * 1.001f + 1e-6f) { ++violations; }
	}
	if (violations != 0)
	{
		error = "clod build: LOD 階層の検証に失敗 (" + std::to_string(violations) + " 件)";
		return false;
	}

	ClodFileHeader h = {};
	h.magic = kClodMagic;
	h.vertexCount = static_cast<uint32_t>(vertexCount);
	h.clusterCount = static_cast<uint32_t>(clusters.size());
	h.groupCount = static_cast<uint32_t>(groups.size());
	h.vertIdxCount = static_cast<uint32_t>(clusterVerts.size());
	h.triIdxByteCount = static_cast<uint32_t>(clusterTris.size());
	h.materialCount = static_cast<uint32_t>(m.materials.size());
	h.boundsMin[0] = h.boundsMin[1] = h.boundsMin[2] = FLT_MAX;
	h.boundsMax[0] = h.boundsMax[1] = h.boundsMax[2] = -FLT_MAX;
	for (size_t i = 0; i < vertexCount; ++i)
	{
		for (int k = 0; k < 3; ++k)
		{
			const float v = m.positions[i * 3 + k];
			h.boundsMin[k] = v < h.boundsMin[k] ? v : h.boundsMin[k];
			h.boundsMax[k] = v > h.boundsMax[k] ? v : h.boundsMax[k];
		}
	}

	const auto append = [&out](const void* p, size_t bytes)
	{
		const auto* b = static_cast<const uint8_t*>(p);
		out.insert(out.end(), b, b + bytes);
	};
	out.clear();
	append(&h, sizeof(h));
	append(m.positions.data(), m.positions.size() * sizeof(float));
	append(m.normals.data(), m.normals.size() * sizeof(float));
	append(m.uvs.data(), m.uvs.size() * sizeof(float));
	append(groups.data(), groups.size() * sizeof(ClodGroup));
	append(clusters.data(), clusters.size() * sizeof(ClodCluster));
	append(clusterVerts.data(), clusterVerts.size() * sizeof(uint32_t));
	append(clusterTris.data(), clusterTris.size());
	append(m.materials.data(), m.materials.size() * sizeof(ClodFileMaterial));
	return true;
}

/// 変換の入力。FBX は隣の `<source>.glb` (asset/FbxImport.hpp) を作ってそれを読む。
/// cache の新しさもその glb と比べるので、FBX の変換器の版が変わって glb を作り直せば .clod も作り直す
std::optional<std::filesystem::path> conversionInput(const std::filesystem::path& src, std::string& error)
{
	if (lowerExt(src.string()) != ".fbx") { return src; }
	const auto glb = asset::ensureFbxGlbCache(src.string(), error);
	return glb ? std::optional(std::filesystem::path(*glb)) : std::nullopt;
}

}  // namespace

bool isImportableModelPath(std::string_view path) noexcept
{
	const std::string ext = lowerExt(path);
	return ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx";
}

std::optional<std::string> ensureClodCache(const std::string& sourcePath, std::string& error)
{
	namespace fs = std::filesystem;
	fs::path src(sourcePath);
	std::error_code ec;
	// 相対パスの解決規約は vfs::readGlobal と揃える (cwd → MITIRU_ASSET_ROOT)
	if (src.is_relative() && !fs::exists(src, ec))
	{
		if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0')
		{
			const fs::path rooted = fs::path(root) / src;
			if (fs::exists(rooted, ec)) { src = rooted; }
		}
	}
	const fs::path cache(src.string() + ".clod");

	if (!fs::exists(src, ec))
	{
		error = "モデルファイルがありません: " + sourcePath;
		return std::nullopt;
	}
	const auto input = conversionInput(src, error);
	if (!input) { return std::nullopt; }
	if (isCurrentCache(cache, *input))
	{
		refreshTextureSidecars(cache, src.parent_path());
		return cache.string();
	}

	std::fprintf(stderr, "[clod] importing %s -> %s (converts once)\n",
	             src.filename().string().c_str(), cache.filename().string().c_str());

	ImportModel model;
	const std::string srcStr = src.string();
	const std::string inputStr = input->string();
	const bool loaded = (lowerExt(inputStr) == ".obj")
		? loadObjModel(inputStr, model, error)
		: loadGltfModel(inputStr, model, error);
	if (!loaded) { return std::nullopt; }
	computeMissingNormals(model);
	compressMaterialTextures(model.materials, src.parent_path());

	std::vector<uint8_t> bytes;
	if (!buildClodBytes(model, bytes, error)) { return std::nullopt; }

	const fs::path tmp(srcStr + ".clod.tmp");
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) { error = "cache を書けません: " + tmp.string(); return std::nullopt; }
		f.write(reinterpret_cast<const char*>(bytes.data()),
		        static_cast<std::streamsize>(bytes.size()));
	}
	fs::rename(tmp, cache, ec);
	if (ec)
	{
		error = "cache の rename に失敗: " + cache.string();
		return std::nullopt;
	}
	return cache.string();
}

}  // namespace mitiru::render::clod
