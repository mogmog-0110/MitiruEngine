#pragma once

/// @file MusicManifestJson.hpp
/// @brief assets/audio/music.json を MusicManifest にする
/// @details 書式は docs/ADAPTIVE_MUSIC.md。区間・層の名前は番号に直す。知らない名前や範囲外の値は、場所と内容を error に書いて失敗させ、再生前に誤りを検出する。

#include <cmath>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/audio/music/MusicManifest.hpp>

namespace mitiru::audio::music
{

struct MusicManifestParse
{
	MusicManifest manifest;
	std::string   error;   ///< 空なら成功

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

namespace detail
{

using Json = nlohmann::json;

/// 読み込み中に見つけた最初の誤りだけを報告する
struct ParseError
{
	std::string message;
};

[[noreturn]] inline void fail(std::string message) { throw ParseError{std::move(message)}; }

inline MusicSync parseSync(const Json& j, std::string_view where, MusicSync fallback)
{
	if (!j.contains("sync")) { return fallback; }
	const std::string s = j.at("sync").get<std::string>();
	if (s == "immediate") { return MusicSync::Immediate; }
	if (s == "beat")      { return MusicSync::Beat; }
	if (s == "bar")       { return MusicSync::Bar; }
	if (s == "marker")    { return MusicSync::Marker; }
	if (s == "exit")      { return MusicSync::Exit; }
	fail(std::string(where) + ": sync \"" + s + "\" は immediate / beat / bar / marker / exit のどれか");
}

inline float nonNegative(const Json& j, const char* key, float fallback, std::string_view where)
{
	const float v = j.value(key, fallback);
	if (!(v >= 0.0f)) { fail(std::string(where) + ": " + key + " は 0 以上"); }
	return v;
}

/// "*" または省略は -1（どれでも）
inline int segmentRef(const MusicManifest& m, const Json& j, const char* key, std::string_view where, bool anyAllowed)
{
	if (!j.contains(key)) { return -1; }
	const std::string id = j.at(key).get<std::string>();
	if (anyAllowed && id == "*") { return -1; }
	const int idx = m.segmentIndex(id);
	if (idx < 0) { fail(std::string(where) + ": " + key + " の区間 \"" + id + "\" が segments に無い"); }
	return idx;
}

inline std::vector<std::array<float, 2>> parseCurvePoints(const Json& points, const std::string& where)
{
	std::vector<std::array<float, 2>> out;
	for (const auto& p : points)
	{
		const float x = p.at(0).get<float>();
		const float g = p.at(1).get<float>();
		if (!out.empty() && x < out.back()[0]) { fail(where + " は軸の値の昇順に並べる"); }
		if (g < 0.0f) { fail(where + " の音量は 0 以上"); }
		out.push_back({x, g});
	}
	return out;
}

/// "curve" は強さの折れ線 1 本か、{"軸の名前": 折れ線, ...} (名前 "intensity" は強さ)。名前の軸は manifest.axes に足す
inline void parseLayerCurves(MusicManifest& m, MusicLayer& layer, const Json& j, const std::string& where)
{
	const Json curve = j.value("curve", Json::array());
	if (!curve.is_object())
	{
		layer.curve = parseCurvePoints(curve, where + ": curve");
		return;
	}
	for (const auto& [name, points] : curve.items())
	{
		const std::string at = where + ": curve." + name;
		if (name == "intensity") { layer.curve = parseCurvePoints(points, at); continue; }
		int axis = m.axisIndex(name);
		if (axis < 0)
		{
			axis = static_cast<int>(m.axes.size());
			m.axes.push_back(name);
		}
		layer.axisCurves.push_back(MusicAxisCurve{axis, parseCurvePoints(points, at)});
	}
}

inline void parseLayers(MusicManifest& m, const Json& root)
{
	for (const auto& j : root.value("layers", Json::array()))
	{
		MusicLayer layer;
		layer.id = j.at("id").get<std::string>();
		const std::string where = "layers." + layer.id;
		parseLayerCurves(m, layer, j, where);
		layer.fadeSec = nonNegative(j, "fadeSec", 1.0f, where);
		layer.sync = parseSync(j, where, MusicSync::Immediate);
		m.layers.push_back(std::move(layer));
	}
}

inline MusicSegment parseSegmentShape(const Json& j, double bpm, int beatsPerBar)
{
	MusicSegment s;
	s.id = j.at("id").get<std::string>();
	const std::string where = "segments." + s.id;
	s.bpm = j.value("bpm", bpm);
	s.beatsPerBar = j.value("beatsPerBar", beatsPerBar);
	s.bars = j.value("bars", 4);
	s.loop = j.value("loop", true);
	if (!(s.bpm > 0.0) || s.beatsPerBar <= 0 || s.bars <= 0) { fail(where + ": bpm / beatsPerBar / bars は正の数"); }
	for (const auto& b : j.value("markers", Json::array()))
	{
		const int beat = b.get<int>();
		if (beat < 0 || beat > s.bars * s.beatsPerBar) { fail(where + ": markers は 0 から区間の拍数まで"); }
		if (!s.markerBeats.empty() && beat <= s.markerBeats.back()) { fail(where + ": markers は昇順"); }
		s.markerBeats.push_back(beat);
	}
	return s;
}

inline void parseStems(MusicManifest& m, MusicSegment& s, const Json& j)
{
	for (const auto& st : j.at("stems"))
	{
		MusicStem stem;
		stem.file = st.at("file").get<std::string>();
		if (st.contains("layer"))
		{
			const std::string id = st.at("layer").get<std::string>();
			stem.layer = m.layerIndex(id);
			if (stem.layer < 0) { fail("segments." + s.id + ": layer \"" + id + "\" が layers に無い"); }
		}
		s.stems.push_back(std::move(stem));
	}
}

inline void parseSegments(MusicManifest& m, const Json& root)
{
	const double bpm = root.value("bpm", 120.0);
	const int beatsPerBar = root.value("beatsPerBar", 4);
	const Json& list = root.at("segments");
	for (const auto& j : list) { m.segments.push_back(parseSegmentShape(j, bpm, beatsPerBar)); }
	for (std::size_t i = 0; i < m.segments.size(); ++i)
	{
		const Json& j = list[i];
		MusicSegment& s = m.segments[i];
		parseStems(m, s, j);
		s.next = segmentRef(m, j, "next", "segments." + s.id, false);
	}
}

inline void parseTransitions(MusicManifest& m, const Json& root)
{
	for (const auto& j : root.value("transitions", Json::array()))
	{
		MusicTransition t;
		const std::string where = "transitions[" + std::to_string(m.transitions.size()) + "]";
		t.from = segmentRef(m, j, "from", where, true);
		t.to = segmentRef(m, j, "to", where, true);
		t.via = segmentRef(m, j, "via", where, false);
		t.sync = parseSync(j, where, MusicSync::Bar);
		t.fadeOutSec = nonNegative(j, "fadeOutSec", 0.0f, where);
		t.fadeInSec = nonNegative(j, "fadeInSec", 0.0f, where);
		m.transitions.push_back(t);
	}
}

inline void parseStingers(MusicManifest& m, const Json& root)
{
	for (const auto& j : root.value("stingers", Json::array()))
	{
		MusicStinger s;
		s.id = j.at("id").get<std::string>();
		s.file = j.at("file").get<std::string>();
		s.sync = parseSync(j, "stingers." + s.id, MusicSync::Beat);
		s.gain = std::pow(10.0f, j.value("gainDb", 0.0f) / 20.0f);
		m.stingers.push_back(std::move(s));
	}
}

}  // namespace detail

/// @brief JSON 文字列を読む。失敗時は error に理由が入る
[[nodiscard]] inline MusicManifestParse parseMusicManifest(std::string_view text)
{
	MusicManifestParse out;
	std::string syntaxError;
	const auto root = data::parseJsonText(text, "music.json", syntaxError);
	if (root.is_discarded() || !root.is_object())
	{
		out.error = syntaxError.empty() ? "JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError;
		return out;
	}
	try
	{
		detail::parseLayers(out.manifest, root);
		detail::parseSegments(out.manifest, root);
		detail::parseTransitions(out.manifest, root);
		detail::parseStingers(out.manifest, root);
	}
	catch (const detail::ParseError& e) { out.error = e.message; }
	catch (const nlohmann::json::exception& e) { out.error = std::string("music.json の項目の型か必須項目が違う: ") + e.what(); }
	if (!out.ok()) { out.manifest = {}; }
	return out;
}

}  // namespace mitiru::audio::music
