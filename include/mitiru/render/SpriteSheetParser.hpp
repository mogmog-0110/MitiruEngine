#pragma once

/// @file SpriteSheetParser.hpp
/// @brief Aseprite JSON形式のスプライトシートパーサー
/// @details Aseprite が出力する JSON（Hash / Array 両形式）を解析し、
///          SpriteAnimationSet および SpriteSheet を生成する。
///          nlohmann/json を使用する。

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <sgc/math/Rect.hpp>

#include <mitiru/render/SpriteAnimation.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief Aseprite JSON パース結果
/// @details SpriteSheet と SpriteAnimationSet をまとめて保持する。
struct AsepriteParseResult
{
	SpriteSheet sheet;              ///< フレーム情報付きスプライトシート
	SpriteAnimationSet animations;  ///< フレームタグから生成されたアニメーション群
};

/// @brief Aseprite JSON スプライトシートパーサー
/// @details Aseprite の「JSON Data」エクスポートで出力される形式を解析する。
///
/// 対応フォーマット:
/// - Hash形式（フレーム名をキーとするオブジェクト）
/// - Array形式（フレームの配列）
/// - frameTagsによるアニメーション定義
///
/// @code
/// std::string jsonStr = loadFile("spritesheet.json");
/// auto animSet = SpriteSheetParser::parseAsepriteJson(jsonStr);
///
/// Texture tex = loadTexture("spritesheet.png");
/// auto [sheet, anims] = SpriteSheetParser::parseAsepriteJsonWithTexture(jsonStr, tex);
/// @endcode
class SpriteSheetParser
{
public:
	/// @brief Aseprite JSON からアニメーションセットを解析する
	/// @param jsonString JSON文字列
	/// @return SpriteAnimationSet
	/// @throw std::runtime_error JSON解析エラー時
	[[nodiscard]] static SpriteAnimationSet parseAsepriteJson(const std::string& jsonString)
	{
		const auto json = nlohmann::json::parse(jsonString);
		const auto frames = parseFrames(json);
		return buildAnimationsFromTags(json, frames);
	}

	/// @brief Aseprite JSON からスプライトシートとアニメーションセットを解析する
	/// @param jsonString JSON文字列
	/// @param texture スプライトシートテクスチャ
	/// @return AsepriteParseResult（SpriteSheet + SpriteAnimationSet）
	/// @throw std::runtime_error JSON解析エラー時
	[[nodiscard]] static AsepriteParseResult parseAsepriteJsonWithTexture(
		const std::string& jsonString,
		const Texture& texture)
	{
		const auto json = nlohmann::json::parse(jsonString);
		const auto frames = parseFrames(json);
		auto animations = buildAnimationsFromTags(json, frames);
		auto sheet = SpriteSheet::createFromFrames(texture, frames);

		return AsepriteParseResult{std::move(sheet), std::move(animations)};
	}

private:
	/// @brief JSON の "frames" セクションからフレーム配列を解析する
	/// @param json 解析済み JSON オブジェクト
	/// @return SpriteFrame 配列
	[[nodiscard]] static std::vector<SpriteFrame> parseFrames(const nlohmann::json& json)
	{
		if (!json.contains("frames"))
		{
			throw std::runtime_error("Aseprite JSON: 'frames' section not found");
		}

		const auto& framesJson = json["frames"];
		std::vector<SpriteFrame> frames;

		if (framesJson.is_object())
		{
			frames = parseHashFrames(framesJson);
		}
		else if (framesJson.is_array())
		{
			frames = parseArrayFrames(framesJson);
		}
		else
		{
			throw std::runtime_error("Aseprite JSON: 'frames' must be an object or array");
		}

		return frames;
	}

	/// @brief Hash形式のフレームを解析する（フレーム名がキー）
	/// @param framesJson "frames" オブジェクト
	/// @return SpriteFrame 配列
	[[nodiscard]] static std::vector<SpriteFrame> parseHashFrames(const nlohmann::json& framesJson)
	{
		/// Hash形式ではキーの順序が保証されないため、
		/// キー名でソートして安定した順序を得る
		std::vector<std::pair<std::string, nlohmann::json>> entries;
		entries.reserve(framesJson.size());

		for (const auto& [key, value] : framesJson.items())
		{
			entries.emplace_back(key, value);
		}

		std::sort(entries.begin(), entries.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });

