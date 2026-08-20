#pragma once

/// @file LineScript.hpp (日本語で書ける行指向スクリプトの汎用パーサ)
/// @details ゲームの譜面・台本・設定のような「人が手で書くファイル」を読む共通の仕組み。
///          JSON はクォートと改行のエスケープが要り、INI は値の列を持てないので、
///          日本語の文を書くファイルには行指向が向く。
///
///          engine は項目名も命令名も一つも知らない。使う側が登録する。
///          登録は ls.keyword("pattern", {"パターン"}) のように代表名 + 別名で行い、
///          命令 (@名前) も ls.directive("demo", {"手本", "デモ"}) と同じ仕組み。
///          別名は何語でもよく、日本語と英語を並べれば書く人がどちらでも書ける。
///          読み込みは parseFile / parse で、失敗時は error() に行番号と理由が入る。
///          records() の key / directive は代表名へ正規化済み。
///
///          書式 (ノベルゲームのスクリプト、KAG / TyranoScript / Ren'Py の慣習に倣う):
///            ・1 行 = 1 レコード。「項目名 本文」。区切りは空白 (全角スペースも可)
///            ・# で始まる行と空行は読み飛ばす
///            ・*名前 はラベル (台本の節目。KAG の *label)
///            ・【名前】 は話者の宣言。同じ行に続けてセリフを書いてもよい
///            ・行頭の @命令 引数 は命令だけの行。引数は 名前=値 でも書ける
///            ・本文の末尾にも「@命令 引数」を 1 つ置ける
///            ・allowText(true) にすると、どの項目名でもない行を本文 (セリフ) として
///              受ける。セリフを一番たくさん書くので、そこに構文を要求しない
///            ・数値は全角 (０１２ ．－) でも読める
///
///          読めない行 (未登録の項目名・命令) は行番号つきのエラーで止める。黙って
///          読み飛ばすと「書き換えたのに変わらない」になり、書いた人が原因を探せない。
///          本文モードでは項目名の綴り間違いがセリフとして通る。これはセリフを裸で
///          書ける形式すべてが持つ性質で、データの節では allowText を切っておくか、
///          ラベルより前の本文を消費者側で弾く。

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/i18n/Utf8Text.hpp>

namespace mitiru::script
{

/// @brief 本文を空白 (全角スペース含む) で切ったトークン列を返す。
/// @details 名前の列 (パターンの並びなど) を読む消費者が使う。string_view は元の
///          文字列を指すので、元より長く持たないこと。
[[nodiscard]] inline std::vector<std::string_view> tokenize(std::string_view text)
{
	std::vector<std::string_view> out;
	std::size_t i = 0;
	while (i < text.size())
	{
		for (std::size_t n = i18n::spaceLenAt(text, i); n != 0;
		     n = i18n::spaceLenAt(text, i)) { i += n; }
		if (i >= text.size()) { break; }
		std::size_t j = i;
		while (j < text.size() && i18n::spaceLenAt(text, j) == 0) { ++j; }
		out.push_back(text.substr(i, j - i));
		i = j;
	}
	return out;
}

/// @brief 行の種別。
enum class LineKind : int
{
	Keyword = 0,  ///< 登録した項目名で始まる行
	Text,         ///< 本文 (セリフ)。allowText か 【話者】 の行
	Command,      ///< 行頭が @命令 の、命令だけの行
	Label,        ///< *名前
};

/// @brief 解析済みの 1 行。key / directive は登録時の代表名に正規化されている。
struct LineRecord
{
	LineKind           kind = LineKind::Keyword;
	std::string        key;          ///< Keyword のときの代表名
	std::string        speaker;      ///< Text のとき、直前に宣言された話者
	std::string        label;        ///< Label のときの名前
	std::string        text;         ///< 本文 (命令部を除き、改行記号を \n に開いた後)
	std::vector<float> numbers;      ///< 本文全体が数値列として読めた場合だけ入る
	std::string        directive;    ///< @命令 の代表名。無い行は空
	std::string        directiveArg; ///< 命令の引数 (生テキスト)
	/// 命令の引数のうち 名前=値 (＝ でもよい) の形のもの。書いた順のまま。
	std::vector<std::pair<std::string, std::string>> params;
	int                line = 0;     ///< 1 始まりの行番号
};

class LineScript
{
public:
	/// @brief 項目名を登録する。canonical が records() に入る代表名。
	/// @details 代表名は受け取る側の名前でしかなく、何語でもよい。同じ名前を重ねて
	///          登録したときは先の登録が勝つ (上書きしない)。
	void keyword(std::string_view canonical, std::initializer_list<std::string_view> aliases = {})
	{
		m_keywords.emplace(std::string(canonical), std::string(canonical));
		for (const auto a : aliases) { m_keywords.emplace(std::string(a), std::string(canonical)); }
	}

