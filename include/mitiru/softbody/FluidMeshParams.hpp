#pragma once

/// @file FluidMeshParams.hpp
/// @brief buildFluidMesh の後に掛ける処理の調整値。FluidParams の大きさを変えない約束 (TestFluidStick) なので別の型にした。

namespace mitiru::softbody
{

inline constexpr int kFluidMeshSmoothMaxIterations = 8;
inline constexpr float kFluidMeshGridBlendMax = 0.9f;   // fluid.json の検査 (FluidParamsJson.hpp) の文面と合わせる

struct FluidMeshParams
{
	/// ラプラシアンの平滑化の回数 (0〜kFluidMeshSmoothMaxIterations)。0 か未指定ならメッシュは 1 bit も変わらない。fluid.json のキーも同じ名前
	int meshSmoothIterations = 0;
	/// 前のフレームの距離グリッドを混ぜる割合 (0〜kFluidMeshGridBlendMax)。0 か未指定ならメッシュは 1 bit も変わらない。前のグリッドは FluidSurfaceScratch が持つので、体ごとに別の物を渡し、1 フレームに 1 回だけ作る。fluid.json のキーも同じ名前
	float meshGridBlend = 0.0f;
};

} // namespace mitiru::softbody
