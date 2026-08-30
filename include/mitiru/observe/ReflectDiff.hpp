#pragma once

/// @file ReflectDiff.hpp
/// @brief reflected GameMemory state の field 単位 diff
/// @details
/// `reflectToJson` が作った 2 つの構造化 state (例 rewind ring の frame A と B) を
/// path 単位で比較し、変化したフィールドだけを `[{path, from, to}]` で返す。AI の
/// 「なぜ HP が落ちた?」が目視グラフ追いではなく構造クエリになる。純関数・例外なし。
///
/// 例: [{"path":"hp","from":80,"to":30},{"path":"enemies[2].alive","from":true,"to":false}]

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

namespace mitiru::observe
{

namespace detail
{

/// @brief a と b を path 単位で再帰比較し、変化を out (配列) に積む。
inline void diffJsonInto(const nlohmann::json& a, const nlohmann::json& b,
                         const std::string& path, nlohmann::json& out)
{
	if (a == b) { return; }

	if (a.is_object() && b.is_object())
	{
		for (auto it = a.begin(); it != a.end(); ++it)
		{
			const std::string p = path.empty() ? it.key() : (path + "." + it.key());
			if (b.contains(it.key())) { diffJsonInto(it.value(), b[it.key()], p, out); }
			else { out.push_back({{"path", p}, {"from", it.value()}, {"to", nullptr}}); }
		}
		for (auto it = b.begin(); it != b.end(); ++it)
		{
			if (!a.contains(it.key()))
			{
				const std::string p = path.empty() ? it.key() : (path + "." + it.key());
				out.push_back({{"path", p}, {"from", nullptr}, {"to", it.value()}});
			}
		}
	}
	else if (a.is_array() && b.is_array())
	{
		const std::size_t n = std::max(a.size(), b.size());
		for (std::size_t i = 0; i < n; ++i)
		{
			const std::string p = path + "[" + std::to_string(i) + "]";
			if (i < a.size() && i < b.size()) { diffJsonInto(a[i], b[i], p, out); }
			else if (i < a.size()) { out.push_back({{"path", p}, {"from", a[i]}, {"to", nullptr}}); }
			else                   { out.push_back({{"path", p}, {"from", nullptr}, {"to", b[i]}}); }
		}
	}
	else
	{
		// leaf (型違い or 値違い)
		out.push_back({{"path", path}, {"from", a}, {"to", b}});
	}
}

}  // namespace detail

/// @brief 2 つの reflected state の field 単位 diff。変化が無ければ空配列。
/// @return `[{"path": "...", "from": <旧値>, "to": <新値>}, ...]`
[[nodiscard]] inline nlohmann::json reflectDiff(const nlohmann::json& a, const nlohmann::json& b)
{
	nlohmann::json out = nlohmann::json::array();
	detail::diffJsonInto(a, b, "", out);
	return out;
}

}  // namespace mitiru::observe
