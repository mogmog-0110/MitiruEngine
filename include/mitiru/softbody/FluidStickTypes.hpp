#pragma once

/// @file FluidStickTypes.hpp
/// @brief 流れる体の粘着 (面に張りついて垂れ、剥がれるときに糸を引く) と、掴んだ物へ伸びる綱の入力の型。
/// @details FluidBodyState、FluidParams、FluidStepInput には何も足さない。粘着の状態 (FluidStickState) は呼び手が持つ POD で、
///          GameMemory に置けば巻き戻しと記録／再生が効く。使うときは FluidStepInput::stick に FluidStickLink を渡す。
///          渡さなければ (nullptr) 今までと 1 bit も変わらない。動きは detail/FluidStick.hpp。

#include <cstdint>

#include <mitiru/softbody/FluidShapes.hpp>

namespace mitiru::softbody
{

/// @brief 粘着と垂れ、床の広がり、形の遅い戻り、綱の調整値。stickReach が 0 なら粘着は全部切り、tetherPull が 0 なら綱の入力は無視する。
///        既定は、遊んで確かめた試作の値。糸の形の調整値 (stickSegments から stickNeckShift) の既定は FluidThreadParams と同じ
struct FluidStickParams
{
	float stickReach = 0.0f;         ///< 糸が切れる長さ (m)。実物に合わせて決める調整値 1
	float stickNeck = 0.04f;         ///< 切れる直前のくびれの半径 (m)。調整値 2 (細り方)
	float stickPull = 0.5f;          ///< 切れる直前に器を糸の側へ伸ばす量。調整値 3 (伸び)
	float stickSpeed = 5.0f;         ///< 面に沿う速さがこれを超えると粘着が弱まる (m/s)
	float stickSlide = 0.3f;         ///< 張りついている間、止まった所と体の足元が離れてよい長さ (m)
	float stickSlack = 0.2f;         ///< 糸がたるむ長さ (m)。これを超えると張る
	float stickRoot = 0.2f;          ///< 糸の体側の半径 (m)
	float stickFoot = 0.13f;         ///< 糸の面側の濡れ跡の半径 (m)
	float stickThinPower = 1.4f;     ///< 張るにつれてくびれが細る速さ (大きいほど切れる間際まで太い)
	float stickRecoilTime = 0.2f;    ///< 切れた糸が体へ戻る時間 (秒)
	float stickPinHz = 2.0f;         ///< 面で止まった所のパーティクルを面へ引くばねの周波数
	float stickPinRadius = 0.26f;    ///< 止まった所のパーティクルを引く半径 (m)
	float stickSmooth = 0.0f;        ///< 糸を細かく分け、くびれを張りで足側へ寄せて先に雫を付け、ゆっくりうねらせる割合 (0 は素直な形)
	float stickBulb = 0.0f;          ///< 張った糸の先の雫の膨らみ (足側の半径に対する割合)
	float stickDrag = 0.0f;          ///< 糸が張っている間、面に近い側のパーティクルを面へ引く加速度 (m/s²)
	float stickSegments = 0.0f;      ///< 糸の分け数 (3 以上。0 は stickSmooth が 0 なら 5、それ以外は 9)
	float stickDroop = 0.25f;        ///< 張っていない糸の真ん中が下へ垂れる量 (糸の長さに対する割合)
	float stickDroopPower = 1.0f;    ///< 垂れが張りで消える曲がり (大きいほど張る間際まで垂れが残る)
	float stickDroopFloor = 0.0f;    ///< 張り切っても残る垂れ (stickDroop に対する割合。0 なら張ると真っすぐになる)
	float stickWobble = 0.05f;       ///< 張った糸の横揺れの振れ幅 (糸の長さに対する割合)
	float stickSway = 0.06f;         ///< stickSmooth が効いているときの、ゆるい糸の横揺れの振れ幅 (糸の長さに対する割合)
	float stickRootPower = 1.6f;     ///< 体側からくびれへの太さの落ち方の指数 (大きいほど根元が長く太い)
	float stickFootPower = 2.4f;     ///< くびれから面側への太さの戻り方の指数
	float stickNeckShift = 0.18f;    ///< stickSmooth が効いているとき、張りでくびれが面側へ寄る量
	float sagMax = 0.0f;             ///< 壁・天井で器が自重で垂れる長さ (m)
	float sagGrowTime = 0.9f;
	float sagReleaseTime = 0.4f;
	float sagGravity = 0.12f;        ///< 垂れている間にパーティクルへ足す重力の割合
	float spreadFlatten = 0.0f;      ///< 床で止まったときに、ゆっくり広がってつぶれる割合
	float spreadGrowTime = 0.8f;
	float spreadReleaseTime = 0.5f;
	float tetherPull = 0.0f;         ///< 張りと引かれる割合が 1 のときに器を物の側へ伸ばす量 (stickPull と同じ尺度)
	float tetherLean = 0.15f;        ///< 軽い物でも体が物へ傾く分。引かれる割合の下限
	float tetherGrowTime = 0.08f;
	float tetherReleaseTime = 0.35f;
	float slowMix = 0.0f;            ///< 形の戻りのうち、遅い成分の割合 (0 は速い成分だけ)
	float slowTime = 0.7f;           ///< 遅い成分の戻りの時間 (秒)
};

/// @brief 掴んだ物へ伸びる綱の、その tick の入力。体はその向きへ伸び、張りが大きく体の側が引かれるほど大きく伸びる
struct FluidTether
{
	Vec3 toward{0.0f, 1.0f, 0.0f};   ///< 体から物へ向かう単位ベクトル
	float tension = 0.0f;            ///< 綱の張り (0 から 1)
	float load = 0.0f;               ///< 体の側が引かれる割合 (0 から 1)。重い物ほど 1 に近い
	std::uint8_t active = 0;         ///< 0 なら綱は無く、伸びは時間をかけて戻る
	std::uint8_t pad[3]{};
};

/// @brief 粘着の状態。時間とともに進む値だけで、毎 tick の入力は持たない。reset はこの型の既定値を代入する
struct FluidStickState
{
	std::uint8_t phase = 0;          ///< 0 は糸なし、1 は面に止まった所がある、2 は切れた糸が戻っている
	std::uint8_t pad[3]{};
	float grip = 0.0f;               ///< 面にくっつく強さ (0 から 1)。ゆっくりの間だけ高い
	float tension = 0.0f;            ///< 糸の張り (0 から 1)
	float pullAmount = 0.0f;         ///< 器を糸の側へ伸ばしている量 (時間をかけて追う)
	float recoil = 0.0f;             ///< 切れた糸の太さの残り (1 から 0)
	float away = 0.0f;               ///< 体が止まった所から離れている度合い (0 から 1)。近づくときは器を縮め、パーティクルを引かない
	Vec3 anchor{};                   ///< 面で止まった所
	Vec3 normal{0.0f, 1.0f, 0.0f};   ///< その面の外向き
	Vec3 tip{};                      ///< 切れた糸の先
	Vec3 dir{0.0f, 1.0f, 0.0f};      ///< 止まった所から体へ向かう向き
	float sag = 0.0f;                ///< 器が自重で垂れている量 (m)
	float spread = 0.0f;             ///< 床でゆっくり広がっている量 (0 から 1)
	float squashSlow = 0.0f;         ///< つぶれの、戻りの遅い成分
	float stretchSlow = 0.0f;        ///< 伸びの、戻りの遅い成分
	float tetherAmount = 0.0f;       ///< 綱の向きへ器を伸ばしている量 (時間をかけて追う)
	float tetherSlow = 0.0f;         ///< その、戻りの遅い成分
	Vec3 tetherToward{0.0f, 1.0f, 0.0f};   ///< 最後に綱があった向き
};

/// @brief FluidStepInput::stick に渡す、粘着の一式。params と state は呼び手のもので、tether はその tick の入力
struct FluidStickLink
{
	const FluidStickParams* params = nullptr;
	FluidStickState* state = nullptr;
	FluidTether tether{};
};

} // namespace mitiru::softbody
