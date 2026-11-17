#pragma once

/// @file FilmEditLoad.hpp
/// @brief `edit`、`titles`、ルートの `time_remap` を Film に読む。FilmLoad.hpp の末尾から include される

#include <memory>
#include <string>

#include <mitiru/film/FilmLoad.hpp>

namespace mitiru::film::film_detail
{

inline bool readTransitionJson(Reader& r, const Json& j, Transition& out, const std::string& w)
{
	if (!onlyKeys(r, j, {"kind", "duration", "direction", "softness"}, w)) { return false; }
	const std::string kind = r.text(j, "kind", w);
	if (kind == "dissolve") { out.kind = TransitionKind::Dissolve; }
	else if (kind == "wipe") { out.kind = TransitionKind::Wipe; }
	else if (kind == "dip") { out.kind = TransitionKind::Dip; }
	else { return r.fail(w + ".kind", "dissolve / wipe / dip のどれか"); }
	out.duration = r.number(j, "duration", 0.0f, w);
	if (!(out.duration > 0.0f)) { return r.fail(w + ".duration", "0 より大きくする"); }
	const std::string dir = r.text(j, "direction", w);
	if (dir == "left") { out.dir = WipeDir::Left; }
	else if (dir == "up") { out.dir = WipeDir::Up; }
	else if (dir == "down") { out.dir = WipeDir::Down; }
	else if (!dir.empty() && dir != "right") { return r.fail(w + ".direction", "right / left / up / down のどれか"); }
	out.softness = r.number(j, "softness", out.softness, w);
	return r.error.empty();
}

inline void readShotLook(Reader& r, const Json& look, ShotEdit& s, const std::string& w)
{
	if (!look.is_object()) { r.fail(w, "オブジェクトでない"); return; }
	for (auto it = look.begin(); it != look.end() && r.error.empty(); ++it)
	{
		const LookField* fld = findLookField(it.key());
		if (fld == nullptr) { r.fail(w + "." + it.key(), "知らない項目。SceneLook の項目名 (exposure, saturation, contrast など)"); return; }
		render::SceneLook scratch{};
		readLookValue(r, it.value(), *fld, scratch, w + "." + it.key());
		LookPatch p;
		p.field = fld;
		const auto* base = reinterpret_cast<const unsigned char*>(&scratch) + fld->offset;
		if (fld->kind == LookKind::Vec3) { std::memcpy(p.v, base, sizeof(float) * 3); }
		else if (fld->kind == LookKind::Float) { std::memcpy(p.v, base, sizeof(float)); }
		else { p.v[0] = fld->kind == LookKind::Bool ? (*reinterpret_cast<const bool*>(base) ? 1.0f : 0.0f) : static_cast<float>(*base); }
		s.look.push_back(p);
	}
}

inline void readEditShots(Reader& r, const Json& arr, Film& f)
{
	if (!arr.is_array()) { r.fail("edit.shots", "配列でない"); return; }
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("edit.shots", i);
		if (!onlyKeys(r, j, {"name", "source", "start", "end", "world_start", "cue_shift", "cameras", "look", "fade", "transition", "time_remap"}, w)) { return; }
		ShotEdit s;
		s.name = r.text(j, "name", w);
		s.source = r.text(j, "source", w);
		s.start = r.required(j, "start", w);
		s.end = r.required(j, "end", w);
		s.worldStart = r.number(j, "world_start", s.start, w);
		s.cueShift = r.number(j, "cue_shift", 0.0f, w);
		if (r.error.empty() && !(s.end > s.start)) { r.fail(w, "start < end にする"); }
		if (j.contains("cameras"))
		{
			if (!j["cameras"].is_array()) { r.fail(w + ".cameras", "名前の配列でない"); return; }
			for (const Json& n : j["cameras"]) { if (n.is_string()) { s.cameraNames.push_back(n.get<std::string>()); } }
		}
		if (j.contains("look")) { readShotLook(r, j["look"], s, w + ".look"); }
		if (j.contains("fade"))
		{
			Json holder = Json::object();
			holder["fade"] = j["fade"];
			narrative::seq_detail::readFloatTrack(r, holder, "fade", s.fade);
			s.hasFade = true;
		}
		if (j.contains("transition")) { readTransitionJson(r, j["transition"], s.transitionIn, w + ".transition"); }
		f.edit.shots.push_back(std::move(s));
	}
}

inline void readTimeMap(Reader& r, const Json& arr, const std::string& w, Film& f)
{
	if (!arr.is_array()) { r.fail(w, "配列でない"); return; }
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const std::string sw = at(w, i);
		if (!onlyKeys(r, arr[i], {"start", "end", "speed"}, sw)) { return; }
		TimeSeg s;
		s.start = r.required(arr[i], "start", sw);
		s.end = r.required(arr[i], "end", sw);
		s.speed = r.required(arr[i], "speed", sw);
		if (r.error.empty() && (!(s.end > s.start) || !(s.speed > 0.0f))) { r.fail(sw, "start < end で speed > 0 にする (時間の巻き戻しは断る)"); }
		if (r.error.empty() && !f.edit.timeMap.empty() && s.start < f.edit.timeMap.back().end) { r.fail(sw, "区間が前の区間と重なっている、または順番が逆"); }
		f.edit.timeMap.push_back(s);
	}
}

