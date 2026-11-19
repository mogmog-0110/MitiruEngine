#pragma once
/// @file SongDefinitions.hpp
/// @brief 曲のテキストのマクロ (#macro) と変数 (#let) の定義と展開
/// @details 展開は MML をパーサーに渡す前の文字列の置き換えで、パーサーとイベントの組み立ては変えない。
///          定義が 1 つも無く、MML に !名前 と %名前 も無いテキストは 1 文字も置き換えないので、これまでの曲の読み方は変わらない。
///
/// 書き方
///   #let lead = 12                  変数。整数 (負も書ける)
///   #macro riff = O4 L8 CDEG        マクロ。名前に MML の断片を結びつける (1 行)
///   #macro bass                     複数行で書くときは、#endmacro までが本体
///   O2 L4 C G
///   C G
///   #endmacro
///   V%lead !riff [!riff]2           %名前 で変数の値、!名前 でマクロの本体に置き換わる
///   O%{oct}4                        名前の直後が英数字や _ のときは {名前} で囲む
///   #macro hit(oct, len) = O%oct L%len C  引数つきのマクロ。本体の中の %引数名 が、呼んだときの文字列に置き換わる
///   !hit(4, 8)                      引数は、整数でも MML の断片 (CDE や [CC]2) でもよい。コンマで区切る
///   V%lead+2 O%{oct*2-1}            整数の式 (足し算、引き算、掛け算、括弧)。% の直後に続けて書くか、%{ } の中に書く
///
/// 引数の名前は、本体の中では同じ名前の #let の変数より優先する。引数は呼んだ側で先に展開してから渡す
/// (だから !hit(%oct+1, 8) の %oct+1 は呼んだ側の変数で計算される)。引数の数が合わない呼び出しはエラー。
/// 引数のあるマクロは、必ず ( ) をつけて呼ぶ。引数のないマクロの直後の ( は、これまでどおり MML の文字として残る。
/// 式は % の直後に隣り合わせで +、-、* と、数字、%名前、( ) をつなげた形 (%a+1、%a*(%b+2))、
/// または %{ } の中に書く形 (%{a*2-1}。中の名前には % が要らず、空白も書ける)。+ の前後が空白や英字のとき (C%a +) は式にならない。
/// 式の値は ±1000000000 まで。引数が整数でないのに式に使うとエラー。
///
/// 定義は曲全体に効き、書く場所は問わない。マクロの本体は、別のマクロと変数を使える (循環はエラー)。
/// 本体の ; 以降は注釈なので展開されない。名前は英字か _ で始まり、英数字と _ が続く (大文字と小文字は別の名前)。
/// 定義が 1 つも無い曲でも、MML の中の !名前 と %名前 は未定義のエラーになる。パーサーは ! を読み飛ばすので、
/// そのままだと、マクロを呼んだつもりの文字が音符として鳴ってしまうため。

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief テキスト内の位置。行と列は 1 から始まり、column が 0 なら列は不明
struct SourcePos
{
	int line = 0;
	int column = 0;
};

/// @brief MML の各文字の出どころ。展開された文字は、一番外側の呼び出し位置を指す
struct SourceOrigin
{
	int line = 0;
	int column = 0;
	int macro = -1;  ///< 文字がマクロの本体から来たとき、そのマクロの番号 (SongDefinitions::macros の添字)。-1 は書いたままの文字
};

struct SongProblem
{
	int line = 0;
	int column = 0;
	std::string message;

	/// @brief 「N 行目 M 列: ...」形式の文字列。列が不明なら「N 行目: ...」
	[[nodiscard]] std::string text() const
	{
		if (line <= 0)
		{
			return message;
		}
		if (column <= 0)
		{
			return std::to_string(line) + " 行目: " + message;
		}
		return std::to_string(line) + " 行目 " + std::to_string(column) + " 列: " + message;
	}
};

struct MacroDef
{
	std::string name;
	std::vector<std::string> params;  ///< 引数の名前 (引数のないマクロは空)
	std::string body;                 ///< 注釈を除いた本体 (複数行は空白でつなぐ)
	std::vector<SourcePos> bodyPos;   ///< body の 1 文字ごとの、テキストの中の位置
	SourcePos at;                     ///< #macro の位置
	int uses = 0;                     ///< 展開された回数
};

struct VariableDef
{
	std::string name;
	int value = 0;
	SourcePos at;
	int uses = 0;
};

