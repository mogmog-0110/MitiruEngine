#pragma once

/// @file AnimAssetLoad.hpp
/// @brief glTF / glb と sidecar (`<名前>.anim.json`) から AnimAsset を読む。
/// @details ゲーム DLL と host の描画は同じこの関数で組むので、同じファイルからは同じ AnimAsset になる。
///          sidecar が無いのは正常 (イベント・マスク・ソケット無し、ルート骨は既定)。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/AnimAssetBuild.hpp>
#include <mitiru/animation/AnimSidecar.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/GltfAnimationLoader.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

/// @brief 読み込み済みの glTF シーンと sidecar の文字列から組む。sidecar が空なら追加の定義なし。
[[nodiscard]] inline AnimAsset buildAnimAssetWithSidecar(render::GltfSceneData scene, std::string_view sidecarJson,
                                                         std::vector<std::string>* warnings = nullptr)
{
	AnimAssetOptions options;
	if (!sidecarJson.empty())
	{
		std::string error;
		if (auto parsed = parseAnimSidecar(sidecarJson, &error)) { options = std::move(*parsed); }
		else if (warnings != nullptr) { warnings->push_back(error); }
	}
	return buildAnimAsset(std::move(scene), options, warnings);
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
	const auto bytes = vfs::readGlobal(modelPath);
	if (!bytes) { return std::nullopt; }
	return loadAnimAssetFromMemory(bytes->data(), bytes->size(), readAnimSidecar(modelPath), warnings);
}

} // namespace mitiru::animation
