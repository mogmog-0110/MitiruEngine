#pragma once

/// @file VirtualCameraStack.hpp
/// @brief ゲーム中の 3D の仮想カメラ。複数の視点を priority で選び、勝者が替わったら blend で混ぜる。
/// @details 状態 VirtualCameraStack は flat POD で GameMemory に置く。混ぜの途中の姿も同じ struct に入れるので、
///          巻き戻すと切り替えの途中のフレームに戻る。時間は resolve に渡す固定刻みの dt だけで進み、実時間を読まない。
///          勝者の選び方と曲線はカットシーン (narrative/Sequence) と同じ camera/CameraPriority.hpp。
///          ゲームは毎 tick、各カメラの視点を setView で書き、出番のあるものを setActive で立ててから resolve を呼び、
///          戻りの CameraView を Screen::camera3D に渡す。cut が立っていたら hud.cameraCut() を呼ぶ (TAA が前の絵を引きずらない)。
///
///          アンカー基準 (VirtualCamera::anchored): 1 のカメラの view は eye と target をアンカー (動く体) からの差で持ち、
///          resolve が setAnchor の位置を足して世界の視点にする。混ぜの間も、アンカーに付いてくる側の視点はアンカーに付いたまま動くので、
///          体に対する相対の位置を保ったまま混ぜられる。anchored を 1 度も立てなければ従来と同じ結果になる。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/CameraRig.hpp>
#include <mitiru/camera/CameraPriority.hpp>

namespace mitiru::camera
{

using action::CameraView;
using action::Vec3;

struct VirtualCamera
{
	CameraView   view{};
	std::int16_t priority = 0;
	std::uint8_t active = 0;
	std::uint8_t curve = static_cast<std::uint8_t>(BlendCurve::Smooth);
	float        blendSec = 0.0f;   ///< このカメラが勝者になったとき、直前の映像からここへ混ぜる秒数。0 で切り替え
	std::uint8_t anchored = 0;      ///< 1 で view の eye と target を、VirtualCameraStack::setAnchor の位置からの差として読む
	std::uint8_t pad[3]{};
};

/// @brief 2 つの視点を割合 w で混ぜる。向きは eye / target / up を直線で混ぜ、rollDeg は混ぜた up から求め直す
///        (角度のまま混ぜると、±180 度をまたぐ傾きが逆回りになる)
[[nodiscard]] inline CameraView mixCameraView(const CameraView& a, const CameraView& b, float w) noexcept
{
	if (w >= 1.0f) { return b; }
	CameraView v;
	v.eye = action::lerp(a.eye, b.eye, w);
	v.target = action::lerp(a.target, b.target, w);
	v.fovDeg = action::lerpf(a.fovDeg, b.fovDeg, w);
	v.up = action::normalizeOr(action::lerp(a.up, b.up, w), b.up);
	v.rollDeg = action::cameraRollDeg(v.eye, v.target, v.up);
	return v;
}

template <int N = 8>
struct VirtualCameraStack
{
	static_assert(N > 0 && N <= 127);

	VirtualCamera cams[N]{};
	CameraView    current{};      ///< 直近の resolve の戻り (次の混ぜの起点)
	CameraView    blendFrom{};
	float         blendElapsed = 0.0f;
	float         blendTotal = 0.0f;
	std::int8_t   winner = -1;    ///< 直近の勝者の添字。-1 は映すものが無い
	std::uint8_t  blendCurve = 0;
	std::uint8_t  cut = 0;        ///< 直近の resolve で混ぜずに替わった (1)
	std::uint8_t  initialized = 0;
	Vec3          anchor{};       ///< 今の tick のアンカー (setAnchor)
	Vec3          shownAnchor{};  ///< current を作ったときのアンカー。勝者が替わるとき、前の映像をアンカーの動きに乗せ直すのに使う
	Vec3          blendAnchor{};  ///< blendFrom を取ったときのアンカー
	float         blendFollow = 0.0f;  ///< blendFrom が混ぜの間アンカーの動きに付いてくる度合い (0..1)。切り替えのとき、前の映像の度合いと勝者の度合いの大きい方
	float         shownFollow = 0.0f;  ///< current がアンカーの動きに付いてくる度合い。混ぜの途中は 2 つの視点の度合いを同じ重みで混ぜた値

