#pragma once
/// @file SongBuilder.hpp
/// @brief 宣言的楽曲構築フレームワーク
///
/// @code
/// SongBuilder song;
/// song.setKey(Key::A, ScaleType::NaturalMinor);
/// song.setTempo(155);
/// song.assignPreset(Role::Melody, 2);  // Brass
/// song.assignPreset(Role::Bass, 6);    // FM Bass
/// song.setDrumPattern("rock_8beat");
///
/// song.setProgression("Im IVm V7 Im");
/// song.setMelody({5,4,3,2,1}, 8);
///
/// auto result = song.build(); // SongData（MMLトラック群）を返す
/// @endcode

#include <mitiru_mml/MusicTheory.hpp>
#include <mitiru_mml/PatternLibrary.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief 楽曲の役割
enum class Role
{
	Melody,      ///< メロディ（主旋律）
	Harmony,     ///< ハーモニー（和音パッド）
	Bass,        ///< ベースライン
	Arpeggio,    ///< アルペジオ
	CounterMel,  ///< カウンターメロディ
	Drums        ///< ドラム
};

/// @brief ビルド結果のトラック
struct BuiltTrack
{
	std::string label;      ///< トラックラベル
	std::string mml;        ///< 生成されたMML文字列
	std::string trackType;  ///< "FM", "SSG", "RHYTHM"
	int fmPreset = 0;       ///< FMプリセット番号
};

/// @brief ビルド結果
struct SongData
{
	std::vector<BuiltTrack> tracks;  ///< トラック群
	int tempo = 120;                 ///< テンポ
};

/// @brief 宣言的楽曲ビルダー
class SongBuilder
{
public:
	/// @brief 調を設定する
	void setKey(Key key, ScaleType scaleType = ScaleType::Major)
	{
		m_scale = Scale(key, scaleType);
	}

	/// @brief テンポを設定する
	void setTempo(int bpm) { m_tempo = std::clamp(bpm, 20, 300); }

	/// @brief 役割にFMプリセットを割り当てる
	void assignPreset(Role role, int presetIndex)
	{
		m_presets[static_cast<int>(role)] = presetIndex;
	}

	/// @brief ドラムパターンを設定する
	void setDrumPattern(std::string_view pattern)
	{
		m_drumPattern = DrumPatterns::get(pattern);
	}

	/// @brief ベースパターンを設定する
	void setBassPattern(std::string_view pattern)
	{
		m_bassPattern = std::string(pattern);
	}

	/// @brief アルペジオパターンを設定する
	void setArpPattern(ArpPattern pattern)
	{
		m_arpPattern = pattern;
	}

	/// @brief コード進行を設定する（ローマ数字表記）
	void setProgression(std::string_view romanNumerals)
	{
		m_chords = ChordProgression::parse(romanNumerals, m_scale.root(), m_scale.type());
	}

	/// @brief プリセット進行名を使う
	void setProgressionPreset(std::string_view name)
	{
		m_chords = ChordProgression::preset(std::string(name), m_scale.root(), m_scale.type());
	}

	/// @brief メロディをスケール度数列で記述する
	/// @param degrees スケール度数列（1=ルート, 2=2度, ...）
	/// @param noteLength デフォルト音符長
	void setMelody(const std::vector<int>& degrees, int noteLength = 8)
	{
		m_melodyDegrees = degrees;
		m_melodyLength = noteLength;
	}

	/// @brief メロディをMML文字列で直接指定する
	void setMelodyMml(std::string_view mml)
	{
		m_melodyMml = std::string(mml);
	}

	/// @brief 繰り返し回数を設定する（全セクション）
	void setRepeat(int times) { m_repeat = std::max(1, times); }

	/// @brief SSGアルペジオを有効にする
	void enableSsgArp(bool enable) { m_useSsgArp = enable; }

