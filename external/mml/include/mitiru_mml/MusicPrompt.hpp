#pragma once
/// @file MusicPrompt.hpp
/// @brief AI向け音楽プロンプトインターフェース
/// @details 高レベルな音楽記述（ジャンル・調・テンポ・気分）から
///          自動的にMMLトラック群を生成する。AIが音楽を作る際の主要エントリーポイント。
///
/// @code
/// MusicPrompt prompt;
/// prompt.genre = "rpg_battle";
/// prompt.key = Key::A;
/// prompt.scaleType = ScaleType::NaturalMinor;
/// prompt.bpm = 155;
/// prompt.energy = 0.9f;
/// auto data = MusicPromptCompiler::compile(prompt);
/// // data.tracks has FM/SSG/Rhythm tracks ready for OpnaSequencer
/// @endcode

#include <mitiru_mml/MusicTheory.hpp>
#include <mitiru_mml/SongBuilder.hpp>
#include <algorithm>
#include <string>
#include <string_view>

namespace mitiru_mml
{

/// @brief 音楽プロンプト（AI入力用）
struct MusicPrompt
{
	std::string genre = "default";         ///< ジャンル名
	Key key = Key::C;                      ///< 調
	ScaleType scaleType = ScaleType::Major; ///< スケール種別
	int bpm = 120;                         ///< テンポ
	float energy = 0.5f;                   ///< エネルギー [0=静か, 1=激しい]
	float complexity = 0.5f;               ///< 複雑さ [0=シンプル, 1=複雑]
	bool useSsg = true;                    ///< SSGアルペジオ使用
	bool useDrums = true;                  ///< ドラム使用
	int repeat = 2;                        ///< 繰り返し回数
};

/// @brief プロンプトからSongDataを生成するコンパイラ
class MusicPromptCompiler
{
public:
	/// @brief プロンプトをコンパイルしてSongDataを返す
	[[nodiscard]] static SongData compile(const MusicPrompt& prompt)
	{
		SongBuilder builder;
		builder.setKey(prompt.key, prompt.scaleType);
		builder.setTempo(prompt.bpm);
		builder.setRepeat(prompt.repeat);
		builder.enableSsgArp(prompt.useSsg);

		// ジャンルに応じた設定
		applyGenre(builder, prompt);

		// エネルギーに応じたドラムパターン
		if (prompt.useDrums)
		{
			if (prompt.energy > 0.7f)
				builder.setDrumPattern("fast_rock");
			else if (prompt.energy > 0.4f)
				builder.setDrumPattern("rock_8beat");
			else if (prompt.energy > 0.2f)
				builder.setDrumPattern("ballad");
			else
				builder.setDrumPattern("silence");
		}

		// 複雑さに応じたアルペジオ
		if (prompt.complexity > 0.6f)
			builder.setArpPattern(ArpPattern::UpDown);
		else if (prompt.complexity > 0.3f)
			builder.setArpPattern(ArpPattern::Up);
		else
			builder.setArpPattern(ArpPattern::Alberti);

		return builder.build();
	}

private:
	/// @brief ジャンルに応じたビルダー設定を適用する
	static void applyGenre(SongBuilder& builder, const MusicPrompt& prompt)
	{
		const auto& genre = prompt.genre;

		if (genre == "rpg_battle")
		{
			builder.assignPreset(Role::Melody, 2);   // Brass
			builder.assignPreset(Role::Harmony, 9);   // SynthLead
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgressionPreset("rpg_battle");
			builder.setBassPattern("pumping");
			if (prompt.bpm < 140) builder.setTempo(155);
		}
		else if (genre == "rpg_town")
		{
			builder.assignPreset(Role::Melody, 0);   // Piano
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgressionPreset("rpg_town");
			builder.setBassPattern("root_fifth");
		}
		else if (genre == "rpg_dungeon")
		{
			builder.assignPreset(Role::Melody, 1);   // Bell
			builder.assignPreset(Role::Harmony, 7);   // Flute
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("Im VIm IVm V7");
			builder.setBassPattern("root");
		}
		else if (genre == "rpg_field")
		{
			builder.assignPreset(Role::Melody, 0);   // Piano
			builder.assignPreset(Role::Harmony, 4);   // Organ
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("I V VIm IV");
			builder.setBassPattern("root_fifth");
		}
		else if (genre == "rpg_boss")
		{
			builder.assignPreset(Role::Melody, 2);   // Brass
			builder.assignPreset(Role::Harmony, 11);  // DistGuitar
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("Im Im IVm V7");
			builder.setBassPattern("pumping");
			if (prompt.bpm < 140) builder.setTempo(148);
		}
		else if (genre == "vn_sad")
		{
			builder.assignPreset(Role::Melody, 5);   // E.Piano
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("Im VIm IVm V7");
			builder.setBassPattern("root");
			if (prompt.bpm > 100) builder.setTempo(84);
		}
		else if (genre == "vn_romantic")
		{
			builder.assignPreset(Role::Melody, 5);   // E.Piano
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("I IIIm VIm IV");
			builder.setBassPattern("root_fifth");
			if (prompt.bpm > 110) builder.setTempo(96);
		}
		else if (genre == "vn_tension")
		{
			builder.assignPreset(Role::Melody, 9);   // SynthLead
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("Im VIIm VIm V7");
			builder.setBassPattern("root");
		}
		else if (genre == "title_screen")
		{
			builder.assignPreset(Role::Melody, 2);   // Brass
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("I V VIm IVm V I");
			builder.setBassPattern("root_fifth");
		}
		else if (genre == "ending")
		{
			builder.assignPreset(Role::Melody, 5);   // E.Piano
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("I V VIm IV I");
			builder.setBassPattern("root_fifth");
			if (prompt.bpm > 110) builder.setTempo(96);
		}
		else // default
		{
			builder.assignPreset(Role::Melody, 0);   // Piano
			builder.assignPreset(Role::Harmony, 3);   // Strings
			builder.assignPreset(Role::Bass, 6);       // Bass
			builder.setProgression("I IV V I");
			builder.setBassPattern("root_fifth");
		}

		// エネルギーに応じたデフォルトメロディ（スケール度数ウォーク）
		if (prompt.energy > 0.6f)
		{
			builder.setMelody({1,3,5,8,7,5,3,1, 2,4,6,8,7,5,3,2}, 8);
		}
		else if (prompt.energy > 0.3f)
		{
			builder.setMelody({5,4,3,2,1,2,3,5, 6,5,4,3,2,3,4,5}, 8);
		}
		else
		{
			builder.setMelody({1,3,5,3,1,5,3,1}, 4);
		}
	}
};

} // namespace mitiru_mml