struct SongReference
{
	char sigil = 0;           ///< '!' (マクロ) か '%' (変数)
	std::string name;
	std::size_t begin = 0;    ///< sigil の位置
	std::size_t end = 0;      ///< 参照の次の位置
	bool expression = false;  ///< '%' の参照が整数の式 (%a+1、%{a*2}) のとき true。このとき name は空で、expr に式の文字
	std::string expr;         ///< 式の元の文字 (% から末尾まで)
};

/// @brief 呼び出し ( ) の中の、引数 1 つぶんの範囲 (前後の空白を除いた [begin, end))
struct ArgRange
{
	std::size_t begin = 0;
	std::size_t end = 0;
};

enum class CallRead
{
	NoArgs,    ///< 名前の直後に ( が無い
	Ok,
	Unclosed,  ///< ( があるのに、対応する ) が無い
};

class SongDefinitions
{
public:
	[[nodiscard]] bool empty() const noexcept
	{
		return m_macros.empty() && m_variables.empty();
	}

	[[nodiscard]] const std::vector<MacroDef>& macros() const noexcept { return m_macros; }
	[[nodiscard]] const std::vector<VariableDef>& variables() const noexcept { return m_variables; }

	[[nodiscard]] static bool isName(std::string_view name) noexcept
	{
		if (name.empty() || !isNameStart(name.front()))
		{
			return false;
		}
		return std::all_of(name.begin(), name.end(), [](char c) { return isNameChar(c); });
	}

	/// @brief text[at] から !名前、%名前、!{名前}、%{名前}、整数の式 (%a+1、%{a*2}) のいずれかを読み取る
	/// @details % の参照は、名前の直後に +、-、* と、数字か % か ( が続く間、式として読み進める (空白は式に入れない)。
	///          %{ } の中が名前でないときは、中全体を式として読む。
	[[nodiscard]] static bool readReference(std::string_view text, std::size_t at, SongReference& out)
	{
		if (at >= text.size() || (text[at] != '!' && text[at] != '%'))
		{
			return false;
		}
		std::size_t i = at + 1;
		std::size_t nameBegin = i;
		std::size_t nameEnd = i;
		std::size_t end = i;
		bool braces = false;
		if (i < text.size() && text[i] == '{')
		{
			braces = true;
			nameBegin = i + 1;
			nameEnd = text.find('}', nameBegin);
			if (nameEnd == std::string_view::npos)
			{
				return false;
			}
			end = nameEnd + 1;
		}
		else
		{
			if (i >= text.size() || !isNameStart(text[i]))
			{
				return false;
			}
			while (i < text.size() && isNameChar(text[i]))
			{
				++i;
			}
			nameEnd = i;
			end = i;
		}
		const std::string_view name = text.substr(nameBegin, nameEnd - nameBegin);
		out = SongReference{};
		out.sigil = text[at];
		out.begin = at;
		if (!isName(name))
		{
			if (!braces || text[at] != '%' || name.empty())
			{
				return false;
			}
			out.end = chainEnd(text, end);
			out.expression = true;
			out.expr = std::string(text.substr(at, out.end - at));
			return true;
		}
		out.name = std::string(name);
		out.end = end;
		if (text[at] == '%')
		{
			const std::size_t chained = chainEnd(text, end);
			if (chained > end)
			{
				out.end = chained;
				out.name.clear();
				out.expression = true;
				out.expr = std::string(text.substr(at, chained - at));
			}
		}
		return true;
	}

	/// @brief 名前の直後の ( から、対応する ) までの引数を読む
	/// @param args 引数ごとの範囲 (コンマで区切る。 ( ) [ ] { } の中のコンマは区切りにしない。括弧の種類が合わない閉じ括弧は、ただの文字として読む)。前後の空白は含めない
	/// @param end 読んだ ) の次の位置
	[[nodiscard]] static CallRead readCall(std::string_view text, std::size_t at, std::vector<ArgRange>& args, std::size_t& end)
	{
		args.clear();
		if (at >= text.size() || text[at] != '(')
		{
			return CallRead::NoArgs;
		}
		std::vector<char> closers; // まだ閉じていない ( [ { に対応する閉じ括弧
		std::size_t begin = at + 1;
		const auto push = [&](std::size_t from, std::size_t to)
		{
			while (from < to && std::isspace(static_cast<unsigned char>(text[from])) != 0)
			{
				++from;
			}
			while (to > from && std::isspace(static_cast<unsigned char>(text[to - 1])) != 0)
			{
				--to;
			}
			args.push_back({from, to});
		};
		for (std::size_t i = at + 1; i < text.size(); ++i)
		{
			const char c = text[i];
			if (c == '(' || c == '[' || c == '{')
			{
				closers.push_back(c == '(' ? ')' : c == '[' ? ']' : '}');
			}
			else if (!closers.empty() && c == closers.back())
			{
				closers.pop_back();
			}
			else if (c == ')' && closers.empty())
			{
				push(begin, i);
				end = i + 1;
				return CallRead::Ok;
			}
			else if (c == ',' && closers.empty())
			{
				push(begin, i);
				begin = i + 1;
			}
		}
		return CallRead::Unclosed;
	}

