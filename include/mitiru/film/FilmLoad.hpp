#pragma once

/// @file FilmLoad.hpp
/// @brief 映像の JSON を Film に読む。間違いは「どこの何が」を付けて 1 つ返す。AI が直しやすいように、知らない項目名も場所つきで断る。

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/film/FilmEditShots.hpp>
#include <mitiru/film/FilmLook.hpp>
#include <mitiru/film/FilmMorph.hpp>
#include <mitiru/film/FilmPreset.hpp>
#include <mitiru/film/FilmScatterLoad.hpp>
#include <mitiru/film/FilmTypes.hpp>
#include <mitiru/narrative/Sequence.hpp>

namespace mitiru::film
{

namespace film_detail
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
		if (!ok)
		{
			std::string list;
			for (const char* a : allowed) { list += std::string(list.empty() ? "" : ", ") + a; }
			return r.fail(where + "." + it.key(), "知らない項目。使えるのは " + list);
		}
	}
	return true;
}

/// @brief 色は "#rrggbb" または [r, g, b]。配列の各値は 0..1。
inline Vec3 color(Reader& r, const Json& j, const char* key, Vec3 fallback, const std::string& where)
{
	if (!j.contains(key)) { return fallback; }
	const Json& v = j[key];
	if (v.is_string())
	{
		const std::string s = v.get<std::string>();
		if (s.size() != 7 || s[0] != '#') { r.fail(where + "." + key, "#rrggbb か [r, g, b] でない"); return fallback; }
		char* end = nullptr;
		const unsigned long rgb = std::strtoul(s.c_str() + 1, &end, 16);
		if (end != s.c_str() + 7) { r.fail(where + "." + key, "#rrggbb の 16 進でない"); return fallback; }
		return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
		        static_cast<float>(rgb & 0xFF) / 255.0f};
	}
	return r.vec3(j, key, fallback, where);
}

inline void readClearColor(Reader& r, const Json& root, Film& f)
{
	if (!root.contains("clear")) { return; }
	const Vec3 c = color(r, root, "clear", {0, 0, 0}, "(root)");
	const auto b = [](float x) { return static_cast<std::uint32_t>(x * 255.0f + 0.5f) & 0xFF; };
	f.clearColor = (b(c.x) << 16) | (b(c.y) << 8) | b(c.z);
}

inline void readSize(Reader& r, const Json& root, Film& f)
{
	if (!root.contains("size")) { return; }
	const Json& a = root["size"];
	if (!a.is_array() || a.size() != 2 || !a[0].is_number_integer() || !a[1].is_number_integer())
	{
		r.fail("size", "[幅, 高さ] の整数でない");
		return;
	}
	f.width = a[0].get<int>();
	f.height = a[1].get<int>();
	if (f.width < 64 || f.height < 64 || f.width > 7680 || f.height > 4320) { r.fail("size", "64 以上 7680x4320 以下にする"); }
}

/// @brief 鍵の配列から key のある鍵だけを集め、疎な 1 本の軌跡にする。値は parse で読む。
template <class T, class Parse>
void readTrack(Reader& r, const Json& keys, const char* key, bool smooth, Track<T>& out, const std::string& where, Parse parse)
{
	out.smooth = smooth;
	for (std::size_t i = 0; i < keys.size() && r.error.empty(); ++i)
	{
		const Json& k = keys[i];
		if (!k.is_object() || !k.contains(key)) { continue; }
		const std::string w = at(where, i);
		TKey<T> tk;
		tk.t = r.required(k, "t", w);
		tk.v = parse(r, k, key, w);
		if (!out.keys.empty() && tk.t < out.keys.back().t) { r.fail(w, "t が前の鍵より小さい"); return; }
		out.keys.push_back(tk);
	}
}

inline Vec3 parseVec3(Reader& r, const Json& k, const char* key, const std::string& w) { return r.vec3(k, key, {}, w); }
inline Vec3 parseColor(Reader& r, const Json& k, const char* key, const std::string& w) { return color(r, k, key, {}, w); }
inline float parseFloat(Reader& r, const Json& k, const char* key, const std::string& w) { return r.number(k, key, 0.0f, w); }

