#pragma once

/// @file SpriteAtlas.hpp
/// @brief spritesheet metadata (frames json) ローダー。
/// @details 1 ファイルに `frames: [{name, x, y, w, h, anchorX, anchorY}]` を持ち、
///          `loadSpriteAtlas("foo.atlas")` で全部読める。`frame(name)` で名前引き。
///          手書きの Sheet 構造体を毎フレーム増やさなくて済む。

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru::render
{

/// @brief atlas 内 1 フレーム。anchorX/Y は描画原点 (フレーム左上からのオフセット、px)。
struct AtlasFrame
{
	std::string name;
	sgc::Rectf  rect;     ///< テクスチャ内 src rect (px)
	float       anchorX = 0.0f;
	float       anchorY = 0.0f;
};

/// @brief 1 つの spritesheet のフレーム集合。name → index で O(1) 引き。
struct SpriteAtlas
{
	std::vector<AtlasFrame> frames;
	std::unordered_map<std::string, int> nameToIndex;

	/// @brief 名前でフレームを探す。なければ nullptr。
	[[nodiscard]] const AtlasFrame* frame(std::string_view name) const
	{
		const auto it = nameToIndex.find(std::string{name});
		if (it == nameToIndex.end()) { return nullptr; }
		return &frames[it->second];
	}
};

/// @brief atlas JSON 文字列から SpriteAtlas を構築する (純関数、テスト容易)。
/// @return 解析成功で SpriteAtlas、frames 配列が無い / 各 frame に name か x/y/w/h が
///         欠ければ std::nullopt。重複 name は後勝ち。
[[nodiscard]] inline std::optional<SpriteAtlas> parseSpriteAtlas(std::string_view jsonText)
{
	try
	{
		const auto j = nlohmann::json::parse(jsonText);
		if (!j.contains("frames") || !j["frames"].is_array()) { return std::nullopt; }
		SpriteAtlas atlas;
		atlas.frames.reserve(j["frames"].size());
		for (const auto& f : j["frames"])
		{
			if (!f.contains("name") || !f["name"].is_string())          { return std::nullopt; }
			if (!f.contains("x") || !f.contains("y")
			    || !f.contains("w") || !f.contains("h"))                  { return std::nullopt; }
			AtlasFrame af;
			af.name = f["name"].get<std::string>();
			af.rect = sgc::Rectf{f["x"].get<float>(), f["y"].get<float>(),
			                     f["w"].get<float>(), f["h"].get<float>()};
			if (f.contains("anchorX")) { af.anchorX = f["anchorX"].get<float>(); }
			if (f.contains("anchorY")) { af.anchorY = f["anchorY"].get<float>(); }
			atlas.nameToIndex[af.name] = static_cast<int>(atlas.frames.size());
			atlas.frames.push_back(std::move(af));
		}
		return atlas;
	}
	catch (...)
	{
		return std::nullopt;
	}
}

/// @brief atlas JSON ファイルから SpriteAtlas を構築する。
[[nodiscard]] inline std::optional<SpriteAtlas> loadSpriteAtlas(const std::string& path)
{
	std::FILE* fp = std::fopen(path.c_str(), "rb");
	if (!fp) { return std::nullopt; }
	std::string body;
	char buf[4096];
	while (true)
	{
		const auto n = std::fread(buf, 1, sizeof(buf), fp);
		if (n == 0) { break; }
		body.append(buf, n);
	}
	std::fclose(fp);
	return parseSpriteAtlas(body);
}

}  // namespace mitiru::render