	void addVariable(VariableDef def, std::vector<SongProblem>& problems)
	{
		if (const VariableDef* old = findVariable(def.name))
		{
			problems.push_back({def.at.line, def.at.column,
				"変数 '" + def.name + "' は " + std::to_string(old->at.line) + " 行目ですでに定義されています"});
			return;
		}
		m_variables.push_back(std::move(def));
	}

	void addMacro(MacroDef def, std::vector<SongProblem>& problems)
	{
		if (const MacroDef* old = findMacro(def.name))
		{
			problems.push_back({def.at.line, def.at.column,
				"マクロ '" + def.name + "' は " + std::to_string(old->at.line) + " 行目ですでに定義されています"});
			return;
		}
		m_macros.push_back(std::move(def));
	}

	void validate(std::vector<SongProblem>& problems) const
	{
		for (const MacroDef& macro : m_macros)
		{
			validateBody(macro, problems);
		}
		std::vector<bool> cyclic(m_macros.size(), false);
		for (std::size_t m = 0; m < m_macros.size(); ++m)
		{
			std::vector<std::size_t> path;
			std::vector<bool> visited(m_macros.size(), false);
			if (reachesItself(m, m, path, visited))
			{
				cyclic[m] = true;
				std::string route = m_macros[m].name;
				for (const std::size_t step : path)
				{
					route += " -> " + m_macros[step].name;
				}
				problems.push_back({m_macros[m].at.line, m_macros[m].at.column,
					"マクロ '" + m_macros[m].name + "' が自分を呼んでいます (" + route + ")"});
			}
		}
		std::vector<int> depth(m_macros.size(), -1);
		for (std::size_t m = 0; m < m_macros.size(); ++m)
		{
			if (!cyclic[m] && nesting(m, depth) > MAX_DEPTH)
			{
				problems.push_back({m_macros[m].at.line, m_macros[m].at.column,
					"マクロ '" + m_macros[m].name + "' の呼び出しの入れ子が " + std::to_string(MAX_DEPTH) + " 段を超えています"});
			}
		}
	}

	/// @param pos text の各文字の位置
	/// @param problems 未定義の名前を追加する。nullptr なら追加しない
	/// @details ; 以降は展開せず、そのまま写す。
	void expandLine(std::string_view text, const std::vector<SourcePos>& pos, std::string& out,
		std::vector<SourceOrigin>& origins, std::vector<SongProblem>* problems)
	{
		std::vector<std::size_t> stack;
		m_overflow = false;
		expand(text, pos, nullptr, -1, true, nullptr, stack, out, origins, problems);
		if (m_overflow && problems != nullptr && !pos.empty())
		{
			problems->push_back({pos.front().line, pos.front().column,
				"マクロの展開が " + std::to_string(MAX_OUTPUT) + " 文字を超えました (途中で止めました)"});
		}
	}

	/// @brief # の指示にある %名前だけを値に置き換え、!名前は変えない
	[[nodiscard]] std::string substituteVariables(std::string_view text, int line, int indent,
		std::vector<SongProblem>& problems)
	{
		std::vector<SourcePos> pos(text.size());
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			pos[i] = {line, indent + static_cast<int>(i) + 1};
		}
		std::string out;
		std::vector<SourceOrigin> origins;
		std::vector<std::size_t> stack;
		expand(text, pos, nullptr, -1, false, nullptr, stack, out, origins, &problems);
		return out;
	}

	[[nodiscard]] const MacroDef* findMacro(std::string_view name) const noexcept
	{
		const int i = macroIndex(name);
		return i < 0 ? nullptr : &m_macros[static_cast<std::size_t>(i)];
	}

	[[nodiscard]] const VariableDef* findVariable(std::string_view name) const noexcept
	{
		for (const VariableDef& v : m_variables)
		{
			if (v.name == name) return &v;
		}
		return nullptr;
	}

	[[nodiscard]] static std::string undefinedMacroMessage(const std::string& name)
	{
		return "マクロ '" + name + "' は定義されていません (#macro " + name + " = ... で定義します)";
	}

	[[nodiscard]] static std::string undefinedVariableMessage(const std::string& name)
	{
		return "変数 '" + name + "' は定義されていません (#let " + name + " = 数 で定義します)";
	}