inline const Json& keysOf(Reader& r, const Json& obj, const std::string& where)
{
	static const Json kEmpty = Json::array();
	if (!obj.contains("keys")) { return kEmpty; }
	if (!obj["keys"].is_array()) { r.fail(where + ".keys", "配列でない"); return kEmpty; }
	return obj["keys"];
}

inline void readPropClips(Reader& r, const Json& j, Prop& p, const std::string& w)
{
	const std::string single = r.text(j, "clip", w);
	if (!single.empty())
	{
		PropClip c;
		c.name = single;
		c.speed = r.number(j, "clipSpeed", 1.0f, w);
		c.offset = r.number(j, "clipOffset", 0.0f, w);
		c.loop = !j.contains("clipLoop") || j["clipLoop"] != false;
		p.clips.push_back(c);
	}
	if (!j.contains("clips")) { return; }
	if (!j["clips"].is_array()) { r.fail(w + ".clips", "配列でない"); return; }
	for (std::size_t i = 0; i < j["clips"].size() && r.error.empty(); ++i)
	{
		const Json& cj = j["clips"][i];
		const std::string cw = at(w + ".clips", i);
		if (!onlyKeys(r, cj, {"clip", "start", "end", "offset", "speed", "blendIn", "blendOut", "loop"}, cw)) { return; }
		PropClip c;
		c.name = r.text(cj, "clip", cw);
		c.start = r.number(cj, "start", 0.0f, cw);
		c.end = r.number(cj, "end", c.end, cw);
		c.offset = r.number(cj, "offset", 0.0f, cw);
		c.speed = r.number(cj, "speed", 1.0f, cw);
		c.blendIn = r.number(cj, "blendIn", 0.0f, cw);
		c.blendOut = r.number(cj, "blendOut", 0.0f, cw);
		c.loop = !cj.contains("loop") || cj["loop"] != false;
		if (r.error.empty() && c.name.empty()) { r.fail(cw, "clip (クリップ名) が要る"); }
		if (r.error.empty() && !(c.start < c.end)) { r.fail(cw, "start < end にする"); }
		p.clips.push_back(c);
	}
}

inline bool readMorphKeys(Reader& r, const Json& mj, MorphChannel& c, const std::string& mw)
{
	c.name = r.text(mj, "name", mw);
	if (r.error.empty() && c.name.empty()) { return r.fail(mw, "name (モーフか表情の名前) が要る。口の表を引くなら visemes を書く"); }
	if (!mj.contains("keys") || !mj["keys"].is_array() || mj["keys"].empty()) { return r.fail(mw + ".keys", "鍵の配列 [{t, w}] が要る"); }
	const std::string mode = r.text(mj, "interp", mw);
	if (!mode.empty() && mode != "linear" && mode != "smooth") { return r.fail(mw + ".interp", "smooth か linear"); }
	c.weight.smooth = mode == "smooth";
	for (std::size_t k = 0; k < mj["keys"].size() && r.error.empty(); ++k)
	{
		const std::string kw = at(mw + ".keys", k);
		if (!onlyKeys(r, mj["keys"][k], {"t", "w"}, kw)) { return false; }
		TKey<float> tk;
		tk.t = r.required(mj["keys"][k], "t", kw);
		tk.v = r.required(mj["keys"][k], "w", kw);
		if (r.error.empty() && !c.weight.keys.empty() && tk.t < c.weight.keys.back().t) { return r.fail(kw, "t が前の鍵より小さい"); }
		c.weight.keys.push_back(tk);
	}
	return r.error.empty();
}

inline bool readMorphVisemes(Reader& r, const Json& mj, MorphChannel& c, const std::string& mw, const std::string& baseDir)
{
	if (mj.contains("name") || mj.contains("keys") || mj.contains("interp")) { return r.fail(mw, "visemes は name / keys / interp と一緒に書けない (1 本の名前に絞るなら morph)"); }
	c.visemes = r.text(mj, "visemes", mw);
	c.start = r.number(mj, "start", 0.0f, mw);
	c.name = r.text(mj, "morph", mw);
	if (!r.error.empty()) { return false; }
	if (c.visemes.empty()) { return r.fail(mw + ".visemes", "口の表のパスが空"); }
	const auto text = readMorphAsset(c.visemes, baseDir);
	if (!text) { return r.fail(mw + ".visemes", "口の表を読めない: " + c.visemes + " (assets からの相対か絶対のパス。tools/film_voice.py が作る)"); }
	std::string why;
	auto track = animation::parseVisemeTrack(*text, &why);
	if (!track) { return r.fail(mw + ".visemes", c.visemes + ": " + why); }
	c.track = std::move(*track);
	return true;
}

