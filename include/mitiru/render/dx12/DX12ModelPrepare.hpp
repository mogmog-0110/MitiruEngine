#pragma once

/// @file DX12ModelPrepare.hpp
/// @brief glTF / FBX のモデルを描ける形にする前半。ワーカースレッドで走り、描画側の表には触れない
/// @details ファイルを読み、glTF を解析し、骨格とクリップを組み、材質の画像を BC 圧縮して DEFAULT heap へ
///          詰め、COPY キューで写すところまでをする。描画側 (DX12ModelStreaming.hpp) はできた物を受け取り、
///          COMMON からの遷移を積んで registry へ入れる。

#ifdef _WIN32

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/asset/FbxImport.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/GltfLoader.hpp>
#include <mitiru/render/MaterialTextures.hpp>
#include <mitiru/render/SkinnedLod.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/dx12/Dx12CopyQueue.hpp>
#include <mitiru/render/dx12/Dx12TextureUpload.hpp>

namespace mitiru::render::dx12
{

/// @brief 材質 1 個ぶんの画像。並びは kModelTextureRoles と同じ。無い役割は空
struct PreparedMaterialTextures
{
	std::array<StagedTexture, 4> maps;
};

/// @brief スキン prim の 1 段ぶんの compute の入力 (バインドポーズの頂点と束縛) と IB
struct PreparedSkinLod
{
	StagedBuffer     base;
	StagedBuffer     binding;
	gfx::GpuResource indices;   ///< UPLOAD heap の IB (32 bit)。描画がそのまま読む
	std::uint32_t    vertexCount = 0;
	std::uint32_t    indexCount = 0;
};

/// @brief prim 1 個ぶん。lods が空なら変形しない (剛体で描く)。段 0 は元のまま、続きは render/SkinnedLod.hpp の簡略化
struct PreparedSkinPrim
{
	std::vector<PreparedSkinLod> lods;
};

struct PreparedSkinnedModel
{
	std::string                                  error;     ///< 空でなければ読めなかった理由
	GltfSceneData                                scene;     ///< meshes と materials だけが残る (画像は詰めた後に捨てる)
	animation::AnimAsset                         anim;
	std::vector<PreparedMaterialTextures>        textures;  ///< 材質ごと
	std::vector<PreparedSkinPrim>                skinPrims; ///< 描画側の prim と同じ通し番号 (ノードの順 × primitive の順)
	bool                                         copied = false;  ///< COPY キューで写し終えた。false なら描画側で写す
	std::uint64_t                                gpuBytes = 0;
};

struct ModelTextureRole
{
	const char* name;
	TextureKind kind;
};
inline constexpr std::array<ModelTextureRole, 4> kModelTextureRoles{{
	{"base", TextureKind::Color}, {"normal", TextureKind::Normal}, {"mr", TextureKind::Data}, {"emissive", TextureKind::Color},
}};

/// @brief 読む glTF のパス。FBX は隣の `<path>.glb` (初回に変換。pack には変換済みを入れる)
[[nodiscard]] inline std::optional<std::string> skinnedSourcePath(const std::string& path, std::string& why)
{
	if (!asset::isFbxPath(path)) { return path; }
	if (vfs::hasGlobalMount()) { return path + ".glb"; }
	return asset::ensureFbxGlbCache(path, why);
}

namespace detail
{

[[nodiscard]] inline const CpuTexture& decodedOf(const GltfMaterialData& m, std::size_t role)
{
	switch (role)
	{
	case 0: return m.baseColorTexture;
	case 1: return m.normalTexture;
	case 2: return m.metallicRoughnessTexture;
	default: return m.emissiveTexture;
	}
}

[[nodiscard]] inline const std::string& uriOf(const GltfMaterialData& m, std::size_t role)
{
	switch (role)
	{
	case 0: return m.baseColorTexturePath;
	case 1: return m.normalTexturePath;
	case 2: return m.metallicRoughnessTexturePath;
	default: return m.emissiveTexturePath;
	}
}

/// @brief 材質の画像 1 枚。埋め込みを優先し、外部 URI はモデルのディレクトリ相対で読む。圧縮の結果は DDS の sidecar に残す
[[nodiscard]] inline StagedTexture stageModelTexture(ID3D12Device* device, const GltfMaterialData& m, std::size_t role,
                                                     const std::string& modelPath, std::size_t material)
{
	const CpuTexture& decoded = decodedOf(m, role);
	const std::string& uri = uriOf(m, role);
	CpuTexture external;
	const CpuTexture* src = &decoded;
	std::string externalPath;
	if (!decoded.valid() && !uri.empty())
	{
		const auto dirEnd = modelPath.find_last_of("/\\");
		externalPath = ((dirEnd == std::string::npos) ? "" : modelPath.substr(0, dirEnd + 1)) + uri;
		if (auto tex = Texture::fromFile(externalPath))
		{
			external.width = tex->width();
			external.height = tex->height();
			external.rgba = tex->pixels();
			src = &external;
		}
		else
		{
			debug::warnOnce("dx12.skinned.tex." + externalPath,
			                "glTF のテクスチャ " + externalPath + " を読めません。.gltf からの相対パスと、PNG か JPEG かを確かめてください。");
		}
	}
	StagedTexture staged;
	if (!src->valid()) { return staged; }
	const auto& r = kModelTextureRoles[role];
	const MaterialTextureSidecar sidecar = materialTextureSidecar(modelPath, material, r.name, externalPath);
	const MipImage image = prepareMaterialTexture(*src, r.kind, &sidecar);
	(void)stageMipImage(device, image, staged, D3D12_RESOURCE_STATE_COMMON);
	return staged;
}

inline void stageModelTextures(ID3D12Device* device, const std::string& path, PreparedSkinnedModel& out)
{
	out.textures.resize(out.scene.materials.size());
	for (std::size_t i = 0; i < out.scene.materials.size(); ++i)
	{
		auto& gmat = out.scene.materials[i];
		for (std::size_t role = 0; role < kModelTextureRoles.size(); ++role)
		{
			out.textures[i].maps[role] = stageModelTexture(device, gmat, role, path, i);
			out.gpuBytes += out.textures[i].maps[role].bytes();
		}
		gmat.baseColorTexture = {};
		gmat.normalTexture = {};
		gmat.metallicRoughnessTexture = {};
		gmat.emissiveTexture = {};
	}
}

/// @brief prim の三角形の添字。glTF が添字を持たなければ頂点の並びそのまま
[[nodiscard]] inline std::vector<std::uint32_t> triangleIndicesOf(const GltfMeshPrimitive& prim)
{
	if (!prim.indices.empty()) { return prim.indices; }
	std::vector<std::uint32_t> seq(prim.vertices.size() / 3 * 3);
	for (std::size_t i = 0; i < seq.size(); ++i) { seq[i] = static_cast<std::uint32_t>(i); }
	return seq;
}

/// @brief COPY キューが無いときは UPLOAD heap に置き、GPU はそこから読む (写しも遷移も要らない)
[[nodiscard]] inline bool stageSkinBuffer(ID3D12Device* device, bool viaCopy, const void* data, std::size_t bytes,
                                          StagedBuffer& out)
{
	if (viaCopy) { return stageBuffer(device, data, bytes, out); }
	out = {};
	if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, out.buffer)))
	{
		return false;
	}
	void* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(out.buffer->Map(0, &noRead, &mapped))) { return false; }
	std::memcpy(mapped, data, bytes);
	out.buffer->Unmap(0, nullptr);
	out.bytes = bytes;
	return true;
}

