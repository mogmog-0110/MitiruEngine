// mitiru_tool。独立ツール窓の汎用ホスト (RmlUi)。
//
// assets/<page>.rml を 1 枚だけ小さな DX12 の窓に出し、C++ は SharedSnapshot を読んで値を data model へ写す。
// 1 ツール = 1 関心事 = 1 窓。観測窓は読み取り専用で、ゲームへ届くのは rewind の巻き戻しの要求と、
// scene_view / why_view / frame_view がゲームの HTTP API に頼む問い合わせだけ。
//
// 使い方:  mitiru_tool --page perf <pid>              動作中のゲームの snapshot を pid で
//          mitiru_tool --page mixer --file <path>     snapshot の JSON を直接
//          mitiru_tool --page replay --mtrr <path>    録画を見る
//          mitiru_tool --page perf --file <path> --capture out.png   窓を出さずに撮る

#include "ToolMain.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace mitiru::tool
{

namespace
{

void printUsage()
{
	std::fputs("usage: mitiru_tool --page <name> <pid> | --file <snapshot.json> | --mtrr <file.mtrr>\n"
	           "         [--http-port N] [--window-pos X Y]\n"
	           "         [--capture <png> [--frames N] [--size WxH] [--dp R]\n"
	           "          [--capture-input \"<frame>:click|down|move|up:X,Y;<frame>:type:TEXT;<frame>:key:VK\"]]\n", stderr);
}

bool parseSize(std::string_view s, int& w, int& h)
{
	const auto x = s.find_first_of("xX");
	if (x == std::string_view::npos) { return false; }
	w = std::atoi(std::string(s.substr(0, x)).c_str());
	h = std::atoi(std::string(s.substr(x + 1)).c_str());
	return w > 0 && h > 0;
}

// "scene?tab=memory" の ? 以降はページへの問い合わせ (mitiru run --learn が game memory の tab を既定で開く口)。
void setPage(ToolOptions& o, const std::string& full)
{
	const auto q = full.find('?');
	o.page = full.substr(0, q);
	o.query = q == std::string::npos ? std::string() : full.substr(q + 1);
}

bool parseOne(CliArgs& out, int argc, char* argv[], int& i)
{
	const std::string a = argv[i];
	const bool hasNext = i + 1 < argc;
	if (a == "--page" && hasNext)            { setPage(out.options, argv[++i]); }
	else if (a == "--file" && hasNext)       { out.options.file = std::filesystem::path(argv[++i]); }
	else if (a == "--mtrr" && hasNext)       { out.options.mtrr = argv[++i]; }
	else if (a == "--http-port" && hasNext)  { out.options.httpPort = std::atoi(argv[++i]); }
	else if (a == "--capture" && hasNext)    { out.capture = std::filesystem::path(argv[++i]); }
	else if (a == "--frames" && hasNext)     { out.captureFrames = std::max(1, std::atoi(argv[++i])); }
	else if (a == "--dp" && hasNext)         { out.captureDp = static_cast<float>(std::atof(argv[++i])); }
	else if (a == "--capture-input" && hasNext) { out.captureInput = argv[++i]; }
	else if (a == "--size" && hasNext)       { return parseSize(argv[++i], out.captureWidth, out.captureHeight); }
	else if (a == "--window-pos" && i + 2 < argc)
	{
		out.windowX = std::atoi(argv[++i]);
		out.windowY = std::atoi(argv[++i]);
	}
	else
	{
		char* end = nullptr;
		const long pid = std::strtol(a.c_str(), &end, 10);
		if (end == a.c_str() || *end != '\0') { return false; }
		out.options.pid = static_cast<int>(pid);
	}
	return true;
}

} // namespace

CliArgs parseArgs(int argc, char* argv[])
{
	CliArgs out;
	for (int i = 1; i < argc && out.ok; ++i) { out.ok = parseOne(out, argc, argv, i); }
	const bool needsSource = out.options.page != "replay" && !pageUsesHttp(out.options.page);
	if (out.ok && needsSource && !out.options.pid && !out.options.file) { out.ok = false; }
	if (out.options.httpPort <= 0) { out.options.httpPort = 8090; }
	if (!out.ok) { printUsage(); }
	return out;
}

} // namespace mitiru::tool

namespace
{

std::filesystem::path executableDir()
{
	wchar_t buf[MAX_PATH] = {};
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

} // namespace

int main(int argc, char* argv[])
{
	using namespace mitiru::tool;
	CliArgs args = parseArgs(argc, argv);
	if (!args.ok) { return 2; }

	args.options.document = findPageDocument(executableDir(), args.options.page);
	if (args.options.document.empty())
	{
		std::fprintf(stderr, "[mitiru_tool] ページ %s の RML が無い (assets/%s.rml、MITIRU_ASSET_ROOT/assets/%s.rml を探した)\n",
		             args.options.page.c_str(), args.options.page.c_str(), args.options.page.c_str());
		return 2;
	}
	const WindowSpec spec = windowSpecFor(args.options.page);
	if (args.capture) { return runCapture(args, spec); }
	const WindowRun run = runToolWindow(args.options, spec, args.windowX, args.windowY);
	if (!run.error.empty())
	{
		std::fprintf(stderr, "[mitiru_tool] UI を始められなかった: %s\n", run.error.c_str());
		return 1;
	}
	return 0;
}