	/// @brief 楽曲をビルドしてMMLトラックを返す
	[[nodiscard]] SongData build() const
	{
		SongData data;
		data.tempo = m_tempo;

		std::string tempoStr = "T" + std::to_string(m_tempo) + " ";

		// コード進行から総小節数を算出する（全トラック共通の基準長）
		// 各コード = 1小節。進行 × repeat = 総小節数。
		const int totalBars = std::max(1, static_cast<int>(m_chords.size())) * m_repeat;

		// --- メロディトラック ---
		{
			BuiltTrack t;
			t.label = "Melody";
			t.trackType = "FM";
			t.fmPreset = m_presets[static_cast<int>(Role::Melody)];

			std::string mml = tempoStr + "O5 ";
			if (!m_melodyMml.empty())
			{
				mml += m_melodyMml;
			}
			else if (!m_melodyDegrees.empty())
			{
				mml += "L" + std::to_string(m_melodyLength) + " ";

				// 4/4拍子: 1小節 = 全音符 = L1で1音, L4で4音, L8で8音
				// notesPerBar = Lの値（L4→4, L8→8）
				const int notesPerBar = m_melodyLength;
				const int totalNotesNeeded = totalBars * notesPerBar;

				// メロディ度数列をサイクリックに繰り返して総小節数分を埋める
				for (int n = 0; n < totalNotesNeeded; ++n)
				{
					const int deg = m_melodyDegrees[
						static_cast<std::size_t>(n) % m_melodyDegrees.size()];
					const int note = m_scale.degreeToNote(deg, 5);
					mml += midiNoteToMml(note) + " ";
				}
			}
			t.mml = mml;
			data.tracks.push_back(std::move(t));
		}

		// --- ハーモニートラック（コードパッド）---
		if (!m_chords.empty())
		{
			BuiltTrack t;
			t.label = "Harmony";
			t.trackType = "FM";
			t.fmPreset = m_presets[static_cast<int>(Role::Harmony)];

			std::string mml = tempoStr + "O4 L1 ";
			// コード進行をサイクリックに totalBars 小節分繰り返す
			for (int bar = 0; bar < totalBars; ++bar)
			{
				const auto& chord = m_chords[
					static_cast<std::size_t>(bar) % m_chords.size()];
				const int rootNote = (4 + 1) * 12 + static_cast<int>(chord.root);
				mml += midiNoteToMml(rootNote) + "1 ";
			}
			t.mml = mml;
			data.tracks.push_back(std::move(t));
		}

		// --- ベーストラック ---
		if (!m_chords.empty())
		{
			BuiltTrack t;
			t.label = "Bass";
			t.trackType = "FM";
			t.fmPreset = m_presets[static_cast<int>(Role::Bass)];

			std::string mml = tempoStr + "O3 ";
			// コード進行をサイクリックに totalBars 小節分繰り返す
			for (int bar = 0; bar < totalBars; ++bar)
			{
				const auto& chord = m_chords[
					static_cast<std::size_t>(bar) % m_chords.size()];
				mml += BassPatterns::generate(chord,
					m_bassPattern.empty() ? "root_fifth" : m_bassPattern, 3, 8);
			}
			t.mml = mml;
			data.tracks.push_back(std::move(t));
		}

		// --- SSGアルペジオトラック ---
		if (m_useSsgArp && !m_chords.empty())
		{
			BuiltTrack t;
			t.label = "SSG Arp";
			t.trackType = "SSG";

			std::string mml = tempoStr + "O5 ";
			// コード進行をサイクリックに totalBars 小節分繰り返す
			// 1コード = アルペジオ2回（16分音符×構成音数×2 = 1小節分）
			for (int bar = 0; bar < totalBars; ++bar)
			{
				const auto& chord = m_chords[
					static_cast<std::size_t>(bar) % m_chords.size()];
				mml += ArpPatterns::generate(chord, m_arpPattern, 5, 16);
				mml += ArpPatterns::generate(chord, m_arpPattern, 5, 16);
			}
			t.mml = mml;
			data.tracks.push_back(std::move(t));
		}

		// --- ドラムトラック ---
		if (!m_drumPattern.empty())
		{
			BuiltTrack t;
			t.label = "Drums";
			t.trackType = "RHYTHM";

			std::string mml = tempoStr;
			// totalBars 小節分ドラムパターンを繰り返す
			for (int bar = 0; bar < totalBars; ++bar)
			{
				mml += m_drumPattern + " ";
			}
			t.mml = mml;
			data.tracks.push_back(std::move(t));
		}

		return data;
	}

private:
	/// @brief MIDIノート番号をMML音名に変換する
	static std::string midiNoteToMml(int midiNote)
	{
		static const char* NAMES[] = {"C","C+","D","D+","E","F","F+","G","G+","A","A+","B"};
		return NAMES[midiNote % 12];
	}

	Scale m_scale{Key::C, ScaleType::Major};
	int m_tempo = 120;
	int m_presets[6] = {0, 3, 6, 0, 0, 0}; ///< デフォルト: Piano, Strings, Bass
	std::string m_drumPattern;
	std::string m_bassPattern;
	ArpPattern m_arpPattern = ArpPattern::Up;
	std::vector<Chord> m_chords;
	std::vector<int> m_melodyDegrees;
	int m_melodyLength = 8;
	std::string m_melodyMml;
	int m_repeat = 1;
	bool m_useSsgArp = false;
};

} // namespace mitiru_mml
