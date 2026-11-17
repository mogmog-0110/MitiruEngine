#pragma once

/// @file FilmEditShots.hpp
/// @brief 別ファイルから連結した `shots` を、1 本の映像を表す平らな JSON に展開する。
/// @details 時刻は JSON の double のまま足し、float への変換は既存の読み込み時の 1 回だけにする。float で足すと、元の 1 本の JSON と 1 ulp ずれることがある。誤りは `shots[2].include: ...` のように場所を付けて返す。

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/film/FilmLook.hpp>

namespace mitiru::film
{

using FileReader = std::function<std::optional<std::string>(const std::string&)>;

/// @brief vfs の assets 配下を先に読み、失敗したら通常のファイルを読む。DLL が絶対パスの映像を撮るときも、この経路で読む。
[[nodiscard]] inline std::optional<std::string> readFileDefault(const std::string& path)
{
	if (const auto bytes = vfs::readAsset(path)) { return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()); }
	std::ifstream in(path, std::ios::binary);
	if (!in) { return std::nullopt; }
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

namespace edit_detail
{

using Json = nlohmann::json;

struct Failure
{
	std::string text;
	bool fail(const std::string& where, const std::string& what)
	{
		if (text.empty()) { text = where + ": " + what; }
		return false;
	}
};

struct Context
{
	const FileReader&        read;
	std::vector<std::string> stack;   ///< 読んでいる最中のファイル (循環の検出)
};

constexpr const char* kEntryKeys = "include, name, start, offset, duration, look, lens, fade, transition, j_cut, l_cut, time_remap";
constexpr const char* kContentKeys = "film, duration, name, cameras, props, lights, emitters, cues, subtitles, titles, letterbox, look, lens, "
                                     "fade, shots, time_remap";

inline bool allowed(const Json& j, std::initializer_list<const char*> keys, const std::string& where, Failure& e, const char* list)
{
	for (auto it = j.begin(); it != j.end(); ++it)
	{
		bool ok = false;
		for (const char* k : keys) { ok = ok || it.key() == k; }
		if (!ok) { return e.fail(where + "." + it.key(), std::string("知らない項目。使えるのは ") + list); }
	}
	return true;
}

inline std::string normalizedKey(const std::string& path)
{
	std::string s = std::filesystem::path(path).lexically_normal().generic_string();
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

inline std::string joinPath(const std::string& dir, const std::string& rel)
{
	const std::filesystem::path r{rel};
	if (dir.empty() || r.is_absolute()) { return r.lexically_normal().generic_string(); }
	return (std::filesystem::path(dir) / r).lexically_normal().generic_string();
}

inline std::string dirOf(const std::string& path) { return std::filesystem::path(path).parent_path().generic_string(); }

inline bool isNumber(const Json& j, const char* key) { return j.contains(key) && j[key].is_number(); }

/// @brief ワールドの時間 τ(t)。区間の外では速さを 1 とし、区間の始めまでに進んだ時間を足す。
struct Seg
{
	double start = 0.0;
	double end = 0.0;
	double speed = 1.0;
};

inline double worldAt(const std::vector<Seg>& segs, double t)
{
	double world = 0.0, cursor = 0.0;
	for (const Seg& s : segs)
	{
		if (t <= s.start) { break; }
		world += s.start - cursor;
		const double upto = std::min(t, s.end);
		world += (upto - s.start) * s.speed;
		cursor = upto;
		if (t <= s.end) { return world; }
	}
	return world + (t - cursor);
}

inline void shiftTimes(Json& arr, const char* key, double d)
{
	if (!arr.is_array()) { return; }
	for (Json& o : arr)
	{
		if (o.is_object() && o.contains(key) && o[key].is_number()) { o[key] = o[key].get<double>() + d; }
	}
}

inline void shiftPairs(Json& arr, double d)
{
	if (!arr.is_array()) { return; }
	for (Json& p : arr)
	{
		if (p.is_array() && !p.empty() && p[0].is_number()) { p[0] = p[0].get<double>() + d; }
	}
}

inline bool readTimeRemap(const Json& arr, const std::string& where, double srcLen, std::vector<Seg>& out, double& outLen, double& consumedOut, Failure& e)
{
	if (!arr.is_array() || arr.empty()) { return e.fail(where, "{start, end, speed} の配列でない"); }
	double cursor = 0.0, consumed = 0.0;
	for (std::size_t i = 0; i < arr.size(); ++i)
	{
		const Json& s = arr[i];
		const std::string w = where + "[" + std::to_string(i) + "]";
		if (!s.is_object() || !isNumber(s, "start") || !isNumber(s, "end") || !isNumber(s, "speed"))
		{
			return e.fail(w, "start, end, speed が要る");
		}
		const double a = s["start"].get<double>(), b = s["end"].get<double>(), v = s["speed"].get<double>();
		if (!(v > 0.0)) { return e.fail(w + ".speed", "0 より大きくする (時間の巻き戻しは断る)"); }
		if (std::fabs(a - cursor) > 1.0e-9) { return e.fail(w + ".start", i == 0 ? "0 から始める" : "前の区間の end と同じにする (重ならず、隙間なく)"); }
		if (!(b > a)) { return e.fail(w, "start < end にする"); }
		out.push_back({a, b, v});
		consumed += (b - a) * v;
		cursor = b;
	}
	if (consumed > srcLen + 1.0e-6) { return e.fail(where, "元のショットの長さを超えて進む (" + std::to_string(consumed) + " > " + std::to_string(srcLen) + ")"); }
	outLen = cursor;
	consumedOut = consumed;
	return true;
}

inline bool readTransition(const Json& j, const std::string& where, Json& out, Failure& e)
{
	if (!j.is_object()) { return e.fail(where, "オブジェクトでない"); }
	if (!allowed(j, {"kind", "duration", "direction", "softness"}, where, e, "kind, duration, direction, softness")) { return false; }
	const std::string kind = j.contains("kind") && j["kind"].is_string() ? j["kind"].get<std::string>() : "";
	if (kind != "dissolve" && kind != "wipe" && kind != "dip") { return e.fail(where + ".kind", "dissolve / wipe / dip のどれか"); }
	if (!isNumber(j, "duration") || !(j["duration"].get<double>() > 0.0)) { return e.fail(where + ".duration", "0 より大きい秒数が要る"); }
	out = Json::object();
	out["kind"] = kind;
	out["duration"] = j["duration"];
	if (kind == "wipe")
	{
		const std::string dir = j.contains("direction") && j["direction"].is_string() ? j["direction"].get<std::string>() : "right";
		if (dir != "right" && dir != "left" && dir != "up" && dir != "down") { return e.fail(where + ".direction", "right / left / up / down のどれか"); }
		out["direction"] = dir;
		if (j.contains("softness"))
		{
			if (!isNumber(j, "softness") || j["softness"].get<double>() < 0.0 || j["softness"].get<double>() > 1.0) { return e.fail(where + ".softness", "0 以上 1 以下の数"); }
			out["softness"] = j["softness"];
		}
	}
	return true;
}

inline bool checkLookNames(const Json& look, const std::string& where, Failure& e)
{
	if (!look.is_object()) { return e.fail(where, "オブジェクトでない"); }
	for (auto it = look.begin(); it != look.end(); ++it)
	{
		if (findLookField(it.key()) == nullptr) { return e.fail(where + "." + it.key(), "知らない項目。SceneLook の項目名 (exposure, saturation, contrast など)"); }
	}
	return true;
}

/// @brief カメラのレンズをショットの lens で上書きする。同じフィールドのキーを消し、カメラの始まりに 1 つ置く。
inline bool overrideLens(Json& camera, const Json& lens, const std::string& where, Failure& e)
{
	if (!lens.is_object()) { return e.fail(where, "オブジェクトでない"); }
	if (!allowed(lens, {"focal", "fstop", "focus", "ev", "sensorHeight"}, where, e, "focal, fstop, focus, ev, sensorHeight")) { return false; }
	const double t0 = camera.contains("start") ? camera["start"].get<double>() : 0.0;
	for (auto it = lens.begin(); it != lens.end(); ++it)
	{
		if (!it.value().is_number()) { return e.fail(where + "." + it.key(), "数でない"); }
		if (it.key() == "sensorHeight") { camera["sensorHeight"] = it.value(); continue; }
		Json keep = Json::array();
		if (camera.contains("lens") && camera["lens"].is_array())
		{
			for (Json k : camera["lens"])
			{
				if (k.is_object()) { k.erase(it.key()); }
				if (k.is_object() && k.size() > 1) { keep.push_back(k); }
			}
		}
		keep.push_back(Json{{"t", t0}, {it.key(), it.value()}});
		camera["lens"] = keep;
	}
	return true;
}

struct Accum
{
	Json cameras = Json::array(), props = Json::array(), lights = Json::array(), emitters = Json::array();
	Json cues = Json::array(), subtitles = Json::array(), titles = Json::array(), letterbox = Json::array();
	Json editShots = Json::array();
	std::vector<Seg> timeMap;
	std::vector<std::pair<std::string, std::pair<double, double>>> named;   ///< alias:// の宛先
	double prevStart = 0.0, prevEnd = 0.0, prevWindowEnd = 0.0;
	double maxEnd = 0.0;
};

inline std::optional<std::pair<double, double>> findNamed(const Accum& a, const std::string& name)
{
	for (const auto& n : a.named) { if (n.first == name) { return n.second; } }
	return std::nullopt;
}

inline bool resolveStart(const Json& entry, const Accum& a, double overlap, const std::string& where, double& out, Failure& e)
{
	double base = a.prevEnd - overlap;
	if (entry.contains("start"))
	{
		const Json& s = entry["start"];
		if (s.is_number()) { base = s.get<double>(); }
		else if (s.is_string() && s.get<std::string>() == "auto") { base = a.prevEnd - overlap; }
		else if (s.is_string() && s.get<std::string>().rfind("alias://", 0) == 0)
		{
			std::string ref = s.get<std::string>().substr(8);
			bool atEnd = false;
			if (ref.size() > 4 && ref.compare(ref.size() - 4, 4, ".end") == 0) { atEnd = true; ref.resize(ref.size() - 4); }
			const auto hit = findNamed(a, ref);
			if (!hit)
			{
				std::string list;
				for (const auto& n : a.named) { list += std::string(list.empty() ? "" : ", ") + n.first; }
				return e.fail(where + ".start", "alias://" + ref + " は前にあるショットの name でない。使える名前は " + (list.empty() ? "(まだ無い)" : list));
			}
			base = atEnd ? hit->second : hit->first;
		}
		else { return e.fail(where + ".start", "秒の数か \"auto\" か \"alias://<name>\" (終わりに合わせるなら alias://<name>.end)"); }
	}
	if (entry.contains("offset"))
	{
		if (!entry["offset"].is_number()) { return e.fail(where + ".offset", "数でない"); }
		base += entry["offset"].get<double>();
	}
	if (base < -1.0e-9) { return e.fail(where + ".start", "0 より前になる (" + std::to_string(base) + ")"); }
	out = std::max(0.0, base);
	return true;
}

inline std::string cameraName(const Json& c, const std::string& shot, std::size_t i)
{
	return c.contains("name") && c["name"].is_string() ? c["name"].get<std::string>() : shot + ".cam" + std::to_string(i);
}

/// @brief doc の `shots` を平らなフィールドへ入れ替える。baseDir は doc があるフォルダ。
inline bool expandShots(Context& ctx, Json& doc, const std::string& baseDir, Failure& e);

inline bool loadIncluded(Context& ctx, const std::string& include, const std::string& baseDir, const std::string& where, Json& out, Failure& e)
{
	const std::string path = joinPath(baseDir, include);
	const std::string key = normalizedKey(path);
	if (std::find(ctx.stack.begin(), ctx.stack.end(), key) != ctx.stack.end())
	{
		std::string chain;
		for (const std::string& s : ctx.stack) { chain += std::filesystem::path(s).filename().generic_string() + " → "; }
		return e.fail(where + ".include", "include が循環している: " + chain + std::filesystem::path(key).filename().generic_string());
	}
	const auto text = ctx.read(path);
	if (!text) { return e.fail(where + ".include", "ファイルを読めない: " + path); }
	out = Json::parse(*text, nullptr, false);
	if (out.is_discarded() || !out.is_object()) { return e.fail(where + ".include", "JSON として読めない: " + path); }
	if (!allowed(out, {"film", "duration", "name", "cameras", "props", "lights", "emitters", "cues", "subtitles", "titles", "letterbox", "look",
	                   "lens", "fade", "shots", "time_remap"}, where + ".include(" + std::filesystem::path(path).filename().generic_string() + ")", e, kContentKeys))
	{
		return false;
	}
	if (out.contains("shots"))
	{
		ctx.stack.push_back(key);
		Failure inner;
		const bool ok = expandShots(ctx, out, dirOf(path), inner);
		ctx.stack.pop_back();
		if (!ok) { return e.fail(where + ".include(" + std::filesystem::path(path).filename().generic_string() + ")", inner.text); }
	}
	return true;
}

/// @brief ショットのフィールドを、時刻をずらして a に足す。tl は映像時間、wd はワールド時間のずれ。
inline bool mergeContent(Json& c, double tl, double wd, double srcLen, double worldLen, bool remapped, double cueShift, const std::string& shotName, const std::string& where,
                         Accum& a, std::vector<std::string>& camNames, Failure& e)
{
	if (c.contains("cameras"))
	{
		if (!c["cameras"].is_array()) { return e.fail(where + ".cameras", "配列でない"); }
		for (std::size_t i = 0; i < c["cameras"].size(); ++i)
		{
			Json cam = c["cameras"][i];
			const std::string w = where + ".cameras[" + std::to_string(i) + "]";
			if (!cam.is_object()) { return e.fail(w, "オブジェクトでない"); }
			if (cam.contains("path")) { return e.fail(w + ".path", "ショットの中のカメラは keys を直に書く (path は時刻をずらせない)"); }
			cam["name"] = cameraName(cam, shotName, i);
			cam["start"] = (cam.contains("start") ? cam["start"].get<double>() : 0.0) + wd;
			double camEnd = cam.contains("end") ? cam["end"].get<double>() : srcLen;
			if (remapped) { camEnd = std::min(camEnd, worldLen); }   // 遅くした区間の後ろまで、このショットのカメラがワールドの時間に居残らない
			cam["end"] = camEnd + wd;
			shiftTimes(cam["keys"], "t", wd);
			if (cam.contains("lens")) { shiftTimes(cam["lens"], "t", wd); }
			for (const Json& other : a.cameras)
			{
				if (other["name"] == cam["name"]) { return e.fail(w + ".name", "カメラの名前が重なっている: " + cam["name"].get<std::string>()); }
			}
			camNames.push_back(cam["name"].get<std::string>());
			a.cameras.push_back(cam);
		}
	}
	for (const char* group : {"props", "lights"})
	{
		if (!c.contains(group)) { continue; }
		Json& dst = std::string(group) == "props" ? a.props : a.lights;
		for (Json o : c[group])
		{
			if (o.contains("keys")) { shiftTimes(o["keys"], "t", wd); }
			if (o.contains("clips"))
			{
				for (Json& clip : o["clips"])
				{
					if (!clip.is_object()) { continue; }
					clip["start"] = (clip.contains("start") ? clip["start"].get<double>() : 0.0) + wd;
					if (clip.contains("end")) { clip["end"] = clip["end"].get<double>() + wd; }
				}
			}
			dst.push_back(o);
		}
	}
	for (Json o : c.value("emitters", Json::array()))
	{
		o["start"] = (o.contains("start") ? o["start"].get<double>() : 0.0) + wd;
		if (o.contains("end")) { o["end"] = o["end"].get<double>() + wd; }
		a.emitters.push_back(o);
	}
	for (Json o : c.value("cues", Json::array()))
	{
		if (!o.is_object() || !isNumber(o, "t")) { return e.fail(where + ".cues", "t が無いキューがある"); }
		const double t = o["t"].get<double>() + tl + cueShift;
		if (t < -1.0e-9) { return e.fail(where + ".cues", "キューが映像の始まりより前になる (j_cut が大きすぎる: " + std::to_string(t) + " 秒)"); }
		o["t"] = std::max(0.0, t);
		a.cues.push_back(o);
	}
	for (Json o : c.value("subtitles", Json::array()))
	{
		if (o.contains("start")) { o["start"] = o["start"].get<double>() + tl; }
		if (o.contains("end")) { o["end"] = o["end"].get<double>() + tl; }
		a.subtitles.push_back(o);
	}
	for (Json o : c.value("titles", Json::array()))
	{
		if (o.contains("start")) { o["start"] = o["start"].get<double>() + tl; }
		if (o.contains("end")) { o["end"] = o["end"].get<double>() + tl; }
		a.titles.push_back(o);
	}
	if (c.contains("letterbox"))
	{
		Json lb = c["letterbox"];
		shiftPairs(lb, tl);
		for (const Json& p : lb) { a.letterbox.push_back(p); }
	}
	return true;
}

inline bool validateSegments(std::vector<Seg>& segs, const std::string& where, Failure& e)
{
	std::stable_sort(segs.begin(), segs.end(), [](const Seg& x, const Seg& y) { return x.start < y.start; });
	for (std::size_t i = 0; i < segs.size(); ++i)
	{
		if (!(segs[i].end > segs[i].start) || !(segs[i].speed > 0.0)) { return e.fail(where, "start < end で speed > 0 にする (時間の巻き戻しは断る)"); }
		if (i > 0 && segs[i].start < segs[i - 1].end - 1.0e-9) { return e.fail(where, "区間が重なっている"); }
	}
	return true;
}

/// @brief 入れ子のファイルから展開した edit のフィールドを、tl と wd だけずらして足す。
inline bool mergeNested(Json& content, double tl, double wd, std::vector<Seg>& remapAll, const std::string& where, Accum& a, Failure& e)
{
	if (!content.contains("edit")) { return true; }
	Json& ed = content["edit"];
	for (Json es : ed.value("shots", Json::array()))
	{
		const std::string name = es.value("name", std::string());
		if (findNamed(a, name)) { return e.fail(where, "ショットの名前が重なっている: " + name); }
		es["start"] = es["start"].get<double>() + tl;
		es["end"] = es["end"].get<double>() + tl;
		es["world_start"] = es.value("world_start", 0.0) + wd;
		if (es.contains("fade")) { shiftPairs(es["fade"], tl); }
		if (es.contains("time_remap"))
		{
			for (Json& s : es["time_remap"]) { s["start"] = s["start"].get<double>() + tl; s["end"] = s["end"].get<double>() + tl; }
		}
		a.named.push_back({name, {es["start"].get<double>(), es["end"].get<double>()}});
		a.editShots.push_back(es);
	}
	for (const Json& s : ed.value("time_remap", Json::array()))
	{
		remapAll.push_back({s["start"].get<double>() + tl, s["end"].get<double>() + tl, s["speed"].get<double>()});
	}
	return true;
}

inline bool expandShots(Context& ctx, Json& doc, const std::string& baseDir, Failure& e)
{
	if (!doc["shots"].is_array() || doc["shots"].empty()) { return e.fail("shots", "ショットの配列 (1 つ以上) でない"); }
	if (doc.contains("edit")) { return e.fail("edit", "shots と同時には書けない (edit は展開した結果のフィールド)"); }
	Accum a;
	Json* slots[] = {&a.cameras, &a.props, &a.lights, &a.emitters, &a.cues, &a.subtitles, &a.titles, &a.letterbox};
	const char* names[] = {"cameras", "props", "lights", "emitters", "cues", "subtitles", "titles", "letterbox"};
	for (std::size_t k = 0; k < 8; ++k) { if (doc.contains(names[k]) && doc[names[k]].is_array()) { *slots[k] = doc[names[k]]; } }
	std::vector<Seg> remapAll;
	const Json shots = doc["shots"];
	for (std::size_t i = 0; i < shots.size(); ++i)
	{
		const Json& entry = shots[i];
		const std::string where = "shots[" + std::to_string(i) + "]";
		if (!entry.is_object()) { return e.fail(where, "オブジェクトでない"); }
		Json content = Json::object();
		std::string source;
		if (entry.contains("include"))
		{
			if (!entry["include"].is_string()) { return e.fail(where + ".include", "ファイルのパス (文字列) でない"); }
			if (!allowed(entry, {"include", "name", "start", "offset", "duration", "look", "lens", "fade", "transition", "j_cut", "l_cut", "time_remap"},
			             where, e, kEntryKeys)) { return false; }
			source = entry["include"].get<std::string>();
			if (!loadIncluded(ctx, source, baseDir, where, content, e)) { return false; }
		}
		else
		{
			content = entry;   // include が無ければ、この項目そのものがショットの中身
			if (!allowed(entry, {"name", "start", "offset", "duration", "look", "lens", "fade", "transition", "j_cut", "l_cut", "time_remap", "film",
			                     "cameras", "props", "lights", "emitters", "cues", "subtitles", "titles", "letterbox"}, where, e, kEntryKeys)) { return false; }
		}
		const auto pick = [&](const char* key) -> const Json* {
			if (entry.contains(key)) { return &entry[key]; }
			if (content.contains(key)) { return &content[key]; }
			return nullptr;
		};
		const bool nested = content.contains("edit");
		std::string name = "shot" + std::to_string(i + 1);
		if (const Json* n = pick("name")) { if (!n->is_string()) { return e.fail(where + ".name", "文字列でない"); } name = n->get<std::string>(); }
		if (!nested && findNamed(a, name)) { return e.fail(where + ".name", "ショットの名前が重なっている: " + name); }
		const Json* dur = pick("duration");
		if (dur == nullptr || !dur->is_number() || !(dur->get<double>() > 0.0)) { return e.fail(where, "duration (ショットの長さ。秒) が要る"); }
		const double srcLen = dur->get<double>();
		if (nested)
		{
			for (const char* k : {"look", "lens", "fade", "transition", "time_remap", "j_cut", "l_cut"})
			{
				if (entry.contains(k) || content.contains(k)) { return e.fail(where + "." + k, "入れ子のショット (shots を持つファイル) には重ねられない。中のショットに書く"); }
			}
		}
		std::vector<Seg> local;
		double outLen = srcLen, consumed = srcLen;
		if (const Json* tr = pick("time_remap")) { if (!readTimeRemap(*tr, where + ".time_remap", srcLen, local, outLen, consumed, e)) { return false; } }
		Json trans;
		if (entry.contains("transition"))
		{
			if (i == 0) { return e.fail(where + ".transition", "最初のショットには置けない (前のショットが無い)"); }
			if (!readTransition(entry["transition"], where + ".transition", trans, e)) { return false; }
			if (trans["duration"].get<double>() > outLen + 1.0e-9) { return e.fail(where + ".transition.duration", "ショットの長さ (" + std::to_string(outLen) + " 秒) より長い"); }
		}
		const double overlap = trans.is_object() ? trans["duration"].get<double>() : 0.0;
		double start = 0.0;
		if (!resolveStart(entry, a, overlap, where, start, e)) { return false; }
		if (trans.is_object() && start < a.prevStart - 1.0e-9) { return e.fail(where + ".start", "前のショットの始まりより前から重ねられない"); }
		if (trans.is_object() && start < a.prevWindowEnd - 1.0e-9)
		{
			return e.fail(where + ".transition", "前のトランジションのウィンドウ (" + std::to_string(a.prevWindowEnd) + " 秒まで) と重なる");
		}
		if (!trans.is_object() && i > 0 && start < a.prevEnd - 1.0e-9)
		{
			return e.fail(where + ".start", "前のショットの終わり (" + std::to_string(a.prevEnd) + " 秒) より前に始まる。重ねるなら transition を付ける");
		}
		double cueShift = 0.0;
		for (const char* k : {"j_cut", "l_cut"})
		{
			if (!entry.contains(k)) { continue; }
			if (!entry[k].is_number() || entry[k].get<double>() < 0.0) { return e.fail(where + "." + k, "0 以上の秒数"); }
			cueShift += (std::string(k) == "l_cut" ? 1.0 : -1.0) * entry[k].get<double>();
		}
		const double world = worldAt(remapAll, start);
		if (const Json* lk = pick("look")) { if (!checkLookNames(*lk, where + ".look", e)) { return false; } }
		if (const Json* fd = pick("fade"))
		{
			if (!fd->is_array()) { return e.fail(where + ".fade", "[[時刻, 量], ...] でない"); }
			for (const Json& p : *fd) { if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) { return e.fail(where + ".fade", "[時刻, 量] の並びでない"); } }
		}
		if (const Json* ln = pick("lens"))
		{
			if (!content.contains("cameras")) { return e.fail(where + ".lens", "このショットにカメラが無い"); }
			Json cams = content["cameras"];
			for (std::size_t c = 0; c < cams.size(); ++c) { if (!overrideLens(cams[c], *ln, where + ".lens", e)) { return false; } }
			content["cameras"] = cams;
		}
		std::vector<std::string> camNames;
		if (!mergeContent(content, start, world, srcLen, consumed, !local.empty(), cueShift, name, where, a, camNames, e)) { return false; }
		if (nested)
		{
			if (!mergeNested(content, start, world, remapAll, where, a, e)) { return false; }
		}
		else
		{
			Json es = Json::object();
			es["name"] = name;
			if (!source.empty()) { es["source"] = source; }
			es["start"] = start;
			es["end"] = start + outLen;
			es["world_start"] = world;
			es["cue_shift"] = cueShift;
			es["cameras"] = camNames;
			if (const Json* lk = pick("look")) { es["look"] = *lk; }
			if (const Json* fd = pick("fade")) { Json f = *fd; shiftPairs(f, start); es["fade"] = f; }
			if (trans.is_object()) { es["transition"] = trans; }
			if (!local.empty())   // どのショットの速度変更かを、書き出しが引けるように残す
			{
				Json own = Json::array();
				for (const Seg& s : local) { own.push_back(Json{{"start", start + s.start}, {"end", start + s.end}, {"speed", s.speed}}); }
				es["time_remap"] = own;
			}
			a.editShots.push_back(es);
			a.named.push_back({name, {start, start + outLen}});
			for (const Seg& s : local) { remapAll.push_back({start + s.start, start + s.end, s.speed}); }
		}
		a.prevStart = start;
		a.prevEnd = start + outLen;
		if (trans.is_object()) { a.prevWindowEnd = start + overlap; }
		a.maxEnd = std::max(a.maxEnd, a.prevEnd);
	}
	if (doc.contains("time_remap"))
	{
		if (!doc["time_remap"].is_array()) { return e.fail("time_remap", "{start, end, speed} の配列でない"); }
		for (const Json& s : doc["time_remap"])
		{
			if (!s.is_object() || !isNumber(s, "start") || !isNumber(s, "end") || !isNumber(s, "speed")) { return e.fail("time_remap", "start, end, speed が要る"); }
			remapAll.push_back({s["start"].get<double>(), s["end"].get<double>(), s["speed"].get<double>()});
		}
		doc.erase("time_remap");
	}
	if (!validateSegments(remapAll, "time_remap", e)) { return false; }
	for (std::size_t k = 0; k < 8; ++k) { if (!slots[k]->empty()) { doc[names[k]] = *slots[k]; } }
	if (doc.contains("letterbox"))
	{
		std::vector<Json> lb(doc["letterbox"].begin(), doc["letterbox"].end());
		std::stable_sort(lb.begin(), lb.end(), [](const Json& x, const Json& y) { return x[0].get<double>() < y[0].get<double>(); });
		doc["letterbox"] = lb;
	}
	Json maps = Json::array();
	for (const Seg& s : remapAll) { maps.push_back(Json{{"start", s.start}, {"end", s.end}, {"speed", s.speed}}); }
	doc["edit"] = Json{{"shots", a.editShots}, {"time_remap", maps}};
	if (!doc.contains("duration")) { doc["duration"] = a.maxEnd; }
	else if (!isNumber(doc, "duration") || doc["duration"].get<double>() < a.maxEnd - 1.0e-9)
	{
		return e.fail("duration", "ショットの終わり (" + std::to_string(a.maxEnd) + " 秒) より短い。書かなければショットの終わりまでになる");
	}
	doc.erase("shots");
	return true;
}

}  // namespace edit_detail

/// @brief json に shots があれば、展開した JSON を out に返す。なければ out を空のままにして成功する。失敗したら、場所を付けた文を error に返す。
[[nodiscard]] inline bool flattenIfShots(std::string_view json, std::string_view baseDir, const FileReader& read, std::string& out, std::string& error)
{
	using edit_detail::Json;
	out.clear();
	Json doc = Json::parse(json.begin(), json.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object() || !doc.contains("shots")) { return true; }
	edit_detail::Context ctx{read, {}};
	edit_detail::Failure fail;
	if (!edit_detail::expandShots(ctx, doc, std::string(baseDir), fail)) { error = fail.text; return false; }
	out = doc.dump();
	return true;
}

}  // namespace mitiru::film
