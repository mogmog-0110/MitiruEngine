#pragma once

/// @file SceneSerializer.hpp
/// @brief シーンシリアライゼーション
///
/// JSON風テキスト形式でシーンデータをシリアライズ/デシリアライズする。
/// 外部ライブラリ不要、シンプルなkey-valueペアベース。
///
/// @code
/// using namespace sgc::data;
///
/// SerializedScene scene;
/// scene.metadata["name"] = "Level1";
/// scene.metadata["version"] = "1";
///
/// SerializedEntity entity;
/// entity.name = "Player";
/// entity.components["Transform"] = {{"x", "100"}, {"y", "200"}};
/// entity.components["Sprite"] = {{"texture", "player.png"}};
/// scene.entities.push_back(std::move(entity));
///
/// // シリアライズ
/// std::string text = serializeScene(scene);
///
/// // デシリアライズ
/// auto restored = deserializeScene(text);
/// @endcode

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::data
{

/// @brief シリアライズされたエンティティ
///
/// 名前とコンポーネント（key-valueマップの辞書）で構成される。
struct SerializedEntity
{
	std::string name;  ///< エンティティ名
	/// @brief コンポーネント辞書（コンポーネント名→プロパティマップ）
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> components;
};

/// @brief シリアライズされたシーン
///
/// メタデータとエンティティの配列で構成される。
struct SerializedScene
{
	std::unordered_map<std::string, std::string> metadata;  ///< シーンメタデータ
	std::vector<SerializedEntity> entities;                  ///< エンティティ配列
};

// ── シリアライズ ────────────────────────────────────────

/// @brief 文字列をエスケープする（ダブルクォート対応）
/// @param s 入力文字列
/// @return エスケープ済み文字列
[[nodiscard]] inline std::string escapeString(const std::string& s)
{
	std::string result;
	result.reserve(s.size() + 4);
	for (const char c : s)
	{
		if (c == '"') result += "\\\"";
		else if (c == '\\') result += "\\\\";
		else if (c == '\n') result += "\\n";
		else if (c == '\t') result += "\\t";
		else result += c;
	}
	return result;
}

/// @brief エスケープ文字列を復元する
/// @param s エスケープ済み文字列
/// @return 復元された文字列
[[nodiscard]] inline std::string unescapeString(const std::string& s)
{
	std::string result;
	result.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\\' && i + 1 < s.size())
		{
			switch (s[i + 1])
			{
			case '"':  result += '"'; ++i; break;
			case '\\': result += '\\'; ++i; break;
			case 'n':  result += '\n'; ++i; break;
			case 't':  result += '\t'; ++i; break;
			default:   result += s[i]; break;
			}
		}
		else
		{
			result += s[i];
		}
	}
	return result;
}

/// @brief シーンを文字列にシリアライズする
/// @param scene シリアライズ対象シーン
/// @return シリアライズされた文字列
[[nodiscard]] inline std::string serializeScene(const SerializedScene& scene)
{
	std::ostringstream ss;
	ss << "[scene]\n";

	// メタデータをキー順でソートして出力
	std::vector<std::string> metaKeys;
	metaKeys.reserve(scene.metadata.size());
	for (const auto& [k, v] : scene.metadata)
	{
		metaKeys.push_back(k);
	}
	std::sort(metaKeys.begin(), metaKeys.end());

	for (const auto& key : metaKeys)
	{
		ss << key << " = \"" << escapeString(scene.metadata.at(key)) << "\"\n";
	}

	// エンティティ
	for (const auto& entity : scene.entities)
	{
		ss << "\n[entity \"" << escapeString(entity.name) << "\"]\n";

		// コンポーネントをキー順でソートして出力
		std::vector<std::string> compKeys;
		compKeys.reserve(entity.components.size());
		for (const auto& [k, v] : entity.components)
		{
			compKeys.push_back(k);
		}
		std::sort(compKeys.begin(), compKeys.end());

		for (const auto& compName : compKeys)
		{
			ss << "  [" << compName << "]\n";
			const auto& props = entity.components.at(compName);

			std::vector<std::string> propKeys;
			propKeys.reserve(props.size());
			for (const auto& [k, v] : props)
			{
				propKeys.push_back(k);
			}
			std::sort(propKeys.begin(), propKeys.end());

			for (const auto& propKey : propKeys)
			{
				ss << "    " << propKey << " = \"" << escapeString(props.at(propKey)) << "\"\n";
			}
		}
	}

	return ss.str();
}

// ── デシリアライズ ──────────────────────────────────────

/// @brief 文字列の前後の空白を除去する
/// @param s 入力文字列
/// @return トリム済み文字列
[[nodiscard]] inline std::string trim(const std::string& s)
{
	const auto start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return {};
	const auto end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

/// @brief 引用符で囲まれた値を取得する
/// @param s 入力文字列（"value"形式）
/// @return 引用符内の文字列（エスケープ復元済み）
[[nodiscard]] inline std::string extractQuoted(const std::string& s)
{
	const auto first = s.find('"');
	if (first == std::string::npos) return s;
	const auto last = s.rfind('"');
	if (last == first) return s;
	return unescapeString(s.substr(first + 1, last - first - 1));
}

/// @brief 文字列からシーンをデシリアライズする
/// @param text シリアライズされた文字列
/// @return デシリアライズされたシーン
[[nodiscard]] inline SerializedScene deserializeScene(const std::string& text)
{
	SerializedScene scene;
	std::istringstream stream(text);
	std::string line;

	enum class Section
	{
		None,
		SceneMeta,
		Entity,
		Component
	};

	Section currentSection = Section::None;
	std::size_t currentEntity = 0;
	std::string currentComponent;

	while (std::getline(stream, line))
	{
		const std::string trimmed = trim(line);
		if (trimmed.empty()) continue;

		// [scene] ヘッダー
		if (trimmed == "[scene]")
		{
			currentSection = Section::SceneMeta;
			continue;
		}

		// [entity "Name"] ヘッダー
		if (trimmed.size() > 9 && trimmed.substr(0, 8) == "[entity ")
		{
			SerializedEntity entity;
			entity.name = extractQuoted(trimmed);
			scene.entities.push_back(std::move(entity));
			currentEntity = scene.entities.size() - 1;
			currentSection = Section::Entity;
			currentComponent.clear();
			continue;
		}

		// コンポーネントヘッダー [ComponentName]（エンティティ内）
		if (currentSection == Section::Entity || currentSection == Section::Component)
		{
			if (trimmed.front() == '[' && trimmed.back() == ']')
			{
				currentComponent = trimmed.substr(1, trimmed.size() - 2);
				currentSection = Section::Component;
				continue;
			}
		}

		// key = "value" のパース
		const auto eqPos = trimmed.find('=');
		if (eqPos != std::string::npos)
		{
			const std::string key = trim(trimmed.substr(0, eqPos));
			const std::string value = extractQuoted(trim(trimmed.substr(eqPos + 1)));

			if (currentSection == Section::SceneMeta)
			{
				scene.metadata[key] = value;
			}
			else if (currentSection == Section::Component && !currentComponent.empty())
			{
				scene.entities[currentEntity].components[currentComponent][key] = value;
			}
		}
	}

	return scene;
}

} // namespace sgc::data
