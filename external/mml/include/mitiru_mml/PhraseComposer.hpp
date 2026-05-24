#pragma once
/// @file PhraseComposer.hpp
/// @brief フレーズベース作曲エンジン
/// @details PhraseDictionaryから旋律断片を選択・変奏・配列して
///          完全な楽曲を自動構成する。

#include <mitiru_mml/PhraseDictionary.hpp>
#include <mitiru_mml/MusicTheory.hpp>
#include <mitiru_mml/MotifEngine.hpp>
#include <mitiru_mml/PatternLibrary.hpp>
#include <mitiru_mml/SongBuilder.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief 作曲レシピ（AIからの入力）
struct CompositionRecipe
{
	std::string mood = "bright";           ///< 気分: bright/dark/heroic/gentle/tense/epic/mysterious
	Key key = Key::C;                       ///< 調
	ScaleType scaleType = ScaleType::Major; ///< スケール
	int bpm = 120;                          ///< テンポ
	std::string progression = "I IV V I";   ///< コード進行（ローマ数字）
	int melodyPreset = 0;                   ///< メロディFMプリセット
	int harmonyPreset = 3;                  ///< ハーモニーFMプリセット
	int bassPreset = 6;                     ///< ベースFMプリセット
	std::string drumPattern = "rock_8beat"; ///< ドラムパターン
	std::string bassPattern = "root_fifth"; ///< ベースパターン
	bool useSsg = true;                     ///< SSGアルペジオ
	uint32_t seed = 42;                     ///< 乱数シード（再現性）
};

/// @brief フレーズベース作曲エンジン
class PhraseComposer
{
public:
	/// @brief レシピから楽曲を作曲する
	/// @param recipe 作曲レシピ
	/// @return 完成した楽曲データ（MMLトラック群）
	[[nodiscard]] static SongData compose(const CompositionRecipe& recipe)
	{
		Scale scale(recipe.key, recipe.scaleType);
		auto chords = ChordProgression::parse(recipe.progression, recipe.key, recipe.scaleType);
		if (chords.empty())
		{
			chords = ChordProgression::preset("pop", recipe.key, recipe.scaleType);
		}

		// 気分に合うフレーズを辞書から検索する
		auto openings = PhraseDictionary::query(recipe.mood, "opening");
		auto mains = PhraseDictionary::query(recipe.mood, "main");
		auto climaxes = PhraseDictionary::query(recipe.mood, "climax");
		auto closings = PhraseDictionary::query(recipe.mood, "closing");
		auto bridges = PhraseDictionary::query(recipe.mood, "bridge");

		// フォールバック: 見つからなければ bright を使う
		if (openings.empty()) openings = PhraseDictionary::query("bright", "opening");
		if (mains.empty()) mains = PhraseDictionary::query("bright", "main");
		if (climaxes.empty()) climaxes = PhraseDictionary::query("bright", "climax");
		if (closings.empty()) closings = PhraseDictionary::query("bright", "closing");
		if (bridges.empty()) bridges = mains;

		// シードで再現可能な選択
		uint32_t rng = recipe.seed;
		auto pick = [&](const std::vector<const Phrase*>& list) -> const Phrase&
		{
			rng = rng * 1103515245u + 12345u;
			return *list[(rng >> 16) % list.size()];
		};

		// ── 楽曲構成: Intro -> A -> A' -> B -> C(climax) -> A'' -> Ending ──

		const auto& introPhrase = pick(openings);
		const auto& mainPhrase = pick(mains);
		const auto& bridgePhrase = pick(bridges);
		const auto& climaxPhrase = pick(climaxes);
		const auto& closingPhrase = pick(closings);

		// メインフレーズの変奏を生成する
		Motif mainMotif{mainPhrase.degrees, mainPhrase.lengths, {}};
		Motif mainVar1 = MotifEngine::transpose(mainMotif, 2);      // 2度上に移高
		Motif mainVar2 = MotifEngine::varyEnding(mainMotif, 1);     // 終止をトニックに変更

		// ── メロディ構築 ──
		std::vector<int> melodyDeg;
		std::vector<int> melodyLen;
		std::vector<int> melodyVel;

		// Intro（オープニングフレーズ）
		appendPhrase(melodyDeg, melodyLen, melodyVel, introPhrase);

		// Aセクション（メインフレーズ）
		appendPhrase(melodyDeg, melodyLen, melodyVel, mainPhrase);

		// A'（移高変奏）
		appendMotif(melodyDeg, melodyLen, melodyVel, mainVar1, 12);

		// Bセクション（ブリッジフレーズ）
		appendPhrase(melodyDeg, melodyLen, melodyVel, bridgePhrase);

		// Cセクション（クライマックス）
		appendPhrase(melodyDeg, melodyLen, melodyVel, climaxPhrase);

		// A''（終止変奏）
		appendMotif(melodyDeg, melodyLen, melodyVel, mainVar2, 12);

		// Ending（終止フレーズ）
		appendPhrase(melodyDeg, melodyLen, melodyVel, closingPhrase);

		// ── MML変換 ──
		std::string melodyMml = "T" + std::to_string(recipe.bpm) + " O5 ";
		melodyMml += degreesToMml(melodyDeg, melodyLen, melodyVel, scale);

		// ── カウンターメロディ（3度上並行） ──
		std::vector<int> counterDeg;
		std::vector<int> counterLen;
		for (size_t i = 0; i < melodyDeg.size(); ++i)
		{
			counterDeg.push_back(melodyDeg[i] + 2); // 3度上
			counterLen.push_back(melodyLen[i]);
		}
		std::string counterMml = "T" + std::to_string(recipe.bpm) + " O4 ";
		counterMml += degreesToMml(counterDeg, counterLen, {}, scale);

		// ── ベースライン ──
		std::string bassMml = "T" + std::to_string(recipe.bpm) + " O3 ";
		const int totalPhrases = 7; // intro + A + A' + B + C + A'' + ending
		for (int i = 0; i < totalPhrases; ++i)
		{
			const auto& chord = chords[static_cast<size_t>(i) % chords.size()];
			bassMml += BassPatterns::generate(chord, recipe.bassPattern, 3, 8);
			bassMml += " ";
		}

		// ── SSGアルペジオ ──
		std::string ssgMml;
		if (recipe.useSsg)
		{
			ssgMml = "T" + std::to_string(recipe.bpm) + " O5 ";
			for (int i = 0; i < totalPhrases; ++i)
			{
				const auto& chord = chords[static_cast<size_t>(i) % chords.size()];
				ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
				ssgMml += ArpPatterns::generate(chord, ArpPattern::Up, 5, 16);
				ssgMml += " ";
			}
		}

		// ── ドラム ──
		std::string drumMml = "T" + std::to_string(recipe.bpm) + " ";
		std::string drumPat = DrumPatterns::get(recipe.drumPattern);
		for (int i = 0; i < totalPhrases; ++i)
		{
			drumMml += drumPat + " ";
		}

		// ── SongData構築 ──
		SongData data;
		data.tempo = recipe.bpm;

		data.tracks.push_back({"Melody", melodyMml, "FM", recipe.melodyPreset});
		data.tracks.push_back({"Harmony", counterMml, "FM", recipe.harmonyPreset});
		data.tracks.push_back({"Bass", bassMml, "FM", recipe.bassPreset});
		if (recipe.useSsg)
		{
			data.tracks.push_back({"SSG Arp", ssgMml, "SSG", 0});
		}
		data.tracks.push_back({"Drums", drumMml, "RHYTHM", 0});

		return data;
	}

private:
	/// @brief フレーズの度数・音長・音量をメロディバッファに追加する
	static void appendPhrase(
		std::vector<int>& deg, std::vector<int>& len, std::vector<int>& vel,
		const Phrase& phrase)
	{
		deg.insert(deg.end(), phrase.degrees.begin(), phrase.degrees.end());
		len.insert(len.end(), phrase.lengths.begin(), phrase.lengths.end());
		if (!phrase.velocities.empty())
		{
			vel.insert(vel.end(), phrase.velocities.begin(), phrase.velocities.end());
		}
		else
		{
			vel.resize(deg.size(), 12);
		}
	}

