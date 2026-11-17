#pragma once

/// @file FilmScatterDraw.hpp
/// @brief scatter の全インスタンスを素材ごとに 1 回のインスタンス描画で出す。風で揺らす行列だけは、ここで時刻から求める。

#include <vector>

#include <mitiru/core/Screen.hpp>
#include <mitiru/film/FilmTypes.hpp>
#include <mitiru/render/DrawParams3D.hpp>

namespace mitiru::film
{

/// @brief 地形、草、水面の world.json を、時刻 t の風と波で描く。主役の小道具より先に描く。
inline void drawOutdoorWorld(Screen& s, const Film& f, float t)
{
	if (f.outdoor.world.empty()) { return; }
	render::OutdoorDrawPod pod;
	pod.timeSec = t;
	pod.windDir[0] = f.outdoor.windDir[0]; pod.windDir[1] = f.outdoor.windDir[1];
	pod.windStrength = f.outdoor.windStrength;
	pod.waveScale = f.outdoor.waveScale;
	pod.waterOffset = f.outdoor.waterOffset;
	pod.flags = (f.outdoor.grass ? 0u : render::kOutdoorNoGrass) | (f.outdoor.scatter ? 0u : render::kOutdoorNoScatter) |
	            (f.outdoor.water ? 0u : render::kOutdoorNoWater);
	s.drawOutdoor(f.outdoor.world.c_str(), pod);
}

/// @brief 時刻 t の scatter を描く。頂点色が単色を濁らせるため、ID、アルファ、奥行きの別の画像 (Pass) には含めない。
inline void drawScatters(Screen& s, const Film& f, float t)
{
	for (const Scatter& sc : f.scatters)
	{
		if (sc.instances.empty()) { continue; }
		for (std::size_t i = 0; i < sc.instances.size(); ++i)
		{
			render::MeshInstance& out = sc.frame[i];
			scatterWorld(sc, sc.instances[i], t, out.world);
			for (int k = 0; k < 4; ++k) { out.tint[k] = sc.instances[i].tint[k]; }
		}
		const int n = static_cast<int>(sc.frame.size());
		if (sc.model.empty()) { s.drawMeshInstanced(sc.shape.c_str(), sc.frame.data(), n); }
		else { s.drawModelInstanced(sc.model.c_str(), sc.frame.data(), n); }
	}
}

}  // namespace mitiru::film
