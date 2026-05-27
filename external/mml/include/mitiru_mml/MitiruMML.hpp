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
