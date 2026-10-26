#pragma once

/// @file Batch2D_Dawn.hpp
/// @brief Dawn の 2D の頂点バッチ。device が DawnDevice のときだけ作れる

#include <memory>

#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/Batch2DSink.hpp>

namespace mitiru::render
{

/// @brief 頂点色の 2D バッチ (Screen の drawRect や drawCircle など) を DawnDevice の最終の色へ重ねる。作れなければ nullptr
/// @param withLighting 2D ライト (R6) の資源も作る。宣言のあるゲームが light_2d を宣言していないときは false
[[nodiscard]] std::shared_ptr<IBatch2DSink> createDawnBatch2D(gfx::IDevice* device, float screenWidth, float screenHeight,
                                                              bool withLighting = true);

} // namespace mitiru::render
