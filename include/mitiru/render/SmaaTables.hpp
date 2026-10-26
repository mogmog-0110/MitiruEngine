#pragma once

/// @file SmaaTables.hpp
/// @brief SMAA が引く事前計算の表 2 枚 (面積 160x560 の RG8、探索 64x16 の R8) を渡す窓口
/// @details 配列の置き場は SmaaTablesData.hpp (tools/gen_smaa_tables.py が作る)。無いビルドは空を返す。
///          DX12 も Dawn も同じ配列から GPU の表を作る。

#include <cstddef>
#include <cstdint>

#if __has_include(<mitiru/render/SmaaTablesData.hpp>)
#include <mitiru/render/SmaaTablesData.hpp>
#define MITIRU_HAS_SMAA_TABLES 1
#endif

namespace mitiru::render
{

inline constexpr int kSmaaAreaWidth = 160;
inline constexpr int kSmaaAreaHeight = 560;
inline constexpr int kSmaaSearchWidth = 64;
inline constexpr int kSmaaSearchHeight = 16;

struct SmaaTables
{
	const std::uint8_t* area = nullptr;     ///< RG8。行は詰めて 2 byte x 幅
	const std::uint8_t* search = nullptr;   ///< R8

	[[nodiscard]] bool available() const noexcept { return area != nullptr && search != nullptr; }
};

/// 試験が配線だけを確かめるために、表の中身を差し替える。製品の経路は触らない
inline const SmaaTables*& smaaTablesOverride() noexcept
{
	static const SmaaTables* table = nullptr;
	return table;
}

[[nodiscard]] inline SmaaTables smaaTables() noexcept
{
	if (const SmaaTables* t = smaaTablesOverride()) { return *t; }
#if defined(MITIRU_HAS_SMAA_TABLES)
	return {kSmaaAreaBytes, kSmaaSearchBytes};
#else
	return {};
#endif
}

}  // namespace mitiru::render
