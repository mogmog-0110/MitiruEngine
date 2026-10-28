#pragma once

/// @file VirtualCameraStack.hpp
/// @brief ゲーム中の 3D の仮想カメラ。複数の視点を priority で選び、勝者が替わったら blend で混ぜる。
/// @details 状態 VirtualCameraStack は flat POD で GameMemory に置く。混ぜの途中の姿も同じ struct に入れるので、
///          巻き戻すと切り替えの途中のフレームに戻る。時間は resolve に渡す固定刻みの dt だけで進み、実時間を読まない。
///          勝者の選び方と曲線はカットシーン (narrative/Sequence) と同じ camera/CameraPriority.hpp。
///          ゲームは毎 tick、各カメラの視点を setView で書き、出番のあるものを setActive で立ててから resolve を呼び、
///          戻りの CameraView を Screen::camera3D に渡す。cut が立っていたら hud.cameraCut() を呼ぶ (TAA が前の絵を引きずらない)。

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

	/// @brief 壁の手前へ引くなど、resolve の後で直した視点を、映した映像として覚える。次に勝者が替わるとき、
	///        直す前の視点からでなく、画面に出ていた視点から混ぜ始める (切り替えで目が跳ばない)
	void commitShown(const CameraView& shown) noexcept { current = shown; }
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

/// @brief 勝者を選び、dt 進めた視点を返す。勝者が替わったフレームは、今の映像から勝者の blendSec かけて混ぜ始める
///        (混ぜの途中で替わっても、途中の姿から混ぜ直す)。映すものが無いときは最後の映像を保つ
template <int N>
[[nodiscard]] CameraView resolveVirtualCamera(VirtualCameraStack<N>& st, float dt) noexcept
{
	dt = std::isfinite(dt) ? action::clampf(dt, 0.0f, 0.1f) : 0.0f;
	st.cut = 0;
	const int w = pickWinner(
		N, [&](int i) { return st.cams[i].active != 0; }, [&](int i) { return st.cams[i].priority; });
	if (w < 0)
	{
		st.winner = -1;
		return st.current;
	}
	const VirtualCamera& cam = st.cams[w];
	if (st.initialized == 0)
	{
		st.initialized = 1;
		st.winner = static_cast<std::int8_t>(w);
		st.blendTotal = 0.0f;
		st.current = cam.view;
		return st.current;
	}
	if (w != st.winner)
	{
		const bool hard = cam.blendSec <= 0.0f || cam.curve == static_cast<std::uint8_t>(BlendCurve::Cut);
		st.blendFrom = st.current;
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
		st.current = mixCameraView(st.blendFrom, cam.view, weight);
	}
	else
	{
		st.current = cam.view;
	}
	return st.current;
}

}  // namespace mitiru::camera
