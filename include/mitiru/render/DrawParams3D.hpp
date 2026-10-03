#pragma once

/// @file DrawParams3D.hpp
/// @brief 3D 描画 1 回ぶんの追加の指定 (色の調整、インスタンス、スキンモデルの姿勢)。どれも POD

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/ColorSpace.hpp>

namespace mitiru::render
{

/// @brief 描画 1 回ぶんの色の調整
/// @details mul は材質の色 (基本色と頂点色) に掛ける。1 を超えてもよい (HDR)。
///          add は照明とフォグの前に足す光の色で、被弾の白い点滅のように照明に関係なく明るくするのに使う。
struct DrawTint
{
	float mul[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float add[3] = {0.0f, 0.0f, 0.0f};
	float reserved = 0.0f;
};

/// @brief インスタンス描画の 1 個ぶん
/// @details world は行優先 (sgc::Mat4f::m[row][col] と同じ並び)、tint は頂点色に掛ける (rgba)。
struct MeshInstance
{
	float world[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
	                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

/// @brief スキンモデルの姿勢。clipA の timeA と clipB の timeB を blend (0 = A、1 = B) で混ぜる。
///        clipB が nullptr なら混ぜない。clipA が nullptr か空ならレストポーズ。時刻は秒でループする
struct ModelPose
{
	const char* clipA = nullptr;
	float timeA = 0.0f;
	const char* clipB = nullptr;
	float timeB = 0.0f;
	float blend = 0.0f;
};

/// @brief PieceInstancePod::flags の bit
inline constexpr std::uint32_t kPieceNoShadow = 1u << 0;   ///< 影を落とさない (細かい破片の影の費用を省く)

/// @brief 破片 1 個 (Screen::drawMeshPieces、ABI v51)。同じ meshId の破片は 1 回の instanced draw にまとまる
/// @details meshId は registerMesh3D が返した番号。world は行優先の上 3 行 (4 行目は 0 0 0 1)。motionKey は前フレームの
///          同じ破片と対にする鍵で、0 なら同じ meshId の中で渡した順に対にする (消えた破片の後ろが隣の破片と対にならないよう、
///          寿命で消える破片には 0 以外を付ける)。
struct PieceInstancePod
{
	std::uint32_t meshId = 0;
	std::uint32_t motionKey = 0;
	float world[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
	float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	std::uint32_t flags = 0;   ///< kPiece*
	std::uint32_t _reserved = 0;
};

static_assert(sizeof(DrawTint) == 32 && std::is_trivially_copyable_v<DrawTint>);
static_assert(sizeof(MeshInstance) == 80 && std::is_trivially_copyable_v<MeshInstance>);
static_assert(sizeof(PieceInstancePod) == 80 && std::is_trivially_copyable_v<PieceInstancePod> &&
              std::is_standard_layout_v<PieceInstancePod>, "PieceInstancePod wire size 固定 (ABI v51)");
static_assert(std::is_trivially_copyable_v<ModelPose>);

[[nodiscard]] inline sgc::Mat4f instanceWorld(const MeshInstance& inst) noexcept
{
	sgc::Mat4f w;
	std::memcpy(w.m, inst.world, sizeof(inst.world));
	return w;
}

[[nodiscard]] inline MeshInstance makeInstance(const sgc::Mat4f& world, float r = 1.0f, float g = 1.0f,
                                               float b = 1.0f, float a = 1.0f) noexcept
{
	MeshInstance inst;
	std::memcpy(inst.world, world.m, sizeof(inst.world));
	inst.tint[0] = r; inst.tint[1] = g; inst.tint[2] = b; inst.tint[3] = a;
	return inst;
}

/// @brief 破片 1 個をインスタンス 1 個にする (4 行目を 0 0 0 1 で埋める)
[[nodiscard]] inline MeshInstance pieceInstance(const PieceInstancePod& piece) noexcept
{
	MeshInstance inst;
	std::memcpy(inst.world, piece.world, sizeof(piece.world));
	std::memcpy(inst.tint, piece.tint, sizeof(piece.tint));
	return inst;
}

/// @brief tint の mul を掛けた色
[[nodiscard]] inline sgc::Colorf tinted(const sgc::Colorf& c, const float mul[4]) noexcept
{
	return {c.r * mul[0], c.g * mul[1], c.b * mul[2], c.a * mul[3]};
}

/// @brief 線形にした色へ、書いた色として受けた mul を線形にして掛ける。アルファはそのまま掛ける
[[nodiscard]] inline sgc::Colorf linearTinted(const sgc::Colorf& linear, const DrawTint& tint) noexcept
{
	const auto m = linearRgb(tint.mul[0], tint.mul[1], tint.mul[2]);
	return {linear.r * m[0], linear.g * m[1], linear.b * m[2], linear.a * tint.mul[3]};
}

} // namespace mitiru::render
