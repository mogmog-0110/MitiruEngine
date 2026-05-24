#pragma once

/// @file TypeId.hpp
/// @brief 軽量な型ID生成ユーティリティ
///
/// std::type_indexより軽量。ECSやイベントシステムで型をキーとして使用する際に便利。

#include <atomic>
#include <cstdint>

namespace sgc
{

/// @brief 型IDの整数型
using TypeIdValue = std::uint32_t;

namespace detail
{

/// @brief 型IDカウンタ（内部用、スレッド安全）
inline TypeIdValue nextTypeId() noexcept
{
	static std::atomic<TypeIdValue> counter{0};
	return counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace detail

/// @brief 型ごとにユニークなIDを返す
///
/// 同じ型に対しては常に同じ値を返す。
/// プログラム実行ごとにIDの値は変わり得る（永続化不可）。
///
/// @tparam T ID を取得する型
/// @return 型に対応するユニークなID
///
/// @code
/// auto id1 = sgc::typeId<int>();
/// auto id2 = sgc::typeId<float>();
/// auto id3 = sgc::typeId<int>();
/// // id1 == id3, id1 != id2
/// @endcode
template <typename T>
[[nodiscard]] TypeIdValue typeId() noexcept
{
	static const TypeIdValue id = detail::nextTypeId();
	return id;
}

} // namespace sgc