/// @brief props[].morphs: キーの channel と口の表の channel。名前の種類は ExpressionPose の項目数 (16) までなので、超えたら断る。
inline void readMorphs(Reader& r, const Json& j, Prop& p, const std::string& w, const std::string& baseDir)
{
	if (!j.contains("morphs")) { return; }
	if (!j["morphs"].is_array()) { r.fail(w + ".morphs", "配列でない"); return; }
	if (p.model.empty()) { r.fail(w + ".morphs", "morphs は model にだけ書ける"); return; }
	for (std::size_t i = 0; i < j["morphs"].size() && r.error.empty(); ++i)
	{
		const Json& mj = j["morphs"][i];
		const std::string mw = at(w + ".morphs", i);
		if (!onlyKeys(r, mj, {"name", "keys", "interp", "gain", "visemes", "start", "morph"}, mw)) { return; }
		MorphChannel c;
		c.gain = r.number(mj, "gain", 1.0f, mw);
		if (r.error.empty() && !(c.gain >= 0.0f)) { r.fail(mw + ".gain", "0 以上にする"); }
		if (!r.error.empty()) { return; }
		const bool ok = mj.contains("visemes") ? readMorphVisemes(r, mj, c, mw, baseDir) : readMorphKeys(r, mj, c, mw);
		if (!ok) { return; }
		p.morphs.push_back(std::move(c));
	}
	if (morphNameCount(p) > animation::kExpressionMaxEntries)
	{
		r.fail(w + ".morphs", "動かす名前が " + std::to_string(animation::kExpressionMaxEntries) + " 種類を超える (口の表は morph を書かなければ 5 種類)");
	}
}

inline void readProps(Reader& r, const Json& arr, Film& f, const std::string& baseDir)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("props", i);
		if (!onlyKeys(r, j, {"name", "shape", "model", "pos", "rot", "scale", "color", "alpha", "clip", "clipSpeed", "clipOffset",
		                     "clipLoop", "clips", "morphs", "interp", "keys", "visible"}, w)) { return; }
		Prop p;
		p.name = r.text(j, "name", w);
		p.shape = r.text(j, "shape", w);
		p.model = r.text(j, "model", w);
		if (r.error.empty() && p.name.empty()) { r.fail(w, "name が要る"); }
		if (r.error.empty() && (p.shape.empty() == p.model.empty())) { r.fail(w, "shape か model のどちらか 1 つを書く"); }
		if (r.error.empty() && p.model.empty() && (j.contains("clip") || j.contains("clips"))) { r.fail(w, "clip は model にだけ書ける"); }
		p.pos = r.vec3(j, "pos", p.pos, w);
		p.rotDeg = r.vec3(j, "rot", p.rotDeg, w);
		p.scale = r.vec3(j, "scale", p.scale, w);
		p.color = color(r, j, "color", p.color, w);
		p.alpha = r.number(j, "alpha", 1.0f, w);
		readPropClips(r, j, p, w);
		readMorphs(r, j, p, w, baseDir);
		const bool smooth = r.smooth(j, w);
		const Json& keys = keysOf(r, j, w);
		readTrack(r, keys, "pos", smooth, p.posTrack, w + ".keys", parseVec3);
		readTrack(r, keys, "rot", smooth, p.rotTrack, w + ".keys", parseVec3);
		readTrack(r, keys, "scale", smooth, p.scaleTrack, w + ".keys", parseVec3);
		readTrack(r, keys, "color", smooth, p.colorTrack, w + ".keys", parseColor);
		readTrack(r, keys, "alpha", false, p.alphaTrack, w + ".keys", parseFloat);
		readTrack(r, keys, "visible", false, p.visibleTrack, w + ".keys", parseFloat);
		for (const Prop& other : f.props)
		{
			if (r.error.empty() && other.name == p.name) { r.fail(w + ".name", "名前が重なっている: " + p.name); }
		}
		f.props.push_back(std::move(p));
	}
}

