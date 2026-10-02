// mitiru_navbake: レベルのメッシュ (.obj /.gltf /.glb) からナビメッシュ (.navmesh) を生成するコンソールツール。
// 引数の一覧は usage() が表示する。生成した .navmesh はゲーム DLL が mitiru/nav/NavMesh.hpp の loadFile で読み込む。
// ビルド工程の一つとして add_custom_command から呼び出す想定で、成功時は 0、引数に誤りがある場合は 2、生成できなければ 1 を返す。

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <mitiru/nav/NavLevelBake.hpp>
#include <mitiru/platform/Utf8Args.hpp>

namespace
{

struct Args
{
	std::filesystem::path level;
	std::filesystem::path out;
	float scale = 1.0f;
	mitiru::nav::NavBakeSettings settings;
};

void usage()
{
	std::fprintf(stderr,
		"usage: mitiru_navbake <level.obj|.gltf|.glb> -o <out.navmesh> [--scale s]\n"
		"         [--radius m] [--height m] [--climb m] [--slope deg] [--cell m] [--cell-height m] [--tile cells]\n");
}

/// @return 数値を取る option なら書き込み先を、未知の option なら nullptr を返す
float* floatOption(Args& a, const std::string& name)
{
	auto& s = a.settings;
	if (name == "--scale") return &a.scale;
	if (name == "--radius") return &s.agentRadius;
	if (name == "--height") return &s.agentHeight;
	if (name == "--climb") return &s.agentMaxClimb;
	if (name == "--slope") return &s.agentMaxSlopeDeg;
	if (name == "--cell") return &s.cellSize;
	if (name == "--cell-height") return &s.cellHeight;
	return nullptr;
}

bool parse(const std::vector<std::string>& argv, Args& a)
{
	for (std::size_t i = 1; i < argv.size(); ++i)
	{
		const std::string& k = argv[i];
		const bool hasValue = i + 1 < argv.size();
		if (k == "-o" && hasValue) { a.out = std::filesystem::u8path(argv[++i]); continue; }
		if (k == "--tile" && hasValue) { a.settings.tileSizeCells = std::atoi(argv[++i].c_str()); continue; }
		if (float* f = floatOption(a, k); f != nullptr && hasValue) { *f = std::strtof(argv[++i].c_str(), nullptr); continue; }
		if (!k.empty() && k[0] == '-') { std::fprintf(stderr, "知らない option: %s\n", k.c_str()); return false; }
		if (!a.level.empty()) { std::fprintf(stderr, "レベルは 1 つだけ: %s\n", k.c_str()); return false; }
		a.level = std::filesystem::u8path(k);
	}
	return !a.level.empty() && !a.out.empty();
}

} // namespace

int main(int argc, char** argv)
{
	std::vector<std::string> args = mitiru::platform::commandLineUtf8Args();
	if (args.empty()) args.assign(argv, argv + argc);
	Args a;
	if (!parse(args, a))
	{
		usage();
		return 2;
	}
	const auto r = mitiru::nav::bakeNavMeshFromFile(a.level, a.settings, a.scale);
	if (r.blob.empty())
	{
		std::fprintf(stderr, "mitiru_navbake: %s\n", r.error.c_str());
		return 1;
	}
	if (!mitiru::nav::writeNavBlob(a.out, r.blob))
	{
		std::fprintf(stderr, "mitiru_navbake: 書けない: %s\n", a.out.string().c_str());
		return 1;
	}
	std::printf("mitiru_navbake: %s (%d tiles, %d polys, %zu bytes)\n", a.out.filename().string().c_str(), r.tileCount,
		r.polyCount, r.blob.size());
	return 0;
}
