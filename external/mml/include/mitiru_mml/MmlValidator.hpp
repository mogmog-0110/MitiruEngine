#pragma once
/// @file MmlValidator.hpp
/// @brief MMLバリデーター — トラック長不一致と、曲のテキストの検査
/// @details validate は複数トラックのMML文字列を解析し、各トラックの推定再生時間を計算して、
///          トラック間の長さの不一致を検出してエラー/警告を報告する。
///          validateSong は曲のテキスト (#track の行、マクロ、変数) 全体を検査し、音域外の音、音量の飽和、
///          ループ点の不整合、未定義のマクロと変数を、行と列つきで報告する (SongLint.hpp)。

#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/SongLint.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief 検証結果
struct ValidationResult
{
	bool valid = true;                     ///< 全体の有効性
	std::vector<std::string> warnings;     ///< 警告メッセージ群
	std::vector<std::string> errors;       ///< エラーメッセージ群
	std::vector<float> trackDurations;     ///< 各トラックの推定秒数
};

/// @brief MMLバリデーター
class MmlValidator
{
public:
	/// @brief 複数トラックのMMLを検証する
	/// @param trackMmls トラックごとのMML文字列配列
	/// @return 検証結果
	[[nodiscard]] static ValidationResult validate(const std::vector<std::string>& trackMmls)
	{
		ValidationResult result;

		for (const auto& mml : trackMmls)
		{
			float dur = estimateDuration(mml);
			result.trackDurations.push_back(dur);
		}

		// トラック長の一致チェック
		if (result.trackDurations.size() >= 2)
		{
			float maxDur = *std::max_element(
				result.trackDurations.begin(), result.trackDurations.end());
			float minDur = *std::min_element(
				result.trackDurations.begin(), result.trackDurations.end());

			if (maxDur - minDur > 0.5f)
			{
				result.valid = false;
				result.errors.push_back(
					"Track duration mismatch: shortest="
					+ std::to_string(minDur)
					+ "s, longest="
					+ std::to_string(maxDur) + "s");
			}
			else if (maxDur - minDur > 0.1f)
			{
				result.warnings.push_back(
					"Minor track duration difference: "
					+ std::to_string(maxDur - minDur) + "s");
			}
		}

		return result;
	}

	/// @brief 曲のテキスト全体を検査する
	/// @param songText #track の行と MML、# の指示、#let と #macro を含むテキスト (ChipSong、ModernSongScript と同じ形)
	/// @return 行と列つきの報告 (Error は書いたとおりに鳴らない物、Warning は鳴るが意図と違うかもしれない物)
	[[nodiscard]] static SongValidation validateSong(std::string_view songText)
	{
		return SongLint::check(songText);
	}

	/// @brief 1トラックのMMLの推定再生時間を計算する
	/// @param mml MML文字列
	/// @return 推定再生時間（秒）
	[[nodiscard]] static float estimateDuration(std::string_view mml)
	{
		auto cmds = MmlParser::parse(mml);
		int tempo = 120;
		int defaultLength = 4;
		float totalTime = 0.0f;

		for (const auto& cmd : cmds)
		{
			switch (cmd.type)
			{
			case CommandType::Tempo:
				tempo = std::max(1, cmd.value);
				break;
			case CommandType::Length:
				defaultLength = std::max(1, cmd.value);
				break;
			case CommandType::Note:
			case CommandType::Rest:
			{
				int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				float dur = (4.0f * 60.0f / static_cast<float>(tempo))
					/ static_cast<float>(len);
				if (cmd.dotted) dur *= 1.5f;
				totalTime += dur;
				break;
			}
			default:
				break;
			}
		}
		return totalTime;
	}
};

} // namespace mitiru_mml
