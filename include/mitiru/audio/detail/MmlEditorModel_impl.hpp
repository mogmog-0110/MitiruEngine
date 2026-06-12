#pragma once

/// @file MmlEditorModel_impl.hpp
/// @brief MmlEditorModel の実装本体（MmlEditorModel.hpp から機械的分割）

#include <mitiru/audio/MmlEditorModel.hpp>

namespace mitiru::audio
{

// ─── トラック管理 ───

/// @brief トラックを追加する
inline void MmlEditorModel::addTrack(std::string_view name, int channel,
                                     std::string_view instrument)
{
	MmlTrack track;
	track.name = std::string(name);
	track.channel = channel;
	track.instrument = std::string(instrument);
	track.tempo = m_composition.tempo;
	m_composition.tracks.push_back(std::move(track));
}

/// @brief トラックを削除する
inline void MmlEditorModel::removeTrack(int trackIndex)
{
	if (!isValidTrack(trackIndex))
	{
		return;
	}
	m_composition.tracks.erase(
		m_composition.tracks.begin() + trackIndex);
	/// 選択をクリア
	if (m_selection.trackIndex == trackIndex)
	{
		m_selection = NoteSelection{};
	}
}

// ─── ノート編集 ───

/// @brief ノートをトラック末尾に追加する
inline void MmlEditorModel::addNote(int trackIndex, const MmlNote& note)
{
	if (!isValidTrack(trackIndex))
	{
		return;
	}

	EditorCommand cmd;
	cmd.type = EditorCommandType::AddNote;
	cmd.trackIndex = trackIndex;
	cmd.noteIndex = static_cast<int>(
		m_composition.tracks[static_cast<size_t>(trackIndex)].notes.size());
	cmd.note = note;
	executeCommand(cmd);
}

/// @brief ノートを削除する
inline void MmlEditorModel::removeNote(int trackIndex, int noteIndex)
{
	if (!isValidNote(trackIndex, noteIndex))
	{
		return;
	}

	EditorCommand cmd;
	cmd.type = EditorCommandType::RemoveNote;
	cmd.trackIndex = trackIndex;
	cmd.noteIndex = noteIndex;
	cmd.previousNote = m_composition.tracks[static_cast<size_t>(trackIndex)]
		.notes[static_cast<size_t>(noteIndex)];
	executeCommand(cmd);
}

/// @brief ノートを指定位置に挿入する
inline void MmlEditorModel::insertNote(int trackIndex, int noteIndex,
                                       const MmlNote& note)
{
	if (!isValidTrack(trackIndex))
	{
		return;
	}
	const auto trackSize = static_cast<int>(
		m_composition.tracks[static_cast<size_t>(trackIndex)].notes.size());
	if (noteIndex < 0 || noteIndex > trackSize)
	{
		return;
	}

	EditorCommand cmd;
	cmd.type = EditorCommandType::InsertNote;
	cmd.trackIndex = trackIndex;
	cmd.noteIndex = noteIndex;
	cmd.note = note;
	executeCommand(cmd);
}

// ─── プロパティ変更 ───

/// @brief グローバルテンポを設定する
inline void MmlEditorModel::setTempo(int bpm)
{
	if (bpm <= 0 || bpm > 999)
	{
		return;
	}

	EditorCommand cmd;
	cmd.type = EditorCommandType::SetTempo;
	cmd.previousTempo = m_composition.tempo;
	cmd.newTempo = bpm;
	executeCommand(cmd);
}

/// @brief トラックの楽器を設定する
inline void MmlEditorModel::setInstrument(int trackIndex,
                                          std::string_view instrument)
{
	if (!isValidTrack(trackIndex))
	{
		return;
	}

	EditorCommand cmd;
	cmd.type = EditorCommandType::SetInstrument;
	cmd.trackIndex = trackIndex;
	cmd.previousInstrument =
		m_composition.tracks[static_cast<size_t>(trackIndex)].instrument;
	cmd.newInstrument = std::string(instrument);
	executeCommand(cmd);
}

// ─── MML文字列変換 ───

/// @brief コンポジションをMML文字列にエクスポートする
inline std::string MmlEditorModel::toMmlString() const
{
	std::ostringstream oss;
	oss << "; " << m_composition.title << "\n";
	if (!m_composition.author.empty())
	{
		oss << "; Author: " << m_composition.author << "\n";
	}
	oss << "\n";

	for (size_t ti = 0; ti < m_composition.tracks.size(); ++ti)
	{
		const auto& trk = m_composition.tracks[ti];
		oss << "; Track " << ti << ": " << trk.name << "\n";
		oss << "@" << trk.instrument << " ";
		oss << "T" << trk.tempo << " ";
		oss << "V" << trk.volume << " ";
		oss << "O" << static_cast<int>(Octave::O4) << " ";

		Octave currentOctave = Octave::O4;
		for (const auto& note : trk.notes)
		{
			if (note.rest)
			{
				oss << "R";
			}
			else
			{
				/// オクターブ変更
				if (note.octave != currentOctave)
				{
					oss << "O" << static_cast<int>(note.octave);
					currentOctave = note.octave;
				}
				oss << pitchToChar(note.pitch);
				if (note.accidental == Accidental::Sharp)
				{
					oss << "+";
				}
				else if (note.accidental == Accidental::Flat)
				{
					oss << "-";
				}
			}
			oss << static_cast<int>(note.duration);
			if (note.dotted)
			{
				oss << ".";
			}
			oss << " ";
		}
		oss << "\n\n";
	}
	return oss.str();
}

/// @brief MML文字列からコンポジションを読み込む
inline void MmlEditorModel::fromMmlString(std::string_view mml)
{
	m_composition = MmlComposition{};
	m_undoStack.clear();
	m_redoStack.clear();
	m_selection = NoteSelection{};

	MmlTrack currentTrack;
	Octave currentOctave = Octave::O4;
	bool hasTrack = false;

	size_t pos = 0;
	const std::string str(mml);

	while (pos < str.size())
	{
		const char ch = str[pos];

		if (ch == ';')
		{
			/// コメント行をスキップ
			while (pos < str.size() && str[pos] != '\n')
			{
				++pos;
			}
			continue;
		}

		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
		{
			++pos;
			continue;
		}

		if (ch == '@')
		{
			/// 楽器指定
			++pos;
			std::string inst;
			while (pos < str.size() && str[pos] != ' '
			       && str[pos] != '\n')
			{
				inst += str[pos++];
			}
			if (!hasTrack)
			{
				currentTrack = MmlTrack{};
				currentTrack.name = "Track " + std::to_string(
					m_composition.tracks.size());
				hasTrack = true;
			}
			currentTrack.instrument = inst;
			continue;
		}

		if (ch == 'T' || ch == 't')
		{
			++pos;
			int val = parseNumber(str, pos);
			if (val > 0)
			{
				if (hasTrack)
				{
					currentTrack.tempo = val;
				}
				m_composition.tempo = val;
			}
			continue;
		}

		if (ch == 'V' || ch == 'v')
		{
			++pos;
			int val = parseNumber(str, pos);
			if (hasTrack)
			{
				currentTrack.volume = std::clamp(val, 0, 127);
			}
			continue;
		}

		if (ch == 'O' || ch == 'o')
		{
			++pos;
			int val = parseNumber(str, pos);
			currentOctave = static_cast<Octave>(
				std::clamp(val, 4, 7));
			continue;
		}

		if (ch == 'R' || ch == 'r')
		{
			/// 休符
			++pos;
			MmlNote note;
			note.rest = true;
			note.octave = currentOctave;
			parseDuration(str, pos, note);
			if (!hasTrack)
			{
				currentTrack = MmlTrack{};
				currentTrack.name = "Track " + std::to_string(
					m_composition.tracks.size());
				hasTrack = true;
			}
			currentTrack.notes.push_back(note);
			continue;
		}

		if (isPitchChar(ch))
		{
			MmlNote note;
			note.pitch = charToPitch(ch);
			note.octave = currentOctave;
			++pos;

			/// 臨時記号
			if (pos < str.size() && str[pos] == '+')
			{
				note.accidental = Accidental::Sharp;
				++pos;
			}
			else if (pos < str.size() && str[pos] == '-')
			{
				note.accidental = Accidental::Flat;
				++pos;
			}

			parseDuration(str, pos, note);
			if (!hasTrack)
			{
				currentTrack = MmlTrack{};
				currentTrack.name = "Track " + std::to_string(
					m_composition.tracks.size());
				hasTrack = true;
			}
			currentTrack.notes.push_back(note);
			continue;
		}

		/// 不明文字をスキップ
		++pos;
	}

	if (hasTrack)
	{
		m_composition.tracks.push_back(std::move(currentTrack));
	}
}

// ─── JSON シリアライズ ───

/// @brief エディタ状態をJSON文字列にエクスポートする
inline std::string MmlEditorModel::toJson() const
{
	std::ostringstream oss;
	oss << "{";
	oss << R"("title":")" << m_composition.title << R"(",)";
	oss << R"("author":")" << m_composition.author << R"(",)";
	oss << R"("tempo":)" << m_composition.tempo << ",";
	oss << R"("timeSignature":{"numerator":)"
	    << m_composition.timeSignature.numerator
	    << R"(,"denominator":)"
	    << m_composition.timeSignature.denominator << "},";
	oss << R"("tracks":[)";

	for (size_t ti = 0; ti < m_composition.tracks.size(); ++ti)
	{
		if (ti > 0)
		{
			oss << ",";
		}
		const auto& trk = m_composition.tracks[ti];
		oss << "{";
		oss << R"("name":")" << trk.name << R"(",)";
		oss << R"("channel":)" << trk.channel << ",";
		oss << R"("instrument":")" << trk.instrument << R"(",)";
		oss << R"("volume":)" << trk.volume << ",";
		oss << R"("tempo":)" << trk.tempo << ",";
		oss << R"("notes":[)";

		for (size_t ni = 0; ni < trk.notes.size(); ++ni)
		{
			if (ni > 0)
			{
				oss << ",";
			}
			const auto& n = trk.notes[ni];
			oss << "{";
			oss << R"("pitch":)" << static_cast<int>(n.pitch) << ",";
			oss << R"("octave":)" << static_cast<int>(n.octave) << ",";
			oss << R"("duration":)" << static_cast<int>(n.duration) << ",";
			oss << R"("accidental":)" << static_cast<int>(n.accidental) << ",";
			oss << R"("dotted":)" << (n.dotted ? "true" : "false") << ",";
			oss << R"("rest":)" << (n.rest ? "true" : "false");
			oss << "}";
		}
		oss << "]}";
	}
	oss << "]}";
	return oss.str();
}

/// @brief JSON文字列からエディタ状態を読み込む
inline void MmlEditorModel::fromJson(std::string_view json)
{
	/// 簡易パース：toJson()で出力した形式を前提とする
	/// 完全なJSONパーサーではなく、既知フォーマットのキーバリューを抽出
	m_composition = MmlComposition{};
	m_undoStack.clear();
	m_redoStack.clear();
	m_selection = NoteSelection{};

	const std::string str(json);
	m_composition.title = extractString(str, "title");
	m_composition.author = extractString(str, "author");
	m_composition.tempo = extractInt(str, "tempo");

	/// トラックの詳細パースは省略（完全なJSON解析は外部ライブラリ推奨）
	/// toMmlString/fromMmlString をメインのシリアライズ手段として推奨する
}

// ─── 再生時間計算 ───

/// @brief コンポジション全体の再生時間を秒で取得する
inline float MmlEditorModel::getDuration() const noexcept
{
	float maxBeats = 0.0f;
	for (const auto& trk : m_composition.tracks)
	{
		maxBeats = std::max(maxBeats, trk.totalBeats());
	}
	if (m_composition.tempo <= 0)
	{
		return 0.0f;
	}
	/// 拍数 / BPM * 60秒
	return maxBeats / static_cast<float>(m_composition.tempo) * 60.0f;
}

// ─── Undo/Redo ───

/// @brief 直前の操作を取り消す
inline bool MmlEditorModel::undo()
{
	if (m_undoStack.empty())
	{
		return false;
	}
	const auto cmd = m_undoStack.back();
	m_undoStack.pop_back();
	undoCommand(cmd);
	m_redoStack.push_back(cmd);
	return true;
}

/// @brief 取り消した操作をやり直す
inline bool MmlEditorModel::redo()
{
	if (m_redoStack.empty())
	{
		return false;
	}
	const auto cmd = m_redoStack.back();
	m_redoStack.pop_back();
	applyCommand(cmd);
	m_undoStack.push_back(cmd);
	return true;
}

// ─── 選択 ───

/// @brief ノート範囲を選択する
inline void MmlEditorModel::selectNotes(int trackIndex, int startIndex,
                                        int endIndex)
{
	if (!isValidTrack(trackIndex))
	{
		return;
	}
	const auto trackSize = static_cast<int>(
		m_composition.tracks[static_cast<size_t>(trackIndex)].notes.size());
	m_selection.trackIndex = trackIndex;
	m_selection.startIndex = std::clamp(startIndex, 0, trackSize);
	m_selection.endIndex = std::clamp(endIndex, startIndex, trackSize);
}

// ─── クリップボード ───

/// @brief 選択範囲をクリップボードにコピーする
inline void MmlEditorModel::copy()
{
	if (!m_selection.valid())
	{
		return;
	}
	const auto& notes = m_composition
		.tracks[static_cast<size_t>(m_selection.trackIndex)].notes;
	m_clipboard.clear();
	for (int i = m_selection.startIndex; i < m_selection.endIndex; ++i)
	{
		m_clipboard.push_back(notes[static_cast<size_t>(i)]);
	}
}

/// @brief 選択範囲をカットする（コピー+削除）
inline void MmlEditorModel::cut()
{
	copy();
	if (!m_selection.valid())
	{
		return;
	}
	/// 後ろから削除してインデックスがずれないようにする
	for (int i = m_selection.endIndex - 1; i >= m_selection.startIndex; --i)
	{
		removeNote(m_selection.trackIndex, i);
	}
	m_selection = NoteSelection{};
}

/// @brief クリップボードの内容を指定位置にペーストする
inline void MmlEditorModel::paste(int trackIndex, int noteIndex)
{
	if (m_clipboard.empty() || !isValidTrack(trackIndex))
	{
		return;
	}
	const auto trackSize = static_cast<int>(
		m_composition.tracks[static_cast<size_t>(trackIndex)].notes.size());
	const int insertAt = std::clamp(noteIndex, 0, trackSize);

	EditorCommand cmd;
	cmd.type = EditorCommandType::PasteNotes;
	cmd.trackIndex = trackIndex;
	cmd.noteIndex = insertAt;
	cmd.notesBatch = m_clipboard;
	executeCommand(cmd);
}

// ─── コマンド実行 (private) ───

/// @brief コマンドを実行しUndoスタックに積む
inline void MmlEditorModel::executeCommand(const EditorCommand& cmd)
{
	applyCommand(cmd);
	m_undoStack.push_back(cmd);
	m_redoStack.clear();
}

/// @brief コマンドを適用する（状態変更）
inline void MmlEditorModel::applyCommand(const EditorCommand& cmd)
{
	switch (cmd.type)
	{
	case EditorCommandType::AddNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.push_back(cmd.note);
		break;
	}
	case EditorCommandType::RemoveNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.erase(notes.begin() + cmd.noteIndex);
		break;
	}
	case EditorCommandType::InsertNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.insert(notes.begin() + cmd.noteIndex, cmd.note);
		break;
	}
	case EditorCommandType::SetTempo:
	{
		m_composition.tempo = cmd.newTempo;
		break;
	}
	case EditorCommandType::SetInstrument:
	{
		m_composition.tracks[static_cast<size_t>(cmd.trackIndex)]
			.instrument = cmd.newInstrument;
		break;
	}
	case EditorCommandType::PasteNotes:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.insert(notes.begin() + cmd.noteIndex,
		             cmd.notesBatch.begin(), cmd.notesBatch.end());
		break;
	}
	}
}

