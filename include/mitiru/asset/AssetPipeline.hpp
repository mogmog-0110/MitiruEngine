#pragma once

/// @file AssetPipeline.hpp
/// @brief JSON マニフェストからSVGアセットを一括生成するパイプライン
/// @details マニフェストJSONにアセット名・テンプレート種別・パラメータを定義し、
///          一括でSVG文字列を生成する。GraphWalkerのアセット管理に使用。
///
/// @code
/// std::string manifest = R"json([
///     {"name":"player","templateType":"player","params":{"radius":"20","color":"#00ffff"}},
///     {"name":"ground","templateType":"platform","params":{"w":"200","h":"24","color":"#00ff88"}}
/// ])json";
/// mitiru::asset::AssetPipeline pipeline;
/// auto entries = pipeline.loadManifest(manifest);
/// auto assets = pipeline.generateAll(entries);
/// // assets["player"] → SVG文字列
/// @endcode

#include "GameAssetTemplates.hpp"
#include "SvgGenerator.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::asset
{

/// @brief アセットマニフェストの1エントリ
struct AssetManifest
{
	std::string name;                          ///< アセット名（一意キー）
	std::string templateType;                  ///< テンプレート種別（"player", "platform" 等）
	std::map<std::string, std::string> params; ///< テンプレートパラメータ
};

/// @brief マニフェスト駆動のSVGアセット一括生成パイプライン
class AssetPipeline
{
public:
	/// @brief JSONマニフェスト文字列を解析してエントリ一覧を返す
	/// @param json JSON配列文字列
	/// @return AssetManifestのベクター
	[[nodiscard]] std::vector<AssetManifest> loadManifest(const std::string& json) const
	{
		std::vector<AssetManifest> result;
		auto trimmed = trim(json);
		if (trimmed.empty() || trimmed.front() != '[')
		{
			return result;
		}

		// 配列内の各オブジェクトを抽出
		size_t pos = 1;
		while (pos < trimmed.size())
		{
			const auto objStart = trimmed.find('{', pos);
			if (objStart == std::string::npos)
			{
				break;
			}
			int depth = 1;
			size_t objEnd = objStart + 1;
			while (objEnd < trimmed.size() && depth > 0)
			{
				if (trimmed[objEnd] == '{')
				{
					++depth;
				}
				else if (trimmed[objEnd] == '}')
				{
					--depth;
				}
				++objEnd;
			}
			const auto objStr = trimmed.substr(objStart, objEnd - objStart);

			AssetManifest entry;
			entry.name = extractString(objStr, "name");
			entry.templateType = extractString(objStr, "templateType");

			// params解析
			auto paramsBlock = extractObject(objStr, "params");
			if (!paramsBlock.empty())
			{
				parseParams(paramsBlock, entry.params);
			}

			if (!entry.name.empty())
			{
				result.push_back(entry);
			}
			pos = objEnd;
		}

		return result;
	}

	/// @brief 全マニフェストエントリからSVGを一括生成する
	/// @param manifests マニフェストエントリ一覧
	/// @return アセット名 → SVG文字列のマップ
	[[nodiscard]] std::map<std::string, std::string> generateAll(
		const std::vector<AssetManifest>& manifests) const
	{
		std::map<std::string, std::string> result;
		for (const auto& m : manifests)
		{
			auto svg = generateOne(m);
			if (svg.has_value())
			{
				result[m.name] = *svg;
			}
		}
		return result;
	}

	/// @brief 単一マニフェストエントリからSVGを生成する
	/// @param manifest マニフェストエントリ
	/// @return 成功時はSVG文字列、失敗時はnullopt
	[[nodiscard]] std::optional<std::string> generateOne(const AssetManifest& manifest) const
	{
		const auto& t = manifest.templateType;
		const auto& p = manifest.params;

		SvgDocument doc;

		if (t == "player")
		{
			doc = GameAssetTemplates::player(
				getFloat(p, "radius", 20.0f),
				getString(p, "color", "#00ffff"));
		}
		else if (t == "platform")
		{
			doc = GameAssetTemplates::platform(
				getFloat(p, "w", 120.0f),
				getFloat(p, "h", 20.0f),
				getString(p, "color", "#00ff88"));
		}
		else if (t == "movingPlatform")
		{
			doc = GameAssetTemplates::movingPlatform(
				getFloat(p, "w", 120.0f),
				getFloat(p, "h", 20.0f),
				getString(p, "color", "#00ff88"),
				getString(p, "arrowDir", "right"));
		}
		else if (t == "crumblingPlatform")
		{
			doc = GameAssetTemplates::crumblingPlatform(
				getFloat(p, "w", 120.0f),
				getFloat(p, "h", 20.0f),
				getString(p, "color", "#ff8800"));
		}
		else if (t == "springPlatform")
		{
			doc = GameAssetTemplates::springPlatform(
				getFloat(p, "w", 80.0f),
				getFloat(p, "h", 20.0f));
		}
		else if (t == "spikeHazard")
		{
			doc = GameAssetTemplates::spikeHazard(
				getFloat(p, "w", 100.0f),
				getFloat(p, "h", 20.0f),
				static_cast<int>(getFloat(p, "count", 5.0f)));
		}
		else if (t == "laserBarrier")
		{
			doc = GameAssetTemplates::laserBarrier(
				getFloat(p, "length", 150.0f),
				getString(p, "color", "#ff00ff"));
		}
		else if (t == "collectible")
		{
			doc = GameAssetTemplates::collectible(
				getFloat(p, "size", 24.0f),
				getString(p, "color", "#00ffff"));
		}
		else if (t == "checkpoint")
		{
			doc = GameAssetTemplates::checkpoint(
				getFloat(p, "size", 40.0f));
		}
		else if (t == "npc")
		{
			doc = GameAssetTemplates::npc(
				getFloat(p, "size", 40.0f),
				getString(p, "color", "#8888ff"));
		}
		else if (t == "enemy")
		{
			doc = GameAssetTemplates::enemy(
				getFloat(p, "size", 32.0f),
				getString(p, "color", "#ff4444"));
		}
		else if (t == "gate")
		{
			doc = GameAssetTemplates::gate(
				getFloat(p, "w", 20.0f),
				getFloat(p, "h", 80.0f),
				getString(p, "locked", "true") == "true");
		}
		else if (t == "goal")
		{
			doc = GameAssetTemplates::goal(
				getFloat(p, "size", 48.0f));
		}
		else if (t == "formulaButton")
		{
			doc = GameAssetTemplates::formulaButton(
				getString(p, "label", "f(x)"),
				getFloat(p, "size", 48.0f),
				getString(p, "color", "#00ffff"));
		}
		else
		{
			return std::nullopt;
		}

		return SvgGenerator::toSvg(doc);
	}

	/// @brief 全テンプレートを網羅するサンプルマニフェストJSONを出力する
	/// @return JSON配列文字列
	[[nodiscard]] std::string exportManifestTemplate() const
	{
		return R"json([
	{"name":"player_cyan","templateType":"player","params":{"radius":"20","color":"#00ffff"}},
	{"name":"ground_green","templateType":"platform","params":{"w":"200","h":"24","color":"#00ff88"}},
	{"name":"moving_right","templateType":"movingPlatform","params":{"w":"120","h":"20","color":"#00ff88","arrowDir":"right"}},
	{"name":"crumble_orange","templateType":"crumblingPlatform","params":{"w":"100","h":"20","color":"#ff8800"}},
	{"name":"spring_yellow","templateType":"springPlatform","params":{"w":"80","h":"20"}},
	{"name":"spikes_red","templateType":"spikeHazard","params":{"w":"100","h":"20","count":"5"}},
	{"name":"laser_magenta","templateType":"laserBarrier","params":{"length":"150","color":"#ff00ff"}},
	{"name":"gem_cyan","templateType":"collectible","params":{"size":"24","color":"#00ffff"}},
	{"name":"flag_green","templateType":"checkpoint","params":{"size":"40"}},
	{"name":"npc_blue","templateType":"npc","params":{"size":"40","color":"#8888ff"}},
	{"name":"enemy_red","templateType":"enemy","params":{"size":"32","color":"#ff4444"}},
	{"name":"gate_locked","templateType":"gate","params":{"w":"20","h":"80","locked":"true"}},
	{"name":"portal_gold","templateType":"goal","params":{"size":"48"}},
	{"name":"btn_sin","templateType":"formulaButton","params":{"label":"sin","size":"48","color":"#00ffff"}}
])json";
	}

