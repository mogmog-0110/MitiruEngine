#pragma once

/// @file FilmDraw.hpp
/// @brief 時刻 t の映像の 1 コマを Screen へ描く。Film と時刻だけを見て、状態を持たない。

#include <cmath>
#include <span>
#include <string>
#include <vector>

#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/animation/MorphExpression.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/film/FilmEdit.hpp>
#include <mitiru/film/FilmEval.hpp>
#include <mitiru/film/FilmMorph.hpp>
#include <mitiru/film/FilmSampling.hpp>
#include <mitiru/film/FilmScatterDraw.hpp>
#include <mitiru/film/FilmTitles.hpp>
#include <mitiru/render/ScreenEnhanced.hpp>

namespace mitiru::film
{

namespace anim = mitiru::animation;

/// @brief 小道具 (props) と同じ並びでモデルのアニメ素材を持つ。init で作り、draw では読むだけ。
struct FilmAssets
{
	std::vector<anim::AnimAsset> models;
	std::vector<std::vector<int>> clips;  ///< props[i].clips[k] が指すモデルのクリップの添字。見つからなければ -1

	[[nodiscard]] static FilmAssets load(const Film& f)
	{
		FilmAssets a;
		a.models.resize(f.props.size());
		a.clips.resize(f.props.size());
		for (std::size_t i = 0; i < f.props.size(); ++i)
		{
			if (f.props[i].model.empty()) { continue; }
			a.models[i] = anim::loadAnimAssetFile(f.props[i].model.c_str()).value_or(anim::AnimAsset{});
			for (const PropClip& c : f.props[i].clips) { a.clips[i].push_back(a.models[i].findClip(c.name.c_str())); }
		}
		return a;
	}
};

namespace draw_detail
{

constexpr float kDegToRad = 3.14159265f / 180.0f;

inline sgc::Vec4f axisQuat(float x, float y, float z, float deg)
{
	const float h = deg * 0.5f * kDegToRad;
	const float s = std::sin(h);
	return {x * s, y * s, z * s, std::cos(h)};
}

/// @brief y (上まわり) → x → z の順に回す。rot の x、y、z は度。
inline sgc::Vec4f eulerQuat(const Vec3& rotDeg)
{
	return anim::quatMul(anim::quatMul(axisQuat(0, 1, 0, rotDeg.y), axisQuat(1, 0, 0, rotDeg.x)), axisQuat(0, 0, 1, rotDeg.z));
}

inline sgc::Mat4f worldOf(const PropState& s)
{
	return anim::localMatrix({{s.pos.x, s.pos.y, s.pos.z}, eulerQuat(s.rotDeg), {s.scale.x, s.scale.y, s.scale.z}});
}

inline sgc::Colorf colorOf(const Vec3& c, float a = 1.0f) { return {c.x, c.y, c.z, a}; }

inline void drawProp(Screen& s, const Prop& p, PropState st, const FilmAssets& assets, std::size_t index, float t, const Vec3* colorOverride,
                     float tFilm)
{
	if (!st.visible || st.alpha <= 0.0f) { return; }
	if (colorOverride != nullptr) { st.color = *colorOverride; st.alpha = 1.0f; }
	const sgc::Mat4f world = worldOf(st);
	render::DrawTint tint;
	tint.mul[0] = st.color.x; tint.mul[1] = st.color.y; tint.mul[2] = st.color.z; tint.mul[3] = st.alpha;
	if (p.model.empty()) { s.drawMeshTinted(p.shape.c_str(), world, tint); return; }
	anim::AnimPoseParams pose;
	PropClipSample active[8];   // 同時に重なるアニメは 8 本まで (超えた分は書いた順で後ろから落ちる)
	const int n = evaluatePropClips(p, t, active, 8);
	bool any = false;
	for (int k = 0; k < n; ++k)
	{
		const int clip = assets.clips[index][active[k].index];
		if (clip < 0) { continue; }
		const float len = assets.models[index].clips[static_cast<std::size_t>(clip)].durationSec;
		float time = active[k].time;
		if (p.clips[active[k].index].loop && len > 0.0f) { time = std::fmod(time, len); }
		anim::pushLayer(pose, anim::AnimLayer::clipAt(clip, time, active[k].weight));
		any = true;
	}
	if (!p.morphs.empty() && assets.models[index].morph.slotCount() > 0)
	{
		float weights[anim::kMorphMaxSlots];
		const anim::ExpressionPose expr = evaluateMorphPose(p, t, tFilm);
		const int count = anim::evaluateMorphWeights(assets.models[index], any ? &pose : nullptr, &expr, std::span<float>(weights, anim::kMorphMaxSlots));
		s.drawModelPose(p.model.c_str(), world, any ? &pose : nullptr, nullptr, 0, tint, weights, count);
		return;
	}
	s.drawModelPose(p.model.c_str(), world, any ? &pose : nullptr, nullptr, 0, tint);
}

inline Vec3 swungToward(const Vec3& d, float radiusDeg, float diskX, float diskY)
{
	const Dir3 m = swingDirection({d.x, d.y, d.z}, radiusDeg, diskX, diskY);
	return {m.x, m.y, m.z};
}

inline void drawLights(Screen& s, const Film& f, float t, const SampleOffsets& o)
{
	for (const Light& l : f.lights)
	{
		const LightState st = evaluateLight(l, t);
		const sgc::Colorf c{st.color.x, st.color.y, st.color.z, 1.0f};
		const sgc::Vec3f pos{st.pos.x, st.pos.y, st.pos.z};
		const Vec3 swung = l.kind == LightKind::Sun ? swungToward(st.dir, f.shadowPenumbraDeg, o.sunX, o.sunY) : st.dir;
		const sgc::Vec3f dir{swung.x, swung.y, swung.z};
		switch (l.kind)
		{
		case LightKind::Sun: s.light3D(dir, {c.r * st.intensity, c.g * st.intensity, c.b * st.intensity, 1.0f}); break;
		case LightKind::Point: s.pointLight3D(pos, st.range, c, st.intensity); break;
		case LightKind::Spot: s.spotLight3D(pos, dir, st.range, l.innerDeg, l.outerDeg, c, st.intensity, l.castShadow); break;
		}
	}
}

inline render::SceneLook withLens(render::SceneLook look, const LensSample& lens)
{
	look.exposure *= std::exp2(lens.evBias);
	if (lens.hasDof)
	{
		look.dofStart = lens.dofStart;
		look.dofEnd = lens.dofEnd;
		look.dofStrength = lens.dofStrengthPx;
	}
	return look;
}

inline void drawEmitters(Screen& s, const Film& f, float t)
{
	for (const Emitter& e : f.emitters)
	{
		if (t < e.start || t > e.end) { continue; }
		render::ParticleEmitterDesc d = e.desc;
		d.age = t - e.start;
		s.particles3D(&d, 1);
	}
}

inline void drawOverlay(Screen& s, const Film& f, float t, const edit::View& view)
{
	const float w = static_cast<float>(s.width()), h = static_cast<float>(s.height());
	const float bar = narrative::letterboxAt(f.seq, t) * h * 0.12f;
	const sgc::Colorf black{0.0f, 0.0f, 0.0f, 1.0f};
	if (bar > 0.5f) { s.drawRect(sgc::Rectf{0, 0, w, bar}, black); s.drawRect(sgc::Rectf{0, h - bar, w, bar}, black); }
	if (const narrative::Subtitle* sub = narrative::subtitleAt(f.seq, t))
	{
		const float size = h * 0.045f;
		std::string line = sub->speaker.empty() ? sub->text : sub->speaker + "「" + sub->text + "」";
		const sgc::Rectf box{w * 0.1f, h - bar - size * 2.6f, w * 0.8f, size * 1.8f};
		s.drawRect(box, sgc::Colorf{0, 0, 0, 0.45f});
		s.drawTextInRect(box, line, {1, 1, 1, 1}, size, Screen::TextAlignH::Center, Screen::TextAlignV::Middle, 8.0f, 2.0f);
	}
	const float fade = edit::fadeFor(f, t, view);
	if (fade > 0.0f) { s.drawRect(sgc::Rectf{-8, -8, w + 16, h + 16}, sgc::Colorf{0, 0, 0, fade > 1.0f ? 1.0f : fade}); }
	edit::drawTitles(s, f, t);
}

}  // namespace draw_detail

/// @brief Beauty 以外は物体だけを照明なしの単色で描く。奥行きは線形の霧で表す。
enum class Pass : std::uint8_t { Beauty, Id, Alpha, Depth };

/// @brief float の補助パス (ADR 0153)。Normal と Id は照明なしの材質 film_aov で小道具だけを描き、HDR 撮影 (kind 0) が float で読む。
///        値の符号化は tools/film_hdr.py の decode_normal / decode_id が戻す。速度は描画器の動きベクトルなので、画像は Beauty のまま (ここには無い)
enum class Aov : std::uint8_t { None, Normal, Id };

struct DrawOptions
{
	SampleOffsets offsets{};
	bool          accumulating = false;   ///< true なら画面空間のぼかしを切る (ボケは標本のレンズのずれで作る)
	Pass          pass = Pass::Beauty;
	Aov           aov = Aov::None;        ///< None 以外なら pass は Beauty のまま、補助パスの画像を描く
	int           layer = 0;              ///< トランジションのレイヤー。0 はプログラム、1 は入ってくるショット (FilmEdit.hpp の viewAt)
	float         depthNear = 0.0f;       ///< Depth: 白 (近い) から黒 (遠い) へ落とす距離の範囲
	float         depthFar = 50.0f;
};

/// @brief props[index] の物体 ID の色。添字 + 1 から決まり、Python 側の tools/film.py も同じ式で表を作る。
[[nodiscard]] inline Vec3 idColor(std::size_t index)
{
	const unsigned i = static_cast<unsigned>(index) + 1u;
	return {static_cast<float>((i * 67u) % 251u + 4u) / 255.0f, static_cast<float>((i * 131u) % 251u + 4u) / 255.0f,
	        static_cast<float>((i * 199u) % 251u + 4u) / 255.0f};
}

inline void drawPass(Screen& s, const Film& f, const FilmAssets& assets, float t, const DrawOptions& opt, const narrative::CameraShot& cam,
                     float fovDeg, float tFilm)
{
	using namespace draw_detail;
	s.clear(sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f});
	s.camera3D(cam.eye, cam.target, cam.up, fovDeg, cam.nearDist, cam.farDist);
	render::SceneLook look;
	look.shadingModel = 1;   // Phong。主光源を黒、環境光を白にして「色そのもの」にする (shadingModel 4 は物体の色が暗くなった)
	look.ambient[0] = look.ambient[1] = look.ambient[2] = 1.0f;
	look.exposure = 1.0f;
	look.aaMode = 2;         // 縁を混ぜない MSAA だけ (ID の境で色が別の物の ID にならないよう FXAA を避ける)
	if (opt.pass == Pass::Depth)
	{
		look.fog = true;
		look.fogColor[0] = look.fogColor[1] = look.fogColor[2] = 0.0f;
		look.fogNear = opt.depthNear;
		look.fogFar = opt.depthFar;
	}
	s.sceneLook3D(look);
	s.light3D(sgc::Vec3f{0.0f, -1.0f, 0.0f}, sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f});
	for (std::size_t i = 0; i < f.props.size(); ++i)
	{
		const Vec3 c = opt.pass == Pass::Id ? idColor(i) : Vec3{1.0f, 1.0f, 1.0f};
		drawProp(s, f.props[i], evaluateProp(f.props[i], t), assets, i, t, &c, tFilm);
	}
}