[[nodiscard]] inline bool stageSkinLod(ID3D12Device* device, bool viaCopy, std::span<const Vertex3D> base,
                                       std::span<const SkinVertexBinding> binding, std::span<const std::uint32_t> indices,
                                       PreparedSkinLod& out)
{
	StagedBuffer ib;
	if (!stageSkinBuffer(device, false, indices.data(), indices.size_bytes(), ib) ||
	    !stageSkinBuffer(device, viaCopy, base.data(), base.size_bytes(), out.base) ||
	    !stageSkinBuffer(device, viaCopy, binding.data(), binding.size_bytes(), out.binding))
	{
		return false;
	}
	out.indices = std::move(ib.buffer);
	out.vertexCount = static_cast<std::uint32_t>(base.size());
	out.indexCount = static_cast<std::uint32_t>(indices.size());
	return true;
}

/// @brief 1 個の prim の段を全部詰める。段の簡略化の結果は `<model>.skinlod` から読み、無ければ作って書く
inline void stageSkinPrim(ID3D12Device* device, bool viaCopy, const GltfMeshPrimitive& prim, std::size_t ordinal,
                          SkinnedLodSidecar& lods, PreparedSkinPrim& out)
{
	const std::vector<std::uint32_t> indices = triangleIndicesOf(prim);
	if (indices.empty()) { return; }
	out.lods.emplace_back();
	if (!stageSkinLod(device, viaCopy, prim.vertices, prim.skin, indices, out.lods.back())) { out.lods.clear(); return; }
	std::vector<Vertex3D> base;
	std::vector<SkinVertexBinding> binding;
	for (const auto& level : lods.levelsFor(ordinal, prim.vertices, indices, prim.skin))
	{
		if (out.lods.size() >= static_cast<std::size_t>(kSkinnedLodLevels)) { break; }
		base.clear();
		binding.clear();
		for (const std::uint32_t v : level.vertices)
		{
			base.push_back(prim.vertices[v]);
			binding.push_back(prim.skin[v]);
		}
		out.lods.emplace_back();
		if (!stageSkinLod(device, viaCopy, base, binding, level.indices, out.lods.back())) { out.lods.pop_back(); break; }
	}
}