private:
	/// @brief パラメータマップからfloat値を取得する
	[[nodiscard]] static float getFloat(const std::map<std::string, std::string>& params,
		const std::string& key, float defaultVal)
	{
		auto it = params.find(key);
		if (it == params.end())
		{
			return defaultVal;
		}
		try
		{
			return std::stof(it->second);
		}
		catch (...)
		{
			return defaultVal;
		}
	}

	/// @brief パラメータマップからstring値を取得する
	[[nodiscard]] static std::string getString(const std::map<std::string, std::string>& params,
		const std::string& key, const std::string& defaultVal)
	{
		auto it = params.find(key);
		if (it == params.end())
		{
			return defaultVal;
		}
		return it->second;
	}

	// ========== 簡易JSONパーサーユーティリティ ==========

	[[nodiscard]] static std::string trim(const std::string& s)
	{
		const auto start = s.find_first_not_of(" \t\n\r");
		if (start == std::string::npos)
		{
			return "";
		}
		const auto end = s.find_last_not_of(" \t\n\r");
		return s.substr(start, end - start + 1);
	}

	[[nodiscard]] static std::string extractString(const std::string& json, const std::string& key)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find(':', pos + search.size());
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find('"', pos + 1);
		if (pos == std::string::npos)
		{
			return "";
		}
		const auto end = json.find('"', pos + 1);
		if (end == std::string::npos)
		{
			return "";
		}
		return json.substr(pos + 1, end - pos - 1);
	}

	[[nodiscard]] static std::string extractObject(const std::string& json, const std::string& key)
	{
		const std::string search = "\"" + key + "\"";
		auto pos = json.find(search);
		if (pos == std::string::npos)
		{
			return "";
		}
		pos = json.find('{', pos + search.size());
		if (pos == std::string::npos)
		{
			return "";
		}
		int depth = 1;
		size_t end = pos + 1;
		while (end < json.size() && depth > 0)
		{
			if (json[end] == '{')
			{
				++depth;
			}
			else if (json[end] == '}')
			{
				--depth;
			}
			++end;
		}
		return json.substr(pos, end - pos);
	}

	/// @brief パラメータオブジェクト内のキー/値ペアを解析する
	static void parseParams(const std::string& json, std::map<std::string, std::string>& out)
	{
		size_t pos = 0;
		while (pos < json.size())
		{
			// キーを探す
			auto keyStart = json.find('"', pos);
			if (keyStart == std::string::npos)
			{
				break;
			}
			auto keyEnd = json.find('"', keyStart + 1);
			if (keyEnd == std::string::npos)
			{
				break;
			}
			const auto key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

			// コロンの後の値
			auto colon = json.find(':', keyEnd + 1);
			if (colon == std::string::npos)
			{
				break;
			}
			auto valStart = json.find('"', colon + 1);
			if (valStart == std::string::npos)
			{
				break;
			}
			auto valEnd = json.find('"', valStart + 1);
			if (valEnd == std::string::npos)
			{
				break;
			}
			out[key] = json.substr(valStart + 1, valEnd - valStart - 1);
			pos = valEnd + 1;
		}
	}
};

} // namespace mitiru::asset
