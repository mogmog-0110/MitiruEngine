#pragma once

/// @file SequenceLoad.hpp
/// @brief カットシーンの JSON を Sequence に読む。間違いは「どこの何が」を付けて 1 つ返す。

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::narrative
{

namespace seq_detail
{

using Json = nlohmann::json;

/// @brief 読み込み中の文脈。最初の間違いだけを覚える
struct Reader
{
	std::string error;

	bool fail(const std::string& where, const std::string& what)
	{
		if (error.empty()) { error = where + ": " + what; }
		return false;
	}

	float number(const Json& j, const char* key, float fallback, const std::string& where)
	{
		if (!j.contains(key)) { return fallback; }
		if (!j[key].is_number()) { fail(where + "." + key, "数でない"); return fallback; }
		return j[key].get<float>();
	}

	float required(const Json& j, const char* key, const std::string& where)
	{
		if (!j.contains(key)) { fail(where, std::string(key) + " が無い"); return 0.0f; }
		return number(j, key, 0.0f, where);
	}

	std::string text(const Json& j, const char* key, const std::string& where)
	{
		if (!j.contains(key)) { return {}; }
		if (!j[key].is_string()) { fail(where + "." + key, "文字列でない"); return {}; }
		return j[key].get<std::string>();
	}

	Vec3 vec3(const Json& j, const char* key, Vec3 fallback, const std::string& where)
	{
		if (!j.contains(key)) { return fallback; }
		const Json& a = j[key];
		if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number())
		{
			fail(where + "." + key, "[x, y, z] でない");
			return fallback;
		}
		return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
	}

	bool smooth(const Json& j, const std::string& where)
	{
		const std::string mode = text(j, "interp", where);
		if (mode.empty() || mode == "smooth") { return true; }
		if (mode != "linear") { fail(where + ".interp", "smooth か linear"); }
		return false;
	}

	/// @brief 鍵の時刻が昇順か
	template <class Key>
	bool ascending(const std::vector<Key>& keys, const std::string& where)
	{
		for (std::size_t i = 1; i < keys.size(); ++i)
		{
			if (keys[i].t < keys[i - 1].t) { return fail(where + "[" + std::to_string(i) + "]", "t が前の鍵より小さい"); }
		}
		return true;
	}
};

[[nodiscard]] inline std::string at(const std::string& where, std::size_t i)
{
	return where + "[" + std::to_string(i) + "]";
}

inline void readCameraKeys(Reader& r, const Json& arr, std::vector<CameraKey>& out, const std::string& where)
{
	if (!arr.is_array()) { r.fail(where, "配列でない"); return; }
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const std::string w = at(where, i);
		CameraKey k;
		k.t = r.required(arr[i], "t", w);
		k.eye = r.vec3(arr[i], "eye", k.eye, w);
		k.target = r.vec3(arr[i], "target", k.target, w);
		k.fovDeg = r.number(arr[i], "fov", k.fovDeg, w);
		k.rollDeg = r.number(arr[i], "roll", k.rollDeg, w);
		out.push_back(k);
	}
	r.ascending(out, where);
}

/// @brief "path" に書いたカメラの軌跡のファイル (Blender の書き出し) を読む。path は台本のファイルからの相対
inline void readCameraPathFile(Reader& r, const std::string& file, std::string_view baseDir, std::vector<CameraKey>& out,
                               const std::string& where)
{
	const std::string full = baseDir.empty() ? file : (std::filesystem::path(baseDir) / file).generic_string();
	const auto bytes = vfs::readAsset(full);
	if (!bytes) { r.fail(where, "カメラの軌跡を読めない: " + full); return; }
	const Json j = Json::parse(bytes->begin(), bytes->end(), nullptr, false);
	if (j.is_discarded() || !j.is_object() || !j.contains("keys")) { r.fail(full, "{\"keys\": [...]} の JSON でない"); return; }
	readCameraKeys(r, j["keys"], out, full + ".keys");
}

