#pragma once

/// @file MidiConverter.hpp
/// @brief MIDI ↔ MML 変換器
/// @details Standard MIDI File (SMF) の読み込みとMML文字列への変換。
///          MMLからMIDIイベント列への逆変換も対応。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru::audio
{

/// @brief MIDIノートイベント
struct MidiNote
{
	uint32_t tick = 0;        ///< 開始ティック
	uint32_t duration = 0;    ///< 長さ（ティック）
	uint8_t channel = 0;     ///< チャンネル (0-15)
	uint8_t note = 60;       ///< ノート番号 (0-127, 60=C4)
	uint8_t velocity = 100;  ///< ベロシティ (0-127)
};

/// @brief MIDIトラック
struct MidiTrack
{
	std::string name;
	std::vector<MidiNote> notes;
	uint8_t program = 0;  ///< 楽器番号 (GM)
};

/// @brief MIDIファイルデータ
struct MidiFile
{
	uint16_t format = 1;      ///< 0=単一トラック, 1=複数同期, 2=複数非同期
	uint16_t ticksPerBeat = 480;
	int tempo = 120;          ///< BPM
	std::vector<MidiTrack> tracks;
};

/// @brief MIDI → MML 変換器
class MidiToMml
{
public:
	/// @brief MIDIファイルを読み込む（簡易SMFパーサー）
	[[nodiscard]] bool loadSmf(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) { return false; }

		// ヘッダー読み込み
		char header[4];
		ifs.read(header, 4);
		if (std::string(header, 4) != "MThd") { return false; }

		const uint32_t headerLen = readBE32(ifs);
		if (headerLen < 6) { return false; }

		m_midi.format = readBE16(ifs);
		const uint16_t numTracks = readBE16(ifs);
		m_midi.ticksPerBeat = readBE16(ifs);

		// 残りのヘッダをスキップ
		if (headerLen > 6) { ifs.seekg(headerLen - 6, std::ios::cur); }

		// トラック読み込み
		for (uint16_t t = 0; t < numTracks; ++t)
		{
			char trackHeader[4];
			ifs.read(trackHeader, 4);
			if (std::string(trackHeader, 4) != "MTrk") { break; }
			const uint32_t trackLen = readBE32(ifs);
			const auto trackStart = ifs.tellg();

			MidiTrack track;
			parseTrack(ifs, track, trackLen);
			m_midi.tracks.push_back(std::move(track));

			ifs.seekg(trackStart + static_cast<std::streamoff>(trackLen));
		}

		return !m_midi.tracks.empty();
	}

	/// @brief MMLに変換する
	[[nodiscard]] std::string toMml(int trackIndex = 0) const
	{
		if (trackIndex < 0 || trackIndex >= static_cast<int>(m_midi.tracks.size()))
		{
			return {};
		}

		const auto& track = m_midi.tracks[static_cast<size_t>(trackIndex)];
		std::string mml = "T" + std::to_string(m_midi.tempo) + " ";

		int lastOctave = 4;
		for (const auto& note : track.notes)
		{
			const int noteOct = note.note / 12 - 1;
			const int notePitch = note.note % 12;

			// オクターブ変更
			while (lastOctave < noteOct) { mml += ">"; ++lastOctave; }
			while (lastOctave > noteOct) { mml += "<"; --lastOctave; }

			// ノート名
			static const char* noteNames[] = {"C", "C+", "D", "D+", "E", "F", "F+", "G", "G+", "A", "A+", "B"};
			mml += noteNames[notePitch];

			// 音価
			const int quarterTicks = m_midi.ticksPerBeat;
			if (note.duration > 0)
			{
				const int ratio = quarterTicks * 4 / static_cast<int>(note.duration);
				if (ratio > 0 && ratio <= 64)
				{
					mml += std::to_string(ratio);
				}
			}
		}

		return mml;
	}

	/// @brief MIDIデータへの参照
	[[nodiscard]] const MidiFile& midi() const noexcept { return m_midi; }

private:
	MidiFile m_midi;

	static uint32_t readBE32(std::istream& s)
	{
		uint8_t buf[4];
		s.read(reinterpret_cast<char*>(buf), 4);
		return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16)
			| (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
	}

	static uint16_t readBE16(std::istream& s)
	{
		uint8_t buf[2];
		s.read(reinterpret_cast<char*>(buf), 2);
		return (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
	}

	static uint32_t readVarLen(std::istream& s)
	{
		uint32_t v = 0;
		uint8_t b;
		do
		{
			s.read(reinterpret_cast<char*>(&b), 1);
			v = (v << 7) | (b & 0x7F);
		} while (b & 0x80);
		return v;
	}

	void parseTrack(std::istream& s, MidiTrack& track, uint32_t len)
	{
		const auto end = s.tellg() + static_cast<std::streamoff>(len);
		uint32_t tick = 0;
		uint8_t runningStatus = 0;

		// ノートオン追跡（キー→開始ティック）
		uint32_t noteOnTick[128] = {};
		uint8_t noteOnVel[128] = {};

		while (s.tellg() < end && s.good())
		{
			tick += readVarLen(s);
			uint8_t status;
			s.read(reinterpret_cast<char*>(&status), 1);

			if (status < 0x80) { s.seekg(-1, std::ios::cur); status = runningStatus; }
			else { runningStatus = status; }

			const uint8_t type = status & 0xF0;
			const uint8_t ch = status & 0x0F;

			if (type == 0x90) // Note On
			{
				uint8_t key, vel;
				s.read(reinterpret_cast<char*>(&key), 1);
				s.read(reinterpret_cast<char*>(&vel), 1);
				if (vel > 0)
				{
					noteOnTick[key] = tick;
					noteOnVel[key] = vel;
				}
				else // vel=0 is Note Off
				{
					track.notes.push_back({noteOnTick[key], tick - noteOnTick[key], ch, key, noteOnVel[key]});
				}
			}
			else if (type == 0x80) // Note Off
			{
				uint8_t key, vel;
				s.read(reinterpret_cast<char*>(&key), 1);
				s.read(reinterpret_cast<char*>(&vel), 1);
				static_cast<void>(vel);
				track.notes.push_back({noteOnTick[key], tick - noteOnTick[key], ch, key, noteOnVel[key]});
			}
			else if (type == 0xC0) { uint8_t prog; s.read(reinterpret_cast<char*>(&prog), 1); track.program = prog; }
			else if (type == 0xD0) { s.seekg(1, std::ios::cur); }
			else if (type == 0xE0) { s.seekg(2, std::ios::cur); }
			else if (type == 0xA0 || type == 0xB0) { s.seekg(2, std::ios::cur); }
			else if (status == 0xFF) // Meta
			{
				uint8_t metaType;
				s.read(reinterpret_cast<char*>(&metaType), 1);
				const uint32_t metaLen = readVarLen(s);
				if (metaType == 0x51 && metaLen == 3) // Tempo
				{
					uint8_t buf[3];
					s.read(reinterpret_cast<char*>(buf), 3);
					const uint32_t usPerBeat = (static_cast<uint32_t>(buf[0]) << 16)
						| (static_cast<uint32_t>(buf[1]) << 8) | buf[2];
					m_midi.tempo = static_cast<int>(60000000.0 / usPerBeat);
				}
				else { s.seekg(metaLen, std::ios::cur); }
			}
			else if (status == 0xF0 || status == 0xF7) // SysEx
			{
				const uint32_t sysLen = readVarLen(s);
				s.seekg(sysLen, std::ios::cur);
			}
		}

		std::sort(track.notes.begin(), track.notes.end(),
			[](const MidiNote& a, const MidiNote& b) { return a.tick < b.tick; });
	}
};

} // namespace mitiru::audio
