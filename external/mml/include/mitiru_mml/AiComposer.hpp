#pragma once
/// @file AiComposer.hpp
/// @brief AI作曲支援フレームワーク
/// @details 音楽理論に基づいた作曲支援ツール。
///          メロディ生成規則、対位法、オーケストレーションの
///          ガイドラインをプログラマティックに提供する。
///
/// @code
/// using namespace mitiru_mml;
/// AiComposer::CompositionPlan plan;
/// plan.genre = "rpg_town";
/// plan.key = Key::C;
/// plan.scaleType = ScaleType::Major;
/// plan.bpm = 120;
/// plan.melodyContour = AiComposer::Contour::Arch;
/// plan.rhythmPattern = "ballad";
/// plan.progression = "I IV V I";
/// auto data = AiComposer::compose(plan);
/// @endcode

#include <mitiru_mml/MusicTheory.hpp>
#include <mitiru_mml/PatternLibrary.hpp>
#include <mitiru_mml/SongBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief AI作曲支援フレームワーク
/// @details メロディ生成規則・対位法・オーケストレーションを
///          プログラマティックに提供する静的ユーティリティクラス。
class AiComposer
{
public:
	// ── メロディック・コントゥール（旋律線の形状） ──

	/// @brief 旋律線形状
	enum class Contour
	{
		Ascending,   ///< 上行
		Descending,  ///< 下行
		Arch,        ///< アーチ形（上行→下行）
		Valley,      ///< 谷形（下行→上行）
		Wave,        ///< 波形（正弦波的）
		Static       ///< 保続音（同度反復）
	};

	/// @brief コントゥールに基づいてスケール度数列を生成する
	/// @param contour 旋律線形状
	/// @param length ノート数
	/// @param range 音域幅（度数）
	/// @param startDegree 開始度数
	/// @return スケール度数列
	[[nodiscard]] static std::vector<int> generateContour(
		Contour contour, int length, int range = 8, int startDegree = 1)
	{
		std::vector<int> degrees;
		degrees.reserve(static_cast<std::size_t>(length));

		const int safeLen = std::max(2, length);
		const int safeRange = std::max(1, range);

		switch (contour)
		{
		case Contour::Ascending:
			for (int i = 0; i < safeLen; ++i)
			{
				const float t = static_cast<float>(i)
					/ static_cast<float>(safeLen - 1);
				degrees.push_back(startDegree
					+ static_cast<int>(t * static_cast<float>(safeRange)));
			}
			break;

		case Contour::Descending:
			for (int i = 0; i < safeLen; ++i)
			{
				const float t = static_cast<float>(i)
					/ static_cast<float>(safeLen - 1);
				degrees.push_back(startDegree + safeRange
					- static_cast<int>(t * static_cast<float>(safeRange)));
			}
			break;

		case Contour::Arch:
			for (int i = 0; i < safeLen; ++i)
			{
				const float t = static_cast<float>(i)
					/ static_cast<float>(safeLen - 1);
				// 放物線: -4(t-0.5)^2 + 1 → ピークは t=0.5 で 1.0
				const float curve = -4.0f * (t - 0.5f) * (t - 0.5f) + 1.0f;
				degrees.push_back(startDegree
					+ static_cast<int>(curve * static_cast<float>(safeRange)));
			}
			break;

		case Contour::Valley:
			for (int i = 0; i < safeLen; ++i)
			{
				const float t = static_cast<float>(i)
					/ static_cast<float>(safeLen - 1);
				// 逆放物線: 4(t-0.5)^2 → 谷は t=0.5 で 0.0
				const float curve = 4.0f * (t - 0.5f) * (t - 0.5f);
				degrees.push_back(startDegree
					+ static_cast<int>(curve * static_cast<float>(safeRange)));
			}
			break;

		case Contour::Wave:
			for (int i = 0; i < safeLen; ++i)
			{
				const float t = static_cast<float>(i)
					/ static_cast<float>(safeLen - 1);
				const float wave = std::sin(t * 3.14159f * 2.0f) * 0.5f + 0.5f;
				degrees.push_back(startDegree
					+ static_cast<int>(wave * static_cast<float>(safeRange)));
			}
			break;

		case Contour::Static:
			for (int i = 0; i < safeLen; ++i)
			{
				degrees.push_back(startDegree);
			}
			break;
		}

		return degrees;
	}

