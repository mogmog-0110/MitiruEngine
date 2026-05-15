#pragma once

/// @file MmlEditorModel.hpp
/// @brief MML作曲エディタのデータモデル
/// @details MML (Music Macro Language) のノート・トラック・コンポジションを
///          管理し、エディタGUI用のUndoRedo・選択・クリップボード・再生カーソル
///          追跡機能を提供する。MML文字列へのエクスポート/インポートもサポート。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::audio
{

/// @brief 音名 (C4-B7)
enum class Pitch : uint8_t
{
	C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B,
};

/// @brief オクターブ (4-7)
enum class Octave : uint8_t
{
	O4 = 4,
	O5 = 5,
	O6 = 6,
	O7 = 7,
};

/// @brief 音符の長さ
enum class Duration : uint8_t
{
	Whole = 1,       ///< 全音符
	Half = 2,        ///< 2分音符
	Quarter = 4,     ///< 4分音符
	Eighth = 8,      ///< 8分音符
	Sixteenth = 16,  ///< 16分音符
};

/// @brief 臨時記号
enum class Accidental : uint8_t
{
	Natural = 0,  ///< ナチュラル
	Sharp,        ///< シャープ
	Flat,         ///< フラット
};

/// @brief MMLノート
struct MmlNote
{
	Pitch pitch = Pitch::C;                  ///< 音名
	Octave octave = Octave::O4;              ///< オクターブ
	Duration duration = Duration::Quarter;   ///< 長さ
	Accidental accidental = Accidental::Natural; ///< 臨時記号
	bool dotted = false;                     ///< 付点音符か
	bool rest = false;                       ///< 休符か

	/// @brief 拍数を取得する（4分音符 = 1.0拍）
	/// @return 拍数
	[[nodiscard]] float beats() const noexcept
	{
		const float base = 4.0f / static_cast<float>(duration);
		return dotted ? base * 1.5f : base;
	}
};

/// @brief MMLトラック
struct MmlTrack
{
	std::string name;                 ///< トラック名
	int channel = 0;                  ///< MIDIチャンネル
	std::vector<MmlNote> notes;       ///< ノート列
	std::string instrument = "Piano"; ///< 楽器名
	int volume = 100;                 ///< ボリューム [0, 127]
	int tempo = 120;                  ///< テンポ (BPM)

	/// @brief トラックの合計拍数を取得する
	/// @return 合計拍数
	[[nodiscard]] float totalBeats() const noexcept
	{
		float total = 0.0f;
		for (const auto& note : notes)
		{
			total += note.beats();
		}
		return total;
	}
};

/// @brief 拍子記号
struct TimeSignature
{
	int numerator = 4;    ///< 分子（1小節あたりの拍数）
	int denominator = 4;  ///< 分母（1拍の音符の種類）
};

/// @brief MMLコンポジション
struct MmlComposition
{
	std::vector<MmlTrack> tracks;  ///< トラック一覧
	std::string title;             ///< 曲名
	std::string author;            ///< 作曲者
	int tempo = 120;               ///< グローバルテンポ (BPM)
	TimeSignature timeSignature;   ///< 拍子記号
};

/// @brief ノート範囲選択
struct NoteSelection
{
	int trackIndex = -1;  ///< 選択中のトラックインデックス（-1 = 未選択）
	int startIndex = 0;   ///< 選択開始ノートインデックス
	int endIndex = 0;     ///< 選択終了ノートインデックス（排他）

	/// @brief 選択が有効か
	[[nodiscard]] bool valid() const noexcept
	{
		return trackIndex >= 0 && startIndex < endIndex;
	}

	/// @brief 選択されているノート数
	[[nodiscard]] int count() const noexcept
	{
		return valid() ? (endIndex - startIndex) : 0;
	}
};

/// @brief Undo/Redoコマンドの種別
enum class EditorCommandType : uint8_t
{
	AddNote,
	RemoveNote,
	InsertNote,
	SetTempo,
	SetInstrument,
	PasteNotes,
};

/// @brief Undo/Redoコマンド
struct EditorCommand
{
	EditorCommandType type = EditorCommandType::AddNote;
	int trackIndex = 0;             ///< 対象トラックインデックス
	int noteIndex = 0;              ///< 対象ノートインデックス
	MmlNote note;                   ///< 追加・挿入時のノート
	MmlNote previousNote;           ///< 上書き前のノート（Undo用）
	std::vector<MmlNote> notesBatch; ///< ペースト時の複数ノート
	int previousTempo = 120;        ///< 変更前のテンポ
	int newTempo = 120;             ///< 変更後のテンポ
	std::string previousInstrument; ///< 変更前の楽器
	std::string newInstrument;      ///< 変更後の楽器
};

/// @brief MML作曲エディタモデル
/// @details MMLコンポジションの編集操作・Undo/Redo・選択・クリップボード・
///          MML文字列変換・再生カーソル追跡を一元管理する。
///
/// @code
/// mitiru::audio::MmlEditorModel editor;
/// editor.addTrack("Lead", 0, "Square");
/// editor.addNote(0, MmlNote{Pitch::C, Octave::O4, Duration::Quarter});
/// editor.addNote(0, MmlNote{Pitch::E, Octave::O4, Duration::Quarter});
/// editor.addNote(0, MmlNote{Pitch::G, Octave::O4, Duration::Half});
/// std::string mml = editor.toMmlString();
/// editor.undo(); // G音を取り消し
/// editor.redo(); // G音を再適用
/// @endcode
class MmlEditorModel
{
public:
	/// @brief デフォルトコンストラクタ
	MmlEditorModel() = default;

	// ─── トラック管理 ───

	/// @brief トラックを追加する
	/// @param name トラック名
	/// @param channel MIDIチャンネル
	/// @param instrument 楽器名
	void addTrack(std::string_view name, int channel,
	              std::string_view instrument = "Piano")
	{
		MmlTrack track;
		track.name = std::string(name);
		track.channel = channel;
		track.instrument = std::string(instrument);
		track.tempo = m_composition.tempo;
		m_composition.tracks.push_back(std::move(track));
	}

	/// @brief トラックを削除する
	/// @param trackIndex トラックインデックス
	void removeTrack(int trackIndex)
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

	/// @brief トラック数を取得する
	[[nodiscard]] int trackCount() const noexcept
	{
		return static_cast<int>(m_composition.tracks.size());
	}

	/// @brief トラックを取得する
	/// @param trackIndex トラックインデックス
	/// @return トラックへのconst参照
	[[nodiscard]] const MmlTrack& track(int trackIndex) const
	{
		return m_composition.tracks.at(static_cast<size_t>(trackIndex));
	}

	// ─── ノート編集 ───

	/// @brief ノートをトラック末尾に追加する
	/// @param trackIndex トラックインデックス
	/// @param note 追加するノート
	void addNote(int trackIndex, const MmlNote& note)
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
	/// @param trackIndex トラックインデックス
	/// @param noteIndex ノートインデックス
	void removeNote(int trackIndex, int noteIndex)
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
	/// @param trackIndex トラックインデックス
	/// @param noteIndex 挿入位置
	/// @param note 挿入するノート
	void insertNote(int trackIndex, int noteIndex, const MmlNote& note)
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
	/// @param bpm テンポ (BPM)
	void setTempo(int bpm)
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
	/// @param trackIndex トラックインデックス
	/// @param instrument 楽器名
	void setInstrument(int trackIndex, std::string_view instrument)
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

	/// @brief 拍子記号を設定する
	/// @param numerator 分子
	/// @param denominator 分母
	void setTimeSignature(int numerator, int denominator)
	{
		m_composition.timeSignature.numerator = numerator;
		m_composition.timeSignature.denominator = denominator;
	}

	/// @brief 曲名を設定する
	/// @param title 曲名
	void setTitle(std::string_view title)
	{
		m_composition.title = std::string(title);
	}

	/// @brief 作曲者を設定する
	/// @param author 作曲者
	void setAuthor(std::string_view author)
	{
		m_composition.author = std::string(author);
	}

	// ─── MML文字列変換 ───

	/// @brief コンポジションをMML文字列にエクスポートする
	/// @return MML形式の文字列
	[[nodiscard]] std::string toMmlString() const
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
	/// @param mml MML文字列
	/// @note 簡易パーサーで主要なMMLコマンドを解析する
	void fromMmlString(std::string_view mml)
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
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
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
	/// @param json JSON文字列
	/// @note 簡易JSONパーサーにより主要なフィールドを解析する
	void fromJson(std::string_view json)
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
	/// @return 再生時間（秒）
	[[nodiscard]] float getDuration() const noexcept
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
	/// @return 取り消しが成功したか
	bool undo()
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
	/// @return やり直しが成功したか
	bool redo()
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

	/// @brief Undoスタックが空でないか
	[[nodiscard]] bool canUndo() const noexcept
	{
		return !m_undoStack.empty();
	}

	/// @brief Redoスタックが空でないか
	[[nodiscard]] bool canRedo() const noexcept
	{
		return !m_redoStack.empty();
	}

	// ─── 選択 ───

	/// @brief ノート範囲を選択する
	/// @param trackIndex トラックインデックス
	/// @param startIndex 開始ノートインデックス
	/// @param endIndex 終了ノートインデックス（排他）
	void selectNotes(int trackIndex, int startIndex, int endIndex)
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

	/// @brief 選択をクリアする
	void clearSelection() noexcept
	{
		m_selection = NoteSelection{};
	}

	/// @brief 現在の選択を取得する
	/// @return 選択情報
	[[nodiscard]] const NoteSelection& selectedNotes() const noexcept
	{
		return m_selection;
	}

	// ─── クリップボード ───

	/// @brief 選択範囲をクリップボードにコピーする
	void copy()
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
	void cut()
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
	/// @param trackIndex トラックインデックス
	/// @param noteIndex 挿入位置
	void paste(int trackIndex, int noteIndex)
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

	/// @brief クリップボードが空でないか
	[[nodiscard]] bool hasClipboardContent() const noexcept
	{
		return !m_clipboard.empty();
	}

	// ─── 再生カーソル ───

	/// @brief 再生カーソル位置を設定する（拍数単位）
	/// @param position カーソル位置（拍数）
	void setPlaybackCursor(float position) noexcept
	{
		m_playbackCursor = std::max(0.0f, position);
	}

	/// @brief 再生カーソル位置を取得する
	/// @return カーソル位置（拍数）
	[[nodiscard]] float playbackCursor() const noexcept
	{
		return m_playbackCursor;
	}

	/// @brief 再生カーソルを時間（秒）で進める
	/// @param deltaTime 経過時間（秒）
	void advancePlaybackCursor(float deltaTime) noexcept
	{
		if (m_composition.tempo <= 0)
		{
			return;
		}
		/// 秒 × (BPM / 60) = 拍数
		m_playbackCursor += deltaTime *
			(static_cast<float>(m_composition.tempo) / 60.0f);
	}

	/// @brief 再生カーソルをリセットする
	void resetPlaybackCursor() noexcept
	{
		m_playbackCursor = 0.0f;
	}

	// ─── コンポジションアクセサ ───

	/// @brief コンポジション全体を取得する
	[[nodiscard]] const MmlComposition& composition() const noexcept
	{
		return m_composition;
	}

private:
	/// @brief コマンドを実行しUndoスタックに積む
	void executeCommand(const EditorCommand& cmd)
	{
		applyCommand(cmd);
		m_undoStack.push_back(cmd);
		m_redoStack.clear();
	}

	/// @brief コマンドを適用する（状態変更）
	void applyCommand(const EditorCommand& cmd)
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
	void undoCommand(const EditorCommand& cmd)
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

	// ─── ユーティリティ ───

	/// @brief トラックインデックスが有効か
	[[nodiscard]] bool isValidTrack(int idx) const noexcept
	{
		return idx >= 0 &&
		       idx < static_cast<int>(m_composition.tracks.size());
	}

	/// @brief ノートインデックスが有効か
	[[nodiscard]] bool isValidNote(int trackIdx, int noteIdx) const noexcept
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
	[[nodiscard]] static char pitchToChar(Pitch p) noexcept
	{
		constexpr char table[] = "CCDDEFFGGAAB";
		return table[static_cast<int>(p) % 12];
	}

	/// @brief 文字が音名か
	[[nodiscard]] static bool isPitchChar(char ch) noexcept
	{
		return ch == 'C' || ch == 'D' || ch == 'E' || ch == 'F' ||
		       ch == 'G' || ch == 'A' || ch == 'B' ||
		       ch == 'c' || ch == 'd' || ch == 'e' || ch == 'f' ||
		       ch == 'g' || ch == 'a' || ch == 'b';
	}

	/// @brief 文字を音名に変換する
	[[nodiscard]] static Pitch charToPitch(char ch) noexcept
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
	[[nodiscard]] static int parseNumber(const std::string& str, size_t& pos)
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
	static void parseDuration(const std::string& str, size_t& pos,
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
	[[nodiscard]] static std::string extractString(
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
	[[nodiscard]] static int extractInt(
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

	MmlComposition m_composition;               ///< コンポジションデータ
	std::vector<EditorCommand> m_undoStack;      ///< Undoスタック
	std::vector<EditorCommand> m_redoStack;      ///< Redoスタック
	NoteSelection m_selection;                   ///< 選択状態
	std::vector<MmlNote> m_clipboard;            ///< クリップボード
	float m_playbackCursor = 0.0f;              ///< 再生カーソル位置（拍数）
};

} // namespace mitiru::audio
