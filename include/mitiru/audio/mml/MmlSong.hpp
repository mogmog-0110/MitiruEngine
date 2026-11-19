#pragma once

/// @file MmlSong.hpp
/// @brief MML のテキストをトラックの並びに読む共通部。assets/audio/<名前>.mml のベイク (MmlBake.hpp) と、
///        実行中の hud.mml (MmlRuntime.hpp) が同じ読み取りを使う。
/// @details 2 つの書き方の違いは MmlDialect の 2 つだけで、音源の指定 (@FM0〜@FM11、@SSG、@RHYTHM) の読み方は同じ。
///          File は 1 行が `;` から後ろのコメントを持ち、音源の無い行は直前のトラックの続き。
///          Inline は ABI v64 の文字列で、`;` と改行がトラックの区切り。

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru_mml/OpnaSequencer.hpp>

namespace mitiru::audio::mml
{

enum class TrackKind
{
	Fm,
	Ssg,
	Rhythm,
};

struct TrackSpec
{
	TrackKind   kind   = TrackKind::Fm;
	int         preset = 0;   ///< Fm の音色番号 (0〜11)
	std::string mml;
	int         line   = 0;   ///< トラックが始まった行 (1 始まり。Inline は 0)
};

struct ParsedSong
{
	std::vector<TrackSpec> tracks;
	std::string            error;   ///< 空なら成功。File の誤りは何行目かを含む

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

enum class MmlDialect
{
	File,     ///< .mml のファイル。行の `;` 以降はコメント、音源の無い行は直前のトラックの続き、知らない `@語` と MML に読めない文字は誤り
	Inline,   ///< hud.mml の文字列。`;` と改行がトラックの区切り、知らない `@語` は MML のコマンドとして渡す
};

namespace detail
{

inline constexpr int         kPresetCount     = 12;
inline constexpr std::size_t kMaxInlineTracks = 9;   // FM 6 + SSG 3

[[nodiscard]] inline std::string trim(std::string s)
{
	const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
	return s;
}

[[nodiscard]] inline std::string upperAscii(std::string s)
{
	for (auto& c : s) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
	return s;
}

[[nodiscard]] inline std::string lineError(int line, const std::string& what)
{
	return line > 0 ? std::to_string(line) + " 行目: " + what : what;
}

/// 行の中の音源の指定 (@FM3 など) を取り出して空白に置き換える。音源が無ければ source は nullopt
struct SourceScan
{
	std::optional<TrackSpec> source;
	std::string              rest;
	std::string              error;
};

[[nodiscard]] inline SourceScan scanSource(const std::string& text, int line, MmlDialect dialect)
{
	SourceScan out;
	out.rest = text;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] != '@') { continue; }
		std::size_t j = i + 1;
		while (j < text.size() && std::isalpha(static_cast<unsigned char>(text[j]))) { ++j; }
		const std::string word = upperAscii(text.substr(i + 1, j - i - 1));
		if (word.empty()) { continue; }   // @3 のような波形の指定はそのまま MML に渡す
		TrackSpec spec;
		spec.line = line;
		std::size_t end = j;
		if (word == "FM")
		{
			spec.kind = TrackKind::Fm;
			std::size_t k = j;
			while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) { ++k; }
			if (k > j)
			{
				if (k - j > 2) { out.error = lineError(line, "@FM の番号は 0〜11 です"); return out; }
				spec.preset = std::stoi(text.substr(j, k - j));
			}
			if (spec.preset < 0 || spec.preset >= kPresetCount)
			{
				out.error = lineError(line, "@FM の番号は 0〜11 です");
				return out;
			}
			end = k;
		}
		else if (word == "SSG") { spec.kind = TrackKind::Ssg; }
		else if (word == "RHYTHM") { spec.kind = TrackKind::Rhythm; }
		else if (dialect == MmlDialect::Inline) { continue; }   // 知らない語は MML のコマンドかもしれないので、そのまま渡す
		else
		{
			out.error = lineError(line, "@" + word + " という音源はありません。@FM0〜@FM11、@SSG、@RHYTHM のどれかを書いてください");
			return out;
		}
		if (out.source)
		{
			out.error = lineError(line, "1 行に音源が 2 つあります。トラックごとに行を分けてください");
			return out;
		}
		out.source = spec;
		out.rest.replace(i, end - i, end - i, ' ');
		i = end - 1;
	}
	return out;
}

[[nodiscard]] inline bool isMmlChar(unsigned char c)
{
	return std::isalnum(c) != 0 || std::isspace(c) != 0 || (c != 0 && std::strchr("+-#.&~<>[](){}_,@%", c) != nullptr);
}

