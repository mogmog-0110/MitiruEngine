#pragma once
/// @file MusicTheory.hpp
/// @brief 音楽理論エンジン — スケール・コード・進行の計算
///
/// @code
/// using namespace mitiru_mml;
/// Scale scale(Key::C, ScaleType::NaturalMinor);
/// auto chord = scale.chord(1);  // Cm (I度)
/// auto notes = chord.notes();   // {0, 3, 7} (C, Eb, G)
/// auto prog = ChordProgression::parse("Im IVm V7 Im", Key::A, ScaleType::NaturalMinor);
/// @endcode

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief 音名（C=0, C#=1, ... B=11）
enum class Key : int
{
	C = 0, Cs = 1, D = 2, Ds = 3, E = 4, F = 5,
	Fs = 6, G = 7, Gs = 8, A = 9, As = 10, B = 11
};

/// @brief スケール種別
enum class ScaleType
{
	Major,           ///< メジャー（長調）
	NaturalMinor,    ///< ナチュラルマイナー（自然的短音階）
	HarmonicMinor,   ///< ハーモニックマイナー
	MelodicMinor,    ///< メロディックマイナー（上行）
	Pentatonic,      ///< ペンタトニック（五音音階）
	MinorPentatonic, ///< マイナーペンタトニック
	Blues,           ///< ブルーススケール
	Dorian,          ///< ドリアン
	Mixolydian,      ///< ミクソリディアン
	Phrygian,        ///< フリジアン
	WholeTone        ///< ホールトーン
};

/// @brief コード種別
enum class ChordType
{
	Major,      ///< メジャー (1-3-5)
	Minor,      ///< マイナー (1-b3-5)
	Dim,        ///< ディミニッシュ (1-b3-b5)
	Aug,        ///< オーギュメント (1-3-#5)
	Sus4,       ///< サスフォー (1-4-5)
	Dom7,       ///< ドミナント7th (1-3-5-b7)
	Maj7,       ///< メジャー7th (1-3-5-7)
	Min7,       ///< マイナー7th (1-b3-5-b7)
	Dim7        ///< ディミニッシュ7th (1-b3-b5-bb7)
};

/// @brief アルペジオパターン
enum class ArpPattern
{
	Up,         ///< 上昇 (C-E-G)
	Down,       ///< 下降 (G-E-C)
	UpDown,     ///< 上昇→下降 (C-E-G-E)
	Random,     ///< ランダム順
	Alberti     ///< アルベルティバス (C-G-E-G)
};

/// @brief コード（和音）前方宣言
struct Chord;

/// @brief スケール（音階）
class Scale
{
public:
	Scale(Key root = Key::C, ScaleType type = ScaleType::Major) noexcept
		: m_root(root), m_type(type) {}

	/// @brief スケール内の音程（半音）リストを返す
	[[nodiscard]] std::vector<int> intervals() const
	{
		switch (m_type)
		{
		case ScaleType::Major:           return {0,2,4,5,7,9,11};
		case ScaleType::NaturalMinor:    return {0,2,3,5,7,8,10};
		case ScaleType::HarmonicMinor:   return {0,2,3,5,7,8,11};
		case ScaleType::MelodicMinor:    return {0,2,3,5,7,9,11};
		case ScaleType::Pentatonic:      return {0,2,4,7,9};
		case ScaleType::MinorPentatonic: return {0,3,5,7,10};
		case ScaleType::Blues:           return {0,3,5,6,7,10};
		case ScaleType::Dorian:          return {0,2,3,5,7,9,10};
		case ScaleType::Mixolydian:      return {0,2,4,5,7,9,10};
		case ScaleType::Phrygian:        return {0,1,3,5,7,8,10};
		case ScaleType::WholeTone:       return {0,2,4,6,8,10};
		}
		return {0,2,4,5,7,9,11};
	}

