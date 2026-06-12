#pragma once

/// @file Reflection.hpp
/// @brief GameMemory のフィールド構造を game が宣言し、host が構造化 JSON 化する記述子 (ADR 0018)
/// @details
/// probe (SeriesProbe = 1 スカラーの accessor) の自然な拡張。probe は「GameMemory から
/// double を 1 つ引く」だったが、reflection は「GameMemory の全フィールドの名前・型・
/// オフセット」を宣言する。host はこの記述子表を使って GameMemory バイト列 (現フレーム +
/// time-travel ring の過去フレーム) を構造化 JSON に変換し、AI が全状態を読めるようにする。
///
/// 設計 (ADR 0005/0017 整合):
/// - host は GameMemory の layout を内蔵しない。**game が記述子で教える** (probe と同契約)。
/// - FieldDescriptor は DLL 境界を memcpy で渡る POD。
/// - v1 対応型: スカラー / FixedString / FixedVec<スカラー,N> / FixedVec<flat-struct,N>
///   (要素 struct も MITIRU_REFLECT_STRUCT 済みなら 1 段ネスト)。2 段以上は対象外。
///
/// 使い方は `MITIRU_REFLECT(Type, field...)` / `MITIRU_REFLECT_STRUCT(E, field...)`
/// (include/mitiru/module/Game.hpp) を参照。

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

