#pragma once

/// @file AnimRuntime.hpp
/// @brief アニメーションランタイム一式。ゲーム DLL が同じフレームの判定 (ヒットボックスの骨、ソケット、
///        ルートモーション、イベント) に使い、host の描画も同じ関数で姿勢を出す。
/// @details 使い方と sidecar の書式は docs/ANIMATION_RUNTIME.md、状態機械の書式は docs/ANIM_GRAPH.md。

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimAssetBuild.hpp>
#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/animation/AnimBlendSpace.hpp>
#include <mitiru/animation/AnimEvents.hpp>
#include <mitiru/animation/AnimGraph.hpp>
#include <mitiru/animation/AnimGraphFile.hpp>
#include <mitiru/animation/AnimGraphLoad.hpp>
#include <mitiru/animation/AnimGraphPose.hpp>
#include <mitiru/animation/AnimGraphStep.hpp>
#include <mitiru/animation/AnimIK.hpp>
#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/animation/AnimRootMotion.hpp>
#include <mitiru/animation/AnimSidecar.hpp>
#include <mitiru/animation/AnimSpringBones.hpp>
