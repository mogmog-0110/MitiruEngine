#pragma once

/// @file ProjectFile.hpp
/// @brief プロジェクトファイル管理
///
/// プロジェクト設定を JSON 形式で保存・読み込みする。
///
/// @code
/// mitiru::data::ProjectConfig config;
/// config.projectName = "MyGame";
/// config.version = "1.0";
/// config.startScene = "main";
/// config.assetPaths = {"assets/textures", "assets/models"};
///
/// auto json = mitiru::data::ProjectFile::saveToString(config);
/// auto loaded = mitiru::data::ProjectFile::loadFromString(json);
/// @endcode

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"

namespace mitiru::data
{

/// @brief プロジェクト設定
struct ProjectConfig
{
	std::string projectName;              ///< プロジェクト名
	std::string version{"1.0"};           ///< バージョン文字列
	std::string startScene{"main"};       ///< 起動シーン名
	std::vector<std::string> assetPaths;  ///< アセットパスリスト
};

/// @brief プロジェクトファイルの保存・読み込みユーティリティ
class ProjectFile
{
public:
	/// @brief プロジェクト設定を JSON 文字列に保存する
	/// @param config プロジェクト設定
	/// @return JSON 文字列
	[[nodiscard]] static std::string saveToString(const ProjectConfig& config)
	{
		return nlohmann::ordered_json{
			{"project", config.projectName},
			{"version", config.version},
			{"startScene", config.startScene},
			{"assets", config.assetPaths},
		}.dump();
	}

	/// @brief JSON 文字列からプロジェクト設定を読み込む
	/// @param json JSON 文字列
	/// @return プロジェクト設定（"project" が無い・JSON として読めないときは nullopt）
	[[nodiscard]] static std::optional<ProjectConfig> loadFromString(const std::string& json)
	{
		const auto doc = nlohmann::json::parse(json, nullptr, false);
		if (!doc.is_object()) return std::nullopt;
		const auto project = doc.find("project");
		if (project == doc.end() || !project->is_string()) return std::nullopt;

		ProjectConfig config;
		config.projectName = project->get<std::string>();
		config.version = fieldOr(doc, "version", config.version);
		config.startScene = fieldOr(doc, "startScene", config.startScene);
		if (const auto assets = doc.find("assets"); assets != doc.end() && assets->is_array())
		{
			for (const auto& path : *assets)
			{
				if (path.is_string()) { config.assetPaths.push_back(path.get<std::string>()); }
			}
		}
		return config;
	}
};

} // namespace mitiru::data
