#include "DawnModelStore.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/asset/AssetReload.hpp>
#include <mitiru/asset/FbxImport.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/GltfLoader.hpp>
#include <mitiru/render/GltfMaterialConverter.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>
#include <mitiru/render/Skinning.hpp>

namespace mitiru::render::dawn_detail
{

namespace
{

// glTF 以外で描き方の違う形式。読もうとして失敗する前に、描けない機能として数える
bool noteUnsupportedFormat(const std::string& path)
{
	const std::size_t dot = path.rfind('.');
	if (dot == std::string::npos) { return false; }
	std::string ext = path.substr(dot);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (ext == ".clod" || ext == ".obj") { features::note(features::Feature::Clod); return true; }
	if (ext == ".efkefc") { features::note(features::Feature::Effekseer); return true; }
	return false;
}

// FBX は隣の <path>.glb を読む。pack があれば変換済みのものを使う。
std::optional<std::string> sourcePath(const std::string& path, std::string& why)
{
	if (!asset::isFbxPath(path)) { return path; }
	if (vfs::hasGlobalMount()) { return path + ".glb"; }
	return asset::ensureFbxGlbCache(path, why);
}

void appendPrims(DawnModel& model, const GltfSceneData& scene, std::size_t nodeIndex,
                 const std::vector<std::shared_ptr<const DawnMaterialMaps>>& maps)
{
	const auto& node = model.anim.nodes[nodeIndex];
	if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= scene.meshes.size()) { return; }
	const auto& skins = model.anim.skins;
	const GltfSkinData* skin = (node.skin >= 0 && static_cast<std::size_t>(node.skin) < skins.size())
		? &skins[static_cast<std::size_t>(node.skin)] : nullptr;
	for (const GltfMeshPrimitive& prim : scene.meshes[static_cast<std::size_t>(node.mesh)].primitives)
	{
		DawnModelPrim out;
		out.nodeIndex = static_cast<int>(nodeIndex);
		if (prim.materialIndex >= 0 && static_cast<std::size_t>(prim.materialIndex) < scene.materials.size())
		{
			out.material = convertGltfMaterial(scene.materials[static_cast<std::size_t>(prim.materialIndex)]);
			out.maps = maps[static_cast<std::size_t>(prim.materialIndex)];
		}
		const bool skinned = skin != nullptr && prim.skin.size() == prim.vertices.size() &&
		                     !skin->inverseBindMatrices.empty() && skin->inverseBindMatrices.size() == skin->joints.size();
		if (skinned)
		{
			out.skinIndex = node.skin;
			out.skinId = nextSkinGeometryId();
			out.baseVertices = prim.vertices;
			out.indices = prim.indices;
			out.binding = prim.skin;
			out.bounds = skinBoundsSource(prim.vertices, prim.skin, skin->inverseBindMatrices);
		}
		else if (prim.skin.empty() == false)
		{
			out.nodeIndex = -1;   // 束縛がおかしいスキンは、変形もノード変換もせずバインドポーズのまま描く
		}
		out.mesh.setVertices(prim.vertices);
		out.mesh.setIndices(prim.indices);
		model.prims.push_back(std::move(out));
	}
}

// スキンの prim の段 1..3 を作る。段は <モデル>.skinlod へ書き、DX12 と同じファイルを使い回す
void buildLods(DawnModel& model, const std::string& path, float radius)
{
	bool any = false;
	for (const DawnModelPrim& prim : model.prims) { any = any || prim.skinIndex >= 0; }
	if (!any) { return; }
	SkinnedLodSidecar sidecar(path, radius);
	std::size_t ordinal = 0;
	for (DawnModelPrim& prim : model.prims)
	{
		if (prim.skinIndex < 0) { continue; }
		const auto& levels = sidecar.levelsFor(ordinal++, prim.baseVertices, prim.indices, prim.binding);
		for (const SkinnedLodLevel& level : levels)
		{
			DawnSkinLod lod;
			lod.baseVertices.reserve(level.vertices.size());
			lod.binding.reserve(level.vertices.size());
			for (const std::uint32_t v : level.vertices)
			{
				lod.baseVertices.push_back(prim.baseVertices[v]);
				lod.binding.push_back(prim.binding[v]);
			}
			lod.indices = level.indices;
			prim.lods.push_back(std::move(lod));
		}
		model.lodLevels = std::max(model.lodLevels, 1 + static_cast<int>(prim.lods.size()));
	}
	sidecar.save();
}

// レストポーズのスキンの頂点を包む球。段を選ぶ画面の大きさに使う
void measureBounds(DawnModel& model)
{
	bool any = false;
	sgc::Vec3f lo{0.0f, 0.0f, 0.0f};
	sgc::Vec3f hi{0.0f, 0.0f, 0.0f};
	std::vector<sgc::Mat4f> joints;
	for (const DawnModelPrim& prim : model.prims)
	{
		if (prim.skinIndex < 0) { continue; }
		const GltfSkinData& skin = model.anim.skins[static_cast<std::size_t>(prim.skinIndex)];
		joints.assign(skin.joints.size(), sgc::Mat4f::identity());
		for (std::size_t j = 0; j < skin.joints.size(); ++j)
		{
			const int node = skin.joints[j];
			if (node >= 0 && static_cast<std::size_t>(node) < model.restModel.size()) { joints[j] = model.restModel[static_cast<std::size_t>(node)]; }
		}
		for (const Vertex3D& v : skinVertices(prim.baseVertices, prim.binding, skin.inverseBindMatrices, joints))
		{
			lo = any ? sgc::Vec3f{std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)} : v.position;
			hi = any ? sgc::Vec3f{std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)} : v.position;
			any = true;
		}
	}
	if (!any) { return; }
	model.boundsCenter = (lo + hi) * 0.5f;
	model.boundsRadius = (hi - lo).length() * 0.5f;
}

} // namespace

