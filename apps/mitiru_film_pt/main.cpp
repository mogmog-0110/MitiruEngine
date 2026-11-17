// mitiru_film_pt: 映像の 1 コマを DXR 1.1 のパストレーサーで撮る試作 (ADR 0154)。
//
// mitiru_film_pt <film.json> --out <frame.png> [--t <秒>] [--spp N] [--bounces N] [--assets <dir>]...
//                [--pfm <接頭辞>] [--watt-scale W] [--seed N]
//   --pfm があれば <接頭辞>_color.pfm / _albedo.pfm / _normal.pfm (線形、OIDN への入力) も書く。
//
// 終了コード: 0 = 成功 / 2 = 引数の誤り / 3 = 映像またはモデルの誤り / 4 = GPU の誤り

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <stb_image_write.h>

#include <mitiru/film/FilmLoad.hpp>
#include <mitiru/film/FilmScene.hpp>

#include "PtRender.hpp"

namespace
{

struct Args
{
	std::string              film, out, pfm;
	float                    t = 0.0f;
	float                    wattScale = 40.0f;
	film_pt::PtOptions       opt;
	std::vector<std::string> assets;
};

int usage(const char* why)
{
	std::fprintf(stderr, "%s\n使い方: mitiru_film_pt <film.json> --out <frame.png> [--t <秒>] [--spp N] [--bounces N] [--assets <dir>]... [--pfm <接頭辞>] [--watt-scale W] [--seed N]\n", why);
	return 2;
}

bool parse(int argc, char** argv, Args& a)
{
	if (argc < 2) { return false; }
	a.film = argv[1];
	for (int i = 2; i + 1 < argc; i += 2)
	{
		const std::string k = argv[i], v = argv[i + 1];
		if (k == "--out") { a.out = v; }
		else if (k == "--pfm") { a.pfm = v; }
		else if (k == "--t") { a.t = static_cast<float>(std::atof(v.c_str())); }
		else if (k == "--spp") { a.opt.spp = std::max(1, std::atoi(v.c_str())); }
		else if (k == "--bounces") { a.opt.maxBounce = std::max(0, std::atoi(v.c_str())); }
		else if (k == "--seed") { a.opt.seed = static_cast<std::uint32_t>(std::atoi(v.c_str())); }
		else if (k == "--watt-scale") { a.wattScale = static_cast<float>(std::atof(v.c_str())); }
		else if (k == "--assets") { a.assets.push_back(v); }
		else { return false; }
	}
	return !a.out.empty() && (argc % 2) == 0;
}

void writePfm(const std::string& path, const std::vector<float>& rgb, int w, int h)
{
	std::ofstream f(path, std::ios::binary);
	f << "PF\n" << w << " " << h << "\n-1.0\n";
	for (int y = h - 1; y >= 0; --y) { f.write(reinterpret_cast<const char*>(&rgb[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3]), static_cast<std::streamsize>(w) * 12); }
}

unsigned char srgb8(float x)
{
	x = std::clamp(x, 0.0f, 1.0f);
	const float e = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
	return static_cast<unsigned char>(e * 255.0f + 0.5f);
}

}  // namespace

int main(int argc, char** argv)
{
	Args a;
	if (!parse(argc, argv, a)) { return usage("引数が合いません"); }
	std::string error;
	const auto film = mitiru::film::loadFilmFile(a.film, error);
	if (!film) { std::fprintf(stderr, "NG: %s\n", error.c_str()); return 3; }
	const nlohmann::json sceneJson = mitiru::film::sceneAt(*film, a.t);

	std::vector<std::string> roots = a.assets;
	for (std::filesystem::path d = std::filesystem::path(a.film).parent_path(); !d.empty(); d = d.parent_path())
	{
		roots.push_back(d.generic_string());
		roots.push_back((d / "examples").generic_string());
		if (d == d.root_path()) { break; }
	}
	const film_pt::PtScene scene = film_pt::buildScene(sceneJson, roots, a.wattScale);
	std::printf("三角形 %zu、材質 %zu、光 %zu、画像 %dx%d、%d spp、跳ね返り %d\n", scene.indices.size() / 3, scene.materials.size(), scene.lights.size(),
	            scene.width, scene.height, a.opt.spp, a.opt.maxBounce);

	film_pt::PtResult r;
	try { r = film_pt::renderPt(scene, a.opt); }
	catch (const std::exception& e) { std::fprintf(stderr, "NG: %s\n", e.what()); return 4; }
	std::printf("GPU: %s\nAS の構築 %.1f ms、%d 標本の実行 %.1f ms (%.2f ms/標本)\n", r.adapter.c_str(), r.buildMs, a.opt.spp, r.traceMs, r.traceMs / a.opt.spp);

	std::vector<unsigned char> png(r.color.size());
	for (std::size_t i = 0; i < png.size(); ++i) { png[i] = srgb8(r.color[i] * scene.exposure); }
	if (stbi_write_png(a.out.c_str(), scene.width, scene.height, 3, png.data(), scene.width * 3) == 0)
	{
		std::fprintf(stderr, "NG: %s に書けません\n", a.out.c_str());
		return 3;
	}
	if (!a.pfm.empty())
	{
		writePfm(a.pfm + "_color.pfm", r.color, scene.width, scene.height);
		writePfm(a.pfm + "_albedo.pfm", r.albedo, scene.width, scene.height);
		writePfm(a.pfm + "_normal.pfm", r.normal, scene.width, scene.height);
	}
	std::printf("OK: %s\n", a.out.c_str());
	return 0;
}