inline void readLights(Reader& r, const Json& arr, Film& f)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("lights", i);
		if (!onlyKeys(r, j, {"name", "kind", "pos", "dir", "color", "intensity", "range", "inner", "outer", "castShadow", "interp",
		                     "keys"}, w)) { return; }
		Light l;
		l.name = r.text(j, "name", w);
		const std::string kind = r.text(j, "kind", w);
		if (kind == "sun") { l.kind = LightKind::Sun; }
		else if (kind == "point") { l.kind = LightKind::Point; }
		else if (kind == "spot") { l.kind = LightKind::Spot; }
		else { r.fail(w + ".kind", "sun / point / spot のどれか"); }
		l.pos = r.vec3(j, "pos", l.pos, w);
		l.dir = r.vec3(j, "dir", l.dir, w);
		l.color = color(r, j, "color", l.color, w);
		l.intensity = r.number(j, "intensity", 1.0f, w);
		l.range = r.number(j, "range", l.range, w);
		l.innerDeg = r.number(j, "inner", l.innerDeg, w);
		l.outerDeg = r.number(j, "outer", l.outerDeg, w);
		l.castShadow = j.contains("castShadow") && j["castShadow"] == true;
		const bool smooth = r.smooth(j, w);
		const Json& keys = keysOf(r, j, w);
		readTrack(r, keys, "pos", smooth, l.posTrack, w + ".keys", parseVec3);
		readTrack(r, keys, "dir", smooth, l.dirTrack, w + ".keys", parseVec3);
		readTrack(r, keys, "color", smooth, l.colorTrack, w + ".keys", parseColor);
		readTrack(r, keys, "intensity", false, l.intensityTrack, w + ".keys", parseFloat);
		readTrack(r, keys, "range", false, l.rangeTrack, w + ".keys", parseFloat);
		f.lights.push_back(std::move(l));
	}
}

inline void floats(Reader& r, const Json& j, const char* key, float* out, std::size_t n, const std::string& where)
{
	if (!j.contains(key)) { return; }
	const Json& a = j[key];
	if (!a.is_array() || a.size() != n) { r.fail(where + "." + key, std::to_string(n) + " 個の数の配列でない"); return; }
	for (std::size_t i = 0; i < n; ++i)
	{
		if (!a[i].is_number()) { r.fail(where + "." + key, "数でない要素がある"); return; }
		out[i] = a[i].get<float>();
	}
}

inline void readEmitterColors(Reader& r, const Json& j, render::ParticleEmitterDesc& d, const std::string& where)
{
	if (!j.contains("colors")) { return; }
	const Json& a = j["colors"];
	if (!a.is_array() || a.size() != 3) { r.fail(where + ".colors", "[ライフタイムの始め, 中ほど, 終わり] の 3 色"); return; }
	for (std::size_t i = 0; i < 3 && r.error.empty(); ++i)
	{
		Json holder = Json::object();
		holder["c"] = a[i].is_array() && a[i].size() == 4 ? Json::array({a[i][0], a[i][1], a[i][2]}) : a[i];
		const Vec3 c = color(r, holder, "c", {1, 1, 1}, where + ".colors");
		d.color[i][0] = c.x; d.color[i][1] = c.y; d.color[i][2] = c.z;
		d.color[i][3] = (a[i].is_array() && a[i].size() == 4 && a[i][3].is_number()) ? a[i][3].get<float>() : (i == 2 ? 0.0f : 1.0f);
	}
}

