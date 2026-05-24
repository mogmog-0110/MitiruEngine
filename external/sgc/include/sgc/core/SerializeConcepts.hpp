#pragma once

/// @file SerializeConcepts.hpp
/// @brief シリアライズ用コンセプト定義
///
/// Visitable / ConstVisitable コンセプトを定義する。
/// JsonWriter / JsonReader の両方から参照されるため、独立ファイルとして分離。

#include <concepts>

namespace sgc
{

// 前方宣言
class JsonWriter;
class JsonReader;

/// @brief visit(visitor)メソッドを持つ型
/// @tparam T 対象の型
/// @tparam Visitor ビジター型
template <typename T, typename Visitor>
concept Visitable = requires(T t, Visitor v) {
	t.visit(v);
};

/// @brief const visit(visitor)メソッドを持つ型
/// @tparam T 対象の型
/// @tparam Visitor ビジター型
template <typename T, typename Visitor>
concept ConstVisitable = requires(const T t, Visitor v) {
	t.visit(v);
};

} // namespace sgc