bool DawnModelStore::load(const std::string& path, DawnModel& out) const
{
	if (noteUnsupportedFormat(path)) { return false; }
	std::string why;
	const auto source = sourcePath(path, why);
	if (!source) { debug::warnOnce("dawn.model.load." + path, "モデル " + path + " を読めません (" + why + ")"); return false; }
	const auto bytes = vfs::readGlobal(*source);
	if (!bytes)
	{
		debug::warnOnce("dawn.model.load." + path, "モデル " + path + " を読めないので、パスを確かめてください");
		return false;
	}
	auto scene = loadGltfFromMemory(bytes->data(), bytes->size());
	if (!scene)
	{
		debug::warnOnce("dawn.model.parse." + path, "モデル " + path + " は glTF として読めません");
		return false;
	}
	GltfSceneData rig;
	rig.nodes = scene->nodes;
	rig.skins = scene->skins;
	rig.animations = scene->animations;
	std::vector<std::string> warnings;
	out.anim = animation::buildAnimAssetWithSidecar(std::move(rig), animation::readAnimSidecar(path), &warnings);
	for (const std::string& w : warnings)
	{
		debug::warnOnce("dawn.model.anim." + path + "." + w, "モデル " + path + " のアニメーションに直すところがあります (" + w + ")");
	}
	std::vector<std::shared_ptr<const DawnMaterialMaps>> maps;
	maps.reserve(scene->materials.size());
	for (std::size_t i = 0; i < scene->materials.size(); ++i) { maps.push_back(m_materials.make(scene->materials[i], path, i)); }
	for (std::size_t n = 0; n < out.anim.nodes.size(); ++n) { appendPrims(out, *scene, n, maps); }
	buildLods(out, path, skinnedSceneRadius(*scene));
	if (out.prims.empty()) { debug::warnOnce("dawn.model.empty." + path, "モデル " + path + " に描ける面がありません"); return false; }
	animation::AnimPose rest;
	animation::evaluatePose(out.anim, animation::AnimPoseParams{}, rest);
	out.restModel = rest.model;
	measureBounds(out);
	return true;
}