	/// @brief 壁の手前へ引くなど、resolve の後で直した視点を、映した映像として覚える。次に勝者が替わるとき、
	///        直す前の視点からでなく、画面に出ていた視点から混ぜ始める (切り替えで目が跳ばない)
	void commitShown(const CameraView& shown) noexcept { current = shown; }
	/// @brief アンカーの世界の位置。resolve の前に毎 tick 書く。有限でない値は無視して前の位置を保つ
	void setAnchor(const Vec3& position) noexcept { if (action::isFinite(position)) { anchor = position; } }
	/// @brief 勝者のあいだに変えると、同じ view の読み方が替わって視点が跳ぶ。勝者になる前に決める
	void setAnchored(int id, bool on) noexcept { if (inRange(id)) { cams[id].anchored = on ? 1 : 0; } }
	void setView(int id, const CameraView& view) noexcept { if (inRange(id)) { cams[id].view = view; } }
	void setActive(int id, bool on) noexcept { if (inRange(id)) { cams[id].active = on ? 1 : 0; } }
	void configure(int id, int priority, float blendSec, BlendCurve curve = BlendCurve::Smooth) noexcept
	{
		if (!inRange(id)) { return; }
		cams[id].priority = static_cast<std::int16_t>(priority < -32768 ? -32768 : (priority > 32767 ? 32767 : priority));
		cams[id].blendSec = blendSec > 0.0f ? blendSec : 0.0f;
		cams[id].curve = static_cast<std::uint8_t>(curve);
	}

private:
	static constexpr bool inRange(int id) noexcept { return id >= 0 && id < N; }
};

namespace anchor_detail
{

[[nodiscard]] inline CameraView shifted(CameraView v, const Vec3& d) noexcept
{
	v.eye = v.eye + d;
	v.target = v.target + d;
	return v;
}

/// 0 のときは足し算もしない (0 を足すと -0 が +0 に変わり、アンカーを使わないときの戻りが従来とバイト単位でずれる)
[[nodiscard]] inline CameraView followed(const CameraView& v, const Vec3& from, const Vec3& to, float follow) noexcept
{
	return follow > 0.0f ? shifted(v, (to - from) * follow) : v;
}

}  // namespace anchor_detail

/// @brief 勝者を選び、dt 進めた視点を返す。勝者が替わったフレームは、今の映像から勝者の blendSec かけて混ぜ始める
///        (混ぜの途中で替わっても、途中の姿から混ぜ直す)。映すものが無いときは最後の映像を保つ
/// @details アンカー基準のカメラは view をアンカーからの差として読み、世界の視点にしてから混ぜる。切り替えの前後どちらかが
///          アンカー基準なら、混ぜの起点の映像も混ぜの間アンカーに付いて動く (切り替えの瞬間の位置は変わらない)。
///          映すものが無いあいだは映像を世界に置いたまま保つ
template <int N>
[[nodiscard]] CameraView resolveVirtualCamera(VirtualCameraStack<N>& st, float dt) noexcept
{
	namespace ad = anchor_detail;
	dt = std::isfinite(dt) ? action::clampf(dt, 0.0f, 0.1f) : 0.0f;
	st.cut = 0;
	const int w = pickWinner(
		N, [&](int i) { return st.cams[i].active != 0; }, [&](int i) { return st.cams[i].priority; });
	if (w < 0)
	{
		st.winner = -1;
		st.shownFollow = 0.0f;
		st.shownAnchor = st.anchor;
		return st.current;
	}
	const VirtualCamera& cam = st.cams[w];
	const float          toFollow = cam.anchored != 0 ? 1.0f : 0.0f;
	const CameraView     goal = cam.anchored != 0 ? ad::shifted(cam.view, st.anchor) : cam.view;
	if (st.initialized == 0)
	{
		st.initialized = 1;
		st.winner = static_cast<std::int8_t>(w);
		st.blendTotal = 0.0f;
		st.current = goal;
		st.shownFollow = toFollow;
		st.shownAnchor = st.anchor;
		return st.current;
	}
	if (w != st.winner)
	{
		const bool hard = cam.blendSec <= 0.0f || cam.curve == static_cast<std::uint8_t>(BlendCurve::Cut);
		st.blendFrom = ad::followed(st.current, st.shownAnchor, st.anchor, st.shownFollow);
		st.blendAnchor = st.anchor;
		st.blendFollow = action::maxf(st.shownFollow, toFollow);
		st.blendElapsed = 0.0f;
		st.blendTotal = hard ? 0.0f : cam.blendSec;
		st.blendCurve = cam.curve;
		st.winner = static_cast<std::int8_t>(w);
		st.cut = hard ? 1 : 0;
	}
	if (st.blendTotal > 0.0f && st.blendElapsed < st.blendTotal)
	{
		st.blendElapsed = action::minf(st.blendElapsed + dt, st.blendTotal);
		const float weight = blendWeight(static_cast<BlendCurve>(st.blendCurve), st.blendElapsed / st.blendTotal);
		const CameraView from = ad::followed(st.blendFrom, st.blendAnchor, st.anchor, st.blendFollow);
		st.current = mixCameraView(from, goal, weight);
		st.shownFollow = action::lerpf(st.blendFollow, toFollow, weight);
	}
	else
	{
		st.current = goal;
		st.shownFollow = toFollow;
	}
	st.shownAnchor = st.anchor;
	return st.current;
}

}  // namespace mitiru::camera
