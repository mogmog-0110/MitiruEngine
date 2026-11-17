#pragma once

/// @file FilmEval.hpp
/// @brief 時刻 t の小道具・光・ルック・レンズを求める純関数。描画 (FilmDraw.hpp) とは分けてある。

#include <cmath>

#include <mitiru/film/FilmTypes.hpp>
#include <mitiru/film/FilmLook.hpp>
#include <mitiru/film/FilmSampling.hpp>
#include <mitiru/narrative/SequencePlayer.hpp>

namespace mitiru::film
{

/// @brief 軌跡を時刻 t で補間する。空なら fallback、範囲外なら端の値を返す。
template <class T>
[[nodiscard]] T sampleTrack(const Track<T>& track, float t, const T& fallback)
{
	if (track.empty()) { return fallback; }
	return narrative::seq_detail::sampleKeys(track.keys, t, track.smooth, [](const TKey<T>& k) { return k.v; });
}

struct PropState
{
	bool  visible = true;
	Vec3  pos{};
	Vec3  rotDeg{};
	Vec3  scale{1.0f, 1.0f, 1.0f};
	Vec3  color{1.0f, 1.0f, 1.0f};
	float alpha = 1.0f;
};

[[nodiscard]] inline PropState evaluateProp(const Prop& p, float t)
{
	PropState s;
	s.pos = sampleTrack(p.posTrack, t, p.pos);
	s.rotDeg = sampleTrack(p.rotTrack, t, p.rotDeg);
	s.scale = sampleTrack(p.scaleTrack, t, p.scale);
	s.color = sampleTrack(p.colorTrack, t, p.color);
	s.alpha = sampleTrack(p.alphaTrack, t, p.alpha);
	s.visible = sampleTrack(p.visibleTrack, t, 1.0f) > 0.5f;
	return s;
}

/// @brief ある時刻に再生中のアニメ 1 本。index は Prop::clips の添字。
struct PropClipSample
{
	std::size_t index = 0;
	float       time = 0.0f;
	float       weight = 0.0f;
};

/// @brief 時刻 t で再生中の区間を out へ cap 本まで書き、本数を返す。重みは blendIn と blendOut による直線補間。
inline int evaluatePropClips(const Prop& p, float t, PropClipSample* out, int cap) noexcept
{
	int n = 0;
	for (std::size_t i = 0; i < p.clips.size() && n < cap; ++i)
	{
		const PropClip& c = p.clips[i];
		if (t < c.start || t >= c.end) { continue; }
		const float in = c.blendIn > 0.0f ? (t - c.start) / c.blendIn : 1.0f;
		const float outW = c.blendOut > 0.0f ? (c.end - t) / c.blendOut : 1.0f;
		const float w = in < outW ? in : outW;
		out[n++] = {i, c.offset + (t - c.start) * c.speed, w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w)};
	}
	return n;
}

