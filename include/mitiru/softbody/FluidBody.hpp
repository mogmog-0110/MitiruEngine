#pragma once

/// @file FluidBody.hpp
/// @brief 輪郭が液体のように流れる体。粒を Position Based Fluids (Macklin と Müller 2013) で非圧縮に保ち、
///        芯の姿勢に付いた柔らかい器の外へ出た粒だけを内へ押す。
/// @details game DLL が自分でリンクして同じフレームの中で使う header-only の部品 (host は呼ばない)。
///          状態 (FluidBodyState) は固定長の POD なので GameMemory にそのまま置け、巻き戻しと記録／再生が効く。
///          作業領域 (FluidScratch) は毎回作り直す中身だけなので GameMemory の外 (DLL の static) に置く。
///          同じ入力なら同じビットになる。反復回数は固定で、近くの粒は粒の番号順に並べ、順序の決まらない容器は使わない。
///          仮足と手足はどちらも体から押し出す形 (FluidShape)。仮足の筒は器に足して粒を移し、
///          歩くときだけ生える手足は面に足す (粒を持たない)。ゲームが自分の形を器に足すこともできる。

#include <cstdint>

#include <mitiru/softbody/FluidParams.hpp>
#include <mitiru/softbody/FluidShapes.hpp>

namespace mitiru::softbody
{

inline constexpr int kFluidMaxParticles = 512;
inline constexpr int kFluidMaxPods = 2;
inline constexpr int kFluidMaxPodMembers = 256;
inline constexpr int kFluidMaxPlanes = 8;
inline constexpr int kFluidMaxShapes = 8;
inline constexpr int kFluidMaxLimbShapes = 16;

/// @brief 芯の状態。壁 (Wall) は壁を走っている間、張りつき (Stick) は面に留まっている間
enum class FluidMode : std::uint8_t { Ground, Air, Wall, Stick };

/// @brief 足の置き場所 (足元の座標系。x は使わず、左右は FluidParams::legSpacing で決まる)
struct FluidFoot
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float toeDeg = 0.0f;
};

/// @brief 腕の振り (前後) と上げ (度)
struct FluidArm
{
	float swingDeg = 0.0f;
	float raiseDeg = 0.0f;
};

/// @brief 芯と動きの層がその tick に決めた体の姿勢。体はこれを読むだけ
struct FluidPose
{
	Vec3 root{};                     ///< 足元の中心 (ワールド)
	Vec3 up{0.0f, 1.0f, 0.0f};
	Vec3 forward{0.0f, 0.0f, 1.0f};  ///< 顔の向き
	float leanDeg = 0.0f;            ///< 前へ倒れる角度
	float rollDeg = 0.0f;            ///< 横に倒れる角度 (+ で右が下がる)
	float lookYawDeg = 0.0f;
	float bob = 0.0f;                ///< 足元から体が浮く量 (m)
	float sway = 0.0f;               ///< 横へずれる量 (m)
	float height = 1.0f;             ///< 体の高さの倍率
	FluidFoot feet[2]{};             ///< 左、右
	FluidArm arms[2]{};              ///< 左、右
};

/// @brief 粒が入れない平面 (床・壁)。normal の側が外
struct FluidPlane
{
	Vec3 point{};
	Vec3 normal{0.0f, 1.0f, 0.0f};
};

/// @brief 粒や格子の行を分けて回す口 (任意)。run は job(ctx, 0) 〜 job(ctx, jobCount - 1) をすべて呼んでから戻る。
///        どの仕事も書く先が重ならないように分けてあり、分け方で結果のビットは変わらない
struct FluidParallel
{
	void* user = nullptr;
	void (*run)(void* user, int jobCount, void* ctx, void (*job)(void* ctx, int index)) = nullptr;
	int jobCount = 1;
};

/// @brief 平面より複雑な地形との当たり。positions を押し出して書き戻す。同じ入力に同じ答えを返すこと
using FluidCollideFn = void (*)(void* user, Vec3* positions, int count);