inline void readEmitters(Reader& r, const Json& arr, Film& f)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("emitters", i);
		if (!onlyKeys(r, j, {"name", "start", "end", "pos", "seed", "duration", "spawnRadius", "dir", "spreadDeg", "speed", "life",
		                     "rate", "burst", "gravity", "drag", "size", "colors", "midPoint", "stretch", "lit", "blend", "shape"}, w)) { return; }
		Emitter e;
		render::ParticleEmitterDesc& d = e.desc;
		e.name = r.text(j, "name", w);
		e.start = r.number(j, "start", 0.0f, w);
		e.end = r.number(j, "end", e.end, w);
		d.key = static_cast<std::uint32_t>(i + 1);
		d.seed = static_cast<std::uint32_t>(r.number(j, "seed", static_cast<float>(i + 1), w));
		d.duration = r.number(j, "duration", 0.0f, w);
		floats(r, j, "pos", d.position, 3, w);
		d.spawnRadius = r.number(j, "spawnRadius", d.spawnRadius, w);
		floats(r, j, "dir", d.direction, 3, w);
		d.spreadDeg = r.number(j, "spreadDeg", d.spreadDeg, w);
		float speed[2] = {d.speedMin, d.speedMax}, life[2] = {d.lifeMin, d.lifeMax};
		floats(r, j, "speed", speed, 2, w);
		floats(r, j, "life", life, 2, w);
		d.speedMin = speed[0]; d.speedMax = speed[1]; d.lifeMin = life[0]; d.lifeMax = life[1];
		d.rate = r.number(j, "rate", 0.0f, w);
		d.burst = static_cast<std::uint32_t>(r.number(j, "burst", 0.0f, w));
		floats(r, j, "gravity", d.gravity, 3, w);
		d.drag = r.number(j, "drag", 0.0f, w);
		floats(r, j, "size", d.size, 3, w);
		readEmitterColors(r, j, d, w);
		d.midPoint = r.number(j, "midPoint", d.midPoint, w);
		d.stretch = r.number(j, "stretch", 0.0f, w);
		d.lit = r.number(j, "lit", 0.0f, w);
		const std::string blend = r.text(j, "blend", w), shape = r.text(j, "shape", w);
		if (blend == "alpha") { d.blend = render::ParticleBlend::Alpha; }
		else if (!blend.empty() && blend != "additive") { r.fail(w + ".blend", "additive / alpha のどちらか"); }
		if (shape == "spark") { d.shape = render::ParticleShape::Spark; }
		else if (shape == "hard") { d.shape = render::ParticleShape::HardDisc; }
		else if (!shape.empty() && shape != "soft") { r.fail(w + ".shape", "soft / spark / hard のどれか"); }
		if (r.error.empty() && d.rate <= 0.0f && d.burst == 0) { r.fail(w, "rate か burst のどちらかは 1 以上にする"); }
		f.emitters.push_back(std::move(e));
	}
}

inline void readLookValue(Reader& r, const Json& v, const LookField& fld, render::SceneLook& look, const std::string& where)
{
	if (fld.kind == LookKind::Vec3)
	{
		Json holder = Json::object();
		holder["v"] = v;
		const Vec3 c = color(r, holder, "v", {}, where);
		setLookVec3(look, fld, c.x, c.y, c.z);
		return;
	}
	if (v.is_boolean()) { setLookScalar(look, fld, v.get<bool>() ? 1.0f : 0.0f); return; }
	if (!v.is_number()) { r.fail(where, "数か真偽値でない"); return; }
	setLookScalar(look, fld, v.get<float>());
}

inline void readLook(Reader& r, const Json& root, Film& f)
{
	if (!root.contains("look")) { return; }
	if (!root["look"].is_object()) { r.fail("look", "オブジェクトでない"); return; }
	for (auto it = root["look"].begin(); it != root["look"].end() && r.error.empty(); ++it)
	{
		if (it.key() == "shadowPenumbraDeg")
		{
			f.shadowPenumbraDeg = r.number(root["look"], "shadowPenumbraDeg", 0.0f, "look");
			if (r.error.empty() && (f.shadowPenumbraDeg < 0.0f || f.shadowPenumbraDeg > 20.0f)) { r.fail("look.shadowPenumbraDeg", "0 以上 20 以下 (度)"); }
			continue;
		}
		const LookField* fld = findLookField(it.key());
		if (fld == nullptr) { r.fail("look." + it.key(), "知らない項目。SceneLook の項目名 (exposure, bloom, dofStrength, sky.enabled など)"); return; }
		readLookValue(r, it.value(), *fld, f.look, "look." + it.key());
	}
}

