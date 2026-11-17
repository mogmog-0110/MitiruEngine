#pragma once

/// @file CameraWallLimit.hpp
/// @brief 面の法線から視線の許可範囲を出す純関数 (action/CameraWallLimit.hpp) を camera 名前空間から使えるようにする。

#include <mitiru/action/CameraWallLimit.hpp>

namespace mitiru::camera
{

using action::allowedPitchRange;
using action::allowedYawArc;
using action::PitchRange;
using action::YawArc;

}  // namespace mitiru::camera
