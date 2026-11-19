#pragma once
/// @file MitiruMML.hpp
/// @brief MitiruMML アンブレラヘッダー
/// @details MML音楽エンジンの全機能を一括インクルードする。
///
/// @code
/// #include <mitiru_mml/MitiruMML.hpp>
///
/// mitiru_mml::Sequencer seq;
/// seq.addTrack("T120 O4 L8 @2 CDEFGAB>C");
/// seq.addTrack("T120 O3 L2 @0 CEG>C");
/// auto pcm = seq.render();
/// mitiru_mml::AudioOutput::play(pcm, seq.sampleRate());
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/Synthesizer.hpp>
#include <mitiru_mml/Track.hpp>
#include <mitiru_mml/Sequencer.hpp>
#include <mitiru_mml/WavWriter.hpp>
#include <mitiru_mml/AudioOutput.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaPresets.hpp>
#include <mitiru_mml/OpnaSequencer.hpp>
#include <mitiru_mml/MusicTheory.hpp>
#include <mitiru_mml/PatternLibrary.hpp>
#include <mitiru_mml/SongBuilder.hpp>
#include <mitiru_mml/MusicPrompt.hpp>
#include <mitiru_mml/AiComposer.hpp>
#include <mitiru_mml/PhraseDictionary.hpp>
#include <mitiru_mml/PhraseComposer.hpp>
#include <mitiru_mml/MmlValidator.hpp>
#include <mitiru_mml/TfiImporter.hpp>
#include <mitiru_mml/VgmPlayer.hpp>
#include <mitiru_mml/Reverb.hpp>
#include <mitiru_mml/SampleInstrument.hpp>
#include <mitiru_mml/MultiSampleInstrument.hpp>
#include <mitiru_mml/SampleSequencer.hpp>
#include <mitiru_mml/WavReader.hpp>
#include <mitiru_mml/Sf2File.hpp>
#include <mitiru_mml/SfzFile.hpp>
// 音質強化（レゾナンスフィルタ + マスター/バスエフェクト + ステレオミックス）
#include <mitiru_mml/Biquad.hpp>
#include <mitiru_mml/Delay.hpp>
#include <mitiru_mml/Chorus.hpp>
#include <mitiru_mml/Equalizer.hpp>
#include <mitiru_mml/Compressor.hpp>
#include <mitiru_mml/MixBus.hpp>
// 複数チップ (OPM、OPL 系、SN76489、AY-3-8910、NES APU) と、トラックごとにチップを選べる曲のテキスト
#include <mitiru_mml/ChipCatalog.hpp>
#include <mitiru_mml/SongDefinitions.hpp>
#include <mitiru_mml/SongText.hpp>
#include <mitiru_mml/SongLint.hpp>
#include <mitiru_mml/ChipSong.hpp>
#include <mitiru_mml/VgmMultiPlayer.hpp>
// 現代的な音源と制作の機能 (シンセ、ドラムシンセ、SF2 サンプラー、サイドチェーン、ステム)
#include <mitiru_mml/MmlExtensions.hpp>
#include <mitiru_mml/SynthDriver.hpp>
#include <mitiru_mml/DrumSynthDriver.hpp>
#include <mitiru_mml/SamplerDriver.hpp>
#include <mitiru_mml/Dynamics.hpp>
#include <mitiru_mml/EventTransforms.hpp>
#include <mitiru_mml/SendBus.hpp>
#include <mitiru_mml/ModernSong.hpp>
#include <mitiru_mml/ModernSongScript.hpp>

// ストリーミング (小さな塊ずつ PCM を出す) と、ゲームの状態で層を切り替える機能
#include <mitiru_mml/LayerGains.hpp>
#include <mitiru_mml/ChipStreamSource.hpp>
#include <mitiru_mml/StreamingDynamics.hpp>
#include <mitiru_mml/MultiChipStream.hpp>
#include <mitiru_mml/ModernSongStream.hpp>