		std::vector<SpriteFrame> frames;
		frames.reserve(entries.size());

		for (const auto& [name, frameData] : entries)
		{
			frames.push_back(parseSingleFrame(frameData));
		}

		return frames;
	}

	/// @brief Array形式のフレームを解析する
	/// @param framesJson "frames" 配列
	/// @return SpriteFrame 配列
	[[nodiscard]] static std::vector<SpriteFrame> parseArrayFrames(const nlohmann::json& framesJson)
	{
		std::vector<SpriteFrame> frames;
		frames.reserve(framesJson.size());

		for (const auto& frameData : framesJson)
		{
			frames.push_back(parseSingleFrame(frameData));
		}

		return frames;
	}

	/// @brief 1フレームの JSON を SpriteFrame に変換する
	/// @param frameData フレームの JSON オブジェクト
	/// @return SpriteFrame
	[[nodiscard]] static SpriteFrame parseSingleFrame(const nlohmann::json& frameData)
	{
		const auto& rect = frameData.at("frame");

		SpriteFrame frame;
		frame.sourceRect = sgc::Recti{
			rect.at("x").get<int>(),
			rect.at("y").get<int>(),
			rect.at("w").get<int>(),
			rect.at("h").get<int>()
		};

		/// Aseprite の duration はミリ秒なので秒に変換する
		frame.duration = frameData.at("duration").get<float>() / 1000.0f;

		/// spriteSourceSize がある場合はオフセットを計算する
		if (frameData.contains("spriteSourceSize"))
		{
			const auto& sss = frameData["spriteSourceSize"];
			frame.offsetX = sss.value("x", 0.0f);
			frame.offsetY = sss.value("y", 0.0f);
		}

		return frame;
	}

	/// @brief frameTags からアニメーションセットを構築する
	/// @param json 全体の JSON
	/// @param frames 解析済みフレーム配列
	/// @return SpriteAnimationSet
	[[nodiscard]] static SpriteAnimationSet buildAnimationsFromTags(
		const nlohmann::json& json,
		const std::vector<SpriteFrame>& frames)
	{
		SpriteAnimationSet animSet;

		/// frameTags がない場合は全フレームを "default" アニメーションとして登録する
		if (!json.contains("meta") || !json["meta"].contains("frameTags"))
		{
			if (!frames.empty())
			{
				SpriteAnimation defaultAnim;
				defaultAnim.name = "default";
				defaultAnim.frames = frames;
				defaultAnim.looping = true;
				animSet.add(std::move(defaultAnim));
			}
			return animSet;
		}

		const auto& tags = json["meta"]["frameTags"];

		for (const auto& tag : tags)
		{
			const auto name = tag.at("name").get<std::string>();
			const auto from = tag.at("from").get<int>();
			const auto to = tag.at("to").get<int>();
			const auto directionStr = tag.value("direction", "forward");

			SpriteAnimation anim;
			anim.name = name;

			const int start = std::max(0, from);
			const int end = std::min(to, static_cast<int>(frames.size()) - 1);

			for (int i = start; i <= end; ++i)
			{
				anim.frames.push_back(frames[static_cast<std::size_t>(i)]);
			}

			/// Aseprite の direction を解釈する
			if (directionStr == "pingpong" || directionStr == "pingpong_reverse")
			{
				anim.pingPong = true;
				anim.looping = true;
			}
			else
			{
				anim.looping = true;
			}

			/// "repeat" フィールドが "1" の場合はループしない
			if (tag.contains("repeat"))
			{
				const auto repeat = tag["repeat"].get<std::string>();
				if (repeat == "1")
				{
					anim.looping = false;
				}
			}

			animSet.add(std::move(anim));
		}

		return animSet;
	}
};

} // namespace mitiru::render
