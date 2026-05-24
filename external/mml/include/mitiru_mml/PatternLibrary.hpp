#pragma once
/// @file PatternLibrary.hpp
/// @brief 音楽パターンライブラリ — ドラム・ベース・アルペジオのテンプレート
///
/// @code
/// auto drum = DrumPatterns::get("rock_8beat"); // "BHSHBHSH"
/// auto bass = BassPatterns::generate(chord, "root_fifth", 8); // MML string
/// @endcode

#include <mitiru_mml/MusicTheory.hpp>
#include <string>
#include <string_view>

namespace mitiru_mml
{

/// @brief ドラムパターンライブラリ
/// @details B=BassDrum, S=Snare, H=HiHat, T=Tom, Y=Cymbal, I=RimShot, R=Rest
struct DrumPatterns
{
	/// @brief パターン名からドラムMML文字列を取得する
	/// @param name パターン名
	/// @return ドラムMML文字列
	[[nodiscard]] static std::string get(std::string_view name)
	{
		if (name == "rock_8beat")     return "L8 BHSHBHSH BHSHBHSH";
		if (name == "rock_16beat")    return "L16 BHHSBHHSBHHSBHHS BHHSBHHSBHHSBHHS";
		if (name == "ballad")         return "L8 B4HSH B4HSH";
		if (name == "waltz")          return "L4 BHH BHH BHH BHH";
		if (name == "march")          return "L8 BSHSBSHS BSHSBSHS";
		if (name == "fast_rock")      return "L16 BHSHBHSHBHSHBHSH BHSHBHSHBHSHBYSH";
		if (name == "fill_basic")     return "L16 BTTTSTTTSTTTSSSS";
		if (name == "fill_crash")     return "L16 TTTTSSSSTTTTYYBB";
		if (name == "intro_count")    return "L4 HHHH";
		if (name == "half_time")      return "L4 B2S2 B2S2";
		if (name == "shuffle")        return "L8 BH.SH. BH.SH.";
		if (name == "bossa_nova")     return "L16 BHRHBRHH BHRHBRHH";
		if (name == "silence")        return "L1 R R R R";
		return "L8 BHSHBHSH";
	}
};

/// @brief ベースラインパターン生成
struct BassPatterns
{
	/// @brief コードとパターン名からベースラインMMLを生成する
	/// @param chord コード
	/// @param pattern パターン名
	/// @param octave オクターブ
	/// @param length デフォルト音長
	/// @return MML文字列（1小節分）
	[[nodiscard]] static std::string generate(
		const Chord& chord, std::string_view pattern,
		int octave = 3, int length = 8)
	{
		auto notes = chord.midiNotes(octave);
		if (notes.empty()) return "R1";

		std::string result = "L" + std::to_string(length) + " ";

		if (pattern == "root")
		{
			// ルート音のみ
			result += noteToMml(notes[0]) + "2 ";
		}
		else if (pattern == "root_fifth")
		{
			// ルート→5度
			result += noteToMml(notes[0]) + " ";
			if (notes.size() >= 3) result += noteToMml(notes[2]) + " ";
			else result += noteToMml(notes[0]) + " ";
		}
		else if (pattern == "walking")
		{
			// ウォーキングベース（ルート→3度→5度→経過音）
			result += noteToMml(notes[0]) + " ";
			if (notes.size() >= 2) result += noteToMml(notes[1]) + " ";
			if (notes.size() >= 3) result += noteToMml(notes[2]) + " ";
			result += noteToMml(notes[0] + 10) + " "; // アプローチノート
		}
		else if (pattern == "octave")
		{
			// オクターブジャンプ
			result += noteToMml(notes[0]) + " " + noteToMml(notes[0] + 12) + " ";
		}
		else if (pattern == "pumping")
		{
			// 8分のパンピングベース
			for (int i = 0; i < 4; ++i)
				result += noteToMml(notes[0]) + " ";
		}
		else
		{
			result += noteToMml(notes[0]) + "2 ";
		}

		return result;
	}

private:
	/// @brief MIDIノート番号をMML音名に変換する
	static std::string noteToMml(int midiNote)
	{
		static const char* NAMES[] = {"C","C+","D","D+","E","F","F+","G","G+","A","A+","B"};
		int n = midiNote % 12;
		return NAMES[n];
	}
};

/// @brief アルペジオパターン生成
struct ArpPatterns
{
	/// @brief コードからアルペジオMMLを生成する
	/// @param chord コード
	/// @param pattern パターン
	/// @param octave オクターブ
	/// @param noteLength 音符長さ（16=16分音符）
	/// @return MML文字列
	[[nodiscard]] static std::string generate(
		const Chord& chord, ArpPattern pattern,
		int octave = 4, int noteLength = 16)
	{
		auto notes = chord.arpeggiate(pattern, octave);
		std::string result = "L" + std::to_string(noteLength) + " ";
		for (int n : notes)
		{
			static const char* NAMES[] = {"C","C+","D","D+","E","F","F+","G","G+","A","A+","B"};
			result += NAMES[n % 12];
			result += " ";
		}
		return result;
	}
};

} // namespace mitiru_mml