/// @brief コマンドを取り消す（逆操作）
inline void MmlEditorModel::undoCommand(const EditorCommand& cmd)
{
	switch (cmd.type)
	{
	case EditorCommandType::AddNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		if (!notes.empty())
		{
			notes.pop_back();
		}
		break;
	}
	case EditorCommandType::RemoveNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.insert(notes.begin() + cmd.noteIndex, cmd.previousNote);
		break;
	}
	case EditorCommandType::InsertNote:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		notes.erase(notes.begin() + cmd.noteIndex);
		break;
	}
	case EditorCommandType::SetTempo:
	{
		m_composition.tempo = cmd.previousTempo;
		break;
	}
	case EditorCommandType::SetInstrument:
	{
		m_composition.tracks[static_cast<size_t>(cmd.trackIndex)]
			.instrument = cmd.previousInstrument;
		break;
	}
	case EditorCommandType::PasteNotes:
	{
		auto& notes = m_composition
			.tracks[static_cast<size_t>(cmd.trackIndex)].notes;
		const auto count = static_cast<int>(cmd.notesBatch.size());
		notes.erase(notes.begin() + cmd.noteIndex,
		            notes.begin() + cmd.noteIndex + count);
		break;
	}
	}
}

// ─── ユーティリティ (private) ───