private:
	static constexpr int MAX_DEPTH = 32;
	static constexpr std::size_t MAX_OUTPUT = 1u << 20; ///< 展開結果の文字数の上限 (マクロが倍々に増えて止まらなくなるのを防ぐ)

	[[nodiscard]] static bool isNameStart(char c) noexcept
	{
		return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	[[nodiscard]] static bool isNameChar(char c) noexcept
	{
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	static constexpr long long VALUE_LIMIT = 1000000000;

	using Env = std::map<std::string, std::string>;

	/// @brief 式の計算の失敗。dynamic が true なら、渡された値で初めて分かる失敗 (本体を調べただけでは分からない)
	struct ExprFailure
	{
		std::string message;
		bool dynamic = false;
	};

	struct ExprResult
	{
		bool ok = false;
		long long value = 0;
		ExprFailure failure;
	};

	/// @brief 名前 name の値を value に入れる。入れられなければ failure を埋めて false
	using Lookup = std::function<bool(const std::string& name, long long& value, ExprFailure& failure)>;

	/// @brief 整数の式の読み取りと計算 (+、-、*、単項の -、括弧、整数、名前、%名前、%{式})
	class ExprParser
	{
	public:
		ExprParser(std::string_view text, const Lookup& lookup) : m_text(text), m_lookup(lookup) {}

		[[nodiscard]] ExprResult run()
		{
			ExprResult result;
			const long long value = parseSum();
			skipSpaces();
			if (!m_failed && m_pos != m_text.size())
			{
				fail(std::string("'") + m_text[m_pos] + "' は式の中では使えません (+ - * と括弧だけです)", false);
			}
			if (m_failed)
			{
				result.failure = m_failure;
				return result;
			}
			result.ok = true;
			result.value = value;
			return result;
		}

	private:
		void fail(std::string message, bool dynamic)
		{
			if (!m_failed)
			{
				m_failed = true;
				m_failure = {std::move(message), dynamic};
			}
		}

		void skipSpaces()
		{
			while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t'))
			{
				++m_pos;
			}
		}

		[[nodiscard]] long long checked(long long value)
		{
			if (value > VALUE_LIMIT || value < -VALUE_LIMIT)
			{
				fail("式の値が大きすぎます (±" + std::to_string(VALUE_LIMIT) + " まで)", true);
				return 0;
			}
			return value;
		}

		long long parseSum()
		{
			long long value = parseProduct();
			while (!m_failed)
			{
				skipSpaces();
				if (m_pos >= m_text.size() || (m_text[m_pos] != '+' && m_text[m_pos] != '-'))
				{
					break;
				}
				const char op = m_text[m_pos++];
				const long long right = parseProduct();
				if (m_failed)
				{
					break;
				}
				value = checked(op == '+' ? value + right : value - right);
			}
			return value;
		}

		long long parseProduct()
		{
			long long value = parseUnary();
			while (!m_failed)
			{
				skipSpaces();
				if (m_pos >= m_text.size() || m_text[m_pos] != '*')
				{
					break;
				}
				++m_pos;
				const long long right = parseUnary();
				if (m_failed)
				{
					break;
				}
				value = checked(value * right); // 左右の値は ±1e9 以内なので 64 bit に収まる
			}
			return value;
		}

		long long parseUnary()
		{
			skipSpaces();
			if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+'))
			{
				const char op = m_text[m_pos++];
				const long long inner = parseUnary();
				return op == '-' ? -inner : inner;
			}
			return parsePrimary();
		}

		long long parsePrimary()
		{
			skipSpaces();
			if (m_pos >= m_text.size())
			{
				fail("式が途中で終わっています", false);
				return 0;
			}
			const char c = m_text[m_pos];
			if (std::isdigit(static_cast<unsigned char>(c)) != 0)
			{
				return parseNumber();
			}
			if (c == '(')
			{
				++m_pos;
				const long long inner = parseSum();
				expectClose(')', "( に対応する ) がありません");
				return inner;
			}
			if (c == '%')
			{
				++m_pos;
				if (m_pos < m_text.size() && m_text[m_pos] == '{')
				{
					++m_pos;
					const long long inner = parseSum();
					expectClose('}', "%{ に対応する } がありません");
					return inner;
				}
				return parseName();
			}
			if (isNameStart(c))
			{
				return parseName();
			}
			if (c == ')' || c == '}')
			{
				fail("式が途中で終わっています", false);
				return 0;
			}
			fail(std::string("'") + c + "' は式の中では使えません (数、名前、+ - * と括弧だけです)", false);
			return 0;
		}

		/// @brief 項の最後にある閉じ括弧を読む。無ければ、閉じ括弧が足りないのか、式に使えない文字があるのかで、理由を分ける
		void expectClose(char close, const char* missing)
		{
			skipSpaces();
			if (m_failed)
			{
				return;
			}
			if (m_pos < m_text.size() && m_text[m_pos] == close)
			{
				++m_pos;
			}
			else if (m_pos >= m_text.size() || m_text[m_pos] == ')' || m_text[m_pos] == '}')
			{
				fail(missing, false);
			}
			else
			{
				fail(std::string("'") + m_text[m_pos] + "' は式の中では使えません (+ - * と括弧だけです)", false);
			}
		}

		long long parseNumber()
		{
			const std::size_t begin = m_pos;
			while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0)
			{
				++m_pos;
			}
			if (m_pos - begin > 10)
			{
				fail("数が大きすぎます (±" + std::to_string(VALUE_LIMIT) + " まで)", false);
				return 0;
			}
			const long long value = std::stoll(std::string(m_text.substr(begin, m_pos - begin)));
			if (value > VALUE_LIMIT)
			{
				fail("数が大きすぎます (±" + std::to_string(VALUE_LIMIT) + " まで)", false);
				return 0;
			}
			return value;
		}

		long long parseName()
		{
			const std::size_t begin = m_pos;
			while (m_pos < m_text.size() && isNameChar(m_text[m_pos]))
			{
				++m_pos;
			}
			if (m_pos == begin)
			{
				fail("% のあとに名前がありません", false);
				return 0;
			}
			long long value = 0;
			ExprFailure failure;
			if (!m_lookup(std::string(m_text.substr(begin, m_pos - begin)), value, failure))
			{
				fail(std::move(failure.message), failure.dynamic);
				return 0;
			}
			return value;
		}

		std::string_view m_text;
		const Lookup& m_lookup;
		std::size_t m_pos = 0;
		bool m_failed = false;
		ExprFailure m_failure;
	};

	/// @brief 引数に渡された文字を、式の中の整数として読む (前後の空白と、符号つきの 10 桁までの数字だけ)
	[[nodiscard]] static bool parseInteger(const std::string& text, long long& value)
	{
		std::size_t begin = 0;
		std::size_t end = text.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
		while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
		std::size_t i = begin;
		if (i < end && (text[i] == '-' || text[i] == '+')) ++i;
		if (i == end || end - i > 10)
		{
			return false;
		}
		for (std::size_t k = i; k < end; ++k)
		{
			if (std::isdigit(static_cast<unsigned char>(text[k])) == 0)
			{
				return false;
			}
		}
		value = std::stoll(text.substr(begin, end - begin));
		return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
	}

	/// @brief 式の 1 つの項 (数字の並び、%名前、%{...}、( ... )) の終わり。項でなければ npos
	[[nodiscard]] static std::size_t operandEnd(std::string_view text, std::size_t at)
	{
		if (at >= text.size())
		{
			return std::string_view::npos;
		}
		const char c = text[at];
		if (std::isdigit(static_cast<unsigned char>(c)) != 0)
		{
			std::size_t i = at;
			while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0)
			{
				++i;
			}
			return i;
		}
		if (c == '%')
		{
			if (at + 1 < text.size() && text[at + 1] == '{')
			{
				const std::size_t close = text.find('}', at + 2);
				return (close == std::string_view::npos || close == at + 2) ? std::string_view::npos : close + 1;
			}
			if (at + 1 < text.size() && isNameStart(text[at + 1]))
			{
				std::size_t i = at + 1;
				while (i < text.size() && isNameChar(text[i]))
				{
					++i;
				}
				return i;
			}
			return std::string_view::npos;
		}
		if (c == '(')
		{
			int depth = 0;
			for (std::size_t i = at; i < text.size(); ++i)
			{
				if (text[i] == '(')
				{
					++depth;
				}
				else if (text[i] == ')' && --depth == 0)
				{
					return i + 1;
				}
			}
		}
		return std::string_view::npos;
	}

	/// @brief 参照の終わり at から、+、-、* と項が隣り合わせに続く間の、連なりの終わり。続かなければ at
	[[nodiscard]] static std::size_t chainEnd(std::string_view text, std::size_t at)
	{
		std::size_t i = at;
		while (i < text.size() && (text[i] == '+' || text[i] == '-' || text[i] == '*'))
		{
			const std::size_t next = operandEnd(text, i + 1);
			if (next == std::string_view::npos)
			{
				break;
			}
			i = next;
		}
		return i;
	}

	/// @brief マクロの本体にある参照を、定義と引数の名前に照らして調べる (呼ばれていなくても調べる)
	void validateBody(const MacroDef& macro, std::vector<SongProblem>& problems) const
	{
		const std::string tail = " (マクロ '" + macro.name + "' の本体)";
		const auto add = [&](std::size_t i, const std::string& message)
		{
			const SourcePos pos = macro.bodyPos[i];
			problems.push_back({pos.line, pos.column, message + tail});
		};
		const Lookup lookup = [&](const std::string& name, long long& value, ExprFailure& failure)
		{
			value = 1;
			if (std::find(macro.params.begin(), macro.params.end(), name) != macro.params.end() || findVariable(name) != nullptr)
			{
				return true;
			}
			failure = {undefinedVariableMessage(name), false};
			return false;
		};
		for (std::size_t i = 0; i < macro.body.size(); ++i)
		{
			SongReference ref;
			if (!readReference(macro.body, i, ref))
			{
				continue;
			}
			if (ref.sigil == '!')
			{
				const MacroDef* callee = findMacro(ref.name);
				if (callee == nullptr)
				{
					add(i, undefinedMacroMessage(ref.name));
				}
				else if (!callee->params.empty())
				{
					std::vector<ArgRange> args;
					std::size_t callEnd = ref.end;
					const CallRead call = readCall(macro.body, ref.end, args, callEnd);
					const std::string problem = callProblem(*callee, call, args.size());
					if (!problem.empty())
					{
						add(i, problem);
					}
				}
			}
			else if (ref.expression)
			{
				const ExprResult result = ExprParser(ref.expr, lookup).run();
				if (!result.ok && !result.failure.dynamic)
				{
					add(i, expressionMessage(ref.expr, result.failure.message));
				}
			}
			else
			{
				const bool isParam = std::find(macro.params.begin(), macro.params.end(), ref.name) != macro.params.end();
				if (!isParam && findVariable(ref.name) == nullptr)
				{
					add(i, undefinedVariableMessage(ref.name));
				}
			}
			i = ref.end - 1;
		}
	}

	/// @brief 引数のあるマクロの呼び出しの問題 (問題が無ければ空)
	[[nodiscard]] static std::string callProblem(const MacroDef& macro, CallRead call, std::size_t given)
	{
		const std::string count = std::to_string(macro.params.size());
		if (call == CallRead::NoArgs)
		{
			return "マクロ '" + macro.name + "' は引数が " + count + " つ要ります (!" + macro.name + "(...) の形で呼びます)";
		}
		if (call == CallRead::Unclosed)
		{
			return "マクロ '" + macro.name + "' の呼び出しに ) がありません";
		}
		if (given != macro.params.size())
		{
			return "マクロ '" + macro.name + "' は引数が " + count + " つ要りますが、" + std::to_string(given) + " つ渡されています";
		}
		return {};
	}

	[[nodiscard]] static std::string expressionMessage(const std::string& expr, const std::string& reason)
	{
		return "式 '" + expr + "' を計算できません (" + reason + ")";
	}

	[[nodiscard]] int macroIndex(std::string_view name) const noexcept
	{
		for (std::size_t i = 0; i < m_macros.size(); ++i)
		{
			if (m_macros[i].name == name) return static_cast<int>(i);
		}
		return -1;
	}

	/// @brief マクロ from から target まで呼び出しをたどれるか調べ、途中のマクロを path に残す
	/// @param visited 調べ済みのマクロ。同じマクロを 2 度たどらず、呼び出しの探索量が急増するのを防ぐ
	[[nodiscard]] bool reachesItself(std::size_t from, std::size_t target, std::vector<std::size_t>& path,
		std::vector<bool>& visited) const
	{
		visited[from] = true;
		const std::string& body = m_macros[from].body;
		for (std::size_t i = 0; i < body.size(); ++i)
		{
			SongReference ref;
			if (!readReference(body, i, ref))
			{
				continue;
			}
			i = ref.end - 1;
			if (ref.sigil != '!')
			{
				continue;
			}
			const int next = macroIndex(ref.name);
			if (next < 0)
			{
				continue;
			}
			const std::size_t n = static_cast<std::size_t>(next);
			path.push_back(n);
			if (n == target || (!visited[n] && reachesItself(n, target, path, visited)))
			{
				return true;
			}
			path.pop_back();
		}
		return false;
	}

	/// @brief マクロ m から呼び出しをたどった入れ子の段数。m 自身を 1 段とし、循環の先は数えない
	/// @param memo 計算済みの段数。-1 は未計算、-2 は計算中
	[[nodiscard]] int nesting(std::size_t m, std::vector<int>& memo) const
	{
		if (memo[m] == -2)
		{
			return 0;
		}
		if (memo[m] >= 0)
		{
			return memo[m];
		}
		memo[m] = -2;
		int deepest = 0;
		const std::string& body = m_macros[m].body;
		for (std::size_t i = 0; i < body.size(); ++i)
		{
			SongReference ref;
			if (!readReference(body, i, ref))
			{
				continue;
			}
			i = ref.end - 1;
			const int next = (ref.sigil == '!') ? macroIndex(ref.name) : -1;
			if (next >= 0)
			{
				deepest = std::max(deepest, nesting(static_cast<std::size_t>(next), memo));
			}
		}
		memo[m] = deepest + 1;
		return memo[m];
	}

	/// @param site 一番外側の呼び出し位置。元のテキストなら nullptr
	/// @param macros false なら !名前を展開せず、そのまま写す
	/// @param env マクロの本体を展開しているときの、引数の名前と渡された文字。なければ nullptr
	/// @details 問題の報告は、元のテキストを展開しているとき (site が nullptr) は、見つけた位置に出す。マクロの本体の中では、
	///          本体を調べるだけで分かる問題 (未定義の名前、式の読み間違い、引数の数) は validate() が報告するので、ここでは出さない。
	///          渡された値で初めて分かる問題 (整数でない引数を式に使った、値が大きすぎる) は、呼んだ位置に、マクロの名前をつけて出す。
	void expand(std::string_view text, const std::vector<SourcePos>& pos, const SourceOrigin* site, int innerMacro,
		bool macros, const Env* env, std::vector<std::size_t>& stack, std::string& out, std::vector<SourceOrigin>& origins,
		std::vector<SongProblem>* problems)
	{
		const bool top = (site == nullptr);
		const auto originAt = [&](std::size_t i)
		{
			if (site != nullptr)
			{
				return SourceOrigin{site->line, site->column, innerMacro};
			}
			return SourceOrigin{pos[i].line, pos[i].column, -1};
		};
		const auto put = [&](char c, const SourceOrigin& o)
		{
			out += c;
			origins.push_back(o);
		};
		const auto reportStatic = [&](std::size_t i, const std::string& message)
		{
			if (top && problems != nullptr)
			{
				problems->push_back({pos[i].line, pos[i].column, message});
			}
		};
		const auto reportDynamic = [&](std::size_t i, const std::string& message)
		{
			if (problems == nullptr)
			{
				return;
			}
			SongProblem problem = top ? SongProblem{pos[i].line, pos[i].column, message}
				: SongProblem{site->line, site->column, message + " (マクロ '" + m_macros[static_cast<std::size_t>(innerMacro)].name + "' の中)"};
			const bool known = std::any_of(problems->begin(), problems->end(), [&](const SongProblem& p)
				{
					return p.line == problem.line && p.column == problem.column && p.message == problem.message;
				});
			if (!known)
			{
				problems->push_back(std::move(problem));
			}
		};

		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == ';' && macros)
			{
				for (; i < text.size(); ++i)
				{
					put(text[i], originAt(i));
				}
				break;
			}
			SongReference ref;
			if (!readReference(text, i, ref) || (ref.sigil == '!' && !macros))
			{
				put(text[i], originAt(i));
				continue;
			}
			const SourceOrigin here = originAt(i);
			if (ref.sigil == '%' && ref.expression)
			{
				const Lookup lookup = [&](const std::string& name, long long& value, ExprFailure& failure)
				{
					return lookupValue(name, env, value, failure);
				};
				const ExprResult result = ExprParser(ref.expr, lookup).run();
				if (result.ok)
				{
					for (const char c : std::to_string(result.value))
					{
						put(c, here);
					}
				}
				else if (result.failure.dynamic)
				{
					reportDynamic(i, expressionMessage(ref.expr, result.failure.message));
				}
				else
				{
					reportStatic(i, expressionMessage(ref.expr, result.failure.message));
				}
			}
			else if (ref.sigil == '%')
			{
				const std::string* param = nullptr;
				if (env != nullptr)
				{
					const auto it = env->find(ref.name);
					param = (it != env->end()) ? &it->second : nullptr;
				}
				VariableDef* variable = (param != nullptr) ? nullptr : mutableVariable(ref.name);
				if (param != nullptr)
				{
					if (out.size() + param->size() > MAX_OUTPUT)
					{
						m_overflow = true;
					}
					else
					{
						for (const char c : *param)
						{
							put(c, here);
						}
					}
				}
				else if (variable == nullptr)
				{
					reportStatic(i, undefinedVariableMessage(ref.name));
				}
				else
				{
					++variable->uses;
					for (const char c : std::to_string(variable->value))
					{
						put(c, here);
					}
				}
			}
			else
			{
				std::vector<ArgRange> args;
				CallRead call = CallRead::NoArgs;
				const int index = macroIndex(ref.name);
				if (index >= 0 && !m_macros[static_cast<std::size_t>(index)].params.empty())
				{
					std::size_t callEnd = ref.end;
					call = readCall(text, ref.end, args, callEnd);
					if (call == CallRead::Ok)
					{
						ref.end = callEnd;
					}
				}
				expandMacro(index, ref, call, args, text, pos, i, here, site, innerMacro, env, stack, out, origins, problems);
			}
			i = ref.end - 1;
		}
	}

	/// @brief 名前を式の値にする。引数の名前は変数より先に探す
	[[nodiscard]] bool lookupValue(const std::string& name, const Env* env, long long& value, ExprFailure& failure)
	{
		if (env != nullptr)
		{
			const auto it = env->find(name);
			if (it != env->end())
			{
				if (parseInteger(it->second, value))
				{
					return true;
				}
				failure = {"引数 '" + name + "' の値 '" + it->second + "' は整数ではないので、式には使えません", true};
				return false;
			}
		}
		VariableDef* variable = mutableVariable(name);
		if (variable == nullptr)
		{
			failure = {undefinedVariableMessage(name), false};
			return false;
		}
		++variable->uses;
		value = variable->value;
		return true;
	}

	void expandMacro(int index, const SongReference& ref, CallRead call, const std::vector<ArgRange>& args,
		std::string_view text, const std::vector<SourcePos>& pos, std::size_t at, const SourceOrigin& here,
		const SourceOrigin* callerSite, int callerMacro, const Env* callerEnv, std::vector<std::size_t>& stack,
		std::string& out, std::vector<SourceOrigin>& origins, std::vector<SongProblem>* problems)
	{
		const bool top = (callerSite == nullptr);
		if (index < 0)
		{
			if (top && problems != nullptr)
			{
				problems->push_back({pos[at].line, pos[at].column, undefinedMacroMessage(ref.name)});
			}
			return;
		}
		const std::size_t m = static_cast<std::size_t>(index);
		if (out.size() > MAX_OUTPUT)
		{
			m_overflow = true;
			return;
		}
		if (std::find(stack.begin(), stack.end(), m) != stack.end() || stack.size() >= static_cast<std::size_t>(MAX_DEPTH))
		{
			return; // 循環と深すぎる入れ子は validate() が報告する。ここでは展開を止めるだけ
		}
		const bool hasParams = !m_macros[m].params.empty();
		Env calleeEnv;
		if (hasParams)
		{
			const std::string problem = callProblem(m_macros[m], call, args.size());
			if (!problem.empty())
			{
				if (top && problems != nullptr)
				{
					problems->push_back({pos[at].line, pos[at].column, problem});
				}
				return;
			}
			// 引数は呼んだ側の定義と引数で、先に展開してから渡す
			for (std::size_t k = 0; k < args.size(); ++k)
			{
				const std::string argText(text.substr(args[k].begin, args[k].end - args[k].begin));
				const std::vector<SourcePos> argPos(pos.begin() + static_cast<std::ptrdiff_t>(args[k].begin),
					pos.begin() + static_cast<std::ptrdiff_t>(args[k].end));
				std::string expanded;
				std::vector<SourceOrigin> discarded;
				expand(argText, argPos, callerSite, callerMacro, true, callerEnv, stack, expanded, discarded, problems);
				calleeEnv[m_macros[m].params[k]] = std::move(expanded);
			}
		}
		++m_macros[m].uses;
		stack.push_back(m);
		const SourceOrigin site{here.line, here.column, -1};
		const std::string body = m_macros[m].body;
		const std::vector<SourcePos> bodyPos = m_macros[m].bodyPos;
		expand(body, bodyPos, &site, index, true, hasParams ? &calleeEnv : nullptr, stack, out, origins, problems);
		stack.pop_back();
	}

	[[nodiscard]] VariableDef* mutableVariable(std::string_view name) noexcept
	{
		for (VariableDef& v : m_variables)
		{
			if (v.name == name) return &v;
		}
		return nullptr;
	}

	std::vector<MacroDef> m_macros;
	std::vector<VariableDef> m_variables;
	bool m_overflow = false;
};

} // namespace mitiru_mml