DawnModel* DawnModelStore::find(const char* path)
{
	if (path == nullptr || path[0] == '\0') { return nullptr; }
	if (const auto it = m_models.find(path); it != m_models.end()) { return it->second.get(); }
	auto model = std::make_unique<DawnModel>();
	DawnModel* result = load(path, *model) ? model.get() : nullptr;
	m_models.emplace(path, result != nullptr ? std::move(model) : nullptr);
	return result;
}

void DawnModelStore::collect(DawnModel& model, const sgc::Mat4f& world, std::span<const sgc::Mat4f> nodeModel)
{
	m_items.clear();
	m_palette.clear();
	const int level = pickLevel(model, world);
	int paletteSkin = -1;
	std::uint32_t paletteFirst = 0;
	std::uint32_t jointCount = 0;
	for (const DawnModelPrim& prim : model.prims)
	{
		if (prim.skinIndex < 0)
		{
			const bool hasNode = prim.nodeIndex >= 0 && static_cast<std::size_t>(prim.nodeIndex) < nodeModel.size();
			const sgc::Mat4f nodeWorld = hasNode ? nodeModel[static_cast<std::size_t>(prim.nodeIndex)] : sgc::Mat4f::identity();
			m_items.push_back({&prim.mesh, world * nodeWorld, &prim.material, prim.maps.get()});
			continue;
		}
		const GltfSkinData& skin = model.anim.skins[static_cast<std::size_t>(prim.skinIndex)];
		if (prim.skinIndex != paletteSkin)
		{
			m_jointScratch.assign(skin.joints.size(), sgc::Mat4f::identity());
			for (std::size_t j = 0; j < skin.joints.size(); ++j)
			{
				const int node = skin.joints[j];
				if (node >= 0 && static_cast<std::size_t>(node) < nodeModel.size()) { m_jointScratch[j] = nodeModel[static_cast<std::size_t>(node)]; }
			}
			paletteFirst = static_cast<std::uint32_t>(m_palette.size());
			jointCount = static_cast<std::uint32_t>(skin.joints.size());
			for (std::size_t j = 0; j < skin.joints.size(); ++j) { m_palette.push_back(m_jointScratch[j] * skin.inverseBindMatrices[j]); }
			paletteSkin = prim.skinIndex;
		}
		const int use = std::min(level, static_cast<int>(prim.lods.size()));
		DawnDrawItem item{nullptr, world, &prim.material, prim.maps.get()};
		if (use > 0)
		{
			const DawnSkinLod& lod = prim.lods[static_cast<std::size_t>(use - 1)];
			item.skin = {lod.id, &lod.baseVertices, &lod.binding, &lod.indices};
		}
		else
		{
			item.skin = {prim.skinId, &prim.baseVertices, &prim.binding, &prim.indices};
		}
		item.skinBounds = skinnedBounds(prim.bounds, m_jointScratch);
		item.paletteFirst = paletteFirst;
		item.jointCount = jointCount;
		m_items.push_back(item);
	}
}

void DawnModelStore::setLodLook(const SkinnedLodLook& look) noexcept
{
	SkinnedLodThresholds t;
	for (int k = 0; k < kSkinnedLodLevels - 1; ++k)
	{
		if (look.screen[k] > 0.0f) { t.screen[k] = look.screen[k]; }
	}
	m_lodThresholds = t;
	m_lodScale = look.scale > 0.0f ? look.scale : 1.0f;
	m_lodForced = std::clamp<int>(look.forcedLevel, -1, kSkinnedLodLevels - 1);
}