/// @brief ノートインデックスが有効か
inline bool MmlEditorModel::isValidNote(int trackIdx, int noteIdx) const noexcept
{
	if (!isValidTrack(trackIdx))
	{
		return false;
	}
	const auto& notes = m_composition
		.tracks[static_cast<size_t>(trackIdx)].notes;
	return noteIdx >= 0 &&
	       noteIdx < static_cast<int>(notes.size());
}

/// @brief 音名を文字に変換する
inline char MmlEditorModel::pitchToChar(Pitch p) noexcept
{
	constexpr char table[] = "CCDDEFFGGAAB";
	return table[static_cast<int>(p) % 12];
}

/// @brief 文字が音名か
inline bool MmlEditorModel::isPitchChar(char ch) noexcept
{
	return ch == 'C' || ch == 'D' || ch == 'E' || ch == 'F' ||
	       ch == 'G' || ch == 'A' || ch == 'B' ||
	       ch == 'c' || ch == 'd' || ch == 'e' || ch == 'f' ||
	       ch == 'g' || ch == 'a' || ch == 'b';
}

/// @brief 文字を音名に変換する
inline Pitch MmlEditorModel::charToPitch(char ch) noexcept
{
	switch (ch)
	{
	case 'C': case 'c': return Pitch::C;
	case 'D': case 'd': return Pitch::D;
	case 'E': case 'e': return Pitch::E;
	case 'F': case 'f': return Pitch::F;
	case 'G': case 'g': return Pitch::G;
	case 'A': case 'a': return Pitch::A;
	case 'B': case 'b': return Pitch::B;
	default: return Pitch::C;
	}
}

