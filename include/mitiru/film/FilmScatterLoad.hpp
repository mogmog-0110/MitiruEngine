#pragma once

/// @file FilmScatterLoad.hpp
/// @brief 映像の JSON の `scatter` 項目を読み、配置を決める。FilmT は props（name、shape、model、pos、rotDeg、scale、posTrack、rotTrack）と scatters を持つ Film 型。props を読んだ後に呼ぶ。

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/film/FilmScatter.hpp>
#include <mitiru/narrative/Sequence.hpp>

namespace mitiru::film
{

namespace scatter_load_detail
{

using Json = nlohmann::json;
using narrative::seq_detail::at;
using narrative::seq_detail::Reader;

inline bool onlyKeys(Reader& r, const Json& j, std::initializer_list<const char*> allowed, const std::string& where)
{
	if (!j.is_object()) { return r.fail(where, "オブジェクトでない"); }
	for (auto it = j.begin(); it != j.end(); ++it)
	{
		bool ok = false;
		for (const char* a : allowed) { ok = ok || it.key() == a; }
		if (ok) { continue; }
		std::string list;
		for (const char* a : allowed) { list += std::string(list.empty() ? "" : ", ") + a; }
		return r.fail(where + "." + it.key(), "知らない項目。使えるのは " + list);
	}
	return true;
}

/// @brief [min, max] の 2 数。min <= max にそろえる
inline void pair(Reader& r, const Json& j, const char* key, float& lo, float& hi, const std::string& where)
{
	if (!j.contains(key)) { return; }
	const Json& a = j[key];
	if (a.is_number()) { lo = hi = a.get<float>(); return; }
	if (!a.is_array() || a.size() != 2 || !a[0].is_number() || !a[1].is_number()) { r.fail(where + "." + key, "[最小, 最大] か 1 つの数でない"); return; }
	lo = a[0].get<float>(); hi = a[1].get<float>();
	if (lo > hi) { r.fail(where + "." + key, "最小が最大より大きい"); }
}

inline bool xz(Reader& r, const Json& v, float& x, float& z, const std::string& where)
{
	if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) { return r.fail(where, "[x, z] の 2 数でない"); }
	x = v[0].get<float>(); z = v[1].get<float>();
	return true;
}

inline void readArea(Reader& r, const Json& j, Scatter& s, const std::string& w)
{
	if (!j.contains("area")) { r.fail(w, "area が無い"); return; }
	const Json& a = j["area"];
	if (!onlyKeys(r, a, {"center", "radius", "inner"}, w + ".area")) { return; }
	if (a.contains("center"))
	{
		const Json& c = a["center"];
		if (c.is_array() && c.size() == 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
		{
			s.center[0] = c[0].get<float>(); s.center[1] = c[1].get<float>(); s.center[2] = c[2].get<float>();
		}
		else if (!xz(r, c, s.center[0], s.center[2], w + ".area.center")) { return; }
	}
	s.radius = r.number(a, "radius", 0.0f, w + ".area");
	s.inner = r.number(a, "inner", 0.0f, w + ".area");
	if (r.error.empty() && !(s.radius > 0.0f)) { r.fail(w + ".area.radius", "0 より大きくする"); }
	if (r.error.empty() && (s.inner < 0.0f || s.inner >= s.radius)) { r.fail(w + ".area.inner", "0 以上で radius より小さくする"); }
}

template <class FilmT>
inline auto findProp(const FilmT& f, const std::string& name) -> decltype(&f.props.front())
{
	for (const auto& p : f.props)
	{
		if (p.name == name) { return &p; }
	}
	return nullptr;
}

/// @brief avoid の 1 項目。prop（小道具の足元）、point（円）、path（折れ線）のいずれか 1 つ
template <class FilmT>
inline void readAvoid(Reader& r, const Json& j, const FilmT& f, Scatter& s, const std::string& w)
{
	if (!onlyKeys(r, j, {"prop", "point", "path", "radius", "margin"}, w)) { return; }
	const int kinds = int(j.contains("prop")) + int(j.contains("point")) + int(j.contains("path"));
	if (kinds != 1) { r.fail(w, "prop・point・path のどれか 1 つを書く"); return; }
	ScatterAvoid a;
	a.margin = r.number(j, "margin", 0.0f, w);
	a.radius = r.number(j, "radius", 0.0f, w);
	if (j.contains("point"))
	{
		a.kind = ScatterAvoid::Kind::Circle;
		xz(r, j["point"], a.x, a.z, w + ".point");
	}
	else if (j.contains("path"))
	{
		a.kind = ScatterAvoid::Kind::Path;
		if (!j["path"].is_array() || j["path"].empty()) { r.fail(w + ".path", "[x, z] の点の配列でない"); return; }
		for (std::size_t i = 0; i < j["path"].size() && r.error.empty(); ++i)
		{
			float x = 0.0f, z = 0.0f;
			if (xz(r, j["path"][i], x, z, at(w + ".path", i))) { a.path.emplace_back(x, z); }
		}
	}
	else
	{
		const std::string name = r.text(j, "prop", w);
		const auto* p = findProp(f, name);
		if (p == nullptr) { r.fail(w + ".prop", "小道具 " + name + " が無い (props を先に書く)"); return; }
		const auto pos = p->posTrack.empty() ? p->pos : p->posTrack.keys.front().v;
		const auto rot = p->rotTrack.empty() ? p->rotDeg : p->rotTrack.keys.front().v;
		a.x = pos.x; a.z = pos.z;
		const bool box = p->shape == "cube" || p->shape == "plane";
		const float yaw = rot.y * (scatter_detail::kPi / 180.0f);
		if (box)
		{
			a.kind = ScatterAvoid::Kind::Rect;
			a.halfX = std::fabs(p->scale.x) * 0.5f; a.halfZ = std::fabs(p->scale.z) * 0.5f;
			a.cosY = std::cos(yaw); a.sinY = std::sin(yaw);
		}
		else
		{
			a.kind = ScatterAvoid::Kind::Circle;
			if (a.radius <= 0.0f && !p->shape.empty()) { a.radius = 0.5f * std::max(std::fabs(p->scale.x), std::fabs(p->scale.z)); }
		}
	}
	if (r.error.empty() && (a.margin < 0.0f || a.radius < 0.0f)) { r.fail(w, "margin と radius は 0 以上にする"); }
	s.avoid.push_back(std::move(a));
}

inline void readSway(Reader& r, const Json& j, Scatter& s, const std::string& w)
{
	if (!j.contains("sway")) { return; }
	const Json& a = j["sway"];
	if (!onlyKeys(r, a, {"amp", "freq", "wind"}, w + ".sway")) { return; }
	s.swayAmpDeg = r.number(a, "amp", 0.0f, w + ".sway");
	s.swayFreq = r.number(a, "freq", s.swayFreq, w + ".sway");
	if (a.contains("wind")) { xz(r, a["wind"], s.windX, s.windZ, w + ".sway.wind"); }
	if (r.error.empty() && (s.swayAmpDeg < 0.0f || !(s.swayFreq > 0.0f) || s.swayAmpDeg > 45.0f))
	{
		r.fail(w + ".sway", "amp は 0 以上 45 度以下、freq は 0 より大きくする");
	}
}

}  // namespace scatter_load_detail

/// @brief `outdoor` の world、wind（dir、strength）、waveScale、waterOffset、grass、scatter、water を読む
template <class FilmT>
inline void readOutdoor(narrative::seq_detail::Reader& r, const nlohmann::json& root, FilmT& f)
{
	using namespace scatter_load_detail;
	if (!root.contains("outdoor")) { return; }
	const Json& j = root["outdoor"];
	if (!onlyKeys(r, j, {"world", "wind", "waveScale", "waterOffset", "grass", "scatter", "water"}, "outdoor")) { return; }
	Outdoor o;
	o.world = r.text(j, "world", "outdoor");
	if (r.error.empty() && o.world.empty()) { r.fail("outdoor", "world (world.json のパス) が要る"); return; }
	o.waveScale = r.number(j, "waveScale", 1.0f, "outdoor");
	o.waterOffset = r.number(j, "waterOffset", 0.0f, "outdoor");
	for (const char* key : {"grass", "scatter", "water"})
	{
		if (j.contains(key) && !j[key].is_boolean()) { r.fail(std::string("outdoor.") + key, "true か false でない"); return; }
	}
	o.grass = !j.contains("grass") || j["grass"].get<bool>();
	o.scatter = !j.contains("scatter") || j["scatter"].get<bool>();
	o.water = !j.contains("water") || j["water"].get<bool>();
	if (j.contains("wind"))
	{
		if (!onlyKeys(r, j["wind"], {"dir", "strength"}, "outdoor.wind")) { return; }
		if (j["wind"].contains("dir")) { xz(r, j["wind"]["dir"], o.windDir[0], o.windDir[1], "outdoor.wind.dir"); }
		o.windStrength = r.number(j["wind"], "strength", -1.0f, "outdoor.wind");
	}
	if (r.error.empty() && o.waveScale < 0.0f) { r.fail("outdoor.waveScale", "0 以上にする"); }
	if (r.error.empty()) { f.outdoor = std::move(o); }
}

template <class FilmT>
inline void readScatters(narrative::seq_detail::Reader& r, const nlohmann::json& arr, FilmT& f)
{
	using namespace scatter_load_detail;
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("scatter", i);
		if (!onlyKeys(r, j, {"name", "model", "shape", "area", "count", "seed", "scale", "scaleY", "minDist", "lean", "color", "colorJitter",
		                     "avoid", "sway"}, w)) { return; }
		Scatter s;
		s.name = r.text(j, "name", w);
		s.model = r.text(j, "model", w);
		s.shape = r.text(j, "shape", w);
		if (r.error.empty() && s.name.empty()) { r.fail(w, "name が要る"); }
		if (r.error.empty() && (s.model.empty() == s.shape.empty())) { r.fail(w, "model か shape のどちらか 1 つを書く"); }
		readArea(r, j, s, w);
		const float count = r.number(j, "count", 0.0f, w);
		if (r.error.empty() && (count < 1.0f || count > static_cast<float>(scatter_detail::kMaxCount) || count != std::floor(count)))
		{
			r.fail(w + ".count", "1 以上 " + std::to_string(scatter_detail::kMaxCount) + " 以下の整数にする");
		}
		s.count = static_cast<int>(count);
		s.seed = static_cast<std::uint32_t>(r.number(j, "seed", 1.0f, w));
		pair(r, j, "scale", s.scaleMin, s.scaleMax, w);
		pair(r, j, "scaleY", s.scaleYMin, s.scaleYMax, w);
		s.minDist = r.number(j, "minDist", 0.0f, w);
		s.leanDeg = r.number(j, "lean", 0.0f, w);
		s.colorJitter = r.number(j, "colorJitter", 0.0f, w);
		if (j.contains("color"))
		{
			Json holder = Json::object();
			holder["c"] = j["color"];
			const Json& c = holder["c"];
			if (c.is_string() && c.get<std::string>().size() == 7 && c.get<std::string>()[0] == '#')
			{
				const unsigned long rgb = std::strtoul(c.get<std::string>().c_str() + 1, nullptr, 16);
				s.color[0] = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f; s.color[1] = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
				s.color[2] = static_cast<float>(rgb & 0xFF) / 255.0f;
			}
			else if (c.is_array() && c.size() == 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
			{
				for (int k = 0; k < 3; ++k) { s.color[k] = c[static_cast<std::size_t>(k)].get<float>(); }
			}
			else { r.fail(w + ".color", "#rrggbb か [r, g, b] でない"); }
		}
		if (r.error.empty() && (!(s.scaleMin > 0.0f) || !(s.scaleYMin > 0.0f))) { r.fail(w + ".scale", "0 より大きくする"); }
		if (r.error.empty() && (s.minDist < 0.0f || s.leanDeg < 0.0f || s.leanDeg > 45.0f || s.colorJitter < 0.0f || s.colorJitter > 1.0f))
		{
			r.fail(w, "minDist は 0 以上、lean は 0〜45 度、colorJitter は 0〜1");
		}
		if (r.error.empty() && s.minDist > 0.0f && 2.0f * s.radius / s.minDist > 1500.0f)
		{
			r.fail(w + ".minDist", "area.radius に比べて小さすぎる (半径 / 間隔 が 750 を超える)");
		}
		readSway(r, j, s, w);
		if (j.contains("avoid"))
		{
			if (!j["avoid"].is_array()) { r.fail(w + ".avoid", "配列でない"); return; }
			for (std::size_t k = 0; k < j["avoid"].size() && r.error.empty(); ++k) { readAvoid(r, j["avoid"][k], f, s, at(w + ".avoid", k)); }
		}
		for (const Scatter& other : f.scatters)
		{
			if (r.error.empty() && other.name == s.name) { r.fail(w + ".name", "名前が重なっている: " + s.name); }
		}
		if (!r.error.empty()) { return; }
		placeScatter(s);
		f.scatters.push_back(std::move(s));
	}
}

}  // namespace mitiru::film