	// ── リズムパターン適用 ──

	/// @brief メロディにリズムパターンを適用する
	/// @param degrees スケール度数列
	/// @param rhythmPattern リズムパターン名
	/// @return {度数列, 音長列} ペア
	[[nodiscard]] static std::pair<std::vector<int>, std::vector<int>> applyRhythm(
		const std::vector<int>& degrees, const std::string& rhythmPattern)
	{
		auto pattern = getRhythmPattern(rhythmPattern);

		// 度数列をパターン長に合わせる
		std::vector<int> outDeg;
		std::vector<int> outLen;
		const std::size_t patLen = pattern.size();
		const std::size_t degLen = degrees.size();

		for (std::size_t i = 0; i < std::max(patLen, degLen); ++i)
		{
			outDeg.push_back(degrees[i % degLen]);
			outLen.push_back(pattern[i % patLen]);
		}

		return {outDeg, outLen};
	}

	// ── 対位法（カウンターメロディ生成） ──

	/// @brief メインメロディに対するカウンターメロディを生成する
	/// @param melody メインメロディの度数列
	/// @param style 対位法スタイル
	/// @return カウンターメロディの度数列
	[[nodiscard]] static std::vector<int> generateCounter(
		const std::vector<int>& melody, const std::string& style)
	{
		std::vector<int> counter;
		counter.reserve(melody.size());

		if (style == "parallel_third")
		{
			// 3度上で並行
			for (int deg : melody)
			{
				counter.push_back(deg + 2);
			}
		}
		else if (style == "parallel_sixth")
		{
			// 6度下で並行
			for (int deg : melody)
			{
				counter.push_back(deg - 5);
			}
		}
		else if (style == "contrary")
		{
			// 反行（メロディが上がれば下がる）
			if (melody.size() < 2)
			{
				return melody;
			}
			counter.push_back(melody[0]);
			for (std::size_t i = 1; i < melody.size(); ++i)
			{
				const int delta = melody[i] - melody[i - 1];
				counter.push_back(counter.back() - delta);
			}
		}
		else if (style == "oblique")
		{
			// 保続音（最初の音を持続）
			const int pedal = melody.empty() ? 1 : melody[0];
			for (std::size_t i = 0; i < melody.size(); ++i)
			{
				counter.push_back(pedal);
			}
		}
		else
		{
			// デフォルト: 3度上
			for (int deg : melody)
			{
				counter.push_back(deg + 2);
			}
		}

		return counter;
	}

	// ── ベースライン生成 ──

	/// @brief コード進行からベースラインMMLを生成する
	/// @param chords コード列
	/// @param scale スケール
	/// @param pattern パターン名
	/// @param octave オクターブ
	/// @return MML文字列
	[[nodiscard]] static std::string generateBassline(
		const std::vector<Chord>& chords, const Scale& scale,
		const std::string& pattern, int octave = 3)
	{
		static_cast<void>(scale);
		std::string mml;

		for (const auto& chord : chords)
		{
			mml += BassPatterns::generate(chord, pattern, octave, 8);
			mml += " ";
		}

		return mml;
	}

	// ── オーケストレーション ──

	/// @brief ジャンルに応じた楽器配置提案
	struct Orchestration
	{
		int melodyPreset = 0;       ///< メロディ用FMプリセット
		int harmonyPreset = 3;      ///< ハーモニー用FMプリセット
		int bassPreset = 6;         ///< ベース用FMプリセット
		int counterPreset = -1;     ///< カウンター用FMプリセット（-1=なし）
		bool useSsg = true;         ///< SSG使用
		std::string drumPattern;    ///< ドラムパターン
		std::string bassPattern;    ///< ベースパターン
	};

