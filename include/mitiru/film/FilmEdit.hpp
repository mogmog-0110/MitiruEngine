#pragma once

/// @file FilmEdit.hpp
/// @brief 時刻 t におけるショット、トランジション、ワールドの時間、ショットごとのルック、暗転を求める純関数。描画は FilmDraw.hpp に分ける。

#include <algorithm>
#include <cmath>

#include <mitiru/film/FilmEval.hpp>
#include <mitiru/film/FilmTypes.hpp>

namespace mitiru::film::edit
{

/// @brief 映像の時間 t からワールドの時間 τ(t) を求める。time_remap が無いときは t とビット単位で同じ値を返す
[[nodiscard]] inline float worldTime(const Film& f, float t) noexcept
{
	const std::vector<TimeSeg>& segs = f.edit.timeMap;
	if (segs.empty()) { return t; }
	double world = 0.0, cursor = 0.0;
	const double tt = t;
	for (const TimeSeg& s : segs)
	{
		if (tt <= s.start) { break; }
		world += static_cast<double>(s.start) - cursor;
		const double upto = std::min(tt, static_cast<double>(s.end));
		world += (upto - s.start) * s.speed;
		cursor = upto;
		if (tt <= s.end) { return static_cast<float>(world); }
	}
	return static_cast<float>(world + (tt - cursor));
}

/// @brief 時刻 t に映るショット。重なるときは後から始まったほう、無ければ -1
[[nodiscard]] inline int shotIndexAt(const Film& f, float t) noexcept
{
	int found = -1;
	for (std::size_t i = 0; i < f.edit.shots.size(); ++i)
	{
		if (f.edit.shots[i].start <= t && t < f.edit.shots[i].end) { found = static_cast<int>(i); }
	}
	return found;
}

/// @brief 入ってくるショット k のトランジションの進み 0..1。トランジション中でなければ負
[[nodiscard]] inline float transitionProgress(const Film& f, int k, float t) noexcept
{
	if (k <= 0 || static_cast<std::size_t>(k) >= f.edit.shots.size()) { return -1.0f; }
	const ShotEdit& s = f.edit.shots[static_cast<std::size_t>(k)];
	if (s.transitionIn.kind == TransitionKind::None || !(s.transitionIn.duration > 0.0f)) { return -1.0f; }
	const float u = (t - s.start) / s.transitionIn.duration;
	return (u >= 0.0f && u < 1.0f) ? u : -1.0f;
}

/// @brief ディゾルブとワイプのウィンドウの前後に足すパディング 0.25 秒。サンプル時刻がウィンドウの端を少し越える間も 2 枚目のレイヤーを描く
constexpr float kLayerPad = 0.25f;

/// @brief 時刻 t がパディングつきのディゾルブかワイプのウィンドウに入るとき、入ってくるショットの添字を返す。無ければ -1
[[nodiscard]] inline int compositedShotNear(const Film& f, float t) noexcept
{
	for (std::size_t i = 1; i < f.edit.shots.size(); ++i)
	{
		const ShotEdit& s = f.edit.shots[i];
		const TransitionKind k = s.transitionIn.kind;
		if ((k == TransitionKind::Dissolve || k == TransitionKind::Wipe) && t >= s.start - kLayerPad && t < s.start + s.transitionIn.duration + kLayerPad)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

/// @brief 時刻 t の 1 コマに映すショットとカメラ、重ねる黒の量
struct View
{
	bool        draw = true;          ///< false なら黒だけ (トランジションの 2 枚目のレイヤーで、ウィンドウの外)
	int         shot = -1;            ///< 映すショット (-1 はショットの表が無い、または範囲外)
	float       tWorld = 0.0f;        ///< ワールドの時間
	float       black = 0.0f;         ///< ディップ・トゥ・ブラックで重ねる黒の量
	const Film* cameraFilm = nullptr;  ///< カメラ・レンズ・リグを引く Film (レイヤーごとにカメラの区間を広げたものか、f そのもの)
};

/// @brief layer 0 はプログラムを描く。トランジション中は前のショット、ディップでは前半に前のショット、後半に次のショットを描く
/// layer 1 はディゾルブとワイプで入ってくるショットを、ウィンドウの前後 kLayerPad 秒を含めて描く。それ以外の時刻では描かない
[[nodiscard]] inline View viewAt(const Film& f, float tl, int layer) noexcept
{
	View v;
	v.tWorld = worldTime(f, tl);
	v.cameraFilm = &f;
	const int k = shotIndexAt(f, tl);
	v.shot = k;
	if (layer == 1)
	{
		const int idx = compositedShotNear(f, tl);
		if (idx < 0) { v.draw = false; return v; }
		v.shot = idx;
		const auto& layers = f.edit.layerFilms;
		if (static_cast<std::size_t>(idx) < layers.size() && layers[static_cast<std::size_t>(idx)] != nullptr)
		{
			v.cameraFilm = layers[static_cast<std::size_t>(idx)].get();
		}
		return v;
	}
	const float u = transitionProgress(f, k, tl);
	if (u >= 0.0f)
	{
		const Transition& tr = f.edit.shots[static_cast<std::size_t>(k)].transitionIn;
		if (tr.kind == TransitionKind::Dip)
		{
			v.shot = u < 0.5f ? k - 1 : k;
			v.black = u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u;
		}
		else { v.shot = k - 1; }
		const auto& layers = f.edit.layerFilms;
		if (v.shot >= 0 && static_cast<std::size_t>(v.shot) < layers.size() && layers[static_cast<std::size_t>(v.shot)] != nullptr)
		{
			v.cameraFilm = layers[static_cast<std::size_t>(v.shot)].get();
		}
	}
	return v;
}

[[nodiscard]] inline render::SceneLook lookFor(const Film& f, int shot, render::SceneLook look)
{
	if (shot < 0 || static_cast<std::size_t>(shot) >= f.edit.shots.size()) { return look; }
	for (const LookPatch& p : f.edit.shots[static_cast<std::size_t>(shot)].look)
	{
		if (p.field->kind == LookKind::Vec3) { setLookVec3(look, *p.field, p.v[0], p.v[1], p.v[2]); }
		else { setLookScalar(look, *p.field, p.v[0]); }
	}
	return look;
}

/// @brief 暗転の量。映るショットに fade があればその区間ではそれを使い、無ければルートの fade を使う
[[nodiscard]] inline float fadeFor(const Film& f, float tl, const View& v)
{
	const int k = v.shot;   // レイヤーごとに自分のショットの fade を使う (出ていくレイヤーに、入ってくるショットの fade を掛けない)
	float fade = narrative::fadeAt(f.seq, tl);
	if (k >= 0 && static_cast<std::size_t>(k) < f.edit.shots.size() && f.edit.shots[static_cast<std::size_t>(k)].hasFade)
	{
		fade = narrative::seq_detail::sampleFloat(f.edit.shots[static_cast<std::size_t>(k)].fade, tl);
	}
	return std::max(fade, v.black);
}

/// @brief ディゾルブで入ってくるほうの重み。線形光で混ぜる
[[nodiscard]] inline float dissolveWeight(float u) noexcept { return u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u); }

/// @brief ワイプで座標 (xn, yn) が入ってくるほうの割合 0..1。座標は左上を原点とする 0..1
[[nodiscard]] inline float wipeMask(const Transition& tr, float u, float xn, float yn) noexcept
{
	const float soft = std::max(tr.softness, 1.0e-4f);
	float pos = (tr.dir == WipeDir::Right || tr.dir == WipeDir::Left) ? xn : yn;
	if (tr.dir == WipeDir::Left || tr.dir == WipeDir::Up) { pos = 1.0f - pos; }
	const float edge = u * (1.0f + soft) - soft;
	const float m = std::clamp((edge + soft - pos) / soft, 0.0f, 1.0f);
	return m * m * (3.0f - 2.0f * m);
}

/// @brief タイトルの濃さ 0..1。fadeIn と fadeOut の短いほうが先に 0 へ向かう
[[nodiscard]] inline float titleAlpha(const TitleCard& c, float t) noexcept
{
	if (t < c.start || t >= c.end) { return 0.0f; }
	const float in = c.fadeIn > 0.0f ? (t - c.start) / c.fadeIn : 1.0f;
	const float out = c.fadeOut > 0.0f ? (c.end - t) / c.fadeOut : 1.0f;
	return std::clamp(std::min(in, out), 0.0f, 1.0f);
}

}  // namespace mitiru::film::edit