#include <mitiru/core/FixedVec.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::module
{

/// @brief GameMemory の 1 フィールドの記述子 (POD、DLL 境界を渡る)
struct FieldDescriptor
{
	char          name[32];      ///< フィールド名 (例 "hp")、null 終端
	char          typeTag[8];    ///< "f32"/"f64"/"i32"/"u32"/"i64"/"u64"/"i16"/"u16"/"i8"/"u8"/"bool"/"str"/"vec"/"struct"
	char          elemType[32];  ///< vec/struct の要素型: スカラー tag か MITIRU_REFLECT_STRUCT の型名。他は ""
	std::uint32_t offset;        ///< GameMemory 内の byte offset
	std::uint32_t elemSize;      ///< 要素 1 個のバイト数 (scalar: sizeof, vec: sizeof(E), str: 1)
	std::uint32_t elemCount;     ///< scalar:1, FixedVec:容量 N, FixedString:N
	std::uint32_t countOffset;   ///< FixedVec: フィールド内の .count の byte offset。他は 0
	std::uint8_t  hasCount;      ///< 1 = FixedVec (live count を countOffset から読む)
	std::uint8_t  _pad[7];
};

/// @brief FixedVec<struct,N> の要素 struct のスキーマ (1 段ネスト、POD)
struct ReflectSchema
{
	char            typeName[32];  ///< 要素型名 (例 "Enemy")、FieldDescriptor.elemType と一致
	std::int32_t    fieldCount;
	std::uint8_t    _pad[4];
	FieldDescriptor fields[16];    ///< 要素 struct のフィールド (スカラーのみ想定)
};

static_assert(std::is_trivially_copyable_v<FieldDescriptor>, "FieldDescriptor は POD (DLL 境界)");
static_assert(std::is_trivially_copyable_v<ReflectSchema>,   "ReflectSchema は POD (DLL 境界)");

namespace detail
{

/// @brief スカラー型 → 型タグ。未知型は nullptr (= スカラーでない)。
template <class U> struct ScalarTag { static constexpr const char* tag = nullptr; };
template <> struct ScalarTag<float>              { static constexpr const char* tag = "f32"; };
template <> struct ScalarTag<double>             { static constexpr const char* tag = "f64"; };
template <> struct ScalarTag<bool>               { static constexpr const char* tag = "bool"; };
template <> struct ScalarTag<signed char>        { static constexpr const char* tag = "i8"; };
template <> struct ScalarTag<unsigned char>      { static constexpr const char* tag = "u8"; };
template <> struct ScalarTag<short>              { static constexpr const char* tag = "i16"; };
template <> struct ScalarTag<unsigned short>     { static constexpr const char* tag = "u16"; };
template <> struct ScalarTag<int>                { static constexpr const char* tag = "i32"; };
template <> struct ScalarTag<unsigned int>       { static constexpr const char* tag = "u32"; };
template <> struct ScalarTag<long>               { static constexpr const char* tag = "i32"; }; // Win: 32bit
template <> struct ScalarTag<unsigned long>      { static constexpr const char* tag = "u32"; };
template <> struct ScalarTag<long long>          { static constexpr const char* tag = "i64"; };
template <> struct ScalarTag<unsigned long long> { static constexpr const char* tag = "u64"; };

/// @brief FixedVec<E,N> 判定
template <class U> struct IsFixedVec : std::false_type {};
template <class E, std::size_t N> struct IsFixedVec<mitiru::FixedVec<E, N>> : std::true_type
{
	using Elem = E;
	static constexpr std::size_t cap = N;
};

/// @brief FixedString<N> 判定
template <class U> struct IsFixedString : std::false_type {};
template <std::size_t N> struct IsFixedString<mitiru::FixedString<N>> : std::true_type
{
	static constexpr std::size_t cap = N;
};

/// @brief MITIRU_REFLECT / MITIRU_REFLECT_STRUCT のフィールド数超過 (>16) を compile error
///        にする番兵。17 個以上を書くと MITIRU_FOR_EACH がこの削除済み関数を選び、
///        「use of deleted function ...Max16Fields...」が出る — 関数名がそのまま対処法:
///        フィールドを 16 個以下に分割するか、ネスト部分を MITIRU_REFLECT_STRUCT へ切り出す。
inline FieldDescriptor mitiruReflect_Max16Fields_SplitOrUseReflectStruct() = delete;

/// @brief 固定長 buffer への安全コピー (null 終端保証)
inline void copyTag(char* dst, std::size_t cap, const char* src)
{
	std::size_t i = 0;
	if (src != nullptr) { for (; src[i] != '\0' && i + 1 < cap; ++i) { dst[i] = src[i]; } }
	dst[i] = '\0';
}

}  // namespace detail

/// @brief 要素 struct の型名を引く trait。`MITIRU_REFLECT_STRUCT` が特殊化する。
/// @details FixedVec<E,N> の E が struct のとき、host が schema を引くキー (型名) を提供する。
template <class E> struct ReflectName { static constexpr const char* value = ""; };

/// @brief メンバ 1 つの FieldDescriptor を組み立てる (registration 時に呼ぶ、runtime)
/// @tparam M メンバの型 (decltype(Type::member))
/// @param name   フィールド名 (#member)
/// @param offset offsetof(Type, member)
template <class M>
inline FieldDescriptor makeFieldDescriptor(const char* name, std::uint32_t offset)
{
	FieldDescriptor d{};
	detail::copyTag(d.name, sizeof(d.name), name);
	d.offset      = offset;
	d.elemCount   = 1;
	d.elemSize    = static_cast<std::uint32_t>(sizeof(M));
	d.countOffset = 0;
	d.hasCount    = 0;

	if constexpr (detail::IsFixedString<M>::value)
	{
		detail::copyTag(d.typeTag, sizeof(d.typeTag), "str");
		d.elemSize  = 1;
		d.elemCount = static_cast<std::uint32_t>(detail::IsFixedString<M>::cap);
	}
	else if constexpr (detail::IsFixedVec<M>::value)
	{
		using E = typename detail::IsFixedVec<M>::Elem;
		detail::copyTag(d.typeTag, sizeof(d.typeTag), "vec");
		d.elemSize    = static_cast<std::uint32_t>(sizeof(E));
		d.elemCount   = static_cast<std::uint32_t>(detail::IsFixedVec<M>::cap);
		d.hasCount    = 1;
		d.countOffset = static_cast<std::uint32_t>(offsetof(M, count));
		if constexpr (detail::ScalarTag<E>::tag != nullptr)
		{
			detail::copyTag(d.elemType, sizeof(d.elemType), detail::ScalarTag<E>::tag);
		}
		else
		{
			detail::copyTag(d.elemType, sizeof(d.elemType), ReflectName<E>::value);
		}
	}
	else if constexpr (detail::ScalarTag<M>::tag != nullptr)
	{
		detail::copyTag(d.typeTag, sizeof(d.typeTag), detail::ScalarTag<M>::tag);
	}
	else
	{
		// 直メンバの nested struct (vec でない)。1 段ネストとして型名を載せる。
		detail::copyTag(d.typeTag, sizeof(d.typeTag), "struct");
		detail::copyTag(d.elemType, sizeof(d.elemType), ReflectName<M>::value);
	}
	return d;
}

/// @brief DLL-local な要素スキーマ登録簿。`MITIRU_REFLECT_STRUCT` が起動時に push し、
///        `MITIRU_REFLECT` の fillApi が ModuleApi へコピーする (FixedVec<struct,N> 用)。
inline std::vector<ReflectSchema>& reflectSchemaRegistry()
{
	static std::vector<ReflectSchema> registry;
	return registry;
}

/// @brief 要素 struct のスキーマを登録簿に積む (MITIRU_REFLECT_STRUCT が呼ぶ、static init 時)。
inline bool registerSchema(const char* typeName, std::initializer_list<FieldDescriptor> fields)
{
	ReflectSchema s{};
	detail::copyTag(s.typeName, sizeof(s.typeName), typeName);
	const std::int32_t cap = static_cast<std::int32_t>(sizeof(s.fields) / sizeof(s.fields[0]));
	if (static_cast<std::int32_t>(fields.size()) > cap)
	{
		// 黙って切り捨てない: 17 個目以降は inspector / AI に出ない。
		const char* name = (typeName != nullptr) ? typeName : "";
		mitiru::debug::warnOnce(std::string("reflect.schema.fields.") + name,
			std::string("MITIRU_REFLECT_STRUCT ") + name +
			": フィールドが上限 16 個を超えています。17 個目以降は inspector / AI へ出ません");
	}
	std::int32_t i = 0;
	for (const auto& f : fields) { if (i >= cap) { break; } s.fields[i++] = f; }
	s.fieldCount = i;
	reflectSchemaRegistry().push_back(s);
	return true;
}

}  // namespace mitiru::module