	/// @brief 指定オクターブでのMIDIノート番号リストを返す
	[[nodiscard]] std::vector<int> midiNotes(int octave = 4) const
	{
		const int base = (octave + 1) * 12 + static_cast<int>(m_root);
		auto ivs = intervals();
		std::vector<int> result;
		for (int iv : ivs) result.push_back(base + iv);
		return result;
	}

	/// @brief 度数からコードを生成する（1=I度, 2=II度, ...）
	[[nodiscard]] Chord chord(int degree) const;

	/// @brief スケールの度数のMIDIノートを返す
	[[nodiscard]] int degreeToNote(int degree, int octave = 4) const
	{
		auto ivs = intervals();
		int idx = (degree - 1) % static_cast<int>(ivs.size());
		if (idx < 0) idx += static_cast<int>(ivs.size());
		int octShift = (degree - 1) / static_cast<int>(ivs.size());
		return (octave + 1) * 12 + static_cast<int>(m_root) + ivs[idx] + octShift * 12;
	}

	[[nodiscard]] Key root() const noexcept { return m_root; }
	[[nodiscard]] ScaleType type() const noexcept { return m_type; }

private:
	Key m_root;
	ScaleType m_type;
};

/// @brief コード（和音）
struct Chord
{
	Key root = Key::C;
	ChordType type = ChordType::Major;

	/// @brief コード構成音の半音オフセットを返す
	[[nodiscard]] std::vector<int> offsets() const
	{
		switch (type)
		{
		case ChordType::Major:  return {0, 4, 7};
		case ChordType::Minor:  return {0, 3, 7};
		case ChordType::Dim:    return {0, 3, 6};
		case ChordType::Aug:    return {0, 4, 8};
		case ChordType::Sus4:   return {0, 5, 7};
		case ChordType::Dom7:   return {0, 4, 7, 10};
		case ChordType::Maj7:   return {0, 4, 7, 11};
		case ChordType::Min7:   return {0, 3, 7, 10};
		case ChordType::Dim7:   return {0, 3, 6, 9};
		}
		return {0, 4, 7};
	}

	/// @brief 指定オクターブでのMIDIノート番号リストを返す
	[[nodiscard]] std::vector<int> midiNotes(int octave = 4) const
	{
		const int base = (octave + 1) * 12 + static_cast<int>(root);
		auto offs = offsets();
		std::vector<int> result;
		for (int o : offs) result.push_back(base + o);
		return result;
	}

	/// @brief アルペジオ展開（MIDIノート番号列）
	[[nodiscard]] std::vector<int> arpeggiate(ArpPattern pattern, int octave = 4) const
	{
		auto notes = midiNotes(octave);
		switch (pattern)
		{
		case ArpPattern::Up:
			return notes;
		case ArpPattern::Down:
			return {notes.rbegin(), notes.rend()};
		case ArpPattern::UpDown:
		{
			std::vector<int> result = notes;
			for (int i = static_cast<int>(notes.size()) - 2; i > 0; --i)
				result.push_back(notes[i]);
			return result;
		}
		case ArpPattern::Alberti:
			if (notes.size() >= 3)
				return {notes[0], notes[2], notes[1], notes[2]};
			return notes;
		case ArpPattern::Random:
			return notes; // 呼び出し側でシャッフルする
		}
		return notes;
	}

	/// @brief コードをMML文字列に変換する（音符列として出力）
	[[nodiscard]] std::string toMml(int octave = 4, int length = 4) const
	{
		static const char* NOTE_NAMES[] = {"C","C+","D","D+","E","F","F+","G","G+","A","A+","B"};
		auto notes = midiNotes(octave);
		std::string result;
		for (int n : notes)
		{
			int noteInOctave = n % 12;
			result += NOTE_NAMES[noteInOctave];
			result += std::to_string(length);
			result += " ";
		}
		return result;
	}
};

