#pragma once

/// @file Key.hpp
/// @brief mitiru::Key — KeyCode への薄いエイリアス
/// @details `mitiru::Key::ArrowLeft` のような名前空間限定アクセスを可能にする
///          薄いラッパー。実体は @ref KeyCode と同じ enum で、ABI 互換性を持つ。
///
///          既存の `mitiru::KeyCode::Left` を使うコードはそのまま動き、
///          新規コードは `mitiru::Key::ArrowLeft` のような DOM 寄りの命名も
///          選択できる。
///
/// @see KeyCode.hpp（実体定義）

#include "mitiru/input/KeyCode.hpp"

namespace mitiru
{

/// @brief KeyCode のエイリアス
/// @details `mitiru::Key::ArrowLeft` のような完全修飾名で参照可能にするための型エイリアス。
///          KeyCode と完全に同一型なので、相互に暗黙変換できる。
using Key = KeyCode;

} // namespace mitiru
