#pragma once
// HTML 版の data-m-format / {path:fmt} と同じ書式を、RmlUi の変換関数 ({{ x | time }}) として登録する。
// 書式の中身は mitiru_bind.js の fmt() と同じ (int / time / comma / f2 / pct / kmb)。

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace mitiru::ui_rml
{

namespace detail
{
inline double toNumber(const Rml::VariantList& args)
{
	double v = 0.0;
	return (!args.empty() && args[0].GetInto(v)) ? v : 0.0;
}

inline std::string withCommas(long long n)
{
	std::string digits = std::to_string(n < 0 ? -n : n);
	for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) { digits.insert(static_cast<std::size_t>(i), ","); }
	return (n < 0 ? "-" : "") + digits;
}

inline std::string kmb(double n)
{
	static const char* const units[] = { "", "K", "M", "B", "T" };
	int i = 0;
	while (n >= 1000.0 && i < 4) { n /= 1000.0; ++i; }
	char buf[32];
	if (i == 0) { std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::floor(n))); }
	else        { std::snprintf(buf, sizeof(buf), "%.1f%s", n, units[i]); }
	return buf;
}
} // namespace detail

inline constexpr const char* kBinderFormats[] = { "int", "time", "comma", "f2", "pct", "kmb" };

/// spec が書式名でなければ数をそのまま (%g) 文字にする。<tween format="..."> も同じ書式を使う。
inline std::string formatNumber(double v, std::string_view spec)
{
	char buf[32];
	if (spec == "int")   { return std::to_string(static_cast<long long>(std::floor(v))); }
	if (spec == "comma") { return detail::withCommas(static_cast<long long>(std::floor(v))); }
	if (spec == "kmb")   { return detail::kmb(v); }
	if (spec == "time")
	{
		const long long s = std::max(0LL, static_cast<long long>(std::floor(v)));
		std::snprintf(buf, sizeof(buf), "%lld:%02lld", s / 60, s % 60);
	}
	else if (spec == "f2")  { std::snprintf(buf, sizeof(buf), "%.2f", v); }
	else if (spec == "pct") { std::snprintf(buf, sizeof(buf), "%g%%", v); }
	else                    { std::snprintf(buf, sizeof(buf), "%g", v); }
	return buf;
}

inline void registerBinderFormatters(Rml::DataModelConstructor& ctor)
{
	for (const char* name : kBinderFormats)
	{
		const std::string spec = name;
		ctor.RegisterTransformFunc(name, [spec](const Rml::VariantList& a) {
			return Rml::Variant(formatNumber(detail::toNumber(a), spec)); });
	}
}

} // namespace mitiru::ui_rml
