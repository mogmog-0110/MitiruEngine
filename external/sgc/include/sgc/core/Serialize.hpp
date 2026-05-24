#pragma once

/// @file Serialize.hpp
/// @brief シリアライズ後方互換アンブレラヘッダー
///
/// 個別ヘッダーに分割済み。既存コードの互換性のためにすべてを再エクスポートする。
/// 新規コードでは直接 JsonWriter.hpp / JsonReader.hpp を使うことを推奨。

#include "sgc/core/SerializeConcepts.hpp"
#include "sgc/core/JsonWriter.hpp"
#include "sgc/core/JsonReader.hpp"