	/// @brief ジャンルに応じた楽器配置を提案する
	/// @param genre ジャンル名
	/// @return オーケストレーション提案
	[[nodiscard]] static Orchestration suggestOrchestration(const std::string& genre)
	{
		if (genre == "rpg_battle")
			return {2, 9, 6, -1, true, "fast_rock", "pumping"};
		if (genre == "rpg_boss")
			return {2, 11, 6, -1, true, "fast_rock", "pumping"};
		if (genre == "rpg_town")
			return {0, 3, 6, 7, true, "rock_8beat", "walking"};
		if (genre == "rpg_field")
			return {0, 4, 6, 7, true, "rock_8beat", "root_fifth"};
		if (genre == "rpg_dungeon")
			return {1, 7, 6, -1, true, "half_time", "root"};
		if (genre == "vn_sad")
			return {5, 3, 6, -1, true, "ballad", "root"};
		if (genre == "vn_romantic")
			return {5, 3, 6, 7, true, "ballad", "root_fifth"};
		if (genre == "vn_tension")
			return {9, 3, 6, -1, true, "rock_8beat", "root"};
		if (genre == "title_screen")
			return {2, 3, 6, 7, true, "rock_8beat", "root_fifth"};
		if (genre == "ending")
			return {5, 3, 6, 7, true, "ballad", "root_fifth"};
		// デフォルト
		return {0, 3, 6, -1, true, "rock_8beat", "root_fifth"};
	}

	// ── フレーズ構造 ──

	/// @brief 問い（前半）フレーズに対する答え（後半）を生成する
	/// @param questionDegrees 問いの度数列
	/// @param scale スケール（解決先の参照用）
	/// @return 答えの度数列（最後がトニックに解決する）
	[[nodiscard]] static std::vector<int> generateAnswer(
		const std::vector<int>& questionDegrees, const Scale& scale)
	{
		static_cast<void>(scale);
		auto answer = questionDegrees;

		if (answer.size() >= 2)
		{
			// 前半を少し変化させる
			for (std::size_t i = 0; i < answer.size() / 2; ++i)
			{
				answer[i] = questionDegrees[i] + 2;
			}
			// 最後から2番目は導音（7度）で緊張を作る
			answer[answer.size() - 2] = 7;
			// 最後の音は必ず1度（トニック）に解決する
			answer[answer.size() - 1] = 1;
		}
		else if (answer.size() == 1)
		{
			answer[0] = 1;
		}

		return answer;
	}

	/// @brief 4小節フレーズを「問い→答え」パターンで生成する
	/// @param scale スケール
	/// @param contour メロディック・コントゥール
	/// @param rhythm リズムパターン
	/// @param octave 基準オクターブ
	/// @return 問い+答えのMML文字列
	[[nodiscard]] static std::string generatePhrase(
		const Scale& scale, Contour contour,
		const std::string& rhythm, int octave = 5)
	{
		auto question = generateContour(contour, 8, 7, 1);
		auto [qDeg, qLen] = applyRhythm(question, rhythm);

		auto aDeg = generateAnswer(qDeg, scale);

		// 問い + 答えを結合する
		std::vector<int> fullDeg;
		std::vector<int> fullLen;
		fullDeg.insert(fullDeg.end(), qDeg.begin(), qDeg.end());
		fullLen.insert(fullLen.end(), qLen.begin(), qLen.end());
		fullDeg.insert(fullDeg.end(), aDeg.begin(), aDeg.end());
		fullLen.insert(fullLen.end(), qLen.begin(), qLen.end());

		return degreesToMml(fullDeg, fullLen, scale, octave, 120);
	}

	// ── 完全な楽曲構成 ──