	/// @brief 命令名 (@名前) を登録する。仕組みは keyword と同じ。
	void directive(std::string_view canonical, std::initializer_list<std::string_view> aliases = {})
	{
		m_directives.emplace(std::string(canonical), std::string(canonical));
		for (const auto a : aliases) { m_directives.emplace(std::string(a), std::string(canonical)); }
	}

	/// @brief 本文の中でこの文字を改行に開く。既定は '|'。0 で無効。
	void newlineMark(char mark) { m_newlineMark = mark; }

	/// @brief どの項目名でもない行を本文 (セリフ) として受ける。既定は受けない。
	/// @details 台本のファイルで使う。データだけのファイルでは切っておくと、項目名の
	///          綴り間違いが行番号つきのエラーで見つかる。
	void allowText(bool on) { m_allowText = on; }

	[[nodiscard]] bool parseFile(const std::string& path)
	{
		std::ifstream f(path);
		if (!f)
		{
			m_error = path + " が開けません";
			m_errorLine = 0;
			return false;
		}
		// reloadIfChanged のために、どのファイルを読んだかと、その時刻を覚える。
		m_path = path;
		std::error_code ec;
		m_mtime = std::filesystem::last_write_time(path, ec);
		std::ostringstream ss;
		ss << f.rdbuf();
		return parse(ss.str());
	}

	/// @brief 読んだファイルが書き換わっていれば読み直す。読み直したら true。
	/// @details 監視スレッドは持たない。呼ぶ側が安全な場面 (メニューや会話の間) で
	///          poll する。いつ適用してよいかはゲームにしか決められない。
	///          読み直しの結果が失敗でも true を返す。書いた人の間違いは error() に
	///          入っていて、それを画面に出すのも「変わった」の一部だから。
	[[nodiscard]] bool reloadIfChanged()
	{
		if (m_path.empty()) { return false; }
		std::error_code ec;
		const auto t = std::filesystem::last_write_time(m_path, ec);
		if (ec || t == m_mtime) { return false; }
		(void)parseFile(m_path);
		return true;
	}

	[[nodiscard]] bool parse(std::string_view content)
	{
		m_records.clear();
		m_error.clear();
		m_errorLine = 0;
		m_speaker.clear();

		// メモ帳系のエディタは先頭に BOM を付けることがある。1 行目の項目名に
		// くっつくと「知らない項目」になるので、ここで剥がす。
		if (content.size() >= 3
		    && static_cast<unsigned char>(content[0]) == 0xEFu
		    && static_cast<unsigned char>(content[1]) == 0xBBu
		    && static_cast<unsigned char>(content[2]) == 0xBFu)
		{
			content.remove_prefix(3);
		}

		int lineNo = 0;
		std::size_t pos = 0;
		while (pos <= content.size())
		{
			const std::size_t nl = content.find('\n', pos);
			const std::string_view raw = content.substr(
				pos, nl == std::string_view::npos ? content.size() - pos : nl - pos);
			pos = (nl == std::string_view::npos) ? content.size() + 1 : nl + 1;
			++lineNo;

			const std::string_view line = i18n::trim(raw);
			if (line.empty() || line.front() == '#') { continue; }
			if (!parseLine(line, lineNo)) { return false; }
		}
		return true;
	}

