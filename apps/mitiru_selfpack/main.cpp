// mitiru_selfpack: 配布フォルダを mitiru_selfrun へ連結して 1 ファイルにする梱包ツール。
//
//   mitiru_selfpack <出力.exe> <selfrun.exe> <配布フォルダ> <name> <exe相対> [args] [cwd相対]
//
// 使い方は「mitiru dist の出力フォルダを 1 ファイル化する」だけ (引数の実例は
// docs 側に置く)。フォルダの中身を再帰で全部パックへ入れ、mitiru_boot.txt を足し、
// selfrun の複製へ連結する。scramble は掛けない。この器は秘匿ではなく同梱のための
// もので、中の assets.mtpak が秘匿を担う。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
	if (argc < 6)
	{
		std::fprintf(stderr,
		             "usage: mitiru_selfpack <out.exe> <selfrun.exe> <distDir> <name> <exeRel> [args] [cwdRel]\n");
		return 2;
	}
	const fs::path out     = argv[1];
	const fs::path selfrun = argv[2];
	const fs::path dir     = argv[3];
	const std::string name = argv[4];
	const std::string exe  = argv[5];
	const std::string args = argc > 6 ? argv[6] : "";
	const std::string cwd  = argc > 7 ? argv[7] : "";

	std::error_code ec;
	if (!fs::is_directory(dir, ec))
	{
		std::fprintf(stderr, "配布フォルダがありません: %s\n", dir.string().c_str());
		return 1;
	}

	std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
	for (const auto& it : fs::recursive_directory_iterator(dir))
	{
		if (!it.is_regular_file()) { continue; }
		const auto rel = fs::relative(it.path(), dir, ec).generic_string();
		std::ifstream f(it.path(), std::ios::binary);
		std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
		                          std::istreambuf_iterator<char>());
		entries.emplace_back(rel, std::move(data));
	}

	std::string boot = "name=" + name + "\nexe=" + exe + "\n";
	if (!args.empty()) { boot += "args=" + args + "\n"; }
	if (!cwd.empty())  { boot += "cwd=" + cwd + "\n"; }
	entries.emplace_back("mitiru_boot.txt", std::vector<uint8_t>(boot.begin(), boot.end()));

	const fs::path tmp = out.string() + ".mtpak.tmp";
	if (!mitiru::vfs::AssetPack::write(tmp, entries, /*scramble=*/false))
	{
		std::fprintf(stderr, "パックの書き出しに失敗しました\n");
		return 1;
	}
	fs::copy_file(selfrun, out, fs::copy_options::overwrite_existing, ec);
	if (ec)
	{
		std::fprintf(stderr, "selfrun の複製に失敗しました: %s\n", ec.message().c_str());
		return 1;
	}
	const bool ok = mitiru::vfs::AssetPack::appendTo(out, tmp);
	fs::remove(tmp, ec);
	if (!ok)
	{
		std::fprintf(stderr, "連結に失敗しました\n");
		return 1;
	}
	std::printf("%s  (%llu ファイル)\n", out.string().c_str(),
	            static_cast<unsigned long long>(entries.size()));
	return 0;
}