/// 補助パスの材質 (examples/film_player/assets/materials/film_aov.slang)。params は {mode (0 = 法線、1 = ID), 小道具の添字 + 1}
inline constexpr const char* kAovMaterial = "film_aov";

inline void drawAov(Screen& s, const Film& f, const FilmAssets& assets, float t, const DrawOptions& opt, const narrative::CameraShot& cam,
                    float fovDeg, float tFilm)
{
	using namespace draw_detail;
	const int material = s.registerMaterial3D(kAovMaterial);
	if (material < 0)
	{
		s.clear(sgc::Colorf{1.0f, 0.0f, 0.0f, 1.0f});
		s.drawTextInRect(sgc::Rectf{16, 16, static_cast<float>(s.width()) - 32, 120}, "assets/materials/film_aov.slang を読めません (-v で理由)",
		                 sgc::Colorf{1, 1, 1, 1}, 20.0f);
		return;
	}
	// clear は sRGB の値として書かれ、線形に直して HDR のターゲットへ入る (0.5 が 0.214 になる)。法線の符号化の線形 0.5 (= 0 ベクトル) にするには sRGB の 0.735357
	const float background = opt.aov == Aov::Normal ? 0.735357f : 0.0f;   // ID の 0 は物が無い
	s.clear(sgc::Colorf{background, background, background, 1.0f});
	s.camera3D(cam.eye, cam.target, cam.up, fovDeg, cam.nearDist, cam.farDist);
	render::SceneLook look;   // 露出 1、ガンマ 2.2、AO・bloom・彩度・コントラスト・フォグなしの既定のまま。撮影の式が値を変えない
	look.aaMode = 2;
	s.sceneLook3D(look);
	const Vec3 white{1.0f, 1.0f, 1.0f};
	for (std::size_t i = 0; i < f.props.size(); ++i)
	{
		const float params[2] = {opt.aov == Aov::Id ? 1.0f : 0.0f, static_cast<float>(i + 1)};
		s.material3D(material, params, 2, 0.0f);
		drawProp(s, f.props[i], evaluateProp(f.props[i], t), assets, i, t, &white, tFilm);
	}
	s.material3D(0);
}

