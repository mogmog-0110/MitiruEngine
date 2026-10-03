#pragma once

/// @file AnimGraphCondition.hpp
/// @brief 遷移の条件の文字列 ("speed > 0.1"、"grounded"、"!grounded"、"attack"、"finished"、"phase >= 0.8") を読む。
/// @details 1 語なら param の真偽 (トリガーなら立っているか) か finished、3 語なら「左辺 比較 数」。
///          左辺の time は状態に入ってからの秒数、phase は周回の中の位置 (0..1) で、どちらも >= だけを受ける。

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/AnimGraph.hpp>

namespace mitiru::animation
{

/// @brief 条件の中で特別な意味を持つ語。param の名前には使えない
[[nodiscard]] inline bool isReservedCondName(std::string_view name) noexcept
{
	return name == "finished" || name == "time" || name == "phase";
}

namespace detail
{

[[nodiscard]] inline std::vector<std::string> splitWords(std::string_view s)
{
	std::vector<std::string> out;
	std::string cur;
	for (const char c : s)
	{
		if (c == ' ' || c == '\t')
		{
			if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
			continue;
		}
		cur += c;
	}
	if (!cur.empty()) { out.push_back(std::move(cur)); }
	return out;
}

[[nodiscard]] inline std::optional<AnimCondOp> compareOp(std::string_view op) noexcept
{
	if (op == ">") { return AnimCondOp::Greater; }
	if (op == ">=") { return AnimCondOp::GreaterEq; }
	if (op == "<") { return AnimCondOp::Less; }
	if (op == "<=") { return AnimCondOp::LessEq; }
	if (op == "==") { return AnimCondOp::Equal; }
	if (op == "!=") { return AnimCondOp::NotEqual; }
	return std::nullopt;
}

[[nodiscard]] inline std::optional<float> parseNumber(const std::string& s) noexcept
{
	float v = 0.0f;
	const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
	if (s.empty() || ec != std::errc{} || end != s.data() + s.size()) { return std::nullopt; }
	return v;
}

inline std::optional<AnimCondition> parseOneWord(const std::string& w, const AnimGraph& graph, std::string& error)
{
	if (w == "finished") { return AnimCondition{AnimCondOp::Finished, -1, 0.0f}; }
	const bool negate = !w.empty() && w[0] == '!';
	const std::string name = negate ? w.substr(1) : w;
	const int p = graph.findParam(name);
	if (p < 0)
	{
		error = "param が無い: " + name;
		return std::nullopt;
	}
	const bool trigger = graph.params[static_cast<std::size_t>(p)].type == AnimParamType::Trigger;
	if (trigger && negate)
	{
		error = "トリガーは ! を付けられない: " + name;
		return std::nullopt;
	}
	const AnimCondOp op = trigger ? AnimCondOp::Trigger : (negate ? AnimCondOp::IsFalse : AnimCondOp::IsTrue);
	return AnimCondition{op, static_cast<std::int16_t>(p), 0.0f};
}

inline std::optional<AnimCondition> parseCompare(const std::vector<std::string>& w, const AnimGraph& graph, std::string& error)
{
	const auto op = compareOp(w[1]);
	const auto value = parseNumber(w[2]);
	if (!op || !value)
	{
		error = "\"左辺 比較 数\" の比較は > >= < <= == != のどれか、右辺は数";
		return std::nullopt;
	}
	if (w[0] == "time" || w[0] == "phase")
	{
		if (*op != AnimCondOp::GreaterEq)
		{
			error = w[0] + " は >= だけを使える";
			return std::nullopt;
		}
		return AnimCondition{w[0] == "time" ? AnimCondOp::TimeAtLeast : AnimCondOp::PhaseAtLeast, -1, *value};
	}
	const int p = graph.findParam(w[0]);
	if (p < 0 || graph.params[static_cast<std::size_t>(p)].type == AnimParamType::Trigger)
	{
		error = p < 0 ? "param が無い: " + w[0] : "トリガーは比べられない: " + w[0];
		return std::nullopt;
	}
	return AnimCondition{*op, static_cast<std::int16_t>(p), *value};
}

} // namespace detail

/// @brief 条件の文字列 1 つを読む。読めなければ nullopt で、理由を error に書く
[[nodiscard]] inline std::optional<AnimCondition> parseAnimCondition(std::string_view text, const AnimGraph& graph,
                                                                     std::string& error)
{
	const auto w = detail::splitWords(text);
	if (w.size() == 1) { return detail::parseOneWord(w[0], graph, error); }
	if (w.size() == 3) { return detail::parseCompare(w, graph, error); }
	error = "条件は 1 語か \"左辺 比較 数\" の 3 語: " + std::string(text);
	return std::nullopt;
}

} // namespace mitiru::animation