/// リズムの行は OpnaSequencer が [ ] を展開しないので、書き出してから渡す。後ろの数字が回数 (無ければ 2 回)
[[nodiscard]] inline bool expandLoops(const std::string& in, std::size_t& pos, int depth, std::string& out,
	std::string& error)
{
	constexpr std::size_t kMaxChars = 200000;
	while (pos < in.size())
	{
		const char c = in[pos];
		if (c == ']')
		{
			if (depth == 0) { error = "] に対応する [ がありません"; return false; }
			return true;
		}
		if (c != '[') { out.push_back(c); ++pos; continue; }
		++pos;
		std::string body;
		if (!expandLoops(in, pos, depth + 1, body, error)) { return false; }
		if (pos >= in.size()) { error = "[ に対応する ] がありません"; return false; }
		++pos;   // ]
		int count = 2;
		if (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos])))
		{
			count = 0;
			while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos])) && count < 100000)
			{
				count = count * 10 + (in[pos++] - '0');
			}
		}
		const auto times = static_cast<std::size_t>(std::max(count, 1));
		if (body.size() * times + out.size() > kMaxChars) { error = "繰り返しが大きすぎます"; return false; }
		for (std::size_t i = 0; i < times; ++i) { out += body; }
	}
	if (depth > 0) { error = "[ に対応する ] がありません"; return false; }
	return true;
}

[[nodiscard]] inline ParsedSong parseFile(std::string_view text)
{
	ParsedSong song;
	std::istringstream in{std::string(text)};
	std::string raw;
	for (int line = 1; std::getline(in, raw); ++line)
	{
		if (line == 1 && raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) { raw.erase(0, 3); }
		raw = raw.substr(0, raw.find(';'));
		const std::string body = trim(raw);
		if (body.empty()) { continue; }
		for (const char c : body)
		{
			if (!isMmlChar(static_cast<unsigned char>(c)))
			{
				song.error = lineError(line, std::string("'") + c + "' は MML として読めません");
				return song;
			}
		}
		auto scan = scanSource(body, line, MmlDialect::File);
		if (!scan.error.empty()) { song.error = scan.error; return song; }
		if (scan.source)
		{
			scan.source->mml = trim(scan.rest);
			song.tracks.push_back(*scan.source);
		}
		else if (song.tracks.empty())
		{
			TrackSpec first;
			first.line = line;
			first.mml = body;
			song.tracks.push_back(first);
		}
		else
		{
			song.tracks.back().mml += " " + body;
		}
	}
	if (song.tracks.empty()) { song.error = "音符が 1 つもありません"; return song; }
	for (auto& t : song.tracks)
	{
		if (t.kind != TrackKind::Rhythm) { continue; }
		std::string expanded;
		std::size_t pos = 0;
		std::string error;
		if (!expandLoops(t.mml, pos, 0, expanded, error))
		{
			song.error = lineError(t.line, error);
			return song;
		}
		t.mml = std::move(expanded);
	}
	return song;
}

[[nodiscard]] inline ParsedSong parseInline(std::string_view text)
{
	ParsedSong song;
	std::vector<std::string> lines(1);
	for (const char c : text)
	{
		if (c == ';' || c == '\n') { lines.emplace_back(); }
		else if (c != '\r') { lines.back().push_back(c); }
	}
	for (const std::string& line : lines)
	{
		if (trim(line).empty()) { continue; }
		auto scan = scanSource(line, 0, MmlDialect::Inline);
		if (!scan.error.empty()) { song.error = scan.error; return song; }
		TrackSpec track = scan.source.value_or(TrackSpec{});   // 書かなければ FM の 0 番
		track.mml = trim(scan.rest);
		song.tracks.push_back(std::move(track));
	}
	if (song.tracks.empty()) { song.error = "音符が 1 つもありません"; }
	else if (song.tracks.size() > kMaxInlineTracks) { song.error = "トラックが多すぎます (9 本まで)"; }
	return song;
}

}  // namespace detail

/// @brief MML のテキストを、トラックの並びに分ける
[[nodiscard]] inline ParsedSong parseMml(std::string_view text, MmlDialect dialect = MmlDialect::File)
{
	return dialect == MmlDialect::File ? detail::parseFile(text) : detail::parseInline(text);
}

/// @brief 読んだトラックを OpnaSequencer に積む
inline void addTracks(mitiru_mml::OpnaSequencer& seq, const ParsedSong& song)
{
	for (const auto& t : song.tracks)
	{
		switch (t.kind)
		{
		case TrackKind::Fm:     seq.addFmTrack(t.mml, t.preset); break;
		case TrackKind::Ssg:    seq.addSsgTrack(t.mml); break;
		case TrackKind::Rhythm: seq.addRhythmTrack(t.mml); break;
		}
	}
}

}  // namespace mitiru::audio::mml
