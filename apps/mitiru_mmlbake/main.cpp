// mitiru_mmlbake: assets/audio/*.mml を同じ名前の .wav へ焼く。hud.play("名前") で鳴らせる。
//
// mitiru_mmlbake <file.mml | フォルダ>... [--force] [--check]
//
// フォルダは中の .mml を全部焼く。MML が変わっていない WAV は書き直さない。
// --force  ハッシュが同じ WAV も、手で置いた WAV (このツールが焼いたものでない) も焼き直す
// --check  書かずに、焼き直しが要る WAV があるかだけを調べる (ビルドの検査用)
//
// 終了コード: 0 = 全部が最新 / 1 = --check で古い WAV があった / 2 = 引数の誤り / 3 = 焼けないものがあった

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <mitiru/audio/mml/MmlBake.hpp>

namespace fs = std::filesystem;
namespace bake = mitiru::audio::mml;

namespace
{

struct Args
{
	std::vector<fs::path> inputs;
	bool                  force = false;
	bool                  check = false;
};

bool parseArgs(int argc, char** argv, Args& a, std::string& err)
{
	for (int i = 1; i < argc; ++i)
	{
		const std::string k = argv[i];
		if (k == "--force") { a.force = true; }
		else if (k == "--check") { a.check = true; }
		else if (k.rfind("--", 0) == 0) { err = k + " という引数はありません。"; return false; }
		else { a.inputs.emplace_back(k); }
	}
	if (a.inputs.empty())
	{
		err = "使い方: mitiru_mmlbake <file.mml | フォルダ>... [--force] [--check]";
		return false;
	}
	return true;
}

bool collect(const fs::path& in, std::vector<fs::path>& out, std::string& err)
{
	std::error_code ec;
	if (fs::is_directory(in, ec))
	{
		for (const auto& e : fs::recursive_directory_iterator(in, ec))
		{
			if (e.is_regular_file() && e.path().extension() == ".mml") { out.push_back(e.path()); }
		}
		return true;
	}
	if (!fs::is_regular_file(in, ec)) { err = in.string() + " が見つかりません。"; return false; }
	out.push_back(in);
	return true;
}

}  // namespace

int main(int argc, char** argv)
{
	Args a;
	std::string err;
	if (!parseArgs(argc, argv, a, err)) { std::fprintf(stderr, "mitiru_mmlbake: %s\n", err.c_str()); return 2; }
	std::vector<fs::path> files;
	for (const auto& in : a.inputs)
	{
		if (!collect(in, files, err)) { std::fprintf(stderr, "mitiru_mmlbake: %s\n", err.c_str()); return 2; }
	}
	std::sort(files.begin(), files.end());
	int stale = 0;
	int broken = 0;
	for (const auto& f : files)
	{
		const auto r = bake::bakeFile(f, a.force, a.check);
		const std::string path = f.generic_string();
		switch (r.status)
		{
		case bake::BakeStatus::Baked:
			std::printf("焼きました %s (%.2f 秒の音、%.2f 秒かかりました)\n", r.wav.generic_string().c_str(), r.seconds, r.elapsedSec);
			break;
		case bake::BakeStatus::UpToDate:
			std::printf("最新です   %s\n", r.wav.generic_string().c_str());
			break;
		case bake::BakeStatus::Stale:
			std::printf("古いです   %s\n", r.wav.generic_string().c_str());
			++stale;
			break;
		case bake::BakeStatus::Refused:
		case bake::BakeStatus::Failed:
			std::fprintf(stderr, "焼けません   %s: %s\n", path.c_str(), r.message.c_str());
			++broken;
			break;
		}
	}
	if (files.empty()) { std::printf(".mml が 1 つも見つかりませんでした\n"); }
	if (broken > 0) { return 3; }
	return (a.check && stale > 0) ? 1 : 0;
}
