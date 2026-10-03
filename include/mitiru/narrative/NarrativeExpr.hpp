#pragma once

/// @file NarrativeExpr.hpp
/// @brief 会話の分岐・選択肢・クエストの条件に書く整数の式 (`trust >= 2 && !refused`)。
/// @details 読み込みの時に逆ポーランドの命令列へ直してアセット側の ExprPool に置き、評価は NarrativeVars だけを読む。
///          評価はヒープも文字列も使わないので、毎フレーム呼んでもよく、同じ表から必ず同じ値が出る。
///          書けるもの: 整数、true / false、名前 (英数字・_・.・UTF-8 の文字)、( )、! と単項の -、+ -、
///          == != < <= > >=、&& ||。値は int32 で、真偽は 0 以外を真とする。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/narrative/NarrativeVars.hpp>

namespace mitiru::narrative
{

enum class ExprOp : std::uint8_t
{
	Push, Var, Not, Neg, Add, Sub, Eq, Ne, Lt, Le, Gt, Ge, And, Or,
};

struct ExprInstr
{
	ExprOp        op = ExprOp::Push;
	std::uint8_t  pad[3]{};
	std::int32_t  literal = 0;  ///< Push の値
	std::uint32_t var = 0;      ///< Var の鍵
};

/// @brief ExprPool の中の 1 本の式。count 0 は「書かれていない」(test は真)
struct ExprRef
{
	std::uint32_t first = 0;
	std::uint32_t count = 0;
};

namespace expr_detail
{

constexpr int kMaxStack = 16;

[[nodiscard]] inline bool isNameByte(char c) noexcept
{
	const auto u = static_cast<unsigned char>(c);
	return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u == '.' || u >= 0x80;
}

/// @brief 再帰下降で読み、命令を out へ後置で積む。depth は評価の時に積む深さの見積り
class Parser
{
public:
	Parser(std::string_view src, NarrativeNames& names, std::vector<ExprInstr>& out)
		: m_src(src), m_names(names), m_out(out) {}

	[[nodiscard]] bool run(std::string& error)
	{
		parseOr();
		skipSpace();
		if (m_error.empty() && m_pos < m_src.size()) { fail("読めない文字"); }
		if (m_error.empty() && m_maxDepth > kMaxStack) { fail("式が深すぎる"); }
		if (m_error.empty() && !m_names.error().empty()) { m_error = m_names.error(); }
		error = m_error;
		return m_error.empty();
	}

private:
	void skipSpace() noexcept
	{
		while (m_pos < m_src.size() && (m_src[m_pos] == ' ' || m_src[m_pos] == '\t')) { ++m_pos; }
	}
	[[nodiscard]] bool eat(std::string_view tok) noexcept
	{
		skipSpace();
		if (m_src.substr(m_pos, tok.size()) != tok) { return false; }
		m_pos += tok.size();
		return true;
	}
	void fail(const char* why)
	{
		if (m_error.empty()) { m_error = std::string(why) + " (" + std::to_string(m_pos + 1) + " 文字目): " + std::string(m_src); }
	}
	void emit(ExprOp op, int stackDelta, std::int32_t literal = 0, std::uint32_t var = 0)
	{
		ExprInstr in;
		in.op = op;
		in.literal = literal;
		in.var = var;
		m_out.push_back(in);
		m_depth += stackDelta;
		if (m_depth > m_maxDepth) { m_maxDepth = m_depth; }
	}

	void parseOr()
	{
		parseAnd();
		while (m_error.empty() && eat("||")) { parseAnd(); emit(ExprOp::Or, -1); }
	}
	void parseAnd()
	{
		parseCompare();
		while (m_error.empty() && eat("&&")) { parseCompare(); emit(ExprOp::And, -1); }
	}
	void parseCompare()
	{
		parseSum();
		static constexpr struct { std::string_view tok; ExprOp op; } kOps[] = {
			{"==", ExprOp::Eq}, {"!=", ExprOp::Ne}, {"<=", ExprOp::Le}, {">=", ExprOp::Ge}, {"<", ExprOp::Lt}, {">", ExprOp::Gt},
		};
		for (const auto& o : kOps)
		{
			if (m_error.empty() && eat(o.tok)) { parseSum(); emit(o.op, -1); return; }
		}
	}
	void parseSum()
	{
		parseUnary();
		while (m_error.empty())
		{
			if (eat("+")) { parseUnary(); emit(ExprOp::Add, -1); }
			else if (eat("-")) { parseUnary(); emit(ExprOp::Sub, -1); }
			else { return; }
		}
	}
	void parseUnary()
	{
		skipSpace();
		if (m_src.substr(m_pos, 2) != "!=" && eat("!")) { parseUnary(); emit(ExprOp::Not, 0); return; }
		if (eat("-")) { parseUnary(); emit(ExprOp::Neg, 0); return; }
		parsePrimary();
	}
	void parsePrimary();

