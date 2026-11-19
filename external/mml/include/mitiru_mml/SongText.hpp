#pragma once
/// @file SongText.hpp
/// @brief 曲のテキスト (# の指示の行と、#track の行に続く MML) を読んで、トラックの宣言と指示の行に分ける
/// @details ChipSong (MultiChipSequencer) と ModernSongScript (ModernSong) が同じ読み取りを使う。
///          どちらも同じ `#track` の行で、全部のチップ (opna、opm、opl、opl2、opl3、sn76489、ay8910、nes、
///          synth、drums、sampler) と、voice、ch、pan、vol、group、drum、part の項目を読める。
///          # で始まるほかの行は、意味を調べずに directives へ順番どおり渡す (読むのは呼び出し側)。
///
/// 行の書き方:
///   #track [名前] chip=<チップ> [voice=<名前か番号>] [ch=<チャンネル>] [pan=left|center|right] [vol=0〜15]
///          [group=<グループ>] [drum=<種類>] [part=fm|ssg|rhythm] [rhythm] [noswing]
///   名前は #track の直後の 1 語だけ。省くと track1、track2 ... になる。
///   chip を省くと OPNA の FM。opna-ssg は chip=opna part=ssg、opna-rhythm は chip=opna rhythm と同じ。
///   最初の #track より前の MML は、曲に #track が 1 つも無いときだけ、OPNA の FM 1 本になる。#track があるのに
///   その前に MML があるときは、どのトラックにも属さないのでエラーにする。
///   ; で始まる行はコメント。
///   send=reverb:30,delay:10 は、そのトラックからエフェクトへ送る量 (% 0〜100)。エフェクトは reverb、delay、chorus。
///   ModernSongScript は ModernSong のミックスに効かせる (SendBus)。ChipSong は send= を読んで無視する。
///   #let と #macro の行はここで読んで、MML の中の %名前、整数の式 (%a+1)、!名前、!名前(引数, ...) を展開する (SongDefinitions.hpp)。

#include <mitiru_mml/ChipCatalog.hpp>
#include <mitiru_mml/SongDefinitions.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief トラックからエフェクトへの送り 1 つ
struct EffectSend
{
	std::string effect;    ///< reverb、delay、chorus のどれか
	float percent = 0.0f;  ///< 送る量 (% 0〜100)
};

/// @brief `#track` の宣言 1 つぶん
struct ChipTrackDecl
{
	ChipKind kind = ChipKind::OpnaFm;
	int channel = -1;      ///< Driver のチャンネル番号。-1 は自動で割り当てる
	int voice = 0;         ///< 音色番号
	bool voiceGiven = false; ///< voice= が書かれていた
	int pan = -1;          ///< 0 左、1 中央、2 右。-1 は指定なし
	int volume = -1;       ///< 0〜15。-1 は指定なし
	std::string mml;       ///< 続く行をつないだ MML (マクロと変数を展開したあと)
	std::vector<SourceOrigin> origins; ///< mml の 1 文字ごとの出どころ (テキストの行と列)
	int line = 0;          ///< 宣言の行番号 (1 から)
	std::string name;      ///< トラックの名前 (ダッキングの合図に使う)。省くと track<通し番号>
	std::string group = "main"; ///< ModernSong のグループ (バス)
	int drum = -1;         ///< drums の種類 0〜7。-1 は GM の割り当て (kit)
	bool percussion = false; ///< sampler のパーカッション (チャンネル 9) に置く
	bool rhythm = false;   ///< MML をリズム文法 (B S H F Y I) として読む
	bool swing = true;     ///< 曲のスウィングを掛ける
	std::vector<EffectSend> sends; ///< send= で書いた、エフェクトへの送り
};

/// @brief 指示の行 (# で始まる、#track 以外の行)
struct SongDirectiveLine
{
	std::string text;      ///< 前後の空白を除いた行
	int line = 0;          ///< 行番号 (1 から)
};

