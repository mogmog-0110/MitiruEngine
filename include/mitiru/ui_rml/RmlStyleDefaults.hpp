#pragma once
// RML の data-style-* の式が使う変数の名前を集める。RmlUi は文書を読み込む途中で式を評価し、無い変数は
// 空として計算する。data-style-width="level + '%'" は "%" になり、RCSS の構文エラーを出す。
// RmlStateModel は読み込む前にここで名前を集め、まだ hud.set されていないものを 0 で置いておく
// (幅や位置の式は 0 で正しい値になる)。

#include <cctype>
#include <set>
#include <string>
#include <string_view>

namespace mitiru::ui_rml
{

namespace detail
{
	[[nodiscard]] inline bool isIdentStart(char c) noexcept
	{
		return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	[[nodiscard]] inline bool isIdentChar(char c) noexcept
	{
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	[[nodiscard]] inline char prevNonSpace(std::string_view s, std::size_t i) noexcept
	{
		while (i > 0) { if (s[--i] != ' ') { return s[i]; } }
		return '\0';
	}

	[[nodiscard]] inline char nextNonSpace(std::string_view s, std::size_t i) noexcept
	{
		for (; i < s.size(); ++i) { if (s[i] != ' ') { return s[i]; } }
		return '\0';
	}
}  // namespace detail

/// @brief 式の先頭の変数名 (`a.b[c]` なら a と c) を out へ足す。文字列・メンバー名・変換 (`| time`)・関数名は除く
inline void collectExpressionRoots(std::string_view expr, std::set<std::string>& out)
{
	std::size_t i = 0;
	while (i < expr.size())
	{
		const char c = expr[i];
		if (c == '&')   // RML の &lt; などの文字参照は変数ではない
		{
			const std::size_t semi = expr.find(';', i);
			i = (semi == std::string_view::npos) ? expr.size() : semi + 1;
			continue;
		}
		if (c == '\'' || c == '"')
		{
			const std::size_t close = expr.find(c, i + 1);
			i = (close == std::string_view::npos) ? expr.size() : close + 1;
			continue;
		}
		if (!detail::isIdentStart(c)) { ++i; continue; }
		std::size_t end = i;
		while (end < expr.size() && detail::isIdentChar(expr[end])) { ++end; }
		const std::string_view word = expr.substr(i, end - i);
		const char before = detail::prevNonSpace(expr, i);
		const bool isRoot = before != '.' && before != '|' && detail::nextNonSpace(expr, end) != '('
			&& word != "true" && word != "false";
		if (isRoot) { out.emplace(word); }
		i = end;
	}
}

namespace detail
{
	/// @brief prefix で始まる属性ごとに (属性名の prefix の後ろ, 値) を f へ渡す
	template <class F>
	void forEachAttribute(std::string_view rml, std::string_view prefix, F&& f)
	{
		for (std::size_t at = rml.find(prefix); at != std::string_view::npos; at = rml.find(prefix, at + 1))
		{
			const std::size_t eq = rml.find('=', at);
			const std::size_t open = (eq == std::string_view::npos) ? eq : rml.find_first_of("\"'", eq);
			if (open == std::string_view::npos) { return; }
			const std::size_t close = rml.find(rml[open], open + 1);
			if (close == std::string_view::npos) { return; }
			f(rml.substr(at + prefix.size(), eq - at - prefix.size()), rml.substr(open + 1, close - open - 1));
			at = close;
		}
	}

	/// @brief data-for の項目名 ("c : list" の c、"c, i : list" の c と i、省くと it と it_index) と data-alias-* の名前
	inline void collectAliases(std::string_view rml, std::set<std::string>& out)
	{
		forEachAttribute(rml, "data-for", [&](std::string_view, std::string_view value) {
			const std::size_t colon = value.find(':');
			if (colon == std::string_view::npos) { out.emplace("it"); out.emplace("it_index"); return; }
			collectExpressionRoots(value.substr(0, colon), out);
		});
		forEachAttribute(rml, "data-alias-", [&](std::string_view name, std::string_view) { out.emplace(name); });
	}
}  // namespace detail

/// @brief RML の本文にある data-style-* 属性の式が使う変数名を集める。data-for などで付けた別名は除く
/// (RmlUi は同じ名前の変数を別名より先に引くので、別名を変数として置くと data-for の中の式が壊れる)
inline void collectStyleVariables(std::string_view rml, std::set<std::string>& out)
{
	std::set<std::string> found, aliases;
	detail::forEachAttribute(rml, "data-style-", [&](std::string_view, std::string_view value) {
		collectExpressionRoots(value, found);
	});
	detail::collectAliases(rml, aliases);
	for (const std::string& name : found)
	{
		if (aliases.count(name) == 0) { out.insert(name); }
	}
}

}  // namespace mitiru::ui_rml