inline void readLookTracks(Reader& r, const Json& root, Film& f)
{
	if (!root.contains("look_tracks")) { return; }
	if (!root["look_tracks"].is_object()) { r.fail("look_tracks", "オブジェクトでない"); return; }
	for (auto it = root["look_tracks"].begin(); it != root["look_tracks"].end() && r.error.empty(); ++it)
	{
		const LookField* fld = findLookField(it.key());
		const std::string w = "look_tracks." + it.key();
		if (fld == nullptr || fld->kind == LookKind::Vec3) { r.fail(w, "数の項目だけ動かせる (exposure, bloomStrength, dofStrength など)"); return; }
		Json holder = Json::object();
		holder[it.key()] = it.value();
		std::vector<narrative::FloatKey> keys;
		narrative::seq_detail::readFloatTrack(r, holder, it.key().c_str(), keys);
		if (r.error.empty()) { f.lookTracks.emplace_back(it.key(), std::move(keys)); }
	}
}

inline void readPair(Reader& r, const Json& root, const char* key, Vec3& a, Vec3& b, bool& has)
{
	if (!root.contains(key)) { return; }
	const Json& o = root[key];
	if (!onlyKeys(r, o, {"zenith", "nadir"}, key)) { return; }
	a = color(r, o, "zenith", a, key);
	b = color(r, o, "nadir", b, key);
	has = true;
}

/// @brief 最上位の hdri: 空と環境光にする正距円筒の HDR。file は `/` `\` `:` を含まなければ film_player/assets/hdri/ の下の名前。null は雛形の hdri を使わない
inline void readHdri(Reader& r, const Json& root, Film& f)
{
	if (!root.contains("hdri") || root["hdri"].is_null()) { return; }
	const Json& o = root["hdri"];
	if (!onlyKeys(r, o, {"file", "intensity", "yaw"}, "hdri")) { return; }
	const std::string file = r.text(o, "file", "hdri");
	if (r.error.empty() && file.empty()) { r.fail("hdri.file", "ファイル名か、assets からの相対か絶対のパスが要る"); return; }
	const bool plainName = file.find_first_of("/\\:") == std::string::npos;
	f.envHdr = plainName ? "film_player/assets/hdri/" + file : file;
	f.envHdrIntensity = r.number(o, "intensity", 1.0f, "hdri");
	f.envHdrYawDeg = r.number(o, "yaw", 0.0f, "hdri");
	if (r.error.empty() && !(f.envHdrIntensity > 0.0f)) { r.fail("hdri.intensity", "0 より大きくする"); }
}

inline int findPropIndex(const Film& f, const std::string& name)
{
	for (std::size_t i = 0; i < f.props.size(); ++i)
	{
		if (f.props[i].name == name) { return static_cast<int>(i); }
	}
	return -1;
}

inline std::string propNames(const Film& f)
{
	std::string list;
	for (const Prop& p : f.props) { list += std::string(list.empty() ? "" : ", ") + p.name; }
	return list.empty() ? "(小道具なし)" : list;
}

/// @brief focusOn.rack: [{t, prop, over, offset}]。t の昇順で、小道具の名前は添字に変換する
inline void readFocusRacks(Reader& r, const Json& j, CameraRig& rig, const Film& f, const std::string& where)
{
	if (!j.contains("rack")) { return; }
	if (!j["rack"].is_array()) { r.fail(where + ".rack", "配列でない"); return; }
	for (std::size_t i = 0; i < j["rack"].size() && r.error.empty(); ++i)
	{
		const Json& rj = j["rack"][i];
		const std::string rw = at(where + ".rack", i);
		if (!onlyKeys(r, rj, {"t", "prop", "over", "offset"}, rw)) { return; }
		FocusRack fr;
		fr.t = r.required(rj, "t", rw);
		fr.over = r.number(rj, "over", 1.0f, rw);
		fr.prop = r.text(rj, "prop", rw);
		fr.offset = r.vec3(rj, "offset", {}, rw);
		fr.index = findPropIndex(f, fr.prop);
		if (r.error.empty() && fr.index < 0) { r.fail(rw + ".prop", "小道具 " + fr.prop + " が無い。使える名前は " + propNames(f)); }
		if (r.error.empty() && !(fr.over >= 0.0f)) { r.fail(rw + ".over", "0 以上にする"); }
		if (r.error.empty() && !rig.racks.empty() && fr.t < rig.racks.back().t + rig.racks.back().over)
		{
			r.fail(rw, "前の rack のウィンドウ (t から over 秒) が終わる前に始まっている。ウィンドウは重ねない");
		}
		rig.racks.push_back(fr);
	}
}

