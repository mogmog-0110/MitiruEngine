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

static_assert(sizeof(DrawTint) == 32 && std::is_trivially_copyable_v<DrawTint>);
static_assert(sizeof(MeshInstance) == 80 && std::is_trivially_copyable_v<MeshInstance>);
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