inline void readCameras(Reader& r, const Json& arr, std::string_view baseDir, Sequence& seq)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("cameras", i);
		SequenceCamera c;
		c.name = r.text(j, "name", w);
		c.priority = static_cast<int>(r.number(j, "priority", 0.0f, w));
		c.start = r.number(j, "start", 0.0f, w);
		c.end = r.number(j, "end", seq.duration, w);
		c.blendIn = r.number(j, "blendIn", 0.0f, w);
		c.nearDist = r.number(j, "near", c.nearDist, w);
		c.farDist = r.number(j, "far", c.farDist, w);
		c.smooth = r.smooth(j, w);
		if (j.contains("keys")) { readCameraKeys(r, j["keys"], c.keys, w + ".keys"); }
		else if (j.contains("path")) { readCameraPathFile(r, r.text(j, "path", w), baseDir, c.keys, w + ".path"); }
		if (r.error.empty() && c.keys.empty()) { r.fail(w, "keys か path で鍵を 1 つ以上書く"); }
		if (r.error.empty() && !(c.start < c.end)) { r.fail(w, "start < end にする"); }
		seq.cameras.push_back(std::move(c));
	}
}

inline void readActors(Reader& r, const Json& arr, Sequence& seq)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const std::string w = at("actors", i);
		SequenceActor a;
		a.name = r.text(arr[i], "name", w);
		a.id = nameHash(a.name);
		a.smooth = r.smooth(arr[i], w);
		if (a.name.empty()) { r.fail(w, "name が無い"); }
		const Json keys = arr[i].contains("keys") ? arr[i]["keys"] : Json::array();
		for (std::size_t k = 0; k < keys.size() && r.error.empty(); ++k)
		{
			ActorKey key;
			key.t = r.required(keys[k], "t", at(w + ".keys", k));
			key.position = r.vec3(keys[k], "pos", key.position, at(w + ".keys", k));
			key.yawDeg = r.number(keys[k], "yaw", 0.0f, at(w + ".keys", k));
			a.keys.push_back(key);
		}
		if (r.error.empty() && a.keys.empty()) { r.fail(w, "keys が無い"); }
		r.ascending(a.keys, w + ".keys");
		seq.actors.push_back(std::move(a));
	}
}

inline void readClips(Reader& r, const Json& arr, Sequence& seq)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("clips", i);
		SequenceClip c;
		c.actor = seq.findActor(r.text(j, "actor", w));
		c.clip = r.text(j, "clip", w);
		c.start = r.number(j, "start", 0.0f, w);
		c.end = r.number(j, "end", seq.duration, w);
		c.offset = r.number(j, "offset", 0.0f, w);
		c.speed = r.number(j, "speed", 1.0f, w);
		c.blendIn = r.number(j, "blendIn", 0.0f, w);
		c.blendOut = r.number(j, "blendOut", 0.0f, w);
		if (c.actor < 0) { r.fail(w + ".actor", "actors に無い名前"); }
		if (c.clip.empty()) { r.fail(w, "clip が無い"); }
		seq.clips.push_back(std::move(c));
	}
}

inline void readCues(Reader& r, const Json& arr, Sequence& seq)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const Json& j = arr[i];
		const std::string w = at("cues", i);
		const auto kind = parseCueKind(r.text(j, "kind", w));
		if (!kind) { r.fail(w + ".kind", "sound / music / vfx / mark / call / anim のどれか"); return; }
		SequenceCue c;
		c.t = r.required(j, "t", w);
		c.cue = makeCue(*kind, r.text(j, "name", w), r.text(j, "actor", w), r.number(j, "value", 1.0f, w),
		                r.number(j, "duration", 0.0f, w));
		if (c.t > seq.duration) { r.fail(w, "t が duration より後"); }
		seq.cues.push_back(std::move(c));
	}
	std::stable_sort(seq.cues.begin(), seq.cues.end(), [](const SequenceCue& a, const SequenceCue& b) { return a.t < b.t; });
}