// キャラの外接球の直径が画面の高さに占める割合で段を選ぶ (DX12 の screenFractionOf / beginSkinnedDraw と同じ)
int DawnModelStore::pickLevel(DawnModel& model, const sgc::Mat4f& world)
{
	if (model.lodLevels <= 1) { return 0; }
	if (model.historyFrame != m_frame) { model.historyFrame = m_frame; model.historyOrdinal = 0; }
	const std::uint32_t ordinal = model.historyOrdinal++;
	if (model.levelHistory.size() <= ordinal) { model.levelHistory.resize(ordinal + 1, 0xFF); }
	const int previous = model.levelHistory[ordinal] == 0xFF ? -1 : model.levelHistory[ordinal];
	int level = 0;
	if (m_lodForced >= 0) { level = std::min(m_lodForced, model.lodLevels - 1); }
	else
	{
		const sgc::Vec3f center = world.transformPoint(model.boundsCenter);
		const float scale = std::max({sgc::Vec3f{world.m[0][0], world.m[1][0], world.m[2][0]}.length(),
		                              sgc::Vec3f{world.m[0][1], world.m[1][1], world.m[2][1]}.length(),
		                              sgc::Vec3f{world.m[0][2], world.m[1][2], world.m[2][2]}.length()});
		const float distance = std::max((center - m_eye).length(), 1e-3f);
		const float halfHeight = distance * std::tan(m_fovY * 0.5f);
		const float fraction = m_lodScale * model.boundsRadius * scale / std::max(halfHeight, 1e-6f);
		level = selectSkinnedLod(fraction, previous, model.lodLevels, m_lodThresholds);
	}
	model.levelHistory[ordinal] = static_cast<std::uint8_t>(level);
	return level;
}

const std::vector<DawnDrawItem>& DawnModelStore::animPose(const char* path, const sgc::Mat4f& world,
                                                          const animation::AnimPoseParams* pose,
                                                          const animation::AnimIkRequest* ik, int ikCount)
{
	m_items.clear();
	DawnModel* model = find(path);
	if (model == nullptr) { return m_items; }
	animation::evaluatePose(model->anim, pose != nullptr ? *pose : animation::AnimPoseParams{}, m_pose);
	animation::applyIkRequests(model->anim, m_pose, world, ik, ikCount);
	collect(*model, world, m_pose.model);
	return m_items;
}

const std::vector<DawnDrawItem>& DawnModelStore::clips(const char* path, const sgc::Mat4f& world, const ModelPose& pose)
{
	m_items.clear();
	DawnModel* model = find(path);
	if (model == nullptr) { return m_items; }
	animation::AnimPoseParams params;
	const auto layer = [&](const char* clip, float time, float weight) {
		animation::AnimLayer l;
		l.clip = static_cast<std::int16_t>((clip != nullptr && clip[0] != '\0') ? model->anim.findClip(clip) : -1);
		l.timeSec = time;
		l.weight = weight;
		animation::pushLayer(params, l);
	};
	layer(pose.clipA, pose.timeA, 1.0f);
	if (pose.clipB != nullptr) { layer(pose.clipB, pose.timeB, std::clamp(pose.blend, 0.0f, 1.0f)); }
	return animPose(path, world, &params, nullptr, 0);
}

const std::vector<DawnDrawItem>& DawnModelStore::nodes(const char* path, const sgc::Mat4f& world,
                                                       const sgc::Mat4f* nodeModel, int count)
{
	m_items.clear();
	DawnModel* model = find(path);
	if (model == nullptr || nodeModel == nullptr || static_cast<std::size_t>(count) != model->anim.nodes.size()) { return m_items; }
	collect(*model, world, std::span<const sgc::Mat4f>(nodeModel, static_cast<std::size_t>(count)));
	return m_items;
}

int DawnModelStore::forget(const std::filesystem::path& changed)
{
	int forgotten = 0;
	for (auto it = m_models.begin(); it != m_models.end();)
	{
		const bool hit = asset::sameAssetFile(it->first, changed);
		if (hit && it->second != nullptr) { m_retired.push_back(std::move(it->second)); }   // 今のフレームの描画記録が指す材質の束を、次のフレームまで残す
		it = hit ? m_models.erase(it) : std::next(it);
		forgotten += hit ? 1 : 0;
	}
	return forgotten;
}

} // namespace mitiru::render::dawn_detail
