// mitiru_musicrender: music.json の曲を、台本どおりに要求し、デバイスを開かずに WAV へ書き出す。
//
// mitiru_musicrender <assets/audio/music.json> --script <台本.txt> --frames N --out out.wav [--rate 48000]
//
// 台本は 1 行 1 要求で「<フレーム> <動詞> [引数]」。# 以降は読まない。1 フレーム = 1/60 秒。
// 0 segment explore     区間を要求する（規則の区切りまで待つ）
// 30 intensity 0.8      強さ
// 200 stinger hit       スティンガー
// 400 stop 1.5          停止（フェード秒）
// 10 gain 0.5           曲全体の音量
//
// ゲームの host と同じ MiniaudioMusicPlayer を、同じ順序（要求 → step → 1 ステップ分を読む）で動かす。
// 書き出した WAV は、ゲームの --audio-capture と同じ小節で切り替わる。
// 隣の <out>.music.jsonl に、鳴り始めた区間とスティンガーの時刻を書く（tools/audio_report.py で WAV と突き合わせる）。
//
// 終了コード: 0 = 書き出し成功 / 2 = 引数、music.json、台本の誤り / 3 = 書き出し失敗

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/OfflineMixCapture.hpp>
#include <mitiru/audio/music/MiniaudioMusicPlayer.hpp>
#include <mitiru/audio/music/MusicManifestJson.hpp>

namespace fs = std::filesystem;
using namespace mitiru::audio::music;