/// @brief テキストを読んだ結果
struct SongTextResult
{
	std::vector<ChipTrackDecl> tracks;
	std::vector<SongDirectiveLine> directives;
	std::vector<std::string> errors;    ///< 読めなかった行の説明 ("N 行目: ..." か "N 行目 M 列: ...")
	std::vector<SongProblem> problems;  ///< errors と同じ物を、行と列に分けたもの (同じ順)
	SongDefinitions definitions;        ///< #let と #macro の定義 (展開された回数を含む)
};

/// @brief 曲のテキストの読み取り
class SongText
{
public:
	[[nodiscard]] static SongTextResult parse(std::string_view text)
	{
		using namespace chip_catalog;
		SongTextResult result;
		ErrorList errors{result};
		const std::vector<Line> lines = splitLines(text);
		std::vector<bool> consumed(lines.size(), false);
		readDefinitions(lines, consumed, result.definitions, errors);

		const bool hasTrackLine = [&]()
		{
			for (std::size_t i = 0; i < lines.size(); ++i)
			{
				if (!consumed[i] && isTrackLine(lines[i].text))
				{
					return true;
				}
			}
			return false;
		}();

		ChipTrackDecl* current = nullptr;
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			const Line& line = lines[index];
			if (consumed[index] || line.text.empty() || line.text[0] == ';')
			{
				continue;
			}
			if (line.text[0] == '#')
			{
				if (line.text.size() < 2 || !std::isalpha(static_cast<unsigned char>(line.text[1])))
				{
					errors.add(line.number, "# のあとに指示の名前が要ります");
					continue;
				}
				const std::string directive = substituteDirective(line, result.definitions, errors);
				if (isTrackLine(line.text))
				{
					result.tracks.emplace_back();
					current = &result.tracks.back();
					current->line = line.number;
					current->name = "track" + std::to_string(result.tracks.size());
					readTrackLine(splitWords(directive), directive, line.indent, *current, errors);
				}
				else
				{
					result.directives.push_back({directive, line.number});
				}
				continue;
			}
			if (current == nullptr)
			{
				if (hasTrackLine)
				{
					errors.add(line.number, "最初の #track より前に MML があります");
					continue;
				}
				// #track が 1 つも無い曲は、全体が OPNA の FM 1 本
				result.tracks.emplace_back();
				current = &result.tracks.back();
				current->line = line.number;
				current->name = "track1";
			}
			appendMml(*current, line, result.definitions, errors);
		}
		return result;
	}

	/// @brief エラーの先頭に付ける「N 行目: 」
	[[nodiscard]] static std::string prefix(int line)
	{
		return std::to_string(line) + " 行目: ";
	}

