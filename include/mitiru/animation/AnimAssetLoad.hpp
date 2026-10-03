#pragma once

/// @file AnimAssetLoad.hpp
/// @brief glTF / glb / FBX と sidecar (`<名前>.anim.json`) から AnimAsset を読む。
/// @details ゲーム DLL と host の描画は同じこの関数で組むので、同じファイルからは同じ AnimAsset になる。
///          sidecar が無いのは正常 (イベント・マスク・ソケット無し、ルート骨は既定)。
///          sidecar の "retarget" に書いた別の骨格のクリップは、組む前にこの骨格へ写して足す (AnimRetarget.hpp)。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/AnimAssetBuild.hpp>
#include <mitiru/animation/AnimRetarget.hpp>
#include <mitiru/animation/AnimSidecar.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/asset/FbxImport.hpp>
#include <mitiru/render/GltfAnimationLoader.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

/// @brief 読む glTF のパス。FBX は隣の `<path>.glb` (初回に変換する。pack には変換済みを入れる)。描画の読み込みと同じ
[[nodiscard]] inline std::optional<std::string> animModelSourcePath(std::string_view path, std::string& error)
{
	if (!asset::isFbxPath(path)) { return std::string(path); }
	if (vfs::hasGlobalMount()) { return std::string(path) + ".glb"; }
	return asset::ensureFbxGlbCache(std::string(path), error);
}

/// @brief glb / glTF / FBX のファイルから骨格とクリップを読む。読めなければ nullopt で、理由を error に書く
[[nodiscard]] inline std::optional<render::GltfSceneData> loadAnimSceneFile(std::string_view path, std::string& error)
{
	const auto source = animModelSourcePath(path, error);
	if (!source) { return std::nullopt; }
	const auto bytes = vfs::readGlobal(*source);
	auto scene = bytes ? render::loadGltfAnimationFromMemory(bytes->data(), bytes->size()) : std::nullopt;
	if (!scene) { error = "読めない: " + std::string(path); }
	return scene;
}

/// @brief sidecar の "retarget" に書いたクリップを、別のファイルから scene の骨格へ写して足す
inline void applyAnimRetargets(render::GltfSceneData& scene, const std::vector<AnimRetargetDef>& defs,
                               std::vector<std::string>& warnings)
{
	for (const auto& def : defs)
	{
		std::string error;
		const auto mapBytes = def.mapJson.empty() ? vfs::readGlobal(def.mapPath) : std::nullopt;
		const std::string mapText = def.mapJson.empty() ? (mapBytes ? std::string(mapBytes->begin(), mapBytes->end()) : std::string())
		                                                : def.mapJson;
		const auto map = parseBoneMap(nlohmann::json::parse(mapText, nullptr, false), &error);
		const auto source = map ? loadAnimSceneFile(def.source, error) : std::nullopt;
		if (!map || !source)
		{
			warnings.push_back("retarget " + def.source + ": " + (def.mapJson.empty() && !mapBytes ? "対応表を読めない: " + def.mapPath : error));
			continue;
		}
		(void)appendRetargetedClips(scene, *source, *map, def.clips, def.prefix, warnings);
	}
}

/// @brief 読み込み済みの glTF シーンと sidecar の文字列から組む。sidecar が空なら追加の定義なし。
[[nodiscard]] inline AnimAsset buildAnimAssetWithSidecar(render::GltfSceneData scene, std::string_view sidecarJson,
                                                         std::vector<std::string>* warnings = nullptr)
{
	std::vector<std::string> localWarnings;
	auto& warn = warnings != nullptr ? *warnings : localWarnings;
	AnimAssetOptions options;
	if (!sidecarJson.empty())
	{
		std::string error;
		if (auto parsed = parseAnimSidecar(sidecarJson, &error)) { options = std::move(*parsed); }
		else { warn.push_back(error); }
	}
	applyAnimRetargets(scene, options.retargets, warn);
	return buildAnimAsset(std::move(scene), options, &warn);
}

/// @brief glb / glTF のバイト列と sidecar の文字列から組む。glTF が読めなければ nullopt。
[[nodiscard]] inline std::optional<AnimAsset> loadAnimAssetFromMemory(const void* gltf, std::size_t size,
                                                                      std::string_view sidecarJson = {},
                                                                      std::vector<std::string>* warnings = nullptr)
{
	auto scene = render::loadGltfAnimationFromMemory(gltf, size);
	if (!scene) { return std::nullopt; }
	return buildAnimAssetWithSidecar(std::move(*scene), sidecarJson, warnings);
}

/// @brief sidecar の中身を読む。無ければ空文字列。
[[nodiscard]] inline std::string readAnimSidecar(std::string_view modelPath)
{
	const auto bytes = vfs::readGlobal(animSidecarPath(modelPath));
	return bytes ? std::string(bytes->begin(), bytes->end()) : std::string{};
}

/// @brief モデルのパスから読む (asset pack を mount 済みならそこから、無ければディスクから)。
[[nodiscard]] inline std::optional<AnimAsset> loadAnimAssetFile(std::string_view modelPath,
                                                                std::vector<std::string>* warnings = nullptr)
{
	std::string error;
	auto scene = loadAnimSceneFile(modelPath, error);
	if (!scene)
	{
		if (warnings != nullptr) { warnings->push_back(error); }
		return std::nullopt;
	}
	return buildAnimAssetWithSidecar(std::move(*scene), readAnimSidecar(modelPath), warnings);
}

} // namespace mitiru::animation
