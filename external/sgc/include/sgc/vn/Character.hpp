#pragma once

/// @file Character.hpp
/// @brief キャラクター定義と状態
///
/// ビジュアルノベルのキャラクターの外観定義と、
/// 現在の表示状態（表情・位置・透明度）を管理する構造体群。
///
/// @code
/// using namespace sgc::vn;
/// CharacterDef def;
/// def.id = "sakura";
/// def.displayName = "Sakura";
/// def.nameColor = sgc::Colorf::fromRGBA8(255, 192, 203);
/// def.expressions["happy"] = 1;
/// def.expressions["sad"] = 2;
/// @endcode

#include <cstdint>
#include <string>
#include <unordered_map>

#include "sgc/types/Color.hpp"

namespace sgc::vn
{

/// @brief キャラクターの立ち位置
enum class CharacterPosition : uint8_t
{
	Left,         ///< 左端
	CenterLeft,   ///< 左寄り中央
	Center,       ///< 中央
	CenterRight,  ///< 右寄り中央
	Right         ///< 右端
};

/// @brief キャラクター定義
///
/// キャラクターの基本情報（名前・表情リスト等）を保持する。
/// 表情名からスプライト/テクスチャIDへのマッピングを含む。
struct CharacterDef
{
	std::string id;                                          ///< キャラクターID（例: "sakura"）
	std::string displayName;                                 ///< 表示名（例: "Sakura"）
	Colorf nameColor = Colorf::white();                      ///< 名前の表示色
	std::unordered_map<std::string, int> expressions;        ///< 表情名 -> スプライトID
	int defaultExpression = 0;                               ///< デフォルト表情ID
};

/// @brief キャラクターの現在の表示状態
///
/// キャラクターの画面上の状態（位置・表情・透明度等）を保持する。
struct CharacterState
{
	std::string characterId;                                 ///< キャラクターID
	std::string expression = "default";                      ///< 現在の表情名
	CharacterPosition position = CharacterPosition::Center;  ///< 現在の立ち位置
	float alpha = 1.0f;                                      ///< 透明度 [0, 1]
	bool visible = true;                                     ///< 表示中か
};

} // namespace sgc::vn
