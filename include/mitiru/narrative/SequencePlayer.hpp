#pragma once

/// @file SequencePlayer.hpp
/// @brief カットシーンの再生。状態は SequencePlayback (flat POD、GameMemory に置く) の時刻だけで、
///        カメラ・役者・アニメ・字幕・覆いは時刻の関数として毎回求める。
/// @details 同じ時刻なら必ず同じ絵になるので、Rewind 窓で GameMemory を戻せばカットシーンもその時刻の姿に戻る。
///          合図は advance が「前の時刻から今の時刻まで」に入ったものを渡す。巻き戻した後に進め直せば同じ合図がもう一度出る。
///          飛ばす (skip) と、残りの call の合図と、最後の music の合図だけが届く (後の状態と BGM が飛ばさない時とそろう)。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/narrative/Sequence.hpp>

namespace mitiru::narrative
{

struct SequencePlayback
{
	std::uint32_t sequence = 0;   ///< 再生中の Sequence::id。0 は止まっている
	float         time = 0.0f;    ///< 秒
	std::uint8_t  state = 0;      ///< 0 = 止まっている、1 = 再生中、2 = 終わった
	std::uint8_t  skipped = 0;
	std::uint8_t  pad[2]{};

	[[nodiscard]] bool playing() const noexcept { return state == 1; }
	[[nodiscard]] bool finished() const noexcept { return state == 2; }
	[[nodiscard]] bool showing(const Sequence& seq) const noexcept { return state == 1 && sequence == seq.id; }
};
static_assert(std::is_trivially_copyable_v<SequencePlayback>);

/// @brief 描くカメラ。Screen::camera3D(eye, target, up, fovDeg, nearDist, farDist) にそのまま渡せる
struct CameraShot
{
	Vec3  eye{};
	Vec3  target{0.0f, 0.0f, 1.0f};
	Vec3  up{0.0f, 1.0f, 0.0f};
	float fovDeg = 55.0f;
	float nearDist = 0.1f;
	float farDist = 500.0f;
};

struct ActorPose
{
	Vec3  position{};
	float yawDeg = 0.0f;
};

/// @brief ある時刻に流れているアニメ 1 本。AnimLayer::clipAt(findClip(clip), time, weight) に渡す
struct ClipSample
{
	const SequenceClip* clip = nullptr;
	float               time = 0.0f;
	float               weight = 0.0f;
};

namespace seq_detail
{

[[nodiscard]] inline float smoothstep01(float x) noexcept
{
	const float t = action::saturate(x);
	return t * t * (3.0f - 2.0f * t);
}

/// @brief 鍵の列 keys の中で t を挟む添字 i (keys[i].t <= t < keys[i+1].t) と割合。端では端の鍵
template <class Key>
[[nodiscard]] std::size_t bracket(const std::vector<Key>& keys, float t, float& u) noexcept
{
	u = 0.0f;
	if (t <= keys.front().t) { return 0; }
	if (t >= keys.back().t) { return keys.size() - 1; }
	std::size_t i = 0;
	while (i + 1 < keys.size() && keys[i + 1].t <= t) { ++i; }
	const float span = keys[i + 1].t - keys[i].t;
	u = span > 1e-6f ? (t - keys[i].t) / span : 0.0f;
	return i;
}

/// @brief 鍵の時刻の間隔を見た Catmull-Rom (Hermite) で p1 と p2 の間を求める
template <class T>
[[nodiscard]] T hermite(const T& p0, const T& p1, const T& p2, const T& p3, float t0, float t1, float t2, float t3, float u)
{
	const float span = t2 - t1;
	const T m1 = (p2 - p0) * (span / action::maxf(t2 - t0, 1e-6f));
	const T m2 = (p3 - p1) * (span / action::maxf(t3 - t1, 1e-6f));
	const float u2 = u * u, u3 = u2 * u;
	return p1 * (2.0f * u3 - 3.0f * u2 + 1.0f) + m1 * (u3 - 2.0f * u2 + u) + p2 * (-2.0f * u3 + 3.0f * u2) + m2 * (u3 - u2);
}

/// @brief keys の値 (get で取り出す) を t で補間する
template <class Key, class Get>
[[nodiscard]] auto sampleKeys(const std::vector<Key>& keys, float t, bool smooth, Get get)
{
	float u = 0.0f;
	const std::size_t i = bracket(keys, t, u);
	if (u <= 0.0f || i + 1 >= keys.size()) { return get(keys[i]); }
	if (!smooth) { return get(keys[i]) + (get(keys[i + 1]) - get(keys[i])) * u; }
	const Key& k0 = keys[i > 0 ? i - 1 : i];
	const Key& k3 = keys[i + 2 < keys.size() ? i + 2 : i + 1];
	return hermite(get(k0), get(keys[i]), get(keys[i + 1]), get(k3), k0.t, keys[i].t, keys[i + 1].t, k3.t, u);
}

[[nodiscard]] inline Vec3 rolledUp(const Vec3& eye, const Vec3& target, float rollDeg) noexcept
{
	const Vec3 f = action::normalizeOr(target - eye, Vec3{0.0f, 0.0f, 1.0f});
	const Vec3 base = (f.y > 0.999f || f.y < -0.999f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
	const Vec3 right = action::normalizeOr(action::cross(f, base), Vec3{1.0f, 0.0f, 0.0f});
	const Vec3 up0 = action::cross(right, f);
	const float r = rollDeg * action::kDegToRad;
	return up0 * std::cos(r) + right * std::sin(r);
}

[[nodiscard]] inline CameraShot cameraAt(const SequenceCamera& cam, float t)
{
	CameraShot s;
	s.eye = sampleKeys(cam.keys, t, cam.smooth, [](const CameraKey& k) { return k.eye; });
	s.target = sampleKeys(cam.keys, t, cam.smooth, [](const CameraKey& k) { return k.target; });
	s.fovDeg = sampleKeys(cam.keys, t, false, [](const CameraKey& k) { return k.fovDeg; });
	s.up = rolledUp(s.eye, s.target, sampleKeys(cam.keys, t, false, [](const CameraKey& k) { return k.rollDeg; }));
	s.nearDist = cam.nearDist;
	s.farDist = cam.farDist;
	return s;
}

[[nodiscard]] inline CameraShot mix(const CameraShot& a, const CameraShot& b, float w) noexcept
{
	CameraShot s;
	s.eye = action::lerp(a.eye, b.eye, w);
	s.target = action::lerp(a.target, b.target, w);
	s.up = action::normalizeOr(action::lerp(a.up, b.up, w), b.up);
	s.fovDeg = action::lerpf(a.fovDeg, b.fovDeg, w);
	s.nearDist = action::lerpf(a.nearDist, b.nearDist, w);
	s.farDist = action::lerpf(a.farDist, b.farDist, w);
	return s;
}

[[nodiscard]] inline int segmentAt(const Sequence& seq, float t) noexcept
{
	for (std::size_t i = 0; i < seq.segments.size(); ++i)
	{
		if (seq.segments[i].start <= t && t < seq.segments[i].end) { return static_cast<int>(i); }
	}
	return -1;
}

/// @brief 区間 segIndex の続きとして時刻 t のカメラを求める。替わり目の途中でまた替わったら、前の混ぜ途中の姿から混ぜる
[[nodiscard]] inline CameraShot shotFrom(const Sequence& seq, int segIndex, float t, const CameraShot& gameplay, int depth)
{
	if (segIndex < 0) { return gameplay; }
	const CameraSegment& seg = seq.segments[static_cast<std::size_t>(segIndex)];
	const CameraShot here = seg.winner >= 0 ? cameraAt(seq.cameras[static_cast<std::size_t>(seg.winner)], t) : gameplay;
	const float blend = seg.winner >= 0 ? seq.cameras[static_cast<std::size_t>(seg.winner)].blendIn : seq.blendOut;
	const float since = t - seq.segments[static_cast<std::size_t>(seg.runFirst)].start;
	if (blend <= 0.0f || since >= blend || depth >= 4) { return here; }
	const int before = seg.runFirst - 1;
	const CameraShot from = before >= 0 ? shotFrom(seq, before, t, gameplay, depth + 1) : gameplay;
	return mix(from, here, smoothstep01(since / blend));
}

/// @brief 最後の区間の後 (カメラが全部終わった後) に、ゲームのカメラへ blendOut 秒かけて戻す
[[nodiscard]] inline CameraShot afterLastCamera(const Sequence& seq, float t, const CameraShot& gameplay)
{
	const CameraSegment& last = seq.segments.back();
	const float since = t - last.end;
	if (last.winner < 0 || seq.blendOut <= 0.0f || since >= seq.blendOut) { return gameplay; }
	const CameraShot from = shotFrom(seq, static_cast<int>(seq.segments.size()) - 1, last.end, gameplay, 0);
	return mix(from, gameplay, smoothstep01(since / seq.blendOut));
}

[[nodiscard]] inline float sampleFloat(const std::vector<FloatKey>& keys, float t) noexcept
{
	if (keys.empty()) { return 0.0f; }
	return sampleKeys(keys, t, false, [](const FloatKey& k) { return k.v; });
}

}  // namespace seq_detail

/// @brief 再生を始める (時刻 0)。合図は最初の advance で時刻 0 のものから届く
inline void playSequence(SequencePlayback& pb, const Sequence& seq) noexcept
{
	pb = SequencePlayback{};
	pb.sequence = seq.id;
	pb.state = 1;
}

/// @brief 時刻を dt 進め、前の時刻 <= t < 今の時刻 の合図を onCue(const Cue&) へ渡す。終わりに着いた回は終わりの合図も渡す
template <class OnCue>
void advanceSequence(SequencePlayback& pb, const Sequence& seq, float dt, OnCue&& onCue)
{
	if (!pb.showing(seq)) { return; }
	const float prev = pb.time;
	const float now = action::minf(prev + action::maxf(dt, 0.0f), seq.duration);
	const bool reachedEnd = now >= seq.duration;
	for (const SequenceCue& c : seq.cues)
	{
		if (c.t >= prev && (c.t < now || (reachedEnd && c.t <= now))) { onCue(c.cue); }
	}
	pb.time = now;
	if (reachedEnd) { pb.state = 2; }
}

/// @brief 終わりまで飛ばす。残りの call の合図と、残りの中で最後の music の合図だけを渡す。飛ばせない台本なら何もしない
template <class OnCue>
void skipSequence(SequencePlayback& pb, const Sequence& seq, OnCue&& onCue)
{
	if (!pb.showing(seq) || !seq.skippable) { return; }
	const Cue* lastMusic = nullptr;
	for (const SequenceCue& c : seq.cues)
	{
		if (c.t < pb.time) { continue; }
		if (c.cue.kind == CueKind::Call) { onCue(c.cue); }
		if (c.cue.kind == CueKind::Music) { lastMusic = &c.cue; }
	}
	if (lastMusic != nullptr) { onCue(*lastMusic); }
	pb.time = seq.duration;
	pb.state = 2;
	pb.skipped = 1;
}

/// @brief 時刻 t のカメラ。カメラの無い時刻は gameplay (ゲームが普段使うカメラ) を返す
[[nodiscard]] inline CameraShot evaluateCamera(const Sequence& seq, float t, const CameraShot& gameplay)
{
	if (seq.segments.empty() || t < seq.segments.front().start) { return gameplay; }
	if (t >= seq.segments.back().end) { return seq_detail::afterLastCamera(seq, t, gameplay); }
	return seq_detail::shotFrom(seq, seq_detail::segmentAt(seq, t), t, gameplay, 0);
}

/// @brief prev から now の間で、混ぜずにカメラが切り替わったか (hud.cameraCut() を呼ぶ合図。TAA が前の絵を引きずらない)
[[nodiscard]] inline bool cameraCutBetween(const Sequence& seq, float prev, float now) noexcept
{
	const int a = seq_detail::segmentAt(seq, prev), b = seq_detail::segmentAt(seq, now);
	if (b < 0 || a == b) { return false; }
	const CameraSegment& sb = seq.segments[static_cast<std::size_t>(b)];
	if (a >= 0 && seq.segments[static_cast<std::size_t>(a)].runFirst == sb.runFirst) { return false; }
	const float blend = sb.winner >= 0 ? seq.cameras[static_cast<std::size_t>(sb.winner)].blendIn : seq.blendOut;
	return blend <= 0.0f;
}

[[nodiscard]] inline ActorPose evaluateActor(const Sequence& seq, int actor, float t)
{
	const SequenceActor& a = seq.actors[static_cast<std::size_t>(actor)];
	ActorPose p;
	p.position = seq_detail::sampleKeys(a.keys, t, a.smooth, [](const ActorKey& k) { return k.position; });
	p.yawDeg = seq_detail::sampleKeys(a.keys, t, false, [](const ActorKey& k) { return k.yawDeg; });
	return p;
}

/// @brief 役者 actor に時刻 t で流れているアニメを out へ書き、本数を返す (書いた順、cap まで)
inline int evaluateClips(const Sequence& seq, int actor, float t, ClipSample* out, int cap) noexcept
{
	int n = 0;
	for (const SequenceClip& c : seq.clips)
	{
		if (c.actor != actor || t < c.start || t >= c.end || n >= cap) { continue; }
		const float in = c.blendIn > 0.0f ? (t - c.start) / c.blendIn : 1.0f;
		const float outW = c.blendOut > 0.0f ? (c.end - t) / c.blendOut : 1.0f;
		out[n++] = {&c, c.offset + (t - c.start) * c.speed, action::saturate(action::minf(in, outW))};
	}
	return n;
}

/// @brief 時刻 t の字幕 (重なったら後に書いたもの)。無ければ nullptr
[[nodiscard]] inline const Subtitle* subtitleAt(const Sequence& seq, float t) noexcept
{
	const Subtitle* found = nullptr;
	for (const Subtitle& s : seq.subtitles)
	{
		if (s.start <= t && t < s.end) { found = &s; }
	}
	return found;
}

[[nodiscard]] inline float fadeAt(const Sequence& seq, float t) noexcept { return seq_detail::sampleFloat(seq.fade, t); }
[[nodiscard]] inline float letterboxAt(const Sequence& seq, float t) noexcept { return seq_detail::sampleFloat(seq.letterbox, t); }

/// @brief 時刻 t に続いている vfx の合図を fn(const Cue&, float age) へ渡す (描く側が経過秒から姿を作る)
template <class Fn>
void forEachActiveCue(const Sequence& seq, float t, Fn&& fn)
{
	for (const SequenceCue& c : seq.cues)
	{
		if (c.cue.kind == CueKind::Vfx && c.t <= t && t < c.t + c.cue.duration) { fn(c.cue, t - c.t); }
	}
}

}  // namespace mitiru::narrative