	[[nodiscard]] const std::vector<LineRecord>& records() const noexcept { return m_records; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] int errorLine() const noexcept { return m_errorLine; }

private:
	[[nodiscard]] static std::size_t wordEnd(std::string_view s)
	{
		std::size_t i = 0;
		while (i < s.size() && i18n::spaceLenAt(s, i) == 0) { ++i; }
		return i;
	}

	[[nodiscard]] bool parseLine(std::string_view line, int lineNo)
	{
		// *名前 はラベル。台本の節目 (KAG の *label)。
		if (line.front() == '*')
		{
			LineRecord rec;
			rec.kind = LineKind::Label;
			rec.label = std::string(i18n::trim(line.substr(1)));
			rec.line = lineNo;
			if (rec.label.empty()) { return fail(lineNo, "ラベルの名前がありません"); }
			m_records.push_back(std::move(rec));
			return true;
		}

		// 【名前】 は話者の宣言。行に続きがあれば、それはその人のセリフ。
		if (line.size() >= 3 && line.substr(0, 3) == "\xE3\x80\x90")
		{
			const std::size_t close = line.find("\xE3\x80\x91");
			if (close == std::string_view::npos) { return fail(lineNo, "【 に対する 】 がありません"); }
			m_speaker = std::string(i18n::trim(line.substr(3, close - 3)));
			const std::string_view rest = i18n::trim(line.substr(close + 3));
			if (rest.empty()) { return true; }
			return pushText(rest, lineNo);
		}

		// 行頭の @命令 は命令だけの行。
		if (line.front() == '@')
		{
			LineRecord rec;
			rec.kind = LineKind::Command;
			rec.line = lineNo;
			if (!readDirective(line.substr(1), lineNo, rec)) { return false; }
			m_records.push_back(std::move(rec));
			return true;
		}

		const std::size_t we = wordEnd(line);
		const std::string head(line.substr(0, we));
		const auto it = m_keywords.find(head);
		if (it == m_keywords.end())
		{
			if (m_allowText) { return pushText(line, lineNo); }
			return fail(lineNo, "知らない項目です: " + head);
		}

		LineRecord rec;
		rec.key = it->second;
		rec.line = lineNo;

		std::string_view body = i18n::trim(line.substr(we));

		// 末尾の @命令。本文中の @ (メールアドレス等) と区別するため、行頭か空白の
		// 直後にある最後の @ だけを命令として読む。
		const std::size_t at = findDirectiveAt(body);
		if (at != std::string_view::npos)
		{
			if (!readDirective(body.substr(at + 1), lineNo, rec)) { return false; }
			body = i18n::trim(body.substr(0, at));
		}

		rec.text = expandNewlines(body);
		parseNumbers(body, rec.numbers);
		m_records.push_back(std::move(rec));
		return true;
	}

	/// @brief 本文 (セリフ) の行を積む。話者は直前の 【名前】 のまま。
	[[nodiscard]] bool pushText(std::string_view body, int lineNo)
	{
		LineRecord rec;
		rec.kind = LineKind::Text;
		rec.speaker = m_speaker;
		rec.line = lineNo;

		const std::size_t at = findDirectiveAt(body);
		if (at != std::string_view::npos)
		{
			if (!readDirective(body.substr(at + 1), lineNo, rec)) { return false; }
			body = i18n::trim(body.substr(0, at));
		}
		rec.text = expandNewlines(body);
		m_records.push_back(std::move(rec));
		return true;
	}