	std::string_view        m_src;
	NarrativeNames&         m_names;
	std::vector<ExprInstr>& m_out;
	std::size_t             m_pos = 0;
	int                     m_depth = 0;
	int                     m_maxDepth = 0;
	std::string             m_error;
};

inline void Parser::parsePrimary()
{
	skipSpace();
	if (eat("("))
	{
		parseOr();
		if (m_error.empty() && !eat(")")) { fail(") が無い"); }
		return;
	}
	const std::size_t start = m_pos;
	if (m_pos < m_src.size() && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
	{
		std::int64_t v = 0;
		while (m_pos < m_src.size() && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
		{
			v = v * 10 + (m_src[m_pos] - '0');
			if (v > 2147483647) { fail("数が大きすぎる"); return; }
			++m_pos;
		}
		emit(ExprOp::Push, 1, static_cast<std::int32_t>(v));
		return;
	}
	while (m_pos < m_src.size() && isNameByte(m_src[m_pos])) { ++m_pos; }
	const std::string_view name = m_src.substr(start, m_pos - start);
	if (name.empty()) { fail("値か名前が要る"); return; }
	if (name == "true" || name == "false") { emit(ExprOp::Push, 1, name == "true" ? 1 : 0); return; }
	emit(ExprOp::Var, 1, 0, m_names.intern(name));
}

[[nodiscard]] inline std::int32_t apply(ExprOp op, std::int32_t a, std::int32_t b) noexcept
{
	// 足し引きは int64 で計算して丸め込み、オーバーフローの未定義動作を避ける
	switch (op)
	{
	case ExprOp::Add: return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(a) + b));
	case ExprOp::Sub: return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(a) - b));
	case ExprOp::Eq:  return a == b ? 1 : 0;
	case ExprOp::Ne:  return a != b ? 1 : 0;
	case ExprOp::Lt:  return a < b ? 1 : 0;
	case ExprOp::Le:  return a <= b ? 1 : 0;
	case ExprOp::Gt:  return a > b ? 1 : 0;
	case ExprOp::Ge:  return a >= b ? 1 : 0;
	case ExprOp::And: return (a != 0 && b != 0) ? 1 : 0;
	case ExprOp::Or:  return (a != 0 || b != 0) ? 1 : 0;
	default:          return 0;
	}
}

}  // namespace expr_detail

/// @brief 式の命令列の置き場。アセット (会話の台本・クエスト表) が 1 つずつ持つ
class ExprPool
{
public:
	/// @brief src を命令列に直して積む。空の src は count 0 の参照 (常に真)。読めなければ count 0 を返し error に理由を置く
	ExprRef compile(std::string_view src, NarrativeNames& names, std::string& error)
	{
		error.clear();
		std::size_t b = 0, e = src.size();
		while (b < e && (src[b] == ' ' || src[b] == '\t')) { ++b; }
		while (e > b && (src[e - 1] == ' ' || src[e - 1] == '\t')) { --e; }
		if (b == e) { return {}; }
		const std::size_t mark = m_code.size();
		expr_detail::Parser parser(src.substr(b, e - b), names, m_code);
		if (!parser.run(error))
		{
			m_code.resize(mark);
			return {};
		}
		return {static_cast<std::uint32_t>(mark), static_cast<std::uint32_t>(m_code.size() - mark)};
	}

	[[nodiscard]] std::int32_t eval(ExprRef ref, const NarrativeVars& vars) const noexcept
	{
		std::int32_t stack[expr_detail::kMaxStack];
		int sp = 0;
		for (std::uint32_t i = 0; i < ref.count; ++i)
		{
			const ExprInstr& in = m_code[ref.first + i];
			switch (in.op)
			{
			case ExprOp::Push: stack[sp++] = in.literal; break;
			case ExprOp::Var:  stack[sp++] = vars.get(in.var); break;
			case ExprOp::Not:  stack[sp - 1] = stack[sp - 1] == 0 ? 1 : 0; break;
			case ExprOp::Neg:  stack[sp - 1] = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(stack[sp - 1])); break;
			default:
				stack[sp - 2] = expr_detail::apply(in.op, stack[sp - 2], stack[sp - 1]);
				--sp;
				break;
			}
		}
		return sp > 0 ? stack[sp - 1] : 0;
	}

	/// @brief 条件として読む。書かれていない式は真
	[[nodiscard]] bool test(ExprRef ref, const NarrativeVars& vars) const noexcept
	{
		return ref.count == 0 || eval(ref, vars) != 0;
	}

private:
	std::vector<ExprInstr> m_code;
};

}  // namespace mitiru::narrative