/// @brief 変形する prim を、描画側が prim を足す順 (ノードの順 × primitive の順) に詰める。
///        変形するかの判定は描画側の appendSkinnedPrims と同じ (骨の数の上限・束縛の数・逆バインド行列)
inline void stageSkinPrims(ID3D12Device* device, bool viaCopy, const std::string& path, std::uint32_t maxSkinJoints,
                           PreparedSkinnedModel& out)
{
	SkinnedLodSidecar lods(path, skinnedSceneRadius(out.scene));
	for (const auto& node : out.anim.nodes)
	{
		if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= out.scene.meshes.size()) { continue; }
		const auto& skins = out.anim.skins;
		const GltfSkinData* skin = (node.skin >= 0 && static_cast<std::size_t>(node.skin) < skins.size())
			? &skins[static_cast<std::size_t>(node.skin)] : nullptr;
		if (skin != nullptr && skin->joints.size() > maxSkinJoints) { skin = nullptr; }
		for (const auto& prim : out.scene.meshes[static_cast<std::size_t>(node.mesh)].primitives)
		{
			const std::size_t ordinal = out.skinPrims.size();
			out.skinPrims.emplace_back();
			const bool paletteValid = skin != nullptr && !prim.skin.empty() && prim.skin.size() == prim.vertices.size() &&
			                          !skin->inverseBindMatrices.empty() &&
			                          skin->inverseBindMatrices.size() == skin->joints.size();
			if (!paletteValid) { continue; }
			stageSkinPrim(device, viaCopy, prim, ordinal, lods, out.skinPrims.back());
			for (const auto& l : out.skinPrims.back().lods) { out.gpuBytes += l.base.bytes + l.binding.bytes + l.indexCount * 4ull; }
		}
	}
	lods.save();
}

/// @brief 詰めた物を 1 回の提出で写し、終わるまで待つ。COPY キューが無いか失敗したら描画側に任せる
inline void copyStaged(Dx12CopyQueue* copy, PreparedSkinnedModel& out)
{
	if (copy == nullptr || !copy->ready()) { return; }
	const std::uint64_t fence = copy->submit([&](ID3D12GraphicsCommandList* list) {
		for (const auto& mat : out.textures)
		{
			for (const auto& t : mat.maps) { if (t.valid()) { recordStagedCopies(list, t); } }
		}
		for (const auto& prim : out.skinPrims)
		{
			for (const auto& l : prim.lods)
			{
				if (l.base.valid()) { recordStagedCopies(list, l.base); }
				if (l.binding.valid()) { recordStagedCopies(list, l.binding); }
			}
		}
	});
	out.copied = fence != 0 && copy->wait(fence);
}

} // namespace detail

/// @brief ワーカーで走る前半。copy が null なら画像の写しは描画側に任せ、スキンの入力は UPLOAD heap に置く
[[nodiscard]] inline PreparedSkinnedModel prepareSkinnedModel(const std::string& path, ID3D12Device* device,
                                                              Dx12CopyQueue* copy, std::uint32_t maxSkinJoints)
{
	PreparedSkinnedModel out;
	const auto source = skinnedSourcePath(path, out.error);
	if (!source) { return out; }
	const auto bytes = vfs::readGlobal(*source);
	if (!bytes) { out.error = "読めない"; return out; }
	auto scene = loadGltfFromMemory(bytes->data(), bytes->size());
	if (!scene) { out.error = "glTF parse 失敗"; return out; }

	GltfSceneData rig;
	rig.nodes = std::move(scene->nodes);
	rig.skins = std::move(scene->skins);
	rig.animations = std::move(scene->animations);
	std::vector<std::string> warnings;
	out.anim = animation::buildAnimAssetWithSidecar(std::move(rig), animation::readAnimSidecar(path), &warnings);
	for (const auto& w : warnings)
	{
		debug::warnOnce("dx12.skinned.anim." + path + "." + w, "モデル " + path + " のアニメーションに直すところがあります (" + w + ")。");
	}
	out.scene = std::move(*scene);

	detail::stageModelTextures(device, path, out);
	detail::stageSkinPrims(device, copy != nullptr && copy->ready(), path, maxSkinJoints, out);
	detail::copyStaged(copy, out);
	return out;
}

} // namespace mitiru::render::dx12

#endif // _WIN32