/// @brief 時刻 t の 1 コマまたは 1 標本を描く。t が映像の長さを超えたときは最後のコマを描く。
inline void drawFilm(Screen& s, const Film& f, const FilmAssets& assets, float t, const DrawOptions& opt = {})
{
	using namespace draw_detail;
	if (t > f.seq.duration) { t = f.seq.duration; }
	if (t < 0.0f) { t = 0.0f; }
	const edit::View view = edit::viewAt(f, t, opt.layer);
	if (!view.draw) { s.clear(sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f}); return; }
	const float tl = t;   // 字幕・暗転・タイトルは映像の時間。他はワールドの時間
	t = view.tWorld;
	const Film& cf = *view.cameraFilm;
	narrative::CameraShot cam = applyRig(cf, t, narrative::evaluateCamera(cf.seq, t, narrative::CameraShot{}));
	LensSample lens = evaluateLens(cf, t, cam.eye);
	const float fov = lens.hasFocal ? lens.fovDeg : cam.fovDeg;
	cam = applySample(cam, opt.offsets, fov, static_cast<float>(s.height()), lens);
	if (opt.aov != Aov::None) { drawAov(s, f, assets, t, opt, cam, fov, tl); return; }
	if (opt.pass != Pass::Beauty) { drawPass(s, f, assets, t, opt, cam, fov, tl); return; }
	const auto b = [](std::uint32_t c, int sh) { return static_cast<float>((c >> sh) & 0xFF) / 255.0f; };
	s.clear(sgc::Colorf{b(f.clearColor, 16), b(f.clearColor, 8), b(f.clearColor, 0), 1.0f});
	if (opt.accumulating) { lens.hasDof = false; }
	s.camera3D(cam.eye, cam.target, cam.up, fov, cam.nearDist, cam.farDist);
	render::SceneLook look = withLens(edit::lookFor(f, view.shot, evaluateLook(f, t)), lens);
	if (f.shadowPenumbraDeg > 0.0f)
	{
		// 太陽の向きと影の向きを同じ振りで動かす。標本の平均が半影になる (ADR 0150)
		const Vec3 sd = swungToward({look.shadowDirection[0], look.shadowDirection[1], look.shadowDirection[2]}, f.shadowPenumbraDeg,
		                            opt.offsets.sunX, opt.offsets.sunY);
		look.shadowDirection[0] = sd.x; look.shadowDirection[1] = sd.y; look.shadowDirection[2] = sd.z;
	}
	s.sceneLook3D(look);
	if (f.hasSky) { s.skybox3D(colorOf(f.skyZenith), colorOf(f.skyNadir)); }
	if (f.hasEnv) { s.environment3D(colorOf(f.envZenith), colorOf(f.envNadir)); }
	if (!f.envHdr.empty()) { s.environmentHdr3D(f.envHdr.c_str(), f.envHdrIntensity, Deg{f.envHdrYawDeg}); }
	drawLights(s, f, t, opt.offsets);
	for (std::size_t i = 0; i < f.props.size(); ++i) { drawProp(s, f.props[i], evaluateProp(f.props[i], t), assets, i, t, nullptr, tl); }
	drawOutdoorWorld(s, f, t);
	drawScatters(s, f, t);
	drawEmitters(s, f, t);
	drawOverlay(s, f, tl, view);
}

}  // namespace mitiru::film
