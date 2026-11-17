#pragma once

// mitiru_film export-blender: 映像のある時刻の場面 (FilmScene.hpp) と glb を書き出し、Blender (Cycles) で撮るスクリプトを隣へ置く。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <mitiru/film/FilmLoad.hpp>
#include <mitiru/film/FilmScene.hpp>

#include "FilmBlenderScript.hpp"

namespace film_export
{

namespace fs = std::filesystem;

struct BlenderOptions
{
	std::string              film;
	std::string              outDir;
	float                    t = 0.0f;
	std::vector<std::string> assetRoots;   ///< モデルのパスの起点。映像の隣とその親も探す
};

inline fs::path findModel(const BlenderOptions& o, const std::string& model)
{
	const fs::path filmDir = fs::path(o.film).parent_path();
	std::vector<fs::path> roots;
	for (const auto& r : o.assetRoots) { roots.emplace_back(r); }
	for (fs::path d = filmDir; !d.empty() && roots.size() < 24; d = d.parent_path())
	{
		roots.push_back(d);
		roots.push_back(d / "examples");
		if (d == d.root_path()) { break; }
	}
	for (const auto& r : roots)
	{
		std::error_code ec;
		if (fs::is_regular_file(r / model, ec)) { return r / model; }
	}
	return {};
}

/// 戻り値は終了コード (0 成功、3 失敗)。glb が見つからない物は警告を出して場面に残す (スクリプトが警告し、描かない)
inline int exportBlender(const BlenderOptions& o)
{
	std::string error;
	const auto film = mitiru::film::loadFilmFile(o.film, error);
	if (!film) { std::fprintf(stderr, "NG: %s\n", error.c_str()); return 3; }
	nlohmann::json scene = mitiru::film::sceneAt(*film, o.t);
	const fs::path out(o.outDir);
	std::error_code ec;
	fs::create_directories(out / "models", ec);
	for (const auto& p : scene["props"])
	{
		const std::string model = p["model"].get<std::string>();
		if (model.empty()) { continue; }
		const fs::path src = findModel(o, model);
		if (src.empty()) { std::fprintf(stderr, "警告: モデルが見つかりません: %s (--assets で起点を渡す)\n", model.c_str()); continue; }
		const fs::path dst = out / "models" / model;
		fs::create_directories(dst.parent_path(), ec);
		fs::copy_file(src, dst, fs::copy_options::skip_existing, ec);
		if (ec) { std::fprintf(stderr, "NG: %s を写せません: %s\n", model.c_str(), ec.message().c_str()); return 3; }
	}
	std::ofstream js(out / "scene.json", std::ios::binary);
	js << scene.dump(1) << "\n";
	std::ofstream py(out / "render_blender.py", std::ios::binary);
	py.write(reinterpret_cast<const char*>(kFilmBlenderScript), sizeof(kFilmBlenderScript) - 1);
	if (!js || !py) { std::fprintf(stderr, "NG: %s に書けません\n", o.outDir.c_str()); return 3; }
	std::printf("OK: %s (t=%.3f 秒、小道具 %zu、光 %zu) に scene.json と render_blender.py を書きました\n", o.outDir.c_str(), o.t,
	            scene["props"].size(), scene["lights"].size());
	return 0;
}

}  // namespace film_export