namespace
{

constexpr std::uint32_t kStepsPerSecond = 60;

struct Args
{
	fs::path      manifest;
	fs::path      script;
	fs::path      out;
	int           frames = 600;
	std::uint32_t rate = 48000;
};

struct Request
{
	std::string verb;
	std::string arg;
};

int fail(const std::string& msg, int code = 2)
{
	std::fprintf(stderr, "mitiru_musicrender: %s\n", msg.c_str());
	return code;
}

bool parseArgs(int argc, char** argv, Args& a, std::string& err)
{
	if (argc < 2) { err = "使い方: mitiru_musicrender <music.json> --script <f> --frames N --out <wav> [--rate R]"; return false; }
	a.manifest = argv[1];
	for (int i = 2; i < argc; i += 2)
	{
		const std::string k = argv[i];
		if (i + 1 >= argc) { err = k + " の値が無い"; return false; }
		const char* v = argv[i + 1];
		if (k == "--script")      { a.script = v; }
		else if (k == "--out")    { a.out = v; }
		else if (k == "--frames") { a.frames = std::atoi(v); }
		else if (k == "--rate")   { a.rate = static_cast<std::uint32_t>(std::atoi(v)); }
		else { err = "知らない引数: " + k; return false; }
	}
	if (a.out.empty() || a.frames <= 0 || a.rate < 8000) { err = "--out と正の --frames、8000 以上の --rate が要る"; return false; }
	return true;
}

std::string readText(const fs::path& p)
{
	std::ifstream f(p, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

std::multimap<int, Request> parseScript(const fs::path& p, std::string& err)
{
	std::multimap<int, Request> out;
	if (p.empty()) { return out; }
	if (!fs::is_regular_file(p)) { err = "台本が読めない: " + p.string(); return {}; }
	std::istringstream in(readText(p));
	std::string line;
	for (int no = 1; std::getline(in, line); ++no)
	{
		line = line.substr(0, line.find('#'));
		std::istringstream ls(line);
		int frame = 0;
		Request r;
		if (!(ls >> frame)) { continue; }
		if (!(ls >> r.verb)) { err = "台本 " + std::to_string(no) + " 行目: 動詞が無い"; return {}; }
		ls >> r.arg;
		out.emplace(frame, r);
	}
	return out;
}

bool apply(const Request& r, const MusicManifest& m, MusicDirector& d, std::string& err)
{
	const float v = r.arg.empty() ? 0.0f : std::strtof(r.arg.c_str(), nullptr);
	if (r.verb == "segment" && m.segmentIndex(r.arg) >= 0) { d.request(m.segmentIndex(r.arg)); }
	else if (r.verb == "stinger" && m.stingerIndex(r.arg) >= 0) { d.playStinger(m.stingerIndex(r.arg)); }
	else if (r.verb == "intensity") { d.setIntensity(v); }
	else if (r.verb == "gain")      { d.setMasterGain(v, 0.02f); }
	else if (r.verb == "stop")      { d.stop(v); }
	else { err = "台本の要求が読めない: " + r.verb + " " + r.arg; return false; }
	return true;
}

std::vector<std::uint8_t> readAudio(const fs::path& dir, std::string_view file)
{
	for (const char* ext : {".wav", ".ogg", ".mp3", ".flac"})
	{
		const fs::path p = dir / (std::string(file) + ext);
		if (!fs::exists(p)) { continue; }
		const std::string s = readText(p);
		return std::vector<std::uint8_t>(s.begin(), s.end());
	}
	return {};
}

void logCommand(std::ofstream& log, const MusicManifest& m, const MusicCommand& c, std::uint32_t rate)
{
	if (c.kind != MusicCommandKind::StartInstance) { return; }
	const bool seg = c.segment >= 0;
	const std::string& id = seg ? m.segments[static_cast<std::size_t>(c.segment)].id
	                            : m.stingers[static_cast<std::size_t>(c.stinger)].id;
	const double sec = static_cast<double>(c.at) / rate;
	log << "{\"kind\":\"" << (seg ? "segment" : "stinger") << "\",\"id\":\"" << id << "\",\"sample\":" << c.at
	    << ",\"timeSec\":" << sec << ",\"frame\":" << sec * kStepsPerSecond << "}\n";
}

}  // namespace

int main(int argc, char** argv)
{
	Args a;
	std::string err;
	if (!parseArgs(argc, argv, a, err)) { return fail(err); }
	if (!fs::is_regular_file(a.manifest)) { return fail("music.json が読めない: " + a.manifest.string()); }
	auto parsed = parseMusicManifest(readText(a.manifest));
	if (!parsed.ok()) { return fail(a.manifest.string() + ": " + parsed.error); }
	const auto script = parseScript(a.script, err);
	if (!err.empty()) { return fail(err); }
	const auto manifest = std::make_shared<const MusicManifest>(std::move(parsed.manifest));

	mitiru::audio::MiniaudioEngine engine(mitiru::audio::MiniaudioEngine::Offline{a.rate, 2});
	MiniaudioMusicPlayer player;
	const fs::path dir = a.manifest.parent_path();
	err = player.load(*engine.rawEngine(), manifest, [&](std::string_view f) { return readAudio(dir, f); },
	                  static_cast<std::int64_t>((a.rate + kStepsPerSecond - 1) / kStepsPerSecond));
	if (!err.empty()) { return fail(err); }

	mitiru::audio::OfflineMixCapture capture([&](float* o, ma_uint64 n) { return engine.renderOffline(o, n); },
	                                         a.rate, 2, kStepsPerSecond);
	if (!capture.open(a.out.string())) { return fail("書き出せない: " + a.out.string(), 3); }
	std::ofstream log(fs::path(a.out).replace_extension(".music.jsonl"), std::ios::trunc);
	player.setCommandLog([&](const MusicCommand& c) { logCommand(log, *manifest, c, a.rate); });

	for (int frame = 0; frame < a.frames; ++frame)
	{
		const auto [lo, hi] = script.equal_range(frame);
		for (auto it = lo; it != hi; ++it)
		{
			if (!apply(it->second, *manifest, player.director(), err)) { return fail(err); }
		}
		player.step();
		capture.step();
	}
	capture.close();
	std::fprintf(stderr, "[mitiru_musicrender] %d frames (%.2f s) -> %s\n", a.frames,
	             static_cast<double>(a.frames) / kStepsPerSecond, a.out.string().c_str());
	return 0;
}
