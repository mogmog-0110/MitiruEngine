#pragma once

/// @file LevelLoader.hpp
/// @brief Blender から書き出したレベルの glb を LevelData にする (ゲーム DLL がそのまま使える header-only)
/// @details glTF のノードの extras (Blender のカスタムプロパティ) を見て分ける。
///          - `mitiru_type` = "collision": 当たり判定だけの面。Entity にしない
///          - `mitiru_type` = "navmesh": ナビメッシュの元にする面。Entity にしない
///          - それ以外の `mitiru_type`: Entity (spawn / trigger / enemy / camera など、名前はゲームが決める)
///          - `mitiru_type` の無いメッシュ: 見た目。`mitiru_collide` / `mitiru_nav` が真なら面もそれぞれへ足す
///          ファイルは vfs::readAsset で読むので、pack 配布でも同じパスで読める。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cgltf.h>
#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/level/LevelData.hpp>
#include <mitiru/level/LevelExtras.hpp>

namespace mitiru::level
{

struct LevelLoadResult
{
	std::optional<LevelData> level;     ///< 読めなければ nullopt (理由は error)
	std::string error;
	std::vector<std::string> warnings;  ///< 読めたが切り詰めや無視をしたもの
};

namespace detail
{

/// UTF-8 の文字の途中で切らないよう、収まらないときは直前の文字の境目まで戻して詰める
[[nodiscard]] inline bool copyTruncated(char* dst, std::size_t cap, std::string_view src)
{
	std::size_t n = src.size() < cap ? src.size() : cap - 1;
	while (n > 0 && n < src.size() && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) { --n; }
	std::memcpy(dst, src.data(), n);
	dst[n] = '\0';
	return n == src.size();
}

struct WorldTrs
{
	float t[3];
	float r[4];
	float s[3];
};

/// 列優先の 4x4 (cgltf_node_transform_world の出力) を平行移動・回転・拡大に分ける。せん断は無い前提
[[nodiscard]] inline WorldTrs decompose(const float m[16])
{
	WorldTrs out{{m[12], m[13], m[14]}, {0, 0, 0, 1}, {1, 1, 1}};
	float c[3][3];
	for (int k = 0; k < 3; ++k)
	{
		out.s[k] = std::sqrt(m[k * 4] * m[k * 4] + m[k * 4 + 1] * m[k * 4 + 1] + m[k * 4 + 2] * m[k * 4 + 2]);
		const float inv = out.s[k] > 1e-12f ? 1.0f / out.s[k] : 0.0f;
		for (int r = 0; r < 3; ++r) { c[k][r] = m[k * 4 + r] * inv; }
	}
	// 鏡映 (行列式が負) は x の拡大を負にして回転を正規直交に保つ
	const float det = c[0][0] * (c[1][1] * c[2][2] - c[2][1] * c[1][2]) - c[1][0] * (c[0][1] * c[2][2] - c[2][1] * c[0][2]) +
	                  c[2][0] * (c[0][1] * c[1][2] - c[1][1] * c[0][2]);
	if (det < 0.0f)
	{
		out.s[0] = -out.s[0];
		for (int r = 0; r < 3; ++r) { c[0][r] = -c[0][r]; }
	}
	// c[col][row]。行列要素 Rrc = c[c][r]
	const float tr = c[0][0] + c[1][1] + c[2][2];
	float* q = out.r;
	if (tr > 0.0f)
	{
		const float s = std::sqrt(tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s; q[0] = (c[1][2] - c[2][1]) / s; q[1] = (c[2][0] - c[0][2]) / s; q[2] = (c[0][1] - c[1][0]) / s;
	}
	else if (c[0][0] > c[1][1] && c[0][0] > c[2][2])
	{
		const float s = std::sqrt(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2.0f;
		q[3] = (c[1][2] - c[2][1]) / s; q[0] = 0.25f * s; q[1] = (c[1][0] + c[0][1]) / s; q[2] = (c[2][0] + c[0][2]) / s;
	}
	else if (c[1][1] > c[2][2])
	{
		const float s = std::sqrt(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2.0f;
		q[3] = (c[2][0] - c[0][2]) / s; q[0] = (c[1][0] + c[0][1]) / s; q[1] = 0.25f * s; q[2] = (c[2][1] + c[1][2]) / s;
	}
	else
	{
		const float s = std::sqrt(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2.0f;
		q[3] = (c[0][1] - c[1][0]) / s; q[0] = (c[2][0] + c[0][2]) / s; q[1] = (c[2][1] + c[1][2]) / s; q[2] = 0.25f * s;
	}
	return out;
}

class LevelBuilder
{
public:
	explicit LevelBuilder(LevelLoadResult& result) : m_result(result) {}

	[[nodiscard]] LevelData build(const cgltf_data& data)
	{
		const cgltf_scene* scene = data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? data.scenes : nullptr);
		if (scene != nullptr)
		{
			for (cgltf_size i = 0; i < scene->nodes_count; ++i) { visit(*scene->nodes[i], -1); }
		}
		else
		{
			for (cgltf_size i = 0; i < data.nodes_count; ++i)
			{
				if (data.nodes[i].parent == nullptr) { visit(data.nodes[i], -1); }
			}
		}
		return std::move(m_level);
	}

private:
	void visit(const cgltf_node& node, int parentEntity)
	{
		const nlohmann::json extras = parseExtras(node.extras.data);
		const std::string type = extrasString(extras, kTypeKey);
		float world[16];
		cgltf_node_transform_world(&node, world);

		int childParent = parentEntity;
		if (type == "collision") { appendMesh(node, world, m_level.m_collision); }
		else if (type == "navmesh") { appendMesh(node, world, m_level.m_navSource); }
		else if (!type.empty()) { childParent = addEntity(node, type, extras, world, parentEntity); }
		else
		{
			if (extrasFlag(extras, kCollideKey)) { appendMesh(node, world, m_level.m_collision); }
			if (extrasFlag(extras, kNavKey)) { appendMesh(node, world, m_level.m_navSource); }
		}
		for (cgltf_size c = 0; c < node.children_count; ++c) { visit(*node.children[c], childParent); }
	}

	[[nodiscard]] int addEntity(const cgltf_node& node, const std::string& type, const nlohmann::json& extras,
	                            const float world[16], int parentEntity)
	{
		Entity e;
		const std::string name = node.name != nullptr ? node.name : "";
		if (!copyTruncated(e.name, kNameMax, name)) { warn("名前が長いので切り詰めた: " + name); }
		if (!copyTruncated(e.type, kTypeMax, type)) { warn("mitiru_type が長いので切り詰めた: " + type); }
		e.nameHash = nameHash(e.name);
		e.typeHash = nameHash(e.type);
		const WorldTrs trs = decompose(world);
		std::memcpy(e.position, trs.t, sizeof(e.position));
		std::memcpy(e.rotation, trs.r, sizeof(e.rotation));
		std::memcpy(e.scale, trs.s, sizeof(e.scale));
		const auto size = extras.find(kSizeKey);
		const float s = (size != extras.end() && size->is_number()) ? size->get<float>() : 1.0f;
		for (int k = 0; k < 3; ++k) { e.halfExtents[k] = std::abs(trs.s[k]) * s; }
		e.parent = parentEntity;
		e.firstProperty = static_cast<std::uint32_t>(m_level.m_properties.size());
		for (const auto& [key, value] : extras.items())
		{
			if (key == kTypeKey || key == kSizeKey) { continue; }
			addProperty(name, key, value);
		}
		e.propertyCount = static_cast<std::uint32_t>(m_level.m_properties.size()) - e.firstProperty;
		m_level.m_entities.push_back(e);
		return static_cast<int>(m_level.m_entities.size()) - 1;
	}

	void addProperty(const std::string& owner, const std::string& key, const nlohmann::json& v)
	{
		Property p;
		if (!copyTruncated(p.key, kKeyMax, key)) { warn(owner + ": プロパティ名が長いので切り詰めた: " + key); }
		p.keyHash = nameHash(p.key);
		if (v.is_boolean()) { p.kind = PropertyKind::Bool; p.value[0] = v.get<bool>() ? 1.0f : 0.0f; }
		else if (v.is_number()) { p.kind = PropertyKind::Number; p.value[0] = v.get<float>(); }
		else if (v.is_string())
		{
			p.kind = PropertyKind::Text;
			if (!copyTruncated(p.text, kTextMax, v.get<std::string>())) { warn(owner + "." + key + ": 文字列を切り詰めた"); }
		}
		else if (!readVector(v, p))
		{
			warn(owner + "." + key + ": 数・真偽・文字列・2〜4 個の数の配列のどれでもないので読まない");
			return;
		}
		m_level.m_properties.push_back(p);
	}

	[[nodiscard]] static bool readVector(const nlohmann::json& v, Property& p)
	{
		if (!v.is_array() || v.size() < 2 || v.size() > 4) { return false; }
		for (std::size_t i = 0; i < v.size(); ++i)
		{
			if (!v[i].is_number()) { return false; }
			p.value[i] = v[i].get<float>();
		}
		p.kind = PropertyKind::Vector;
		p.vectorSize = static_cast<std::uint8_t>(v.size());
		return true;
	}

	static void appendMesh(const cgltf_node& node, const float world[16], TriangleSoup& out)
	{
		if (node.mesh == nullptr) { return; }
		for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
		{
			const cgltf_primitive& prim = node.mesh->primitives[p];
			const cgltf_accessor* pos = nullptr;
			for (cgltf_size a = 0; a < prim.attributes_count; ++a)
			{
				if (prim.attributes[a].type == cgltf_attribute_type_position) { pos = prim.attributes[a].data; }
			}
			if (prim.type != cgltf_primitive_type_triangles || pos == nullptr) { continue; }
			appendPrimitive(prim, *pos, world, out);
		}
	}

	static void appendPrimitive(const cgltf_primitive& prim, const cgltf_accessor& pos, const float m[16], TriangleSoup& out)
	{
		const auto base = static_cast<std::uint32_t>(out.positions.size() / 3);
		for (cgltf_size v = 0; v < pos.count; ++v)
		{
			float p[3] = {0, 0, 0};
			cgltf_accessor_read_float(&pos, v, p, 3);
			out.positions.push_back(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]);
			out.positions.push_back(m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]);
			out.positions.push_back(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
		}
		const cgltf_size count = prim.indices != nullptr ? prim.indices->count : pos.count;
		for (cgltf_size i = 0; i + 2 < count; i += 3)
		{
			for (cgltf_size k = 0; k < 3; ++k)
			{
				const cgltf_size idx = prim.indices != nullptr ? cgltf_accessor_read_index(prim.indices, i + k) : i + k;
				out.indices.push_back(base + static_cast<std::uint32_t>(idx));
			}
		}
	}

	void warn(std::string message) { m_result.warnings.push_back(std::move(message)); }

	LevelLoadResult& m_result;
	LevelData m_level;
};

}  // namespace detail

/// @brief glb (または buffer を埋め込んだ glTF) のバイト列からレベルを作る
[[nodiscard]] inline LevelLoadResult loadLevelFromMemory(const void* data, std::size_t size)
{
	LevelLoadResult result;
	cgltf_options options{};
	cgltf_data* gltf = nullptr;
	if (cgltf_parse(&options, data, size, &gltf) != cgltf_result_success)
	{
		result.error = "level: glTF として読めない";
		return result;
	}
	if (cgltf_load_buffers(&options, gltf, nullptr) != cgltf_result_success)
	{
		cgltf_free(gltf);
		result.error = "level: buffer を読めない (外部 .bin の glTF は glb で書き出す)";
		return result;
	}
	result.level = detail::LevelBuilder(result).build(*gltf);
	cgltf_free(gltf);
	return result;
}

/// @brief レベルのファイルを vfs::readAsset で読んで作る (pack 配布でも同じパス)
[[nodiscard]] inline LevelLoadResult loadLevelFile(std::string_view path)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes)
	{
		LevelLoadResult result;
		result.error = "level: ファイルを読めない: " + std::string(path);
		return result;
	}
	return loadLevelFromMemory(bytes->data(), bytes->size());
}

/// @brief `asset.reloaded` の payload が、このレベルのファイルを指すか
/// @details payload の path は `mitiru_host --watch-assets` のフォルダからの相対なので、levelPath の末尾と比べる
[[nodiscard]] inline bool isReloadOf(const char* reloadPayloadJson, std::string_view levelPath)
{
	if (reloadPayloadJson == nullptr) { return false; }
	const auto payload = nlohmann::json::parse(reloadPayloadJson, nullptr, false);
	if (!payload.is_object() || !payload.contains("path") || !payload["path"].is_string()) { return false; }
	// 大文字と小文字は Windows だけ同一視する (asset::FileWatcher と同じ)
	const auto norm = [](std::string s) {
		for (auto& c : s)
		{
			if (c == '\\') { c = '/'; }
#if defined(_WIN32)
			if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
#endif
		}
		return s;
	};
	const std::string changed = norm(payload["path"].get<std::string>());
	const std::string level = norm(std::string(levelPath));
	if (changed.empty() || changed.size() > level.size()) { return false; }
	const bool tail = level.compare(level.size() - changed.size(), changed.size(), changed) == 0;
	return tail && (level.size() == changed.size() || level[level.size() - changed.size() - 1] == '/');
}

/// @brief レベルのファイル 1 つ。ゲーム DLL の static に置き、`asset.reloaded` が届いたら読み直す
/// @details 読み直しに失敗したら (書き出しの途中など) 前の中身を残し、理由を error() に置く。
class LevelFile
{
public:
	explicit LevelFile(std::string path) : m_path(std::move(path)) {}

	/// @brief 初めて呼んだときに読む。読めなければ空のレベルを返し、理由は error()
	[[nodiscard]] const LevelData& get()
	{
		if (!m_loaded) { reload(); }
		return m_level;
	}

	/// @brief payload (`in.actionPayload("asset.reloaded")`) がこのファイルなら読み直して true
	bool reloadIf(const char* reloadPayloadJson)
	{
		if (!isReloadOf(reloadPayloadJson, m_path)) { return false; }
		reload();
		return true;
	}

	[[nodiscard]] const std::string& path() const noexcept { return m_path; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return m_warnings; }

private:
	void reload()
	{
		auto result = loadLevelFile(m_path);
		m_loaded = true;
		m_error = std::move(result.error);
		m_warnings = std::move(result.warnings);
		if (result.level) { m_level = std::move(*result.level); }
	}

	std::string m_path;
	LevelData m_level;
	std::string m_error;
	std::vector<std::string> m_warnings;
	bool m_loaded = false;
};

}  // namespace mitiru::level