	/// @brief 楽曲全体を構成する高レベル設定
	struct CompositionPlan
	{
		std::string genre = "default";          ///< ジャンル名
		Key key = Key::C;                       ///< 調
		ScaleType scaleType = ScaleType::Major;  ///< スケール種別
		int bpm = 120;                          ///< テンポ
		std::string progression = "I IV V I";    ///< コード進行（ローマ数字）
		Contour melodyContour = Contour::Arch;   ///< メロディ形状
		std::string rhythmPattern = "ballad";    ///< リズムパターン
		std::string bassPattern;                 ///< ベースパターン（空=自動）
		std::string counterStyle;                ///< 対位法スタイル（空=なし）
		bool useSsg = true;                      ///< SSG使用
		bool useDrums = true;                    ///< ドラム使用
		int repeatCount = 2;                     ///< 繰り返し回数
	};

	/// @brief CompositionPlanからSongDataを生成する
	/// @param plan 作曲計画
	/// @return 生成されたSongData
	[[nodiscard]] static SongData compose(const CompositionPlan& plan)
	{
		Scale scale(plan.key, plan.scaleType);
		auto chords = ChordProgression::parse(plan.progression, plan.key, plan.scaleType);
		auto orch = suggestOrchestration(plan.genre);

		// プランの設定でオーケストレーションを上書きする
		if (!plan.bassPattern.empty()) orch.bassPattern = plan.bassPattern;

		// メロディ生成: コントゥール + リズム
		auto contourDegrees = generateContour(plan.melodyContour, 16, 8, 1);
		auto [melodyDeg, melodyLen] = applyRhythm(contourDegrees, plan.rhythmPattern);

		// 答えフレーズを生成する
		auto answerDeg = generateAnswer(melodyDeg, scale);

		// フルメロディ = 問い + 答え × 繰返し
		std::vector<int> fullMelody;
		std::vector<int> fullLengths;
		for (int r = 0; r < plan.repeatCount; ++r)
		{
			fullMelody.insert(fullMelody.end(), melodyDeg.begin(), melodyDeg.end());
			fullLengths.insert(fullLengths.end(), melodyLen.begin(), melodyLen.end());
			fullMelody.insert(fullMelody.end(), answerDeg.begin(), answerDeg.end());
			fullLengths.insert(fullLengths.end(), melodyLen.begin(), melodyLen.end());
		}

		SongData data;
		data.tempo = plan.bpm;

		// --- メロディトラック ---
		{
			BuiltTrack melody;
			melody.label = "Melody";
			melody.trackType = "FM";
			melody.fmPreset = orch.melodyPreset;
			melody.mml = degreesToMml(fullMelody, fullLengths, scale, 5, plan.bpm);
			data.tracks.push_back(std::move(melody));
		}

		// --- カウンターメロディトラック ---
		if (!plan.counterStyle.empty() && orch.counterPreset >= 0)
		{
			auto counterDeg = generateCounter(fullMelody, plan.counterStyle);
			BuiltTrack counter;
			counter.label = "Counter";
			counter.trackType = "FM";
			counter.fmPreset = orch.counterPreset;
			counter.mml = degreesToMml(counterDeg, fullLengths, scale, 4, plan.bpm);
			data.tracks.push_back(std::move(counter));
		}

		// --- ベーストラック ---
		if (!chords.empty())
		{
			BuiltTrack bass;
			bass.label = "Bass";
			bass.trackType = "FM";
			bass.fmPreset = orch.bassPreset;
			bass.mml = "T" + std::to_string(plan.bpm) + " O3 "
				+ generateBassline(chords, scale, orch.bassPattern, 3);
			data.tracks.push_back(std::move(bass));
		}

		// --- SSGアルペジオトラック ---
		if (plan.useSsg && !chords.empty())
		{
			BuiltTrack ssgTrack;
			ssgTrack.label = "SSG Arp";
			ssgTrack.trackType = "SSG";
			std::string ssgMml = "T" + std::to_string(plan.bpm) + " O5 ";
			for (const auto& chord : chords)
			{
				ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
				ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
			}
			// 繰返し分
			std::string baseSsg = ssgMml;
			for (int r = 1; r < plan.repeatCount; ++r)
			{
				for (const auto& chord : chords)
				{
					ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
					ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
				}
			}
			ssgTrack.mml = ssgMml;
			data.tracks.push_back(std::move(ssgTrack));
		}

		// --- ドラムトラック ---
		if (plan.useDrums)
		{
			BuiltTrack drums;
			drums.label = "Drums";
			drums.trackType = "RHYTHM";
			std::string drumMml = "T" + std::to_string(plan.bpm) + " ";
			const auto drumPat = DrumPatterns::get(orch.drumPattern);
			// コード数 × 繰返し分のドラムパターン
			const int totalBars = std::max(1, static_cast<int>(chords.size()))
				* plan.repeatCount * 2;
			for (int b = 0; b < totalBars; ++b)
			{
				drumMml += drumPat + " ";
			}
			drums.mml = drumMml;
			data.tracks.push_back(std::move(drums));
		}

		return data;
	}