inline void readTitles(Reader& r, const Json& arr, Film& f)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("titles", i);
		if (!onlyKeys(r, j, {"kind", "text", "sub", "start", "end", "fade_in", "fade_out", "pos", "size", "color", "panel"}, w)) { return; }
		TitleCard c;
		const std::string kind = r.text(j, "kind", w);
		if (kind == "lower_third") { c.kind = TitleKind::LowerThird; c.x = 0.08f; c.y = 0.78f; c.size = 0.055f; c.panel = 0.55f; }
		else if (!kind.empty() && kind != "title") { r.fail(w + ".kind", "title / lower_third のどちらか"); return; }
		c.text = r.text(j, "text", w);
		c.sub = r.text(j, "sub", w);
		c.start = r.required(j, "start", w);
		c.end = r.required(j, "end", w);
		c.fadeIn = r.number(j, "fade_in", c.fadeIn, w);
		c.fadeOut = r.number(j, "fade_out", c.fadeOut, w);
		c.size = r.number(j, "size", c.size, w);
		c.panel = r.number(j, "panel", c.panel, w);
		if (j.contains("pos"))
		{
			float xy[2] = {c.x, c.y};
			floats(r, j, "pos", xy, 2, w);
			c.x = xy[0]; c.y = xy[1];
		}
		const Vec3 col = color(r, j, "color", {1.0f, 1.0f, 1.0f}, w);
		c.color[0] = col.x; c.color[1] = col.y; c.color[2] = col.z;
		if (r.error.empty() && c.text.empty()) { r.fail(w, "text が要る"); }
		if (r.error.empty() && !(c.end > c.start)) { r.fail(w, "start < end にする"); }
		if (r.error.empty() && (c.fadeIn < 0.0f || c.fadeOut < 0.0f || !(c.size > 0.0f))) { r.fail(w, "fade_in と fade_out は 0 以上、size は 0 より大きくする"); }
		f.edit.titles.push_back(std::move(c));
	}
}

inline void readEdit(Reader& r, const Json& root, Film& f)
{
	if (root.contains("edit"))
	{
		const Json& ed = root["edit"];
		if (!onlyKeys(r, ed, {"shots", "time_remap"}, "edit")) { return; }
		if (ed.contains("shots")) { readEditShots(r, ed["shots"], f); }
		if (ed.contains("time_remap")) { readTimeMap(r, ed["time_remap"], "edit.time_remap", f); }
	}
	if (root.contains("time_remap"))
	{
		if (!f.edit.timeMap.empty()) { r.fail("time_remap", "edit.time_remap と同時には書けない"); return; }
		readTimeMap(r, root["time_remap"], "time_remap", f);
	}
	if (root.contains("titles"))
	{
		if (!root["titles"].is_array()) { r.fail("titles", "配列でない"); return; }
		readTitles(r, root["titles"], f);
	}
}

/// @brief トランジション前後のショットだけを映す Film のコピー。カメラ区間を前後に広げ、キーの端の姿で止める
inline std::shared_ptr<const Film> makeLayerFilm(const Film& f, const ShotEdit& shot)
{
	auto layer = std::make_shared<Film>(f);
	layer->edit.layerFilms.clear();
	auto& cams = layer->seq.cameras;
	float first = 1.0e30f, last = -1.0e30f;
	int firstIdx = -1, lastIdx = -1;
	for (std::size_t i = 0; i < cams.size(); ++i)
	{
		const bool mine = std::find(shot.cameras.begin(), shot.cameras.end(), static_cast<int>(i)) != shot.cameras.end();
		if (!mine) { cams[i].start = cams[i].end = 0.0f; continue; }
		if (cams[i].start < first) { first = cams[i].start; firstIdx = static_cast<int>(i); }
		if (cams[i].end > last) { last = cams[i].end; lastIdx = static_cast<int>(i); }
	}
	if (firstIdx < 0) { return nullptr; }
	cams[static_cast<std::size_t>(firstIdx)].start = -1.0e6f;
	cams[static_cast<std::size_t>(firstIdx)].blendIn = 0.0f;
	cams[static_cast<std::size_t>(lastIdx)].end = 1.0e6f;
	narrative::buildCameraSegments(layer->seq);
	return layer;
}

/// @brief カメラ名を添字に変え、トランジション前後のレイヤーを作る。読み込みの最後に 1 度だけ呼ぶ
inline void finishEdit(Reader& r, Film& f)
{
	for (std::size_t i = 0; i < f.edit.shots.size() && r.error.empty(); ++i)
	{
		ShotEdit& s = f.edit.shots[i];
		for (const std::string& n : s.cameraNames)
		{
			int found = -1;
			for (std::size_t c = 0; c < f.seq.cameras.size(); ++c) { if (f.seq.cameras[c].name == n) { found = static_cast<int>(c); } }
			if (found < 0) { r.fail(at("edit.shots", i) + ".cameras", "カメラ " + n + " が cameras に無い"); return; }
			s.cameras.push_back(found);
		}
		if (s.transitionIn.kind != TransitionKind::None && (i == 0 || s.cameras.empty() || f.edit.shots[i - 1].cameras.empty()))
		{
			r.fail(at("edit.shots", i) + ".transition", "前と後のショットのどちらにもカメラが要る");
			return;
		}
	}
	f.edit.layerFilms.assign(f.edit.shots.size(), nullptr);
	for (std::size_t i = 1; i < f.edit.shots.size(); ++i)
	{
		if (f.edit.shots[i].transitionIn.kind == TransitionKind::None) { continue; }
		for (std::size_t k : {i - 1, i})
		{
			if (f.edit.layerFilms[k] == nullptr) { f.edit.layerFilms[k] = makeLayerFilm(f, f.edit.shots[k]); }
		}
	}
}

}  // namespace mitiru::film::film_detail