	/// @brief モチーフの度数・音長をメロディバッファに追加する
	static void appendMotif(
		std::vector<int>& deg, std::vector<int>& len, std::vector<int>& vel,
		const Motif& motif, int defaultVel = 12)
	{
		deg.insert(deg.end(), motif.degrees.begin(), motif.degrees.end());
		len.insert(len.end(), motif.lengths.begin(), motif.lengths.end());
		vel.resize(deg.size(), defaultVel);
	}

	/// @brief スケール度数列+音長列+音量列をMML文字列に変換する
	static std::string degreesToMml(
		const std::vector<int>& degrees,
		const std::vector<int>& lengths,
		const std::vector<int>& velocities,
		const Scale& scale)
	{
		static const char* NOTE_NAMES[] = {
			"C", "C+", "D", "D+", "E", "F",
			"F+", "G", "G+", "A", "A+", "B"
		};

		auto scaleIntervals = scale.intervals();
		const int rootNote = static_cast<int>(scale.root());

		std::string result;
		int prevVel = -1;

		for (size_t i = 0; i < degrees.size(); ++i)
		{
			// 音量変更
			if (!velocities.empty() && i < velocities.size() && velocities[i] != prevVel)
			{
				result += "v" + std::to_string(velocities[i]) + " ";
				prevVel = velocities[i];
			}

			// 度数→MIDIノート変換
			int deg = degrees[i];
			int octaveShift = 0;
			while (deg > static_cast<int>(scaleIntervals.size()))
			{
				deg -= static_cast<int>(scaleIntervals.size());
				octaveShift++;
			}
			while (deg < 1)
			{
				deg += static_cast<int>(scaleIntervals.size());
				octaveShift--;
			}

			int semitone = scaleIntervals[static_cast<size_t>(deg - 1)];
			int noteIdx = (rootNote + semitone) % 12;

			// オクターブ移動
			for (int j = 0; j < octaveShift; ++j) result += ">";
			for (int j = 0; j < -octaveShift; ++j) result += "<";

			// ノート名 + 音長
			result += NOTE_NAMES[noteIdx];
			if (i < lengths.size() && lengths[i] > 0)
			{
				result += std::to_string(lengths[i]);
			}
			result += " ";

			// オクターブ戻す
			for (int j = 0; j < octaveShift; ++j) result += "<";
			for (int j = 0; j < -octaveShift; ++j) result += ">";
		}

		return result;
	}
};

} // namespace mitiru_mml
