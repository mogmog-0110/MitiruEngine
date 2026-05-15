#pragma once

/// @file ProjectFile.hpp
/// @brief プロジェクトファイル管理
///
/// プロジェクト設定をJSON形式で保存・読み込みする。
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

#include "mitiru/data/JsonBuilder.hpp"

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
	/// @brief プロジェクト設定をJSON文字列に保存する
	/// @param config プロジェクト設定
	/// @return JSON文字列
	[[nodiscard]] static std::string saveToString(const ProjectConfig& config)
	{
		JsonBuilder builder;
		builder.beginObject();
		builder.key("project").value(config.projectName);
		builder.key("version").value(config.version);
		builder.key("startScene").value(config.startScene);
		builder.key("assets");
		builder.beginArray();
		for (const auto& path : config.assetPaths)
		{
			builder.value(path);
		}
		builder.endArray();
		builder.endObject();
		return builder.build();
	}

	/// @brief JSON文字列からプロジェクト設定を読み込む
	/// @param json JSON文字列
	/// @return プロジェクト設定（失敗時nullopt）
	[[nodiscard]] static std::optional<ProjectConfig> loadFromString(const std::string& json)
	{
		JsonReader reader;
		if (!reader.parse(json))
		{
			return std::nullopt;
		}

		ProjectConfig config;

		if (auto v = reader.getString("project"))
		{
			config.projectName = *v;
		}
		else
		{
			return std::nullopt;
		}

		if (auto v = reader.getString("version"))
		{
			config.version = *v;
		}

		if (auto v = reader.getString("startScene"))
		{
			config.startScene = *v;
		}

		if (auto arr = reader.getArray("assets"))
		{
			config.assetPaths = *arr;
		}

		return config;
	}
};

} // namespace mitiru::data
