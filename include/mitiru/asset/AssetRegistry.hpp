#pragma once

/// @file AssetRegistry.hpp
/// @brief エンジンレベルでアセットを管理するレジストリ
///
/// アセットのメタデータを一元的に管理し、マニフェストファイルを使った
/// 一括登録と書き出しを提供する。
///
/// @code
/// mitiru::asset::AssetRegistry registry;
/// registry.registerAsset({"player_tex", AssetType::Texture, "textures/player.png", 1024, false});
/// auto textures = registry.findByType(AssetType::Texture);
/// registry.saveManifest("assets.manifest");
/// @endcode

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::asset
{

/// @brief アセットの種類
enum class AssetType
{
	Texture,   ///< テクスチャ
	Mesh,      ///< メッシュ
	Shader,    ///< シェーダー
	Sound,     ///< サウンド
	Script,    ///< スクリプト
	Scene,     ///< シーン
	Material   ///< マテリアル
};

/// @brief アセットのメタデータ
struct AssetMetadata
{
	std::string id;         ///< アセットID
	AssetType type{};       ///< アセット種類
	std::string path;       ///< ファイルパス
	uint64_t sizeBytes{0};  ///< ファイルサイズ（バイト）
	bool loaded{false};     ///< ロード済みか
};

/// @brief AssetType を文字列に変換する
/// @param type アセット種類
/// @return 文字列表現
[[nodiscard]] inline std::string assetTypeToString(AssetType type)
{
	switch (type)
	{
	case AssetType::Texture:  return "Texture";
	case AssetType::Mesh:     return "Mesh";
	case AssetType::Shader:   return "Shader";
	case AssetType::Sound:    return "Sound";
	case AssetType::Script:   return "Script";
	case AssetType::Scene:    return "Scene";
	case AssetType::Material: return "Material";
	}
	return "Unknown";
}

/// @brief 文字列から AssetType に変換する
/// @param str 文字列
/// @return 対応する AssetType（不明なら nullopt）
[[nodiscard]] inline std::optional<AssetType> stringToAssetType(const std::string& str)
{
	if (str == "Texture")  return AssetType::Texture;
	if (str == "Mesh")     return AssetType::Mesh;
	if (str == "Shader")   return AssetType::Shader;
	if (str == "Sound")    return AssetType::Sound;
	if (str == "Script")   return AssetType::Script;
	if (str == "Scene")    return AssetType::Scene;
	if (str == "Material") return AssetType::Material;
	return std::nullopt;
}

/// @brief アセットのメタデータを管理するレジストリ
class AssetRegistry
{
public:
	/// @brief アセットを登録する
	/// @param metadata アセットのメタデータ
	/// @return 登録に成功した場合は true（ID が重複している場合は false）
	bool registerAsset(AssetMetadata metadata)
	{
		if (m_assets.count(metadata.id) > 0) return false;
		const auto id = metadata.id;
		m_assets.emplace(id, std::move(metadata));
		return true;
	}

	/// @brief アセットのメタデータを取得する
	/// @param id アセット ID
	/// @return メタデータへのポインタ（存在しない場合は nullptr）
	[[nodiscard]] const AssetMetadata* getMetadata(const std::string& id) const noexcept
	{
		auto it = m_assets.find(id);
		if (it == m_assets.end()) return nullptr;
		return &it->second;
	}

	/// @brief アセットのメタデータを取得する（変更可能）
	[[nodiscard]] AssetMetadata* getMetadata(const std::string& id) noexcept
	{
		auto it = m_assets.find(id);
		if (it == m_assets.end()) return nullptr;
		return &it->second;
	}

	/// @brief 種類でアセットを検索する
	/// @param type アセットの種類
	/// @return 該当するメタデータのリスト
	[[nodiscard]] std::vector<const AssetMetadata*> findByType(AssetType type) const
	{
		std::vector<const AssetMetadata*> result;
		for (const auto& [id, meta] : m_assets)
		{
			if (meta.type == type)
			{
				result.push_back(&meta);
			}
		}
		return result;
	}

	/// @brief パスでアセットを検索する
	/// @param path ファイルパス
	/// @return メタデータへのポインタ（存在しない場合は nullptr）
	[[nodiscard]] const AssetMetadata* findByPath(const std::string& path) const noexcept
	{
		for (const auto& [id, meta] : m_assets)
		{
			if (meta.path == path)
			{
				return &meta;
			}
		}
		return nullptr;
	}

	/// @brief マニフェスト文字列を読み込み、アセットを一括登録する
	/// @param manifestContent マニフェストの内容（"id,type,path\n" 形式）
	/// @return 登録に成功した数
	size_t loadManifest(const std::string& manifestContent)
	{
		size_t count = 0;
		std::istringstream stream(manifestContent);
		std::string line;

		while (std::getline(stream, line))
		{
			if (line.empty()) continue;

			// id,type,path の形式
			const auto firstComma = line.find(',');
			if (firstComma == std::string::npos) continue;

			const auto secondComma = line.find(',', firstComma + 1);
			if (secondComma == std::string::npos) continue;

			const auto id = line.substr(0, firstComma);
			const auto typeStr = line.substr(firstComma + 1, secondComma - firstComma - 1);
			const auto path = line.substr(secondComma + 1);

			auto assetType = stringToAssetType(typeStr);
			if (!assetType) continue;

			if (registerAsset({id, *assetType, path, 0, false}))
			{
				++count;
			}
		}
		return count;
	}

	/// @brief マニフェスト文字列を生成する
	/// @return "id,type,path\n" 形式のマニフェスト文字列
	[[nodiscard]] std::string saveManifest() const
	{
		std::ostringstream oss;
		for (const auto& [id, meta] : m_assets)
		{
			oss << meta.id << ',' << assetTypeToString(meta.type) << ',' << meta.path << '\n';
		}
		return oss.str();
	}

	/// @brief 登録されているアセット数を返す
	[[nodiscard]] size_t count() const noexcept { return m_assets.size(); }

	/// @brief アセットの登録を解除する
	/// @param id アセット ID
	/// @return 解除に成功した場合は true
	bool unregister(const std::string& id)
	{
		return m_assets.erase(id) > 0;
	}

	/// @brief 全アセットをクリアする
	void clear() { m_assets.clear(); }

private:
	std::unordered_map<std::string, AssetMetadata> m_assets;  ///< アセット格納マップ
};

} // namespace mitiru::asset
