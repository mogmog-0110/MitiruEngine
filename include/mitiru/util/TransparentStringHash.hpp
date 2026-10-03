#pragma once

/// @file TransparentStringHash.hpp
/// @brief std::string の鍵の unordered 容器を、std::string を作らずに string_view や const char* で引く
/// @details `std::unordered_map<std::string, T, TransparentStringHash, std::equal_to<>>` と組で使う。
///          毎フレーム名前で引く所で、鍵の一時 std::string の確保を無くす。

#include <cstddef>
#include <functional>
#include <string_view>

namespace mitiru::util
{

struct TransparentStringHash
{
	using is_transparent = void;
	[[nodiscard]] std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

} // namespace mitiru::util
