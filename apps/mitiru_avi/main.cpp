// mitiru_avi: PNG 連番 (--capture-dir) と WAV (--audio-capture) から AVI を作る。ゲームも host も動かさない後処理。
//
// mitiru_avi --frames <dir> --out <out.avi> [--audio <a.wav>] [--fps 60] [--quality 90]
//
// 終了コード: 0 = 書き出し成功 / 2 = 引数の誤り / 3 = 書き出し失敗

#include <cstdio>
#include <cstdlib>
#include <string>

#include <mitiru/video/CaptureAviExport.hpp>

namespace
{

constexpr int kExitUsage = 2;
constexpr int kExitFailed = 3;

int usage(const char* why)
{
	std::fprintf(stderr, "%s\n使い方: mitiru_avi --frames <dir> --out <out.avi> [--audio <a.wav>] [--fps 60] [--quality 90]\n", why);
	return kExitUsage;
}

}  // namespace

int main(int argc, char** argv)
{
	mitiru::video::CaptureAviOptions opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		const bool hasValue = i + 1 < argc;
		if (a == "--frames" && hasValue) { opt.framesDir = argv[++i]; }
		else if (a == "--audio" && hasValue) { opt.audioWav = argv[++i]; }
		else if (a == "--out" && hasValue) { opt.out = argv[++i]; }
		else if (a == "--fps" && hasValue) { opt.fps = static_cast<std::uint32_t>(std::atoi(argv[++i])); }
		else if (a == "--quality" && hasValue) { opt.jpegQuality = std::atoi(argv[++i]); }
		else { return usage(("引数 " + a + " が分かりません").c_str()); }
	}
	if (opt.framesDir.empty() || opt.out.empty()) { return usage("--frames と --out は必須です"); }
	if (opt.fps == 0 || opt.jpegQuality < 1 || opt.jpegQuality > 100) { return usage("--fps は 1 以上、--quality は 1 から 100 です"); }

	const mitiru::video::CaptureAviResult r = mitiru::video::exportCaptureAvi(opt);
	if (!r.ok)
	{
		std::fprintf(stderr, "AVI を書けませんでした: %s\n", r.error.c_str());
		return kExitFailed;
	}
	std::printf("%s: %llu フレーム %ux%u %u fps (%.2f 秒)、音 %llu サンプル、%llu byte\n", opt.out.string().c_str(),
	            static_cast<unsigned long long>(r.frames), r.width, r.height, opt.fps,
	            static_cast<double>(r.frames) / opt.fps, static_cast<unsigned long long>(r.audioSamples),
	            static_cast<unsigned long long>(r.bytes));
	if (r.audioPaddedSamples > 0 || r.audioDroppedSamples > 0)
	{
		std::printf("音の長さが画像と合わない分: 無音で埋めた %llu、捨てた %llu サンプル\n",
		            static_cast<unsigned long long>(r.audioPaddedSamples), static_cast<unsigned long long>(r.audioDroppedSamples));
	}
	return 0;
}
