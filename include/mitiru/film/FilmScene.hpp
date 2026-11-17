#pragma once

/// @file FilmScene.hpp
/// @brief 映像のある 1 時刻を、描画器に依らない 1 枚の場面 (JSON) にする。Cycles への書き出しとパストレーサーが同じ値を読む。
/// @details 描画 (FilmDraw.hpp) と同じ評価関数で求めるので、ラスタライザーと同じカメラ・小道具・光になる。
/// 世界の軸は film のまま (y が上の右手系、glTF と同じ)。軸の入れ替えは読む側が行う。

#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/film/FilmEval.hpp>
#include <mitiru/film/FilmTypes.hpp>
#include <mitiru/narrative/SequencePlayer.hpp>

namespace mitiru::film
{

namespace scene_detail
{

struct M3
{
	float m[3][3];
};

inline M3 mul(const M3& a, const M3& b)
{
	M3 r{};
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			for (int k = 0; k < 3; ++k) { r.m[i][j] += a.m[i][k] * b.m[k][j]; }
		}
	}
	return r;
}

inline M3 rotAxis(int axis, float deg)
{
	const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
	if (axis == 0) { return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; }
	if (axis == 1) { return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; }
	return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

/// FilmDraw の eulerQuat (y → x → z の順に回す、q = qy·qx·qz) と同じ向き
inline M3 eulerMatrix(const Vec3& r) { return mul(mul(rotAxis(1, r.y), rotAxis(0, r.x)), rotAxis(2, r.z)); }

inline nlohmann::json vec(const Vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

}  // namespace scene_detail

/// @brief 時刻 t の場面。props の matrix は 3x4 (行優先、拡大を含む) で、モデルの座標を世界へ送る
[[nodiscard]] inline nlohmann::json sceneAt(const Film& f, float t)
{
	using nlohmann::json;
	narrative::CameraShot cam = applyRig(f, t, narrative::evaluateCamera(f.seq, t, narrative::CameraShot{}));
	const LensSample lens = evaluateLens(f, t, cam.eye);
	const render::SceneLook look = evaluateLook(f, t);

	json camera = {{"eye", scene_detail::vec(cam.eye)}, {"target", scene_detail::vec(cam.target)}, {"up", scene_detail::vec(cam.up)},
	               {"fovDeg", lens.hasFocal ? lens.fovDeg : cam.fovDeg}, {"near", cam.nearDist}, {"far", cam.farDist},
	               {"focusDist", lens.focusDist}, {"apertureRadius", lens.apertureRadius}};

	json props = json::array();
	for (const Prop& p : f.props)
	{
		const PropState s = evaluateProp(p, t);
		if (!s.visible || s.alpha <= 0.0f) { continue; }
		const scene_detail::M3 r = scene_detail::eulerMatrix(s.rotDeg);
		const float sc[3] = {s.scale.x, s.scale.y, s.scale.z};
		const float pos[3] = {s.pos.x, s.pos.y, s.pos.z};
		json mat = json::array();
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j) { mat.push_back(r.m[i][j] * sc[j]); }
			mat.push_back(pos[i]);
		}
		props.push_back({{"name", p.name}, {"model", p.model}, {"shape", p.shape}, {"matrix", mat},
		                 {"color", scene_detail::vec(s.color)}, {"alpha", s.alpha}, {"animated", !p.clips.empty()}});
	}

	json lights = json::array();
	const char* kinds[] = {"sun", "point", "spot"};
	for (const Light& l : f.lights)
	{
		const LightState st = evaluateLight(l, t);
		lights.push_back({{"name", l.name}, {"kind", kinds[static_cast<int>(l.kind)]}, {"pos", scene_detail::vec(st.pos)},
		                  {"dir", scene_detail::vec(st.dir)}, {"color", scene_detail::vec(st.color)}, {"intensity", st.intensity},
		                  {"range", st.range}, {"innerDeg", l.innerDeg}, {"outerDeg", l.outerDeg}});
	}

	const auto rgb = [](const float* c) { return json::array({c[0], c[1], c[2]}); };
	return {{"scene", 1}, {"t", t}, {"size", {f.width, f.height}}, {"camera", camera}, {"props", props}, {"lights", lights},
	        {"world", {{"ambient", rgb(look.ambient)}, {"ambientSky", rgb(look.ambientSky)}, {"ambientGround", rgb(look.ambientGround)},
	                   {"exposure", look.exposure * std::exp2(lens.evBias)}, {"skyZenith", scene_detail::vec(f.skyZenith)},
	                   {"skyNadir", scene_detail::vec(f.skyNadir)}, {"hasSky", f.hasSky}}},
	        {"unsupported", {{"emitters", f.emitters.size()}, {"scatters", f.scatters.size()}}}};
}

}  // namespace mitiru::film