/// @brief 文字列から数値をパースする
inline int MmlEditorModel::parseNumber(const std::string& str, size_t& pos)
{
	int val = 0;
	while (pos < str.size() && str[pos] >= '0' && str[pos] <= '9')
	{
		val = val * 10 + (str[pos] - '0');
		++pos;
	}
	return val;
}

/// @brief MML文字列から音価をパースする
inline void MmlEditorModel::parseDuration(const std::string& str, size_t& pos,
                                          MmlNote& note)
{
	const int val = parseNumber(str, pos);
	if (val > 0)
	{
		switch (val)
		{
		case 1:  note.duration = Duration::Whole; break;
		case 2:  note.duration = Duration::Half; break;
		case 4:  note.duration = Duration::Quarter; break;
		case 8:  note.duration = Duration::Eighth; break;
		case 16: note.duration = Duration::Sixteenth; break;
		default: note.duration = Duration::Quarter; break;
		}
	}
	if (pos < str.size() && str[pos] == '.')
	{
		note.dotted = true;
		++pos;
	}
}

/// @brief 簡易JSON文字列値抽出
inline std::string MmlEditorModel::extractString(
	const std::string& json, const std::string& key)
{
	const std::string pattern = "\"" + key + "\":\"";
	const auto start = json.find(pattern);
	if (start == std::string::npos)
	{
		return "";
	}
	const auto valStart = start + pattern.size();
	const auto valEnd = json.find('"', valStart);
	if (valEnd == std::string::npos)
	{
		return "";
	}
	return json.substr(valStart, valEnd - valStart);
}

/// @brief 簡易JSON整数値抽出
inline int MmlEditorModel::extractInt(
	const std::string& json, const std::string& key)
{
	const std::string pattern = "\"" + key + "\":";
	const auto start = json.find(pattern);
	if (start == std::string::npos)
	{
		return 0;
	}
	const auto valStart = start + pattern.size();
	size_t pos = valStart;
	return parseNumber(json, pos);
}

} // namespace mitiru::audio
