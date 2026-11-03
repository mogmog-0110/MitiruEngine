// mitiru_texc: 画像 1 枚を mip 連鎖付きの BC7 / BC5 / BC4 / BC6H DDS に変換する (コンソール)
//   mitiru_texc <画像> [--kind color|normal|mask|data|hdr] [-o <出力 .dds>]
// drawModel は import 時に同じ圧縮を自動で行う。これは手動で配置する画像や確認に使う。
// --kind hdr は Radiance の .hdr を BC6H (UF16) にする。

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <stb_image.h>

#include <mitiru/render/HdrEnvironment.hpp>
#include <mitiru/render/TextureCompress.hpp>

namespace
{

namespace mr = mitiru::render;

struct Args
{
	std::string input;
	std::string output;
	mr::TextureKind kind = mr::TextureKind::Color;
	bool hdr = false;
};

std::optional<Args> parseArgs(int argc, char** argv)
{
	Args a;
	for (int i = 1; i < argc; ++i)
	{
		const std::string s = argv[i];
		if (s == "--kind" && i + 1 < argc)
		{
			const std::string k = argv[++i];
			if (k == "color") { a.kind = mr::TextureKind::Color; }
			else if (k == "normal") { a.kind = mr::TextureKind::Normal; }
			else if (k == "mask") { a.kind = mr::TextureKind::Mask; }
			else if (k == "data") { a.kind = mr::TextureKind::Data; }
			else if (k == "hdr") { a.hdr = true; }
			else { return std::nullopt; }
		}
		else if (s == "-o" && i + 1 < argc) { a.output = argv[++i]; }
		else if (a.input.empty() && !s.starts_with("-")) { a.input = s; }
		else { return std::nullopt; }
	}
	if (a.input.empty()) { return std::nullopt; }
	if (a.output.empty()) { a.output = a.input + ".dds"; }
	return a;
}

/// 比較する成分 (BC5 は RG、BC4 は R) のみの PSNR
double psnr(const std::vector<std::uint8_t>& a, const std::uint8_t* b, std::size_t pixels, int channels)
{
	double se = 0.0;
	for (std::size_t i = 0; i < pixels; ++i)
	{
		for (int c = 0; c < channels; ++c)
		{
			const double d = double(a[i * 4 + c]) - double(b[i * 4 + c]);
			se += d * d;
		}
	}
	const double mse = se / double(pixels * channels);
	return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

struct RelativeError
{
	double mean = 0.0;
	double max = 0.0;
};

/// 線形 RGB の相対誤差 |a - b| / max(b, 1/1024)。暗い値の揺れを過大に数えないよう分母に下限を置く
RelativeError relativeError(const std::vector<float>& a, const std::vector<float>& b)
{
	RelativeError e;
	if (a.size() != b.size() || a.empty()) { e.max = 1.0; return e; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		const double r = std::abs(double(a[i]) - double(b[i])) / std::max(double(b[i]), 1.0 / 1024.0);
		e.mean += r;
		e.max = std::max(e.max, r);
	}
	e.mean /= double(a.size());
	return e;
}

bool writeDds(const std::string& path, const mr::MipImage& img)
{
	const auto bytes = mr::encodeDds(img);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!f)
	{
		std::fprintf(stderr, "mitiru_texc: %s に書き込めません。\n", path.c_str());
		return false;
	}
	std::printf("%s: %ux%u, %zu mips, %.1f KiB", path.c_str(), img.width, img.height, img.mips.size(), double(bytes.size()) / 1024.0);
	return true;
}

int runHdr(const Args& args)
{
	std::ifstream in(args.input, std::ios::binary);
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const auto image = mr::decodeRadianceHdr(bytes);
	if (!image)
	{
		std::fprintf(stderr, "mitiru_texc: %s を Radiance の .hdr として読めません。\n", args.input.c_str());
		return 1;
	}
	const auto img = mr::compressHdrTexture(image->rgb.data(), static_cast<std::uint32_t>(image->width), static_cast<std::uint32_t>(image->height));
	if (!img)
	{
		std::fprintf(stderr, "mitiru_texc: %dx%d の画像は圧縮できません。幅と高さを 4 の倍数にしてください。\n", image->width, image->height);
		return 1;
	}
	const RelativeError err = relativeError(mr::decompressLevelHdr(*img, 0), image->rgb);
	if (!writeDds(args.output, *img)) { return 1; }
	std::printf(", BC6H の相対誤差 平均 %.2f%% 最大 %.2f%%\n", err.mean * 100.0, err.max * 100.0);
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	const auto args = parseArgs(argc, argv);
	if (!args)
	{
		std::fprintf(stderr, "使い方: mitiru_texc <画像> [--kind color|normal|mask|data|hdr] [-o <出力.dds>]\n");
		return 2;
	}
	if (args->hdr) { return runHdr(*args); }
	int w = 0, h = 0, comp = 0;
	std::uint8_t* px = stbi_load(args->input.c_str(), &w, &h, &comp, 4);
	if (px == nullptr)
	{
		std::fprintf(stderr, "mitiru_texc: 画像 %s を読めません。\n", args->input.c_str());
		return 1;
	}
	const auto img = mr::compressTexture(px, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), args->kind);
	if (!img)
	{
		stbi_image_free(px);
		std::fprintf(stderr, "mitiru_texc: %dx%d の画像は圧縮できません。幅と高さを 4 の倍数にしてください。\n", w, h);
		return 1;
	}
	const bool fourChannels = args->kind == mr::TextureKind::Color || args->kind == mr::TextureKind::Data;
	const int channels = fourChannels ? 4 : args->kind == mr::TextureKind::Normal ? 2 : 1;
	const double db = psnr(mr::decompressLevel(*img, 0), px, static_cast<std::size_t>(w) * h, channels);
	stbi_image_free(px);
	if (!writeDds(args->output, *img)) { return 1; }
	std::printf(", PSNR %.1f dB\n", db);
	return 0;
}
