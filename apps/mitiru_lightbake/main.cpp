// mitiru_lightbake: *.lighting.json (レベルの glb、箱、太陽、局所光、空、プローブの格子、反射のプローブ) から
// *.lighting.bin を焼くコンソールツール。描く側は Renderer3D_DX12::loadLightingBake で読む。
// 同じ入力からは、スレッドの数に関わらず同じバイト列が出る。成功は 0、引数の誤りは 2、焼けなければ 1 を返す。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <mitiru/platform/Utf8Args.hpp>
#include <mitiru/render/gi/LightingBakeJob.hpp>

namespace
{

namespace gi = mitiru::render::gi;

struct Args
{
	std::filesystem::path json;
	std::filesystem::path out;
	std::uint32_t threads = 0;
	std::uint32_t rays = 0;
};

void usage()
{
	std::fprintf(stderr, "usage: mitiru_lightbake <level.lighting.json> [-o out.lighting.bin] [--threads n] [--rays n]\n");
}

/// @brief 1..maxValue の整数。それ以外 (負、0、数でない) は false
bool positiveInt(const std::string& text, std::uint32_t maxValue, std::uint32_t& out)
{
	char* end = nullptr;
	const long long v = std::strtoll(text.c_str(), &end, 10);
	if (end == text.c_str() || *end != '\0' || v < 1 || v > static_cast<long long>(maxValue)) { return false; }
	out = static_cast<std::uint32_t>(v);
	return true;
}

bool parse(const std::vector<std::string>& argv, Args& a)
{
	for (std::size_t i = 1; i < argv.size(); ++i)
	{
		const std::string& k = argv[i];
		const bool hasValue = i + 1 < argv.size();
		if (k == "-o" && hasValue) { a.out = std::filesystem::u8path(argv[++i]); continue; }
		if ((k == "--threads" || k == "--rays") && hasValue)
		{
			std::uint32_t& dst = (k == "--threads") ? a.threads : a.rays;
			if (!positiveInt(argv[++i], k == "--threads" ? 256u : 65536u, dst))
			{
				std::fprintf(stderr, "mitiru_lightbake: %s には 1 以上の整数を書いてください (--threads は 256、--rays は 65536 まで)。\n", k.c_str());
				return false;
			}
			continue;
		}
		if (!k.empty() && k[0] == '-') { std::fprintf(stderr, "mitiru_lightbake: %s というオプションはありません。\n", k.c_str()); return false; }
		if (!a.json.empty()) { std::fprintf(stderr, "mitiru_lightbake: lighting.json は 1 つだけ渡してください (%s が 2 つ目です)。\n", k.c_str()); return false; }
		a.json = std::filesystem::u8path(k);
	}
	return !a.json.empty();
}

int bake(const Args& a)
{
	std::string error;
	auto job = gi::loadLightingBakeJob(a.json, error);
	if (!job)
	{
		std::fprintf(stderr, "mitiru_lightbake: %s\n", error.c_str());
		return 1;
	}
	if (!a.out.empty()) { job->output = a.out; }
	job->gi.threads = a.threads;
	job->reflection.threads = a.threads;
	if (a.rays > 0) { job->gi.raysPerProbe = a.rays; }
	const auto start = std::chrono::steady_clock::now();
	const gi::LightingBake result = gi::runLightingBake(*job);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	if (!gi::writeLightingBake(job->output, result, error))
	{
		std::fprintf(stderr, "mitiru_lightbake: %s\n", error.c_str());
		return 1;
	}
	std::printf("mitiru_lightbake: %s (%zu triangles, %u probes, %zu reflection probes, %.2f s)\n",
	            job->output.filename().string().c_str(), job->scene.triangleCount(), result.hasVolume ? result.volume.grid.count() : 0u,
	            result.reflections.size(), seconds);
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	std::vector<std::string> args = mitiru::platform::commandLineUtf8Args();
	if (args.empty()) { args.assign(argv, argv + argc); }
	Args a;
	if (!parse(args, a))
	{
		usage();
		return 2;
	}
	return bake(a);
}
