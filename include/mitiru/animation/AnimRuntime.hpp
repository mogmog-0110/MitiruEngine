#pragma once

/// @file AnimRuntime.hpp
/// @brief アニメーションランタイム一式。ゲーム DLL が同じフレームの判定 (ヒットボックスの骨、ソケット、
///        ルートモーション、イベント) に使い、host の描画も同じ関数で姿勢を出す。
/// @details 使い方と sidecar の書式は docs/ANIMATION_RUNTIME.md。

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimAssetBuild.hpp>
#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/animation/AnimBlendSpace.hpp>
#include <mitiru/animation/AnimEvents.hpp>
#include <mitiru/animation/AnimIK.hpp>
#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/animation/AnimRootMotion.hpp>
#include <mitiru/animation/AnimSidecar.hpp>