	// ── MML変換ユーティリティ ──

	/// @brief 度数列+音長列をMML文字列に変換する
	/// @param degrees スケール度数列
	/// @param lengths 音長列
	/// @param scale スケール
	/// @param octave 基準オクターブ
	/// @param tempo テンポ
	/// @return MML文字列
	[[nodiscard]] static std::string degreesToMml(
		const std::vector<int>& degrees,
		const std::vector<int>& lengths,
		const Scale& scale,
		int octave = 5,
		int tempo = 120)
	{
		static const char* NOTE_NAMES[] = {
			"C", "C+", "D", "D+", "E", "F",
			"F+", "G", "G+", "A", "A+", "B"
		};

		const auto ivs = scale.intervals();
		const int scaleSize = static_cast<int>(ivs.size());
		const int rootSemitone = static_cast<int>(scale.root());

		std::string mml = "T" + std::to_string(tempo) + " O"
			+ std::to_string(octave) + " ";

		int currentOctave = octave;

		for (std::size_t i = 0; i < degrees.size(); ++i)
		{
			const int len = (i < lengths.size()) ? lengths[i] : 4;
			const int degree = degrees[i];

			// 度数からMIDIノート番号を計算する
			int idx = ((degree - 1) % scaleSize);
			if (idx < 0) idx += scaleSize;
			int octShift = 0;
			if (degree > 0)
			{
				octShift = (degree - 1) / scaleSize;
			}
			else
			{
				octShift = (degree - scaleSize) / scaleSize;
			}

			const int semitone = (rootSemitone
				+ ivs[static_cast<std::size_t>(idx)]) % 12;
			const int noteOctave = octave + octShift;

			// オクターブ変更
			if (noteOctave != currentOctave)
			{
				if (noteOctave > currentOctave)
				{
					for (int o = 0; o < noteOctave - currentOctave; ++o)
						mml += ">";
				}
				else
				{
					for (int o = 0; o < currentOctave - noteOctave; ++o)
						mml += "<";
				}
				currentOctave = noteOctave;
			}

			mml += NOTE_NAMES[semitone];
			mml += std::to_string(len);
			mml += " ";
		}

		return mml;
	}

private:
	/// @brief リズムパターン名から音長列を取得する
	/// @param name パターン名
	/// @return 音長列（MMLのL値）
	[[nodiscard]] static std::vector<int> getRhythmPattern(const std::string& name)
	{
		if (name == "steady_8th")
			return {8, 8, 8, 8, 8, 8, 8, 8};
		if (name == "syncopated")
			return {8, 4, 8, 8, 4, 8};
		if (name == "ballad")
			return {4, 4, 2, 4, 4, 2};
		if (name == "running_16th")
			return {16, 16, 16, 16, 8, 16, 16, 16, 16, 8};
		if (name == "heroic")
			return {4, 8, 8, 2, 4, 8, 8, 2};
		if (name == "waltz")
			return {4, 4, 4};
		// デフォルト: 8分音符均等
		return {8, 8, 8, 8, 8, 8, 8, 8};
	}
};

} // namespace mitiru_mml