	/// @brief 「命令 引数...」を rec へ読む。名前=値 (全角の ＝ でも) は params にも入る。
	[[nodiscard]] bool readDirective(std::string_view src, int lineNo, LineRecord& rec)
	{
		const std::string_view d = i18n::trim(src);
		const std::size_t de = wordEnd(d);
		const std::string name(d.substr(0, de));
		const auto dit = m_directives.find(name);
		if (dit == m_directives.end())
		{
			return fail(lineNo, "知らない命令です: @" + name);
		}
		rec.directive = dit->second;
		rec.directiveArg = std::string(i18n::trim(d.substr(de)));

		for (const auto token : tokenize(rec.directiveArg))
		{
			std::size_t eq = token.find('=');
			std::size_t eqLen = 1;
			if (eq == std::string_view::npos)
			{
				eq = token.find("\xEF\xBC\x9D");
				eqLen = 3;
			}
			if (eq == std::string_view::npos || eq == 0 || eq + eqLen >= token.size()) { continue; }
			rec.params.emplace_back(std::string(token.substr(0, eq)),
			                        std::string(token.substr(eq + eqLen)));
		}
		return true;
	}

	[[nodiscard]] std::size_t findDirectiveAt(std::string_view body) const
	{
		for (std::size_t i = body.size(); i-- > 0;)
		{
			if (body[i] != '@') { continue; }
			if (i == 0) { return i; }
			// 直前が空白 (全角含む) のときだけ命令。全角空白は 3 バイト目が直前に来る。
			if (body[i - 1] == ' ' || body[i - 1] == '\t') { return i; }
			if (i >= 3 && i18n::isIdeographicSpaceAt(body, i - 3)) { return i; }
			return std::string_view::npos;
		}
		return std::string_view::npos;
	}

	[[nodiscard]] std::string expandNewlines(std::string_view body) const
	{
		std::string out;
		out.reserve(body.size());
		for (const char c : body)
		{
			out.push_back((m_newlineMark != '\0' && c == m_newlineMark) ? '\n' : c);
		}
		return out;
	}

	/// @brief 本文全体が数値の列なら out に入れる。1 つでも数値以外があれば空のまま。
	/// @details 全角の数字・小数点・負号は半角に写してから読む。日本語入力のまま数字を
	///          打つと全角になり、見た目では区別が付かない。
	static void parseNumbers(std::string_view body, std::vector<float>& out)
	{
		std::string ascii;
		ascii.reserve(body.size());
		for (std::size_t i = 0; i < body.size();)
		{
			// 全角の数字と記号は 3 バイトで、EF BC に続く 1 バイトが半角との対応を持つ。
			if (i + 2 < body.size()
			    && static_cast<unsigned char>(body[i]) == 0xEFu
			    && static_cast<unsigned char>(body[i + 1]) == 0xBCu)
			{
				const unsigned char b2 = static_cast<unsigned char>(body[i + 2]);
				if (b2 >= 0x90u && b2 <= 0x99u) { ascii.push_back(static_cast<char>('0' + (b2 - 0x90u))); i += 3; continue; }
				if (b2 == 0x8Eu) { ascii.push_back('.'); i += 3; continue; }
				if (b2 == 0x8Du) { ascii.push_back('-'); i += 3; continue; }
			}
			const std::size_t sp = i18n::spaceLenAt(body, i);
			if (sp > 0) { ascii.push_back(' '); i += sp; continue; }
			ascii.push_back(body[i]);
			++i;
		}

		std::vector<float> vals;
		const char* p = ascii.c_str();
		while (*p != '\0')
		{
			while (*p == ' ') { ++p; }
			if (*p == '\0') { break; }
			char* end = nullptr;
			const float v = std::strtof(p, &end);
			if (end == p || (*end != ' ' && *end != '\0')) { return; }
			vals.push_back(v);
			p = end;
		}
		if (!vals.empty()) { out = std::move(vals); }
	}

	bool fail(int lineNo, std::string msg)
	{
		m_error = std::to_string(lineNo) + " 行目: " + std::move(msg);
		m_errorLine = lineNo;
		return false;
	}

	std::unordered_map<std::string, std::string> m_keywords;
	std::string m_speaker;
	bool m_allowText = false;
	std::unordered_map<std::string, std::string> m_directives;
	std::vector<LineRecord> m_records;
	std::string m_error;
	std::string m_path;
	std::filesystem::file_time_type m_mtime{};
	int  m_errorLine = 0;
	char m_newlineMark = '|';
};

}  // namespace mitiru::script
