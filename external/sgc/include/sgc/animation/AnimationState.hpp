#pragma once

/// @file AnimationState.hpp
/// @brief アニメーション状態・遷移定義
///
/// アニメーションステートマシンで使用する状態と遷移の構造体を定義する。
///
/// @code
/// sgc::animation::AnimationState state;
/// state.name = "idle";
/// state.animation = idleAnim;
/// state.looping = true;
///
/// sgc::animation::AnimationTransition trans;
/// trans.fromState = "idle";
/// trans.toState = "run";
/// trans.condition = [&]{ return speed > 0.1f; };
/// @endcode

#include <functional>
#include <string>

#include "sgc/asset/SpriteSheet.hpp"

namespace sgc::animation
{

/// @brief アニメーション状態
///
/// ステートマシン内の1つの状態を表す。
/// 名前、再生するアニメーション、ループ設定、割り込み可否を保持する。
struct AnimationState
{
	std::string name;                    ///< 状態名（"idle", "run", "attack"等）
	asset::SpriteAnimation animation;   ///< 再生するアニメーション定義
	bool looping = true;                ///< ループ再生するか
	bool interruptible = true;          ///< 他の状態に割り込み可能か
};

/// @brief アニメーション遷移定義
///
/// ある状態から別の状態への遷移条件を定義する。
/// fromStateが空文字列の場合、任意の状態からの遷移（ワイルドカード）となる。
struct AnimationTransition
{
	std::string fromState;              ///< 遷移元の状態名（空文字列 = 任意の状態）
	std::string toState;                ///< 遷移先の状態名
	std::function<bool()> condition;    ///< 遷移条件（trueで遷移実行）
	float blendDuration = 0.0f;         ///< ブレンド時間（0 = 即時切替）
	int priority = 0;                   ///< 優先度（高い値が優先）
};

} // namespace sgc::animation
