#pragma once

/// @file GlbWriter.hpp
/// @brief GltfSceneData を glb (glTF 2.0 のバイナリ) に書き出す
/// @details FBX の取り込みは変換結果を `<source>.glb` として残し、以後は glTF と同じ読み込み口
///          (GltfLoader / clod の import) を通す。読み手が 1 つで済むよう、書く範囲は GltfLoader が
///          読む範囲に合わせてある。モーフターゲットと 2 つ目以降の UV は書かない。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::render
{

/// @brief glb に入れる画像 1 枚。bytes が空なら uri の外部ファイルを参照する
struct GlbImage
{
	std::string name;
	std::vector<std::uint8_t> bytes;   ///< PNG / JPEG をそのまま埋め込む
	std::string mimeType;              ///< "image/png" / "image/jpeg"
	std::string uri;                   ///< glb の置き場所からの相対パス
};

/// @brief マテリアルが使う画像 (GlbDocument::images の添字、-1 = 無し)
struct GlbMaterialImages
{
	int baseColor = -1;
	int normal = -1;
};

struct GlbDocument
{
	GltfSceneData scene;
	std::vector<GlbImage> images;
	std::vector<GlbMaterialImages> materialImages;   ///< scene.materials と同順。足りない分は画像なし
	std::string generator = "mitiru";
};

namespace detail
{

class GlbBuilder
{
public:
	/// 書いた順に並べ、asset (generator) を先頭に置く。cache の版をファイルの頭だけ読んで確かめられる
	using json = nlohmann::ordered_json;

	explicit GlbBuilder(const GlbDocument& doc) : m_doc(doc) {}

	[[nodiscard]] std::vector<std::uint8_t> build()
	{
		m_root["asset"] = {{"version", "2.0"}, {"generator", m_doc.generator}};
		writeNodes();
		writeMeshes();
		writeSkins();
		writeAnimations();
		writeMaterials();
		finishBuffers();
		return pack();
	}

private:
	static constexpr int kFloat = 5126;
	static constexpr int kUShort = 5123;
	static constexpr int kUInt = 5125;
	static constexpr int kArrayBuffer = 34962;
	static constexpr int kElementBuffer = 34963;

	int addView(const void* data, std::size_t bytes, int target)
	{
		while (m_bin.size() % 4 != 0) { m_bin.push_back(0); }
		json view = {{"buffer", 0}, {"byteOffset", m_bin.size()}, {"byteLength", bytes}};
		if (target != 0) { view["target"] = target; }
		const auto* p = static_cast<const std::uint8_t*>(data);
		m_bin.insert(m_bin.end(), p, p + bytes);
		m_views.push_back(std::move(view));
		return static_cast<int>(m_views.size()) - 1;
	}

	int addAccessor(const std::vector<float>& values, int comps, const char* type, int target, bool bounds)
	{
		json acc = {{"bufferView", addView(values.data(), values.size() * sizeof(float), target)},
		            {"componentType", kFloat}, {"count", values.size() / static_cast<std::size_t>(comps)},
		            {"type", type}};
		if (bounds) { addBounds(acc, values, comps); }
		m_accessors.push_back(std::move(acc));
		return static_cast<int>(m_accessors.size()) - 1;
	}

	static void addBounds(json& acc, const std::vector<float>& values, int comps)
	{
		std::vector<float> lo(static_cast<std::size_t>(comps), 0.0f);
		std::vector<float> hi(static_cast<std::size_t>(comps), 0.0f);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			const auto c = i % static_cast<std::size_t>(comps);
			const bool first = i < static_cast<std::size_t>(comps);
			lo[c] = first ? values[i] : std::min(lo[c], values[i]);
			hi[c] = first ? values[i] : std::max(hi[c], values[i]);
		}
		acc["min"] = lo;
		acc["max"] = hi;
	}

	template <typename T>
	int addIntAccessor(const std::vector<T>& values, int componentType, int comps, const char* type, int target)
	{
		json acc = {{"bufferView", addView(values.data(), values.size() * sizeof(T), target)},
		            {"componentType", componentType},
		            {"count", values.size() / static_cast<std::size_t>(comps)}, {"type", type}};
		m_accessors.push_back(std::move(acc));
		return static_cast<int>(m_accessors.size()) - 1;
	}

	void writeNodes()
	{
		json nodes = json::array();
		json roots = json::array();
		const auto& src = m_doc.scene.nodes;
		for (std::size_t i = 0; i < src.size(); ++i)
		{
			nodes.push_back(nodeJson(src[i]));
			if (src[i].parent < 0) { roots.push_back(i); }
		}
		m_root["nodes"] = std::move(nodes);
		m_root["scenes"] = json::array({json{{"nodes", std::move(roots)}}});
		m_root["scene"] = 0;
	}

	[[nodiscard]] static json nodeJson(const GltfNode& n)
	{
		json node = {{"name", n.name}};
		const auto& t = n.translation;
		const auto& r = n.rotation;
		const auto& s = n.scale;
		if (t.x != 0.0f || t.y != 0.0f || t.z != 0.0f) { node["translation"] = {t.x, t.y, t.z}; }
		if (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f || r.w != 1.0f) { node["rotation"] = {r.x, r.y, r.z, r.w}; }
		if (s.x != 1.0f || s.y != 1.0f || s.z != 1.0f) { node["scale"] = {s.x, s.y, s.z}; }
		if (!n.children.empty()) { node["children"] = n.children; }
		if (n.mesh >= 0) { node["mesh"] = n.mesh; }
		if (n.skin >= 0) { node["skin"] = n.skin; }
		return node;
	}

	void writeMeshes()
	{
		json meshes = json::array();
		for (const auto& mesh : m_doc.scene.meshes)
		{
			json prims = json::array();
			for (const auto& prim : mesh.primitives) { prims.push_back(primitiveJson(prim)); }
			meshes.push_back({{"name", mesh.name}, {"primitives", std::move(prims)}});
		}
		if (!meshes.empty()) { m_root["meshes"] = std::move(meshes); }
	}

	[[nodiscard]] json primitiveJson(const GltfMeshPrimitive& prim)
	{
		std::vector<float> pos, nrm, uv, col;
		bool tinted = false;
		for (const auto& v : prim.vertices)
		{
			pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
			nrm.insert(nrm.end(), {v.normal.x, v.normal.y, v.normal.z});
			uv.insert(uv.end(), {v.texCoord.x, v.texCoord.y});
			col.insert(col.end(), {v.color.r, v.color.g, v.color.b, v.color.a});
			tinted = tinted || v.color.r != 1.0f || v.color.g != 1.0f || v.color.b != 1.0f || v.color.a != 1.0f;
		}
		json attrs = {{"POSITION", addAccessor(pos, 3, "VEC3", kArrayBuffer, true)},
		              {"NORMAL", addAccessor(nrm, 3, "VEC3", kArrayBuffer, false)},
		              {"TEXCOORD_0", addAccessor(uv, 2, "VEC2", kArrayBuffer, false)}};
		if (tinted) { attrs["COLOR_0"] = addAccessor(col, 4, "VEC4", kArrayBuffer, false); }
		if (!prim.skin.empty() && prim.skin.size() == prim.vertices.size()) { addSkinAttributes(prim, attrs); }
		json out = {{"attributes", std::move(attrs)},
		            {"indices", addIntAccessor(prim.indices, kUInt, 1, "SCALAR", kElementBuffer)}};
		if (prim.materialIndex >= 0) { out["material"] = prim.materialIndex; }
		return out;
	}

	void addSkinAttributes(const GltfMeshPrimitive& prim, json& attrs)
	{
		std::vector<std::uint16_t> joints;
		std::vector<float> weights;
		for (const auto& s : prim.skin)
		{
			for (int k = 0; k < 4; ++k)
			{
				joints.push_back(static_cast<std::uint16_t>(s.joints[k]));
				weights.push_back(s.weights[k]);
			}
		}
		attrs["JOINTS_0"] = addIntAccessor(joints, kUShort, 4, "VEC4", kArrayBuffer);
		attrs["WEIGHTS_0"] = addAccessor(weights, 4, "VEC4", kArrayBuffer, false);
	}

	void writeSkins()
	{
		json skins = json::array();
		for (const auto& skin : m_doc.scene.skins)
		{
			std::vector<float> ibm;
			for (const auto& m : skin.inverseBindMatrices)
			{
				// sgc::Mat4f は行優先、glTF は列優先で並べる
				for (int c = 0; c < 4; ++c)
				{
					for (int r = 0; r < 4; ++r) { ibm.push_back(m.m[r][c]); }
				}
			}
			json s = {{"name", skin.name}, {"joints", skin.joints}};
			if (!ibm.empty()) { s["inverseBindMatrices"] = addAccessor(ibm, 16, "MAT4", 0, false); }
			if (skin.skeletonRoot >= 0) { s["skeleton"] = skin.skeletonRoot; }
			skins.push_back(std::move(s));
		}
		if (!skins.empty()) { m_root["skins"] = std::move(skins); }
	}

	void writeAnimations()
	{
		json anims = json::array();
		for (const auto& clip : m_doc.scene.animations)
		{
			json channels = json::array();
			json samplers = json::array();
			for (const auto& ch : clip.channels)
			{
				if (ch.times.empty() || ch.times.size() != ch.values.size()) { continue; }
				channels.push_back({{"sampler", samplers.size()},
				                    {"target", {{"node", ch.nodeIndex}, {"path", pathName(ch.path)}}}});
				samplers.push_back(samplerJson(ch));
			}
			if (!channels.empty())
			{
				anims.push_back({{"name", clip.name}, {"channels", std::move(channels)}, {"samplers", std::move(samplers)}});
			}
		}
		if (!anims.empty()) { m_root["animations"] = std::move(anims); }
	}

	[[nodiscard]] json samplerJson(const GltfAnimationChannel& ch)
	{
		const bool rot = ch.path == GltfAnimPath::Rotation;
		const bool cubic = ch.interpolation == GltfAnimInterp::CubicSpline &&
		                   ch.inTangents.size() == ch.values.size() && ch.outTangents.size() == ch.values.size();
		std::vector<float> out;
		const auto push = [&](const sgc::Vec4f& v) {
			out.insert(out.end(), {v.x, v.y, v.z});
			if (rot) { out.push_back(v.w); }
		};
		for (std::size_t k = 0; k < ch.values.size(); ++k)
		{
			if (cubic) { push(ch.inTangents[k]); }
			push(ch.values[k]);
			if (cubic) { push(ch.outTangents[k]); }
		}
		const char* interp = cubic ? "CUBICSPLINE" : ch.interpolation == GltfAnimInterp::Step ? "STEP" : "LINEAR";
		return {{"input", addAccessor(ch.times, 1, "SCALAR", 0, true)},
		        {"output", addAccessor(out, rot ? 4 : 3, rot ? "VEC4" : "VEC3", 0, false)},
		        {"interpolation", interp}};
	}

	[[nodiscard]] static const char* pathName(GltfAnimPath path)
	{
		switch (path)
		{
		case GltfAnimPath::Rotation: return "rotation";
		case GltfAnimPath::Scale: return "scale";
		default: return "translation";
		}
	}

	void writeMaterials()
	{
		json mats = json::array();
		const auto& src = m_doc.scene.materials;
		for (std::size_t i = 0; i < src.size(); ++i)
		{
			const GlbMaterialImages imgs = i < m_doc.materialImages.size() ? m_doc.materialImages[i] : GlbMaterialImages{};
			mats.push_back(materialJson(src[i], imgs));
		}
		if (!mats.empty()) { m_root["materials"] = std::move(mats); }
	}

	[[nodiscard]] json materialJson(const GltfMaterialData& m, const GlbMaterialImages& imgs)
	{
		json pbr = {{"baseColorFactor", {m.baseColor.r, m.baseColor.g, m.baseColor.b, m.baseColor.a}},
		            {"metallicFactor", m.metallic}, {"roughnessFactor", m.roughness}};
		if (imgs.baseColor >= 0) { pbr["baseColorTexture"] = {{"index", textureFor(imgs.baseColor, m.nearestFilter)}}; }
		json out = {{"name", m.name}, {"pbrMetallicRoughness", std::move(pbr)}, {"doubleSided", m.doubleSided}};
		if (imgs.normal >= 0) { out["normalTexture"] = {{"index", textureFor(imgs.normal, false)}}; }
		if (m.alphaMode == GltfAlphaMode::Mask) { out["alphaMode"] = "MASK"; out["alphaCutoff"] = m.alphaCutoff; }
		if (m.alphaMode == GltfAlphaMode::Blend) { out["alphaMode"] = "BLEND"; }
		return out;
	}

	/// 同じ画像とフィルタの組は 1 つの texture を共有する
	int textureFor(int image, bool nearest)
	{
		const std::uint64_t key = (static_cast<std::uint64_t>(image) << 1) | (nearest ? 1u : 0u);
		const auto it = std::find(m_textureKeys.begin(), m_textureKeys.end(), key);
		if (it != m_textureKeys.end()) { return static_cast<int>(it - m_textureKeys.begin()); }
		m_textureKeys.push_back(key);
		return static_cast<int>(m_textureKeys.size()) - 1;
	}

	void finishTextures()
	{
		if (m_textureKeys.empty()) { return; }
		json textures = json::array();
		for (const auto key : m_textureKeys)
		{
			textures.push_back({{"source", key >> 1}, {"sampler", key & 1u}});
		}
		m_root["textures"] = std::move(textures);
		m_root["samplers"] = json::array({json::object(), json{{"magFilter", 9728}, {"minFilter", 9728}}});
		json images = json::array();
		for (const auto& img : m_doc.images)
		{
			json j = {{"name", img.name}};
			if (!img.bytes.empty())
			{
				j["bufferView"] = addView(img.bytes.data(), img.bytes.size(), 0);
				j["mimeType"] = img.mimeType;
			}
			else
			{
				j["uri"] = encodeUri(img.uri);
			}
			images.push_back(std::move(j));
		}
		m_root["images"] = std::move(images);
	}

	/// glTF の uri は RFC 3986。区切りの / は残し、それ以外の予約文字と ASCII 外を %XX にする
	[[nodiscard]] static std::string encodeUri(const std::string& path)
	{
		static const char* kHex = "0123456789ABCDEF";
		std::string out;
		for (const char ch : path)
		{
			const auto c = static_cast<unsigned char>(ch == '\\' ? '/' : ch);
			const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			                   c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
			if (plain) { out.push_back(static_cast<char>(c)); continue; }
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 15]);
		}
		return out;
	}

	void finishBuffers()
	{
		finishTextures();
		if (!m_accessors.empty()) { m_root["accessors"] = std::move(m_accessors); }
		if (!m_views.empty()) { m_root["bufferViews"] = std::move(m_views); }
		while (m_bin.size() % 4 != 0) { m_bin.push_back(0); }
		if (!m_bin.empty()) { m_root["buffers"] = json::array({json{{"byteLength", m_bin.size()}}}); }
	}

	[[nodiscard]] std::vector<std::uint8_t> pack() const
	{
		std::string text = m_root.dump();
		while (text.size() % 4 != 0) { text.push_back(' '); }
		const std::size_t total = 12 + 8 + text.size() + (m_bin.empty() ? 0 : 8 + m_bin.size());
		std::vector<std::uint8_t> out;
		out.reserve(total);
		const auto put32 = [&out](std::uint32_t v) {
			for (int k = 0; k < 4; ++k) { out.push_back(static_cast<std::uint8_t>(v >> (8 * k))); }
		};
		put32(0x46546C67u);   // "glTF"
		put32(2);
		put32(static_cast<std::uint32_t>(total));
		put32(static_cast<std::uint32_t>(text.size()));
		put32(0x4E4F534Au);   // "JSON"
		out.insert(out.end(), text.begin(), text.end());
		if (!m_bin.empty())
		{
			put32(static_cast<std::uint32_t>(m_bin.size()));
			put32(0x004E4942u);   // "BIN\0"
			out.insert(out.end(), m_bin.begin(), m_bin.end());
		}
		return out;
	}

	const GlbDocument& m_doc;
	json m_root = json::object();
	json m_views = json::array();
	json m_accessors = json::array();
	std::vector<std::uint8_t> m_bin;
	std::vector<std::uint64_t> m_textureKeys;
};

} // namespace detail

/// @brief GlbDocument を glb のバイト列にする
[[nodiscard]] inline std::vector<std::uint8_t> writeGlb(const GlbDocument& doc)
{
	return detail::GlbBuilder(doc).build();
}

} // namespace mitiru::render
