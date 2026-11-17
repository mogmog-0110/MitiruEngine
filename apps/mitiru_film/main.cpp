// mitiru_film: 映像の JSON（docs/FILM.md）を、描かずに読み、直す。
//
// mitiru_film check <film.json>                           検査し、長さ・カット・合図・小道具の要約を出す
// mitiru_film patch <film.json> <patch.json> --out <file> JSON Patch（RFC 6902）を適用し、読み込めることを確かめてから書く
// mitiru_film schema                                      使える項目の一覧（ルックの項目名と種類）
// mitiru_film flatten <film.json> [--out <file>]          shots の include を展開した 1 本の JSON を書く（--out が無ければ標準出力）
// mitiru_film export-blender <film.json> --out <dir> [--t <秒>] [--assets <dir>]...  その時刻の場面と glb を書き出し、Cycles で撮るスクリプトを置く (ADR 0154)
//
// 終了コード: 0 = 成功 / 2 = 引数の誤り / 3 = JSON の誤り（場所を添えて 1 行出す）

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/film/FilmLoad.hpp>

#include "ExportBlender.hpp"

namespace
{

constexpr int kExitUsage = 2;
constexpr int kExitBad = 3;
constexpr double kFps = 60.0;

int usage(const char* why)
{
	std::fprintf(stderr, "%s\n使い方: mitiru_film check <film.json> | patch <film.json> <patch.json> --out <file> | schema | flatten <film.json> [--out <file>]\n", why);
	return kExitUsage;
}

bool readText(const std::string& path, std::string& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) { return false; }
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

const char* cueKindName(mitiru::narrative::CueKind k)
{
	using mitiru::narrative::CueKind;
	switch (k)
	{
	case CueKind::Sound: return "sound";
	case CueKind::Music: return "music";
	case CueKind::Vfx: return "vfx";
	case CueKind::Mark: return "mark";
	case CueKind::Call: return "call";
	default: return "anim";
	}
}

void printSummary(const mitiru::film::Film& f, const std::string& name)
{
	const auto& seq = f.seq;
	std::printf("OK: %s %.2f 秒 (%lld コマ @ 60 fps) %dx%d\n", name.c_str(), seq.duration,
	            static_cast<long long>(std::ceil(seq.duration * kFps)), f.width, f.height);
	std::printf("小道具 %zu、光 %zu、カメラ %zu、合図 %zu、字幕 %zu、ルックの軌跡 %zu、パーティクルの発生源 %zu\n", f.props.size(), f.lights.size(),
	            seq.cameras.size(), seq.cues.size(), seq.subtitles.size(), f.lookTracks.size(), f.emitters.size());
	std::size_t scattered = 0;
	for (const auto& sc : f.scatters) { scattered += sc.instances.size(); }
	if (!f.scatters.empty()) { std::printf("散布 %zu 種、インスタンス合計 %zu\n", f.scatters.size(), scattered); }
	for (const auto& sc : f.scatters)
	{
		std::printf("  散布 %s (%s) %zu / %d 個%s\n", sc.name.c_str(), sc.model.empty() ? sc.shape.c_str() : sc.model.c_str(), sc.instances.size(),
		            sc.count, sc.instances.size() < static_cast<std::size_t>(sc.count) ? " 避けて置けない分が減った" : "");
	}
	for (const auto& s : seq.segments)
	{
		const std::string who = s.winner >= 0 ? seq.cameras[static_cast<std::size_t>(s.winner)].name : "(カメラなし)";
		std::printf("  カット %7.2f - %7.2f  %s\n", s.start, s.end, who.c_str());
	}
	for (const auto& s : f.edit.shots)
	{
		const char* tr[] = {"", " ディゾルブ", " ワイプ", " ディップ"};
		std::printf("  ショット %7.2f - %7.2f  %s%s\n", s.start, s.end, s.name.c_str(), tr[static_cast<int>(s.transitionIn.kind)]);
	}
	for (const auto& s : f.edit.timeMap) { std::printf("  時間 %7.2f - %7.2f  %.3f 倍\n", s.start, s.end, s.speed); }
	for (const auto& t : f.edit.titles) { std::printf("  題 %7.2f - %7.2f  %s\n", t.start, t.end, t.text.c_str()); }
	for (const auto& c : seq.cues) { std::printf("  キュー  %7.2f  %s %s\n", c.t, cueKindName(c.cue.kind), c.cue.name.c_str()); }
	for (const auto& p : f.props)
	{
		std::size_t mouths = 0;
		for (const auto& m : p.morphs) { mouths += m.visemes.empty() ? 0 : 1; }
		std::printf("  小道具 %s (%s)%s", p.name.c_str(), p.model.empty() ? p.shape.c_str() : p.model.c_str(),
		            (p.posTrack.empty() && p.rotTrack.empty() && p.scaleTrack.empty()) ? "" : " 動く");
		if (!p.morphs.empty()) { std::printf(" モーフ %zu 本 (口の表 %zu)", p.morphs.size(), mouths); }
		std::printf("\n");
	}
}

int check(const std::string& path)
{
	std::string text;
	if (!readText(path, text)) { std::fprintf(stderr, "読めません: %s\n", path.c_str()); return kExitBad; }
	std::string error;
	const std::filesystem::path p{path};
	const auto film = mitiru::film::loadFilmFromJson(text, p.stem().string().substr(0, p.stem().string().find('.')),
	                                                 p.parent_path().generic_string(), error);
	if (!film) { std::fprintf(stderr, "NG: %s\n", error.c_str()); return kExitBad; }
	printSummary(*film, p.filename().string());
	return 0;
}

int flatten(const std::string& path, const std::string& outPath)
{
	std::string text;
	if (!readText(path, text)) { std::fprintf(stderr, "読めません: %s\n", path.c_str()); return kExitBad; }
	std::string flat, error;
	const std::filesystem::path p{path};
	if (!mitiru::film::flattenIfShots(text, p.parent_path().generic_string(), mitiru::film::readFileDefault, flat, error))
	{
		std::fprintf(stderr, "NG: %s\n", error.c_str());
		return kExitBad;
	}
	if (flat.empty()) { flat = text; }
	if (!mitiru::film::loadFilmFromJson(flat, p.stem().string().substr(0, p.stem().string().find('.')), p.parent_path().generic_string(), error))
	{
		std::fprintf(stderr, "NG: %s\n", error.c_str());
		return kExitBad;
	}
	const std::string pretty = nlohmann::json::parse(flat).dump(1);
	if (outPath.empty()) { std::printf("%s\n", pretty.c_str()); return 0; }
	std::ofstream out(outPath, std::ios::binary);
	out << pretty << "\n";
	out.close();
	if (!out) { std::fprintf(stderr, "書けません: %s\n", outPath.c_str()); return kExitBad; }
	return 0;
}

int patch(const std::string& filmPath, const std::string& patchPath, const std::string& outPath)
{
	std::string filmText, patchText;
	if (!readText(filmPath, filmText) || !readText(patchPath, patchText)) { std::fprintf(stderr, "読めません\n"); return kExitBad; }
	const auto doc = nlohmann::json::parse(filmText, nullptr, false);
	const auto ops = nlohmann::json::parse(patchText, nullptr, false);
	if (doc.is_discarded() || ops.is_discarded() || !ops.is_array()) { std::fprintf(stderr, "NG: JSON として読めません (patch は配列)\n"); return kExitBad; }
	nlohmann::json next;
	try { next = doc.patch(ops); }
	catch (const std::exception& e) { std::fprintf(stderr, "NG: patch を当てられません: %s\n", e.what()); return kExitBad; }
	const std::string dumped = next.dump(2);
	std::string error;
	if (!mitiru::film::loadFilmFromJson(dumped, "patched", std::filesystem::path(filmPath).parent_path().generic_string(), error)) { std::fprintf(stderr, "NG: 当てた結果が映像として読めません: %s\n", error.c_str()); return kExitBad; }
	std::ofstream out(outPath, std::ios::binary);
	if (!out) { std::fprintf(stderr, "書けません: %s\n", outPath.c_str()); return kExitBad; }
	out << dumped << "\n";
	out.close();
	if (!out) { std::fprintf(stderr, "書き込みに失敗しました: %s\n", outPath.c_str()); return kExitBad; }
	std::printf("OK: %zu 件の操作を当てて %s に書きました\n", ops.size(), outPath.c_str());
	return 0;
}

int schema()
{
	nlohmann::json look = nlohmann::json::array();
	for (const auto& f : mitiru::film::kLookFields)
	{
		const char* kind[] = {"float", "bool", "u8", "vec3"};
		look.push_back({{"name", f.name}, {"kind", kind[static_cast<int>(f.kind)]}});
	}
	// SceneLook のフィールドではなく Film のフィールドだが、look の中に書く (FilmLoad.hpp の readLook が特別に読む)
	look.push_back({{"name", "shadowPenumbraDeg"}, {"kind", "float"}, {"note", "太陽の見かけの半径 (度、0..20)。標本ごとに光の向きを振って半影を作る。--sub 8 以上で効く"}});
	const nlohmann::json doc = {
		{"film", 1},
		{"top", {"duration", "size", "clear", "sky", "environment", "hdri", "lighting_bake", "look_preset", "outdoor", "look", "look_tracks", "props", "scatter", "lights", "emitters", "cameras",
		         "cues", "subtitles", "fade", "letterbox", "blendOut", "shots", "time_remap", "titles"}},
			{"shot", {"include", "name", "start=<秒>|auto|alias://<name>[.end]", "offset", "duration", "look", "lens{focal,fstop,focus,ev,sensorHeight}", "fade", "transition{kind=dissolve|wipe|dip,duration,direction,softness}", "j_cut", "l_cut", "time_remap[start,end,speed]", "cameras", "props", "lights", "emitters", "cues", "subtitles", "titles", "letterbox"}},
			{"title", {"kind=title|lower_third", "text", "sub", "start", "end", "fade_in", "fade_out", "pos", "size", "color", "panel"}},
		{"prop", {"name", "shape|model", "pos", "rot", "scale", "color", "alpha", "clip", "clipSpeed", "clipOffset", "clips[clip,start,end,offset,speed,blendIn,blendOut,loop]", "morphs[{name,keys[t,w],interp,gain}|{visemes,start,morph,gain}]", "interp", "keys[t,pos,rot,scale,color,alpha,visible]"}},
		{"scatter", {"name", "shape|model", "area{center[x,z],radius,inner}", "count", "seed", "scale[min,max]", "scaleY[min,max]", "minDist", "lean", "color", "colorJitter", "avoid[{prop,margin}|{point[x,z],radius,margin}|{path[[x,z]],margin}]", "sway{amp,freq,wind[x,z]}"}},
		{"emitter", {"name", "start", "end", "pos", "seed", "duration", "spawnRadius", "dir", "spreadDeg", "speed[min,max]", "life[min,max]", "rate", "burst", "gravity", "drag", "size[3]", "colors[3]", "midPoint", "stretch", "lit", "blend=additive|alpha", "shape=soft|spark|hard"}},
		{"light", {"name", "kind=sun|point|spot", "pos", "dir", "color", "intensity", "range", "inner", "outer", "castShadow", "interp", "keys[t,pos,dir,color,intensity,range]"}},
		{"camera", {"name", "priority", "start", "end", "blendIn", "near", "far", "interp", "keys[t,eye,target,fov,roll]", "sensorHeight", "lens[t,focal,fstop,focus,ev,distortion]", "lookAt{prop,offset}", "focusOn{prop,offset,rack[t,prop,over,offset]}", "shake{pos,rot,freq,seed}"}},
		{"lighting_bake", {"焼いた間接光 (mitiru_lightbake の *.lighting.bin) のパス。look の indirect.flags=1 (kIndirectGi) と一緒に使う。tools/film_bake.py が作る"}},
		{"hdri", {"file=<正距円筒の .hdr/.exr。名前だけなら film_player/assets/hdri/ の下。パスなら assets からの相対か絶対>", "intensity", "yaw=<度>"}},
		{"look", look},
	};
	std::printf("%s\n", doc.dump(2).c_str());
	return 0;
}

int exportBlenderCommand(int argc, char** argv)
{
	film_export::BlenderOptions o;
	if (argc < 3) { return usage("export-blender には映像の JSON が要ります"); }
	o.film = argv[2];
	for (int i = 3; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (i + 1 >= argc) { return usage("引数の値がありません"); }
		if (a == "--out") { o.outDir = argv[++i]; }
		else if (a == "--t") { o.t = static_cast<float>(std::atof(argv[++i])); }
		else if (a == "--assets") { o.assetRoots.emplace_back(argv[++i]); }
		else { return usage("知らない引数です"); }
	}
	if (o.outDir.empty()) { return usage("--out <dir> が要ります"); }
	return film_export::exportBlender(o);
}

}  // namespace

int main(int argc, char** argv)
{
	if (argc < 2) { return usage("サブコマンドがありません"); }
	const std::string cmd = argv[1];
	if (cmd == "schema") { return schema(); }
	if (cmd == "check" && argc == 3) { return check(argv[2]); }
	if (cmd == "flatten" && argc == 3) { return flatten(argv[2], ""); }
	if (cmd == "flatten" && argc == 5 && std::string(argv[3]) == "--out") { return flatten(argv[2], argv[4]); }
	if (cmd == "patch" && argc == 6 && std::string(argv[4]) == "--out") { return patch(argv[2], argv[3], argv[5]); }
	if (cmd == "export-blender") { return exportBlenderCommand(argc, argv); }
	return usage("引数が合いません");
}
