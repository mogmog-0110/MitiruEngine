#pragma once

/// @file MmlPhraseGenerator.hpp
/// @brief AIフレーズ自動生成（パターンDB + ジャンル別テンプレート）

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mitiru::audio
{

/// @brief ジャンル
enum class MusicGenre : uint8_t
{
	RPGField, RPGBattle, RPGBoss, RPGTown,
	Puzzle, Action, Horror, Menu,
	Custom,
};

/// @brief フレーズテンプレート
struct PhraseTemplate
{
	std::string name;
	std::string mml;
	MusicGenre genre = MusicGenre::Custom;
	int tempo = 120;
};

/// @brief MMLフレーズ自動生成器
class MmlPhraseGenerator
{
public:
	MmlPhraseGenerator() { initDefaultPhrases(); }

	/// @brief ジャンルに基づいてランダムフレーズを生成する
	[[nodiscard]] std::string generate(MusicGenre genre, int measures = 4, uint32_t seed = 0)
	{
		// seed 未指定 (0) は固定既定で決定論を保つ (caller が明示 seed を渡せば任意列)。
		if (seed == 0) { seed = 0x4D6C5031u; }
		std::mt19937 rng(seed);

		// ジャンルのテンプレートを取得
		std::vector<const PhraseTemplate*> templates;
		for (const auto& t : m_phrases)
		{
			if (t.genre == genre) { templates.push_back(&t); }
		}
		if (templates.empty())
		{
			return generateRandom(rng, measures, 120);
		}

		// テンプレートを組み合わせる
		std::string result;
		const int tempo = templates[0]->tempo;
		result += "T" + std::to_string(tempo) + " ";

		for (int m = 0; m < measures; ++m)
		{
			const auto* tmpl = templates[static_cast<size_t>(rng() % templates.size())];
			result += tmpl->mml + " ";
		}

		return result;
	}

	/// @brief コード進行からメロディを自動生成する
	[[nodiscard]] std::string generateMelody(
		const std::vector<std::string>& chords, int tempo = 120, uint32_t seed = 0)
	{
		// seed 未指定 (0) は固定既定で決定論を保つ。
		if (seed == 0) { seed = 0x4D656C6Fu; }
		std::mt19937 rng(seed);

		std::string result = "T" + std::to_string(tempo) + " O5 L8 ";

		for (const auto& chord : chords)
		{
			// コードに含まれるノートから選択
			auto notes = chordToNotes(chord);
			for (int i = 0; i < 4; ++i) // 4音/コード
			{
				const auto& note = notes[rng() % notes.size()];
				result += note;
				// リズムパターン
				if (rng() % 4 == 0) { result += "4"; }
				else if (rng() % 3 == 0) { result += "16"; }
			}
			result += " ";
		}

		return result;
	}

	/// @brief フレーズテンプレートを追加する
	void addPhrase(PhraseTemplate phrase) { m_phrases.push_back(std::move(phrase)); }

	/// @brief フレーズ数を取得する
	[[nodiscard]] size_t phraseCount() const noexcept { return m_phrases.size(); }

private:
	std::vector<PhraseTemplate> m_phrases;

	void initDefaultPhrases()
	{
		// RPG フィールド
		m_phrases.push_back({"Field A", "L4 CEGA L8 GFED", MusicGenre::RPGField, 100});
		m_phrases.push_back({"Field B", "L4 EFGA L8 BAGF", MusicGenre::RPGField, 100});
		m_phrases.push_back({"Field C", "L8 CDECDECD L4 EG", MusicGenre::RPGField, 100});

		// RPG バトル
		m_phrases.push_back({"Battle A", "L16 CEGCEGCE L8 G>C<B", MusicGenre::RPGBattle, 160});
		m_phrases.push_back({"Battle B", "L8 C+D+F+G+ L16 A+G+F+D+", MusicGenre::RPGBattle, 160});
		m_phrases.push_back({"Battle C", "L16 EDEDEDED L8 C<B>C4", MusicGenre::RPGBattle, 160});

		// RPG ボス
		m_phrases.push_back({"Boss A", "L8 O3 CDCD L16 EFGAB>C<", MusicGenre::RPGBoss, 180});
		m_phrases.push_back({"Boss B", "L16 O4 CDCDEFEF L8 GAB>C", MusicGenre::RPGBoss, 180});

		// RPG 街
		m_phrases.push_back({"Town A", "L4 CEG L8 AGFE L4 C2", MusicGenre::RPGTown, 90});
		m_phrases.push_back({"Town B", "L8 GECE GECE L4 C2", MusicGenre::RPGTown, 90});

		// パズル
		m_phrases.push_back({"Puzzle A", "L8 CEC DFD L4 EGE4", MusicGenre::Puzzle, 110});
		m_phrases.push_back({"Puzzle B", "L16 CDEFGAB> L8 CDEC", MusicGenre::Puzzle, 110});

		// アクション
		m_phrases.push_back({"Action A", "L16 CDCD EFEF L8 GAB>C", MusicGenre::Action, 140});

		// ホラー
		m_phrases.push_back({"Horror A", "L2 C L8 R C+R L4 D+ L2 R", MusicGenre::Horror, 60});

		// メニュー
		m_phrases.push_back({"Menu A", "L4 EGE L8 DC L4 E2", MusicGenre::Menu, 80});
	}

	[[nodiscard]] static std::string generateRandom(std::mt19937& rng, int measures, int tempo)
	{
		static const char* notes[] = {"C", "D", "E", "F", "G", "A", "B"};
		static const char* lengths[] = {"4", "8", "16"};

		std::string result = "T" + std::to_string(tempo) + " O4 ";
		for (int m = 0; m < measures; ++m)
		{
			for (int n = 0; n < 8; ++n)
			{
				result += notes[rng() % 7];
				result += lengths[rng() % 3];
			}
			result += " ";
		}
		return result;
	}

	[[nodiscard]] static std::vector<std::string> chordToNotes(const std::string& chord)
	{
		if (chord == "C" || chord == "CM") return {"C", "E", "G"};
		if (chord == "Dm") return {"D", "F", "A"};
		if (chord == "Em") return {"E", "G", "B"};
		if (chord == "F" || chord == "FM") return {"F", "A", ">C"};
		if (chord == "G" || chord == "GM") return {"G", "B", ">D"};
		if (chord == "Am") return {"A", ">C", ">E"};
		if (chord == "Bdim") return {"B", ">D", ">F"};
		return {"C", "E", "G"}; // デフォルト
	}
};

} // namespace mitiru::audio