/// @brief cameras[i].lookAt、focusOn、shake を読む。小道具名は props を読んだ後、読み込みの最後に添字へ変換する。
inline void readRigs(Reader& r, const Json& cameras, Film& f)
{
	f.rigs.resize(f.seq.cameras.size());
	for (std::size_t i = 0; i < cameras.size() && i < f.rigs.size() && r.error.empty(); ++i)
	{
		const Json& c = cameras[i];
		const std::string w = at("cameras", i);
		CameraRig& rig = f.rigs[i];
		if (c.contains("lookAt"))
		{
			const Json& j = c["lookAt"];
			if (!onlyKeys(r, j, {"prop", "offset"}, w + ".lookAt")) { return; }
			rig.lookAtProp = r.text(j, "prop", w + ".lookAt");
			rig.lookAtOffset = r.vec3(j, "offset", {}, w + ".lookAt");
			rig.lookAtIndex = findPropIndex(f, rig.lookAtProp);
			if (r.error.empty() && rig.lookAtIndex < 0) { r.fail(w + ".lookAt.prop", "小道具 " + rig.lookAtProp + " が無い。使える名前は " + propNames(f)); }
		}
		if (c.contains("focusOn"))
		{
			const Json& j = c["focusOn"];
			if (!onlyKeys(r, j, {"prop", "offset", "rack"}, w + ".focusOn")) { return; }
			rig.focusProp = r.text(j, "prop", w + ".focusOn");
			rig.focusOffset = r.vec3(j, "offset", {}, w + ".focusOn");
			rig.focusIndex = findPropIndex(f, rig.focusProp);
			if (r.error.empty() && rig.focusIndex < 0) { r.fail(w + ".focusOn.prop", "小道具 " + rig.focusProp + " が無い。使える名前は " + propNames(f)); }
			readFocusRacks(r, j, rig, f, w + ".focusOn");
		}
		if (c.contains("shake"))
		{
			const Json& j = c["shake"];
			if (!onlyKeys(r, j, {"pos", "rot", "freq", "seed"}, w + ".shake")) { return; }
			rig.shake.posAmp = r.number(j, "pos", 0.0f, w + ".shake");
			rig.shake.rotAmpDeg = r.number(j, "rot", 0.0f, w + ".shake");
			rig.shake.freq = r.number(j, "freq", rig.shake.freq, w + ".shake");
			rig.shake.seed = static_cast<std::uint32_t>(r.number(j, "seed", 1.0f, w + ".shake"));
			if (r.error.empty() && (rig.shake.posAmp < 0.0f || rig.shake.rotAmpDeg < 0.0f || !(rig.shake.freq > 0.0f)))
			{
				r.fail(w + ".shake", "pos と rot は 0 以上、freq は 0 より大きくする");
			}
		}
	}
}

inline void readLenses(Reader& r, const Json& cameras, Film& f)
{
	f.lenses.resize(f.seq.cameras.size());
	for (std::size_t i = 0; i < cameras.size() && i < f.lenses.size() && r.error.empty(); ++i)
	{
		const Json& c = cameras[i];
		const std::string w = at("cameras", i);
		Lens& lens = f.lenses[i];
		lens.sensorHeightMm = r.number(c, "sensorHeight", lens.sensorHeightMm, w);
		if (!c.contains("lens")) { continue; }
		const Json& keys = c["lens"];
		if (!keys.is_array()) { r.fail(w + ".lens", "鍵の配列でない"); return; }
		readTrack(r, keys, "focal", true, lens.focalMm, w + ".lens", parseFloat);
		readTrack(r, keys, "fstop", true, lens.fstop, w + ".lens", parseFloat);
		readTrack(r, keys, "focus", true, lens.focusDist, w + ".lens", parseFloat);
		readTrack(r, keys, "ev", true, lens.evBias, w + ".lens", parseFloat);
		readTrack(r, keys, "distortion", false, lens.distortion, w + ".lens", parseFloat);
		for (const auto& k : lens.distortion.keys) { if (!(k.v >= -0.3f && k.v <= 0.3f)) { r.fail(w + ".lens", "distortion は -0.3 以上 0.3 以下にする"); } }
		for (const auto& k : lens.focalMm.keys) { if (!(k.v > 0.0f)) { r.fail(w + ".lens", "focal は 0 より大きくする"); } }
		for (const auto& k : lens.fstop.keys) { if (!(k.v > 0.0f)) { r.fail(w + ".lens", "fstop は 0 より大きくする"); } }
		for (const auto& k : lens.focusDist.keys) { if (!(k.v > 0.0f)) { r.fail(w + ".lens", "focus は 0 より大きくする"); } }
	}
}