struct LightState
{
	Vec3  pos{};
	Vec3  dir{0.0f, -1.0f, 0.0f};
	Vec3  color{1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 10.0f;
};

[[nodiscard]] inline LightState evaluateLight(const Light& l, float t)
{
	LightState s;
	s.pos = sampleTrack(l.posTrack, t, l.pos);
	s.dir = sampleTrack(l.dirTrack, t, l.dir);
	s.color = sampleTrack(l.colorTrack, t, l.color);
	s.intensity = sampleTrack(l.intensityTrack, t, l.intensity);
	s.range = sampleTrack(l.rangeTrack, t, l.range);
	return s;
}

[[nodiscard]] inline render::SceneLook evaluateLook(const Film& f, float t)
{
	render::SceneLook look = f.look;
	for (const auto& track : f.lookTracks)
	{
		const LookField* fld = findLookField(track.first);
		if (fld != nullptr && !track.second.empty()) { setLookScalar(look, *fld, narrative::seq_detail::sampleFloat(track.second, t)); }
	}
	return look;
}

[[nodiscard]] inline float verticalFovDeg(float focalMm, float sensorHeightMm) noexcept
{
	return 2.0f * std::atan(sensorHeightMm / (2.0f * focalMm)) * 180.0f / 3.14159265f;
}

/// @brief 無限遠の点の錯乱円から、ぼけの設定を求める。描画側はピント距離より奥だけをぼかすため、手前はぼけない。
/// @param focusDist ワールドの 1 単位を 1 m とした距離
/// @param outputHeightPx ぼけの半径の単位。SceneLook では 720p の画素。
inline void applyDof(LensSample& out, float focalMm, float fstop, float focusDist, float sensorHeightMm, float outputHeightPx = 720.0f)
{
	const float s = focusDist * 1000.0f;
	if (!(s > focalMm * 1.01f) || !(fstop > 0.0f)) { return; }
	const float cocMm = (focalMm * focalMm) / (fstop * (s - focalMm));
	const float diameterPx = cocMm / sensorHeightMm * outputHeightPx;
	out.hasDof = true;
	out.dofStart = focusDist;
	out.dofEnd = focusDist * 4.0f;
	out.dofStrengthPx = 0.75f * 0.5f * diameterPx;
}

/// @brief focusOn の被写体までの距離。racks があれば、前の被写体までの距離から、次の被写体までの距離へ over 秒で (smoothstep) 動く。
[[nodiscard]] inline float followedFocusDistance(const Film& f, const CameraRig& rig, float t, const Vec3& eye)
{
	const auto distanceTo = [&](int index, const Vec3& offset) {
		const Vec3 target = evaluateProp(f.props[static_cast<std::size_t>(index)], t).pos + offset;
		return action::maxf(action::length(target - eye), 0.05f);
	};
	float focus = distanceTo(rig.focusIndex, rig.focusOffset);
	for (const FocusRack& r : rig.racks)
	{
		if (t < r.t) { break; }
		const float u = r.over > 0.0f ? action::minf((t - r.t) / r.over, 1.0f) : 1.0f;
		const float to = distanceTo(r.index, r.offset);
		if (u >= 1.0f) { focus = to; continue; }
		focus += (to - focus) * (u * u * (3.0f - 2.0f * u));
		break;
	}
	return focus;
}

/// @brief 時刻 t に映るカメラのレンズ。カメラの鍵に focal がなければ、fov は鍵の値を保ち、hasFocal は false になる。
[[nodiscard]] inline LensSample evaluateLens(const Film& f, float t, const Vec3& eye = {}, float fallbackFocal = 35.0f)
{
	LensSample out;
	const int seg = narrative::seq_detail::segmentAt(f.seq, t);
	if (seg < 0) { return out; }
	const int cam = f.seq.segments[static_cast<std::size_t>(seg)].winner;
	if (cam < 0 || static_cast<std::size_t>(cam) >= f.lenses.size()) { return out; }
	const Lens& lens = f.lenses[static_cast<std::size_t>(cam)];
	out.evBias = sampleTrack(lens.evBias, t, 0.0f);
	out.distortion = sampleTrack(lens.distortion, t, 0.0f);
	const float focal = sampleTrack(lens.focalMm, t, fallbackFocal);
	if (!lens.focalMm.empty())
	{
		out.hasFocal = true;
		out.fovDeg = verticalFovDeg(focal, lens.sensorHeightMm);
	}
	const CameraRig* rig = static_cast<std::size_t>(cam) < f.rigs.size() ? &f.rigs[static_cast<std::size_t>(cam)] : nullptr;
	const bool followFocus = rig != nullptr && rig->focusIndex >= 0;
	if (!lens.fstop.empty() && (!lens.focusDist.empty() || followFocus))
	{
		const float fstop = sampleTrack(lens.fstop, t, 2.8f);
		float focus = sampleTrack(lens.focusDist, t, 5.0f);
		if (followFocus)
		{
			focus = followedFocusDistance(f, *rig, t, eye);
		}
		applyDof(out, focal, fstop, focus, lens.sensorHeightMm);
		out.focusDist = focus;
		out.apertureRadius = focal / (2.0f * fstop) / 1000.0f;
	}
	return out;
}

/// @brief 手ぶれの 1 軸分。周波数 f と 2.31 f の正弦を足して -1 から 1 に収め、軸と seed で位相をずらす。
[[nodiscard]] inline float shakeAxis(const CameraShake& s, float t, unsigned axis) noexcept
{
	const float p1 = 6.2831853f * static_cast<float>((s.seed * 2654435761u + axis * 40503u) % 1000u) / 1000.0f;
	const float p2 = 6.2831853f * static_cast<float>((s.seed * 2246822519u + axis * 69069u) % 1000u) / 1000.0f;
	return 0.62f * std::sin(6.2831853f * s.freq * t + p1) + 0.38f * std::sin(6.2831853f * s.freq * 2.31f * t + p2);
}

/// @brief カメラに、小道具を見続ける lookAt と手ぶれを重ねる。
[[nodiscard]] inline narrative::CameraShot applyRig(const Film& f, float t, narrative::CameraShot cam)
{
	const int seg = narrative::seq_detail::segmentAt(f.seq, t);
	if (seg < 0) { return cam; }
	const int idx = f.seq.segments[static_cast<std::size_t>(seg)].winner;
	if (idx < 0 || static_cast<std::size_t>(idx) >= f.rigs.size()) { return cam; }
	const CameraRig& rig = f.rigs[static_cast<std::size_t>(idx)];
	if (rig.lookAtIndex >= 0)
	{
		cam.target = evaluateProp(f.props[static_cast<std::size_t>(rig.lookAtIndex)], t).pos + rig.lookAtOffset;
	}
	if (rig.shake.active())
	{
		const Vec3 fwd = action::normalizeOr(cam.target - cam.eye, Vec3{0.0f, 0.0f, 1.0f});
		const Vec3 right = action::normalizeOr(action::cross(fwd, cam.up), Vec3{1.0f, 0.0f, 0.0f});
		const Vec3 up = action::cross(right, fwd);
		const float dist = action::maxf(action::length(cam.target - cam.eye), 0.01f);
		const Vec3 move = right * (shakeAxis(rig.shake, t, 0) * rig.shake.posAmp) + up * (shakeAxis(rig.shake, t, 1) * rig.shake.posAmp);
		const float k = 3.14159265f / 180.0f * rig.shake.rotAmpDeg;
		const Vec3 dir = action::normalizeOr(fwd + right * (shakeAxis(rig.shake, t, 2) * k) + up * (shakeAxis(rig.shake, t, 3) * k), fwd);
		cam.eye = cam.eye + move;
		cam.target = cam.eye + dir * dist;
	}
	return cam;
}

/// @brief 画面のずれは、視線を画素に対応する角度だけ回す。レンズのずれは視点を口径の円板上で動かし、ピント距離の点を見続ける。これによりピント面は動かず、手前と奥がぼける。
[[nodiscard]] inline narrative::CameraShot applySample(narrative::CameraShot cam, const SampleOffsets& o, float fovDeg, float heightPx,
                                                       const LensSample& lens)
{
	using action::cross;
	using action::normalizeOr;
	const Vec3 fwd = normalizeOr(cam.target - cam.eye, Vec3{0.0f, 0.0f, 1.0f});
	const Vec3 right = normalizeOr(cross(fwd, cam.up), Vec3{1.0f, 0.0f, 0.0f});
	const Vec3 up = cross(right, fwd);
	const float dist = action::maxf(action::length(cam.target - cam.eye), 0.01f);
	Vec3 aim = cam.eye + fwd * dist;
	if (lens.focusDist > 0.0f && lens.apertureRadius > 0.0f)
	{
		aim = cam.eye + fwd * lens.focusDist;
		cam.eye = cam.eye + right * (o.apertureX * lens.apertureRadius) + up * (o.apertureY * lens.apertureRadius);
	}
	const Vec3 toAim = aim - cam.eye;
	const float aimDist = action::maxf(action::length(toAim), 0.01f);
	const Vec3 dir = normalizeOr(toAim, fwd);
	const float pixelAngle = fovDeg * 3.14159265f / 180.0f / heightPx;
	const Vec3 jittered = normalizeOr(dir + right * (o.jitterX * pixelAngle) + up * (-o.jitterY * pixelAngle), dir);
	cam.target = cam.eye + jittered * aimDist;
	return cam;
}

}  // namespace mitiru::film