private:
	static constexpr std::size_t MAX_MACRO_PARAMS = 16;

	struct Line
	{
		std::string text;  ///< 前後の空白を除いた行
		int number = 0;    ///< 1 から
		int indent = 0;    ///< 行頭の空白の数 (列の数え直しに使う)
	};

	/// @brief errors と problems に同じ問題を同じ順で足す
	struct ErrorList
	{
		SongTextResult& result;

		/// @param column 0 なら列を付けない
		void add(int line, int column, std::string message)
		{
			SongProblem problem{line, column, std::move(message)};
			result.errors.push_back(problem.text());
			result.problems.push_back(std::move(problem));
		}

		void add(int line, std::string message)
		{
			add(line, 0, std::move(message));
		}

		void addAll(const std::vector<SongProblem>& found)
		{
			for (const SongProblem& p : found)
			{
				add(p.line, p.column, p.message);
			}
		}
	};

	[[nodiscard]] static std::vector<Line> splitLines(std::string_view text)
	{
		std::vector<Line> lines;
		std::size_t pos = 0;
		int number = 0;
		while (pos <= text.size())
		{
			const std::size_t end = std::min(text.find('\n', pos), text.size());
			++number;
			const std::string_view raw = text.substr(pos, end - pos);
			int indent = 0;
			while (indent < static_cast<int>(raw.size()) && (raw[static_cast<std::size_t>(indent)] == ' ' || raw[static_cast<std::size_t>(indent)] == '	'))
			{
				++indent;
			}
			lines.push_back({chip_catalog::trim(raw), number, indent});
			pos = end + 1;
		}
		return lines;
	}

	/// @brief send=reverb:30,delay:10 を読んで track.sends に足す
	static void readSends(const std::string& value, ChipTrackDecl& track, int column, ErrorList& errors)
	{
		using namespace chip_catalog;
		std::size_t begin = 0;
		while (begin <= value.size())
		{
			const std::size_t comma = std::min(value.find(',', begin), value.size());
			const std::string item = value.substr(begin, comma - begin);
			begin = comma + 1;
			const std::size_t colon = item.find(':');
			const std::string effect = lower(item.substr(0, colon));
			if (colon == std::string::npos || colon + 1 >= item.size())
			{
				errors.add(track.line, column, "send は 名前:量 の形で書きます ('" + item + "')。例: send=reverb:30,delay:10");
				continue;
			}
			if (effect != "reverb" && effect != "delay" && effect != "chorus")
			{
				errors.add(track.line, column, "エフェクト '" + effect + "' は使えません (reverb、delay、chorus)");
				continue;
			}
			const std::string amountText = item.substr(colon + 1);
			char* end = nullptr;
			const float amount = std::strtof(amountText.c_str(), &end);
			if (end == amountText.c_str() || *end != '\0')
			{
				errors.add(track.line, column, "send の量 '" + amountText + "' は数で書きます (% 0〜100)");
				continue;
			}
			if (!(amount >= 0.0f && amount <= 100.0f))
			{
				errors.add(track.line, column, "send " + effect + " の量 " + amountText + " は 0〜100 (%) の外です");
				continue;
			}
			const bool dup = std::any_of(track.sends.begin(), track.sends.end(), [&](const EffectSend& e)
				{
					return e.effect == effect;
				});
			if (dup)
			{
				errors.add(track.line, column, "send で " + effect + " が 2 回書かれています");
				continue;
			}
			track.sends.push_back({effect, amount});
		}
	}

	/// @brief # の指示の行の %名前 を変数の値にする (#sf2 のパスは触らない)
	[[nodiscard]] static std::string substituteDirective(const Line& line, SongDefinitions& defs, ErrorList& errors)
	{
		const std::vector<std::string> words = chip_catalog::splitWords(line.text);
		if (!words.empty() && chip_catalog::lower(words[0]) == "#sf2")
		{
			return line.text;
		}
		std::vector<SongProblem> problems;
		std::string out = defs.substituteVariables(line.text, line.number, line.indent, problems);
		errors.addAll(problems);
		return out;
	}

	/// @brief MML の 1 行を、マクロと変数を展開しながらトラックの MML の末尾に足す
	static void appendMml(ChipTrackDecl& track, const Line& line, SongDefinitions& defs, ErrorList& errors)
	{
		if (!track.mml.empty())
		{
			track.mml += '\n';
			track.origins.push_back({line.number, line.indent + 1, -1});
		}
		std::vector<SourcePos> pos(line.text.size());
		for (std::size_t i = 0; i < pos.size(); ++i)
		{
			pos[i] = {line.number, line.indent + static_cast<int>(i) + 1};
		}
		std::vector<SongProblem> problems;
		defs.expandLine(line.text, pos, track.mml, track.origins, &problems);
		errors.addAll(problems);
	}

	/// @brief 行のうち ; から後ろ (注釈) を除いた長さ
	[[nodiscard]] static std::size_t codeLength(const std::string& text)
	{
		const std::size_t semicolon = text.find(';');
		return semicolon == std::string::npos ? text.size() : semicolon;
	}

	[[nodiscard]] static std::size_t skipSpaces(const std::string& text, std::size_t at)
	{
		while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
		{
			++at;
		}
		return at;
	}

	/// @brief #let と #macro (と #endmacro) の行を全部読んで、読んだ行の consumed を true にする
	static void readDefinitions(const std::vector<Line>& lines, std::vector<bool>& consumed, SongDefinitions& defs,
		ErrorList& errors)
	{
		std::vector<SongProblem> problems;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			const Line& line = lines[i];
			if (line.text.size() < 2 || line.text[0] != '#' || !std::isalpha(static_cast<unsigned char>(line.text[1])))
			{
				continue;
			}
			const std::string head = chip_catalog::lower(chip_catalog::splitWords(line.text)[0]);
			if (head == "#let")
			{
				consumed[i] = true;
				readLet(line, defs, problems);
			}
			else if (head == "#macro")
			{
				consumed[i] = true;
				i = readMacro(lines, i, consumed, defs, problems);
			}
			else if (head == "#endmacro")
			{
				consumed[i] = true;
				problems.push_back({line.number, line.indent + 1, "#endmacro に対応する #macro がありません"});
			}
		}
		defs.validate(problems);
		errors.addAll(problems);
	}

	/// @brief 名前を読む。読めなければ problems に積んで空文字を返す
	[[nodiscard]] static std::string readDefinitionName(const Line& line, std::size_t& at, const char* what,
		std::vector<SongProblem>& problems)
	{
		at = skipSpaces(line.text, at);
		const std::size_t begin = at;
		while (at < line.text.size() && (std::isalnum(static_cast<unsigned char>(line.text[at])) != 0 || line.text[at] == '_'))
		{
			++at;
		}
		const std::string name = line.text.substr(begin, at - begin);
		if (!SongDefinitions::isName(name))
		{
			problems.push_back({line.number, line.indent + static_cast<int>(begin) + 1,
				std::string(what) + "の名前は、英字か _ で始まり英数字と _ が続く形です"});
			return {};
		}
		return name;
	}

	/// @brief #let 名前 = 整数
	static void readLet(const Line& line, SongDefinitions& defs, std::vector<SongProblem>& problems)
	{
		std::size_t at = 4;
		const std::string name = readDefinitionName(line, at, "変数", problems);
		if (name.empty())
		{
			return;
		}
		at = skipSpaces(line.text, at);
		if (at >= line.text.size() || line.text[at] != '=')
		{
			problems.push_back({line.number, line.indent + static_cast<int>(at) + 1, "#let " + name + " のあとに = と整数が要ります"});
			return;
		}
		at = skipSpaces(line.text, at + 1);
		const std::size_t valueBegin = at;
		std::size_t valueEnd = codeLength(line.text);
		while (valueEnd > valueBegin && (line.text[valueEnd - 1] == ' ' || line.text[valueEnd - 1] == '\t'))
		{
			--valueEnd;
		}
		const std::string valueText = line.text.substr(valueBegin, valueEnd - valueBegin);
		char* end = nullptr;
		const long value = std::strtol(valueText.c_str(), &end, 10);
		if (valueText.empty() || *end != '\0' || value < -100000 || value > 100000)
		{
			problems.push_back({line.number, line.indent + static_cast<int>(valueBegin) + 1,
				"変数 " + name + " の値 '" + valueText + "' は整数 (-100000〜100000) で書きます"});
			return;
		}
		VariableDef def;
		def.name = name;
		def.value = static_cast<int>(value);
		def.at = {line.number, line.indent + 1};
		defs.addVariable(std::move(def), problems);
	}

	/// @brief #macro 名前(a, b) の ( ) の中の引数の名前を読む
	/// @param at ( の位置。読めたら ) の次の位置になる
	[[nodiscard]] static bool readParams(const Line& line, std::size_t& at, MacroDef& def, std::vector<SongProblem>& problems)
	{
		const std::size_t close = line.text.find(')', at);
		if (close == std::string::npos)
		{
			problems.push_back({line.number, line.indent + static_cast<int>(at) + 1,
				"#macro " + def.name + " の引数の ( に対応する ) がありません"});
			return false;
		}
		bool ok = true;
		std::size_t begin = at + 1;
		while (true)
		{
			const std::size_t comma = std::min(line.text.find(',', begin), close);
			std::size_t from = skipSpaces(line.text, begin);
			std::size_t to = comma;
			while (to > from && (line.text[to - 1] == ' ' || line.text[to - 1] == '	'))
			{
				--to;
			}
			const std::string param = line.text.substr(from, to - from);
			const int column = line.indent + static_cast<int>(std::min(from, comma)) + 1;
			if (!SongDefinitions::isName(param))
			{
				problems.push_back({line.number, column, "マクロ '" + def.name + "' の引数の名前は、英字か _ で始まり英数字と _ が続く形です"});
				ok = false;
			}
			else if (std::find(def.params.begin(), def.params.end(), param) != def.params.end())
			{
				problems.push_back({line.number, column, "マクロ '" + def.name + "' の引数 '" + param + "' が 2 回書かれています"});
				ok = false;
			}
			else
			{
				def.params.push_back(param);
			}
			if (comma >= close)
			{
				break;
			}
			begin = comma + 1;
		}
		if (def.params.size() > MAX_MACRO_PARAMS)
		{
			problems.push_back({line.number, line.indent + static_cast<int>(at) + 1,
				"マクロ '" + def.name + "' の引数は " + std::to_string(MAX_MACRO_PARAMS) + " 個までです"});
			ok = false;
		}
		at = close + 1;
		return ok;
	}

	/// @brief #macro 名前 = MML、または #macro 名前 から #endmacro までの行
	/// @return 読んだ最後の行の添字
	static std::size_t readMacro(const std::vector<Line>& lines, std::size_t first, std::vector<bool>& consumed,
		SongDefinitions& defs, std::vector<SongProblem>& problems)
	{
		const Line& line = lines[first];
		std::size_t at = 6;
		const std::string name = readDefinitionName(line, at, "マクロ", problems);
		if (name.empty())
		{
			return first;
		}
		MacroDef def;
		def.name = name;
		def.at = {line.number, line.indent + 1};
		if (at < line.text.size() && line.text[at] == '(' && !readParams(line, at, def, problems))
		{
			return first;
		}
		at = skipSpaces(line.text, at);
		std::size_t last = first;
		if (at < line.text.size() && line.text[at] == '=')
		{
			appendBody(def, line, at + 1, codeLength(line.text));
		}
		else if (at >= line.text.size() || line.text[at] == ';')
		{
			bool terminated = false;
			last = readMacroBlock(lines, first, consumed, def, problems, terminated);
			if (!terminated)
			{
				return last;
			}
		}
		else
		{
			problems.push_back({line.number, line.indent + static_cast<int>(at) + 1,
				"#macro " + name + " のあとは = と MML、または行末 (#endmacro までが本体) です"});
			return first;
		}
		if (def.body.empty())
		{
			problems.push_back({line.number, line.indent + 1, "マクロ '" + name + "' の本体が空です"});
			return last;
		}
		defs.addMacro(std::move(def), problems);
		return last;
	}

	/// @brief 行の [from, to) を本体に足す (前の行があれば空白 1 つでつなぐ)
	static void appendBody(MacroDef& def, const Line& line, std::size_t from, std::size_t to)
	{
		from = skipSpaces(line.text, from);
		while (to > from && (line.text[to - 1] == ' ' || line.text[to - 1] == '\t'))
		{
			--to;
		}
		if (from >= to)
		{
			return;
		}
		if (!def.body.empty())
		{
			def.body += ' ';
			def.bodyPos.push_back(def.bodyPos.back());
		}
		for (std::size_t i = from; i < to; ++i)
		{
			def.body += line.text[i];
			def.bodyPos.push_back({line.number, line.indent + static_cast<int>(i) + 1});
		}
	}

	/// @brief 複数行のマクロの本体 (#endmacro まで)
	/// @return 読んだ最後の行の添字
	static std::size_t readMacroBlock(const std::vector<Line>& lines, std::size_t first, std::vector<bool>& consumed,
		MacroDef& def, std::vector<SongProblem>& problems, bool& terminated)
	{
		std::size_t last = first;
		for (std::size_t j = first + 1; j < lines.size(); ++j)
		{
			const Line& body = lines[j];
			const bool directive = body.text.size() >= 2 && body.text[0] == '#'
				&& std::isalpha(static_cast<unsigned char>(body.text[1])) != 0;
			if (directive && chip_catalog::lower(chip_catalog::splitWords(body.text)[0]) == "#endmacro")
			{
				consumed[j] = true;
				terminated = true;
				return j;
			}
			if (directive)
			{
				break;
			}
			consumed[j] = true;
			last = j;
			appendBody(def, body, 0, codeLength(body.text));
		}
		problems.push_back({lines[first].number, lines[first].indent + 1,
			"マクロ '" + def.name + "' の #endmacro がありません (本体の途中に # の行は書けません)"});
		return last;
	}

	[[nodiscard]] static bool isTrackLine(const std::string& line)
	{
		if (line.size() < 2 || line[0] != '#' || !std::isalpha(static_cast<unsigned char>(line[1])))
		{
			return false;
		}
		const std::vector<std::string> words = chip_catalog::splitWords(line);
		return chip_catalog::lower(words[0]) == "#track";
	}

	[[nodiscard]] static int drumIndex(const std::string& lowered)
	{
		if (lowered == "kick") return 0;
		if (lowered == "snare") return 1;
		if (lowered == "hat" || lowered == "closedhat" || lowered == "hihat") return 2;
		if (lowered == "tom") return 3;
		if (lowered == "cymbal" || lowered == "crash") return 4;
		if (lowered == "rim") return 5;
		if (lowered == "clap") return 6;
		if (lowered == "openhat") return 7;
		if (lowered == "kit") return -1;
		return -2;
	}

	/// @brief #track の行を読んで track に入れる
	static void readTrackLine(const std::vector<std::string>& words, const std::string& lineText, int indent,
		ChipTrackDecl& track, ErrorList& errors)
	{
		using namespace chip_catalog;
		std::string channelText;
		std::string voiceText;
		std::string drumText;
		std::string partText;
		bool rhythmFlag = false;
		std::size_t cursor = 0;
		for (std::size_t i = 1; i < words.size(); ++i)
		{
			const std::size_t found = lineText.find(words[i], cursor);
			const std::size_t wordAt = (found == std::string::npos) ? cursor : found;
			cursor = wordAt + words[i].size();
			const int column = indent + static_cast<int>(wordAt) + 1;
			const std::size_t eq = words[i].find('=');
			if (eq == std::string::npos)
			{
				const std::string flag = lower(words[i]);
				if (flag == "rhythm")
				{
					rhythmFlag = true;
				}
				else if (flag == "noswing")
				{
					track.swing = false;
				}
				else if (i == 1)
				{
					track.name = words[i];
				}
				else
				{
					errors.add(track.line, "'" + words[i] + "' は key=value の形ではありません (名前は #track の直後の 1 語だけです)");
				}
				continue;
			}
			if (eq == 0 || eq + 1 == words[i].size())
			{
				errors.add(track.line, "'" + words[i] + "' は key=value の形ではありません");
				continue;
			}
			const std::string key = lower(words[i].substr(0, eq));
			const std::string value = words[i].substr(eq + 1);
			if (key == "chip")
			{
				if (const auto kind = kindFromName(value))
				{
					track.kind = *kind;
				}
				else
				{
					errors.add(track.line, "チップ '" + value + "' は使えません (opna、opm、opl、opl2、opl3、sn76489、ay8910、nes、synth、drums、sampler)");
				}
			}
			else if (key == "voice")
			{
				voiceText = value;
			}
			else if (key == "ch")
			{
				channelText = value;
			}
			else if (key == "pan")
			{
				const std::string v = lower(value);
				if (v == "left" || v == "0") track.pan = 0;
				else if (v == "center" || v == "1") track.pan = 1;
				else if (v == "right" || v == "2") track.pan = 2;
				else errors.add(track.line, "pan は left、center、right のどれかです");
			}
			else if (key == "vol")
			{
				const auto v = toInt(value);
				if (v && *v <= 15)
				{
					track.volume = *v;
				}
				else
				{
					errors.add(track.line, "vol は 0〜15 です");
				}
			}
			else if (key == "group")
			{
				track.group = value;
			}
			else if (key == "drum")
			{
				drumText = value;
			}
			else if (key == "percussion")
			{
				track.percussion = (lower(value) == "on");
			}
			else if (key == "part")
			{
				partText = lower(value);
			}
			else if (key == "send")
			{
				readSends(value, track, column, errors);
			}
			else
			{
				errors.add(track.line, "知らない項目 '" + key + "' です");
			}
		}

		resolveOpnaPart(track, partText, rhythmFlag, errors);
		if (!drumText.empty())
		{
			resolveDrum(track, drumText, errors);
		}
		if (!voiceText.empty())
		{
			if (const auto voice = voiceFromName(track.kind, voiceText))
			{
				track.voice = *voice;
				track.voiceGiven = true;
			}
			else
			{
				errors.add(track.line, "音色 '" + voiceText + "' はこのチップにありません");
			}
		}
		if (!channelText.empty())
		{
			if (const auto channel = channelFromName(track.kind, channelText))
			{
				track.channel = *channel;
			}
			else
			{
				errors.add(track.line, "チャンネル '" + channelText + "' を読めません");
			}
		}
	}

	/// @brief part= と rhythm の語を、OPNA の FM、SSG、リズムの区別とリズム文法の指定に直す
	static void resolveOpnaPart(ChipTrackDecl& track, const std::string& part, bool rhythmFlag,
		ErrorList& errors)
	{
		if (!part.empty())
		{
			if (track.kind != ChipKind::OpnaFm)
			{
				errors.add(track.line, "part= は chip=opna だけに付けます");
			}
			else if (part == "ssg")
			{
				track.kind = ChipKind::OpnaSsg;
			}
			else if (part == "rhythm")
			{
				track.kind = ChipKind::OpnaRhythm;
			}
			else if (part != "fm")
			{
				errors.add(track.line, "part は fm、ssg、rhythm のどれかです");
			}
		}
		if (rhythmFlag)
		{
			if (track.kind == ChipKind::OpnaFm)
			{
				track.kind = ChipKind::OpnaRhythm;
			}
			else if (track.kind != ChipKind::Drums && track.kind != ChipKind::Sampler && track.kind != ChipKind::OpnaRhythm)
			{
				errors.add(track.line, "rhythm はこのチップには付けられません (opna、drums、sampler)");
			}
		}
		track.rhythm = rhythmFlag || track.kind == ChipKind::OpnaRhythm;
	}

	static void resolveDrum(ChipTrackDecl& track, const std::string& text, ErrorList& errors)
	{
		const int d = drumIndex(chip_catalog::lower(text));
		if (d == -2)
		{
			errors.add(track.line, "ドラムの種類 '" + text + "' を知りません (kick snare hat tom cymbal rim clap openhat kit)");
		}
		else if (track.kind == ChipKind::Sampler)
		{
			track.percussion = true; // サンプラーのパーカッションは、鍵がドラムの種類
		}
		else
		{
			track.drum = d;
		}
	}
};

} // namespace mitiru_mml