inline void readEdit(Reader& r, const Json& root, Film& f);
inline void finishEdit(Reader& r, Film& f);

}  // namespace film_detail

/// @brief JSON の文字列から読む。name はスクリプト名、baseDir はカメラの軌跡ファイルの基準。
[[nodiscard]] inline std::optional<Film> loadFilmFromJson(std::string_view json, std::string_view name, std::string_view baseDir,
                                                          std::string& error)
{
	using namespace film_detail;
	std::string flat;
	if (!flattenIfShots(json, baseDir, readFileDefault, flat, error)) { error = std::string(name) + ": " + error; return std::nullopt; }
	if (!flat.empty()) { json = flat; }
	Json root = Json::parse(json.begin(), json.end(), nullptr, false);
	if (root.is_discarded() || !root.is_object()) { error = std::string(name) + ": JSON として読めない"; return std::nullopt; }
	if (!applyLookPreset(root, baseDir, error)) { error = std::string(name) + ": " + error; return std::nullopt; }
	auto seq = narrative::loadSequenceFromJson(json, name, baseDir, error);
	if (!seq) { return std::nullopt; }
	Film f;
	f.seq = std::move(*seq);
	Reader r;
	if (root.contains("film") && root["film"] != 1) { r.fail("film", "版は 1"); }
	readSize(r, root, f);
	readClearColor(r, root, f);
	readPair(r, root, "sky", f.skyZenith, f.skyNadir, f.hasSky);
	readPair(r, root, "environment", f.envZenith, f.envNadir, f.hasEnv);
	readHdri(r, root, f);
	(void)r.text(root, "lighting_bake", "film");   // 読むのは tools/film.py (host の --lighting-bake)。ここでは型だけ確かめる (ADR 0151)
	readLook(r, root, f);
	readLookTracks(r, root, f);
	readProps(r, narrative::seq_detail::arrayOrEmpty(root, "props"), f, std::string(baseDir));
	readOutdoor(r, root, f);
	readScatters(r, narrative::seq_detail::arrayOrEmpty(root, "scatter"), f);
	readLights(r, narrative::seq_detail::arrayOrEmpty(root, "lights"), f);
	readEmitters(r, narrative::seq_detail::arrayOrEmpty(root, "emitters"), f);
	readLenses(r, narrative::seq_detail::arrayOrEmpty(root, "cameras"), f);
	readRigs(r, narrative::seq_detail::arrayOrEmpty(root, "cameras"), f);
	readEdit(r, root, f);
	finishEdit(r, f);
	if (!r.error.empty()) { error = std::string(name) + ": " + r.error; return std::nullopt; }
	for (const auto& c : f.seq.cues)
	{
		if (c.cue.kind == narrative::CueKind::Sound || c.cue.kind == narrative::CueKind::Music) { f.sounds.push_back(c.cue.name); }
	}
	error.clear();
	return f;
}

[[nodiscard]] inline std::optional<Film> loadFilmFile(std::string_view path, std::string& error)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes) { error = "映像の JSON を読めない: " + std::string(path); return std::nullopt; }
	const std::filesystem::path p{std::string(path)};
	std::string stem = p.filename().string();
	stem = stem.substr(0, stem.find('.'));
	const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
	return loadFilmFromJson(text, stem, p.parent_path().generic_string(), error);
}

}  // namespace mitiru::film

#include <mitiru/film/FilmEditLoad.hpp>