struct FluidStepInput
{
	FluidPose pose{};
	Vec3 coreVelocity{};             ///< 芯の速度 (粒の速さを寄せる先)
	FluidMode mode = FluidMode::Ground;
	std::uint8_t pad[3]{};
	float phase = 0.0f;              ///< 歩きの位相 (1 歩で 1 進む)。手足のばたつきと輪に使う
	float cadence = 0.0f;            ///< 1 秒の歩数
	const FluidPlane* planes = nullptr;
	int planeCount = 0;
	const FluidShape* shapes = nullptr;   ///< 器に足す形。kFluidMaxShapes まで
	int shapeCount = 0;
	FluidCollideFn collide = nullptr;
	void* collideUser = nullptr;
	const FluidParallel* parallel = nullptr;   ///< 粒を分けて回す口 (無ければ 1 本で回す)
};

enum class FluidPodState : std::uint8_t
{
	Off,
	Shoot,     ///< target へ伸びている
	Grab,      ///< 先が target に付いている
	Release,   ///< 体へ戻っている
};

/// @brief 仮足 1 本。体の表面から target へ伸びる筒で、粒が先から順に筒へ移る
struct FluidPod
{
	FluidPodState state = FluidPodState::Off;
	std::uint8_t pad[3]{};
	Vec3 target{};
	float length = 0.0f;
	float born = 0.0f;
	int memberCount = 0;
	std::uint16_t members[kFluidMaxPodMembers]{};   ///< 先から根元の順の粒の番号
};

/// @brief 体の状態のすべて (GameMemory に置ける POD)
struct FluidBodyState
{
	int count = 0;
	float rho0 = 1.0f;               ///< 休みの密度
	float t = 0.0f;                  ///< 体の時計 (秒)
	float landT = -10.0f;            ///< 最後に着地した時刻
	float brakeT = -10.0f;           ///< 最後に急停止した時刻
	float prevSpeed = 0.0f;
	float accelSmooth = 0.0f;        ///< ならした加速度 (m/s²)
	float stretch = 0.0f;            ///< 器を伸ばしている量 (時間をかけて追う)
	Vec3 moveLocal{};                ///< 体の座標系で、器を伸ばす向き (ゆっくり回す)
	Vec3 brakeForward{};             ///< 急停止で器をずらす向き
	float limbGrow = 0.0f;           ///< 手足の生えた量 (0〜1、生えるときに行き過ぎる)
	float limbGrowVelocity = 0.0f;
	FluidMode mode = FluidMode::Ground;
	std::uint8_t initialized = 0;
	std::uint8_t faceValid = 0;
	std::uint8_t pad = 0;
	Vec3 face{};                     ///< 目と口を置く点。粒の重心をばねで追う
	Vec3 faceVelocity{};
	Vec3 faceForward{0.0f, 0.0f, 1.0f};   ///< 顔の向き (張りついたときは面の外へ起こす)
	Vec3 faceUp{0.0f, 1.0f, 0.0f};
	Vec3 x[kFluidMaxParticles]{};
	Vec3 v[kFluidMaxParticles]{};
	Vec3 xStepStart[kFluidMaxParticles]{};   ///< 直前の fluidStep を始めたときの位置 (面の前フレームの位置に使う)
	std::uint8_t inPod[kFluidMaxParticles]{};
	FluidPod pods[kFluidMaxPods]{};
	FluidShape limbs[kFluidMaxLimbShapes]{};       ///< 面に足す手足 (ワールド)
	FluidShape limbsBefore[kFluidMaxLimbShapes]{}; ///< 直前の fluidStep の前の手足 (面の前フレームの位置に使う)
	int limbCount = 0;
	int limbCountBefore = 0;
};

} // namespace mitiru::softbody

#include <mitiru/softbody/detail/FluidScratch.hpp>
#include <mitiru/softbody/detail/FluidContainer.hpp>
#include <mitiru/softbody/detail/FluidLimbs.hpp>
#include <mitiru/softbody/detail/FluidSolver.hpp>
#include <mitiru/softbody/detail/FluidStep.hpp>