/// @brief Scale::chord の実装
inline Chord Scale::chord(int degree) const
{
	auto ivs = intervals();
	int idx = (degree - 1) % static_cast<int>(ivs.size());
	if (idx < 0) idx += static_cast<int>(ivs.size());

	int rootNote = (static_cast<int>(m_root) + ivs[idx]) % 12;

	// スケール音程から3度と5度の間隔を計算してコード種別を決定する
	int thirdIdx = (idx + 2) % static_cast<int>(ivs.size());
	int fifthIdx = (idx + 4) % static_cast<int>(ivs.size());

	int third = ((ivs[thirdIdx] - ivs[idx]) + 12) % 12;
	int fifth = ((ivs[fifthIdx] - ivs[idx]) + 12) % 12;

	ChordType ct = ChordType::Major;
	if (third == 3 && fifth == 7) ct = ChordType::Minor;
	else if (third == 3 && fifth == 6) ct = ChordType::Dim;
	else if (third == 4 && fifth == 8) ct = ChordType::Aug;
	else if (third == 4 && fifth == 7) ct = ChordType::Major;

	return {static_cast<Key>(rootNote), ct};
}

/// @brief コード進行
class ChordProgression
{
public:
	/// @brief ローマ数字表記からコード進行を解析する
	/// @param text "Im IVm V7 Im" のような文字列（スペース区切り）
	/// @param key 調のルート
	/// @param scaleType スケール種別
	/// @return コード列
	[[nodiscard]] static std::vector<Chord> parse(
		std::string_view text, Key key, ScaleType scaleType)
	{
		Scale scale(key, scaleType);
		std::vector<Chord> result;

		std::size_t pos = 0;
		while (pos < text.size())
		{
			// 空白をスキップする
			while (pos < text.size() && text[pos] == ' ') ++pos;
			if (pos >= text.size()) break;

			// ローマ数字を読み取る
			int degree = 0;
			bool isMinor = false;
			bool is7th = false;

			// I, II, III, IV, V, VI, VII を解析する
			if (pos < text.size() && (text[pos] == 'I' || text[pos] == 'V'))
			{
				std::string numeral;
				while (pos < text.size() && (text[pos] == 'I' || text[pos] == 'V'))
				{
					numeral += text[pos++];
				}
				degree = romanToInt(numeral);
			}

			// 修飾子を解析する: m (マイナー), 7 (セブンス)
			while (pos < text.size() && text[pos] != ' ')
			{
				if (text[pos] == 'm') isMinor = true;
				if (text[pos] == '7') is7th = true;
				++pos;
			}

			if (degree > 0)
			{
				Chord c = scale.chord(degree);
				if (isMinor) c.type = ChordType::Minor;
				if (is7th)
				{
					c.type = isMinor ? ChordType::Min7 : ChordType::Dom7;
				}
				result.push_back(c);
			}
		}

		return result;
	}

	/// @brief よく使われる進行のプリセット
	[[nodiscard]] static std::vector<Chord> preset(
		const std::string& name, Key key, ScaleType scaleType = ScaleType::Major)
	{
		if (name == "pop" || name == "I-V-vi-IV")
			return parse("I V VIm IV", key, scaleType);
		if (name == "blues")
			return parse("I I I I IV IV I I V IV I V", key, scaleType);
		if (name == "jazz_251")
			return parse("IIm V7 I", key, scaleType);
		if (name == "canon")
			return parse("I V VIm IIIm IV I IV V", key, scaleType);
		if (name == "minor_dramatic")
			return parse("Im IVm V7 Im", key, ScaleType::NaturalMinor);
		if (name == "rpg_town")
			return parse("I IVm V I", key, scaleType);
		if (name == "rpg_battle")
			return parse("Im Im IVm V7", key, ScaleType::NaturalMinor);
		return parse("I IV V I", key, scaleType);
	}

private:
	/// @brief ローマ数字文字列を整数に変換する
	static int romanToInt(const std::string& s)
	{
		if (s == "I") return 1;
		if (s == "II") return 2;
		if (s == "III") return 3;
		if (s == "IV") return 4;
		if (s == "V") return 5;
		if (s == "VI") return 6;
		if (s == "VII") return 7;
		return 0;
	}
};

} // namespace mitiru_mml