inline void readSubtitles(Reader& r, const Json& arr, Sequence& seq)
{
	for (std::size_t i = 0; i < arr.size() && r.error.empty(); ++i)
	{
		const std::string w = at("subtitles", i);
		Subtitle s;
		s.start = r.required(arr[i], "start", w);
		s.end = r.required(arr[i], "end", w);
		s.speaker = r.text(arr[i], "speaker", w);
		s.source = r.text(arr[i], "text", w);
		s.text = s.source;
		s.key = r.text(arr[i], "key", w);
		if (r.error.empty() && !(s.start < s.end)) { r.fail(w, "start < end にする"); }
		seq.subtitles.push_back(std::move(s));
	}
}

inline void readFloatTrack(Reader& r, const Json& root, const char* key, std::vector<FloatKey>& out)
{
	if (!root.contains(key)) { return; }
	const Json& arr = root[key];
	if (!arr.is_array()) { r.fail(key, "[[t, 値], ...] でない"); return; }
	for (std::size_t i = 0; i < arr.size(); ++i)
	{
		const Json& p = arr[i];
		if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number())
		{
			r.fail(at(key, i), "[t, 値] でない");
			return;
		}
		out.push_back({p[0].get<float>(), p[1].get<float>()});
	}
	r.ascending(out, key);
}

[[nodiscard]] inline const Json& arrayOrEmpty(const Json& root, const char* key)
{
	static const Json kEmpty = Json::array();
	return (root.contains(key) && root[key].is_array()) ? root[key] : kEmpty;
}

}  // namespace seq_detail

/// @brief JSON の文字列から読む。name は合図や再生の照合に使う名前、baseDir はカメラの軌跡ファイルの基準
[[nodiscard]] inline std::optional<Sequence> loadSequenceFromJson(std::string_view json, std::string_view name,
                                                                std::string_view baseDir, std::string& error)
{
	using namespace seq_detail;
	const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
	if (root.is_discarded() || !root.is_object()) { error = std::string(name) + ": JSON として読めない"; return std::nullopt; }
	Reader r;
	Sequence seq;
	seq.name = std::string(name);
	seq.id = nameHash(seq.name);
	seq.duration = r.required(root, "duration", "(root)");
	seq.skippable = !root.contains("skippable") || root["skippable"] != false;
	seq.blendOut = r.number(root, "blendOut", 0.0f, "(root)");
	if (r.error.empty() && !(seq.duration > 0.0f)) { r.fail("duration", "0 より大きくする"); }
	readCameras(r, arrayOrEmpty(root, "cameras"), baseDir, seq);
	readActors(r, arrayOrEmpty(root, "actors"), seq);
	readClips(r, arrayOrEmpty(root, "clips"), seq);
	readCues(r, arrayOrEmpty(root, "cues"), seq);
	readSubtitles(r, arrayOrEmpty(root, "subtitles"), seq);
	readFloatTrack(r, root, "fade", seq.fade);
	readFloatTrack(r, root, "letterbox", seq.letterbox);
	if (!r.error.empty()) { error = std::string(name) + ": " + r.error; return std::nullopt; }
	buildCameraSegments(seq);
	error.clear();
	return seq;
}

/// @brief ファイルから読む (vfs::readAsset。pack 配布でも同じパス)。名前はファイル名から拡張子を除いたもの
[[nodiscard]] inline std::optional<Sequence> loadSequenceFile(std::string_view path, std::string& error)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes) { error = "カットシーンを読めない: " + std::string(path); return std::nullopt; }
	const std::filesystem::path p{std::string(path)};
	std::string stem = p.filename().string();
	stem = stem.substr(0, stem.find('.'));
	const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
	return loadSequenceFromJson(text, stem, p.parent_path().generic_string(), error);
}

}  // namespace mitiru::narrative
