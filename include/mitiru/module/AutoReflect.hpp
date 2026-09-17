#pragma once

/// @file AutoReflect.hpp
/// @brief aggregate のフィールドを自動列挙する `MITIRU_REFLECT_AUTO(Type)` の宣言。
/// フィールド数の検出、0..64 個の構造化束縛、関数シグネチャからの名前抽出の 3 段で反射する。
/// `FixedVec<Struct,N>` と直ネストした aggregate は再帰し、固定長 C 配列は `elem.0` 形式で展開する。
/// enum 名・値域・UI 範囲・表示グループは `FieldAttr.hpp` の登録簿を経由して
/// `FieldDescriptor::elemType` に格納し、ABI を変えない。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <mitiru/module/FieldAttr.hpp>
#include <mitiru/module/Game.hpp>  // ReflectionOf / ModuleApi / FieldDescriptor / detail::registerReflection
#include <mitiru/module/detail/AutoReflectBind.hpp>
#include <mitiru/module/detail/AutoReflectCount.hpp>

namespace mitiru::module::detail
{

/// `Base` を加えて型ごとに異なるシグネチャを生成し、MSVC による別の型の同名メンバとの混同を防ぐ。
template<auto Base, auto Ptr>
constexpr std::string_view rawSignatureOf() noexcept
{
#if defined(_MSC_VER)
	return std::string_view{ __FUNCSIG__ };
#elif defined(__clang__) || defined(__GNUC__)
	return std::string_view{ __PRETTY_FUNCTION__ };
#else
	return std::string_view{};
#endif
}

[[nodiscard]] constexpr bool isIdentChar(char c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// MSVC の `->member` と clang/gcc の `.member` から、最後に現れるフィールド名を切り出す。
[[nodiscard]] constexpr std::string_view extractFieldName(std::string_view sig) noexcept
{
	std::size_t start = std::string_view::npos;
	if (const auto p = sig.rfind("->"); p != std::string_view::npos) { start = p + 2; }
	if (const auto p = sig.rfind('.'); p != std::string_view::npos)
	{
		const std::size_t cand = p + 1;
		if (start == std::string_view::npos || cand > start) { start = cand; }
	}
	if (start == std::string_view::npos || start >= sig.size()) { return {}; }
	std::size_t end = start;
	while (end < sig.size() && isIdentChar(sig[end])) { ++end; }
	return sig.substr(start, end - start);
}

/// 抽出できない環境では空を返し、呼び出し側で代替名を作る。
template<class T, std::size_t I>
constexpr std::string_view fieldNameOf() noexcept
{
	constexpr auto& ref = std::get<I>(tieFields(fakeObject<T>));
	constexpr auto   sig = rawSignatureOf<&fakeObject<T>, &ref>();
	return extractFieldName(sig);
}

/// TU で共有する `fakeObject<T>` とのアドレス差から byte offset を求める。
template<class T, std::size_t I>
[[nodiscard]] inline std::uint32_t fieldOffsetOf() noexcept
{
	const auto& ref  = std::get<I>(tieFields(fakeObject<T>));
	const auto* base = reinterpret_cast<const unsigned char*>(&fakeObject<T>);
	const auto* mem  = reinterpret_cast<const unsigned char*>(&ref);
	return static_cast<std::uint32_t>(mem - base);
}

template<class T, std::size_t I>
using FieldTypeAt = std::remove_cv_t<std::remove_reference_t<decltype(std::get<I>(tieFields(fakeObject<T>)))>>;

template<class M>
inline constexpr bool isDirectlyReflectable =
	(ScalarTag<M>::tag != nullptr) || IsFixedString<M>::value || IsFixedVec<M>::value;

/// `MitiruOpaqueReflect` を持つ型は、固定長 C 配列として展開せず中身を反射しない。
template<class T, class = void>
struct IsOpaqueReflect : std::false_type {};
template<class T>
struct IsOpaqueReflect<T, std::void_t<typename T::MitiruOpaqueReflect>> : std::true_type {};

template<class M>
inline constexpr bool isOpaqueReflectable = IsOpaqueReflect<M>::value;

template<class M>
inline constexpr bool isNestedAggregateReflectable =
	std::is_class_v<M> && std::is_aggregate_v<M> && std::is_trivially_copyable_v<M> &&
	!IsFixedString<M>::value && !IsFixedVec<M>::value && !isOpaqueReflectable<M>;

template<class M>
inline constexpr bool isEnumReflectable = std::is_enum_v<M>;

/// `MITIRU_ENUM` が特殊化する名前表。未登録の enum は名前を持たない。
template<class E> struct EnumNames
{
	static const char* joined() noexcept { return nullptr; }
};

/// enum 名をカンマ区切りにし、`FieldDescriptor::elemType` の 32 byte に収まらない分は切り捨てる。
inline std::string joinEnumNames(std::initializer_list<const char*> names)
{
	std::string s;
	bool first = true;
	for (const char* n : names)
	{
		if (!first) { s += ","; }
		if (n != nullptr) { s += n; }
		first = false;
	}
	return s;
}

/// `MITIRU_FIELD_RANGE` / `MITIRU_FIELD_UI_RANGE` / `MITIRU_FIELD_GROUP` は `FieldAttr.hpp` の
/// 統合レジストリへ登録する (MSVC はメンバ参照のアドレスを別インスタンス化として扱うため、
/// 照合は "TypeName.fieldName" の文字列キーで行う)。ここでは薄いラッパーだけを持つ。
inline void registerFieldRange(const char* typeName, const char* fieldName, float minV, float maxV)
{
	registerFieldAttr(typeName, fieldName, FieldAttr{ FieldAttrKind::Range, minV, maxV, "" });
}

inline void registerFieldUiRange(const char* typeName, const char* fieldName, float minV, float maxV)
{
	registerFieldAttr(typeName, fieldName, FieldAttr{ FieldAttrKind::UiRange, minV, maxV, "" });
}

inline void registerFieldGroup(const char* typeName, const char* fieldName, const char* name)
{
	registerFieldAttr(typeName, fieldName, FieldAttr{ FieldAttrKind::Group, 0.0f, 0.0f, name });
}

// ── 反射できるかの判定 ──

template<class T>
constexpr bool isTypeFullyReflectable() noexcept;

template<class M>
constexpr bool isFieldTypeReflectable() noexcept
{
	if constexpr (isDirectlyReflectable<M>) { return true; }
	else if constexpr (isOpaqueReflectable<M>) { return true; }
	else if constexpr (isEnumReflectable<M>) { return true; }
	else if constexpr (std::is_array_v<M> && std::rank_v<M> == 1)
	{
		return isFieldTypeReflectable<std::remove_extent_t<M>>();
	}
	else if constexpr (isNestedAggregateReflectable<M>) { return isTypeFullyReflectable<M>(); }
	else { return false; }
}

template<class T, std::size_t... I>
constexpr bool isTypeFullyReflectableImpl(std::index_sequence<I...>) noexcept
{
	return (isFieldTypeReflectable<FieldTypeAt<T, I>>() && ...);
}

/// 非対応型でもコンパイルを止めず、全フィールドを反射できるか返す。
template<class T>
constexpr bool isTypeFullyReflectable() noexcept
{
	if constexpr (!std::is_aggregate_v<T> || !std::is_trivially_copyable_v<T>) { return false; }
	else { return isTypeFullyReflectableImpl<T>(std::make_index_sequence<fieldCount<T>>{}); }
}

template<class T>
inline constexpr bool isFullyReflectable = isTypeFullyReflectable<T>();

// ── FieldDescriptor 列の組み立て ──

/// `rangeTypeName` は `MITIRU_FIELD_RANGE` を探す直接の親の型名。
/// ネストしたフィールドは表示範囲の対象外なので、再帰時は `nullptr` を渡す。
template<class T>
std::vector<FieldDescriptor> collectFields(
	std::uint32_t baseOffset, const std::string& prefix, const char* rangeTypeName = nullptr);

template<class E>
std::string registerElementSchema(const std::string& syntheticName);

/// 非対応型では、エラーに `&fakeObject<Type>->member` を表示してフィールドを示す。
template<auto FieldPtr>
void unsupportedFieldType() = delete;

template<class T, std::size_t I>
void collectOneField(
	std::vector<FieldDescriptor>& out, std::uint32_t baseOffset, const std::string& prefix,
	const char* rangeTypeName)
{
	using M = FieldTypeAt<T, I>;
	constexpr auto& ref = std::get<I>(tieFields(fakeObject<T>));

	constexpr std::string_view extracted = fieldNameOf<T, I>();
	std::string name;
	if (!extracted.empty()) { name.assign(extracted); }
	else { char fallback[16]; std::snprintf(fallback, sizeof(fallback), "field%zu", I); name = fallback; }

	const std::uint32_t offset   = baseOffset + fieldOffsetOf<T, I>();
	const std::string   fullName = prefix + name;

	if constexpr (isDirectlyReflectable<M>)
	{
		FieldDescriptor d = makeFieldDescriptor<M>(fullName.c_str(), offset);
		if constexpr (IsFixedVec<M>::value)
		{
			using E = typename IsFixedVec<M>::Elem;
			if constexpr (ScalarTag<E>::tag == nullptr && isNestedAggregateReflectable<E>)
			{
				// 要素 struct をフィールド固有の合成型名で schema に登録する。親型名も含めないと、別の型が
				// 同名の FixedVec<struct> フィールドを持つ時に同じ名前で登録され、後の型が前の schema を掴む。
				const std::string synthetic = registerElementSchema<E>(
					std::string("Auto$") + (rangeTypeName != nullptr ? std::string(rangeTypeName) + "." : std::string{}) + fullName);
				copyTag(d.elemType, sizeof(d.elemType), synthetic.c_str());
			}
		}
		else if constexpr (ScalarTag<M>::tag != nullptr)
		{
			if (rangeTypeName != nullptr)
			{
				// scalar では未使用の `elemType` に range/ui/group を合成して格納する (FieldAttr.hpp)。ABI は変えない。
				writeElemTypeTag(d.elemType, sizeof(d.elemType), std::string(rangeTypeName) + "." + name);
			}
		}
		out.push_back(d);
	}
	else if constexpr (isOpaqueReflectable<M>)
	{
		// `PodArena` など中身を反射しない型は、inspector に表示しない。
	}
	else if constexpr (isEnumReflectable<M>)
	{
		// `reflectToJson` が値を読めるよう、`typeTag` は下層の整数型のままにする。
		// 登録された enum 名は、未使用の `elemType` に `"enum:Idle,Run,..."` として格納する。
		using Under = std::underlying_type_t<M>;
		FieldDescriptor d = makeFieldDescriptor<Under>(fullName.c_str(), offset);
		if (const char* names = EnumNames<M>::joined(); names != nullptr && names[0] != '\0')
		{
			std::string tag = std::string("enum:") + names;
			// enum フィールドにも MITIRU_FIELD_GROUP を許す (enum: を group: より先に置く)。
			if (rangeTypeName != nullptr)
			{
				if (const auto* attrs = findFieldAttrs(std::string(rangeTypeName) + "." + name))
				{
					if (const auto* group = findFieldAttrOfKind(*attrs, FieldAttrKind::Group))
					{
						tag += ";group:" + group->text;
					}
				}
			}
			copyTag(d.elemType, sizeof(d.elemType), tag.c_str());
		}
		out.push_back(d);
	}
	else if constexpr (std::is_array_v<M> && std::rank_v<M> == 1)
	{
		// `_pad` で始まる配列は schema から除き、`reflectFields[128]` の消費を防ぐ。
		// padding の計算には `sizeof(M)` を使うため、検出結果には影響しない。
		if (name.size() >= 4 && name.compare(0, 4, "_pad") == 0) { return; }

		using Elem = std::remove_extent_t<M>;
		constexpr std::size_t extent = std::extent_v<M>;
		for (std::size_t k = 0; k < extent; ++k)
		{
			const std::string   elemName   = fullName + "." + std::to_string(k);
			const std::uint32_t elemOffset = offset + static_cast<std::uint32_t>(k * sizeof(Elem));
			if constexpr (ScalarTag<Elem>::tag != nullptr)
			{
				out.push_back(makeFieldDescriptor<Elem>(elemName.c_str(), elemOffset));
			}
			else if constexpr (isNestedAggregateReflectable<Elem>)
			{
				auto nested = collectFields<Elem>(elemOffset, elemName + ".");
				out.insert(out.end(), nested.begin(), nested.end());
			}
			else { unsupportedFieldType<&ref>(); }
		}
	}
	else if constexpr (isNestedAggregateReflectable<M>)
	{
		auto nested = collectFields<M>(offset, fullName + ".");
		out.insert(out.end(), nested.begin(), nested.end());
	}
	else
	{
		// 非対応型のフィールド名は、エラー中の `&fakeObject<Type>->member` で示す。
		unsupportedFieldType<&ref>();
	}
}

template<class T, std::size_t... I>
std::vector<FieldDescriptor> collectFieldsImpl(
	std::uint32_t baseOffset, const std::string& prefix, const char* rangeTypeName,
	std::index_sequence<I...>)
{
	std::vector<FieldDescriptor> out;
	(collectOneField<T, I>(out, baseOffset, prefix, rangeTypeName), ...);
	return out;
}

/// ネストした aggregate を再帰してフラット化する。
/// `baseOffset` はトップレベルからの byte offset、`prefix` は `outer.` 形式の接頭辞。
template<class T>
std::vector<FieldDescriptor> collectFields(
	std::uint32_t baseOffset, const std::string& prefix, const char* rangeTypeName)
{
	return collectFieldsImpl<T>(baseOffset, prefix, rangeTypeName, std::make_index_sequence<fieldCount<T>>{});
}

/// `FixedVec<Struct,N>` の要素を再帰して反射し、合成型名で schema に登録する。
template<class E>
std::string registerElementSchema(const std::string& syntheticName)
{
	const auto fields = collectFields<E>(0, "", nullptr);
	ReflectSchema s{};
	copyTag(s.typeName, sizeof(s.typeName), syntheticName.c_str());
	const std::int32_t cap = static_cast<std::int32_t>(sizeof(s.fields) / sizeof(s.fields[0]));
	std::int32_t n = static_cast<std::int32_t>(fields.size());
	if (n > cap) { n = cap; }
	for (std::int32_t i = 0; i < n; ++i) { s.fields[i] = fields[static_cast<std::size_t>(i)]; }
	s.fieldCount = n;
	reflectSchemaRegistry().push_back(s);
	return syntheticName;
}

// ── GameMemory の padding 検出 ──
// padding の不定値による録画再生の不一致を検出する。
// `sizeof(T)` と全リーフフィールドの byte 数の差を padding とみなす。
// ネストした aggregate とその配列だけ再帰し、それ以外は `sizeof(M)` で数える。

template<class T>
constexpr std::size_t reflectedLeafBytesOf() noexcept;

template<class M>
constexpr std::size_t reflectedLeafBytesOfField() noexcept
{
	if constexpr (std::is_array_v<M> && std::rank_v<M> == 1)
	{
		using Elem = std::remove_extent_t<M>;
		if constexpr (isNestedAggregateReflectable<Elem>)
		{
			return std::extent_v<M> * reflectedLeafBytesOf<Elem>();
		}
		else { return sizeof(M); }  // C 配列は要素間に padding が無い
	}
	else if constexpr (isNestedAggregateReflectable<M>) { return reflectedLeafBytesOf<M>(); }
	else { return sizeof(M); }
}

template<class T, std::size_t... I>
constexpr std::size_t reflectedLeafBytesOfImpl(std::index_sequence<I...>) noexcept
{
	return (reflectedLeafBytesOfField<FieldTypeAt<T, I>>() + ... + std::size_t{0});
}

template<class T>
constexpr std::size_t reflectedLeafBytesOf() noexcept
{
	return reflectedLeafBytesOfImpl<T>(std::make_index_sequence<fieldCount<T>>{});
}

/// padding の探索に使うリーフ。名前は `outer.inner` 形式、範囲は絶対 offset の `[begin,end)`。
struct PaddingProbeEntry { std::string name; std::uint32_t begin; std::uint32_t end; };

template<class T>
void collectPaddingProbe(std::uint32_t baseOffset, const std::string& prefix, std::vector<PaddingProbeEntry>& out);

template<class T, std::size_t I>
void collectOnePaddingProbe(std::vector<PaddingProbeEntry>& out, std::uint32_t baseOffset, const std::string& prefix)
{
	using M = FieldTypeAt<T, I>;
	constexpr std::string_view extracted = fieldNameOf<T, I>();
	std::string name;
	if (!extracted.empty()) { name.assign(extracted); }
	else { char fallback[16]; std::snprintf(fallback, sizeof(fallback), "field%zu", I); name = fallback; }

	const std::uint32_t offset   = baseOffset + fieldOffsetOf<T, I>();
	const std::string   fullName = prefix + name;

	if constexpr (std::is_array_v<M> && std::rank_v<M> == 1 && isNestedAggregateReflectable<std::remove_extent_t<M>>)
	{
		using Elem = std::remove_extent_t<M>;
		for (std::size_t k = 0; k < std::extent_v<M>; ++k)
		{
			collectPaddingProbe<Elem>(offset + static_cast<std::uint32_t>(k * sizeof(Elem)),
				fullName + "." + std::to_string(k) + ".", out);
		}
	}
	else if constexpr (isNestedAggregateReflectable<M>)
	{
		collectPaddingProbe<M>(offset, fullName + ".", out);
	}
	else
	{
		out.push_back(PaddingProbeEntry{ fullName, offset, offset + static_cast<std::uint32_t>(sizeof(M)) });
	}
}

template<class T, std::size_t... I>
void collectPaddingProbeImpl(std::uint32_t baseOffset, const std::string& prefix,
	std::vector<PaddingProbeEntry>& out, std::index_sequence<I...>)
{
	(collectOnePaddingProbe<T, I>(out, baseOffset, prefix), ...);
}

template<class T>
void collectPaddingProbe(std::uint32_t baseOffset, const std::string& prefix, std::vector<PaddingProbeEntry>& out)
{
	collectPaddingProbeImpl<T>(baseOffset, prefix, out, std::make_index_sequence<fieldCount<T>>{});
}

/// padding があれば `warnOnce` で起動時に 1 件だけ警告する。
/// 手動の `MITIRU_REFLECT` は対象外。
template<class T>
void warnIfGameMemoryPadding(const char* typeName)
{
	constexpr std::size_t leafBytes  = reflectedLeafBytesOf<T>();
	constexpr std::size_t totalBytes = sizeof(T);
	if constexpr (totalBytes > leafBytes)
	{
		std::vector<PaddingProbeEntry> entries;
		collectPaddingProbe<T>(0, "", entries);
		std::sort(entries.begin(), entries.end(),
			[](const PaddingProbeEntry& a, const PaddingProbeEntry& b) { return a.begin < b.begin; });

		// 先頭からフィールドをたどり、最初のギャップの直前にあるフィールドを特定する。
		std::string   afterField = "先頭";
		std::uint32_t expected   = 0;
		for (const auto& e : entries)
		{
			if (e.begin > expected) { break; }
			expected   = e.end;
			afterField = e.name;
		}

		char what[192];
		std::snprintf(what, sizeof(what), "%s に GameMemory padding が %u byte ある (%s の後ろ)",
			typeName, static_cast<unsigned>(totalBytes - leafBytes), afterField.c_str());
		mitiru::debug::warnOnceFix("module.gamememory.padding", what,
			"field の並び順とアライメントの都合で隙間ができている",
			"field を並べ替えるか、明示の pad を足す (一時オブジェクト代入で隙間に不定値が混ざり"
			"録画再生が一致しなくなることがある)");
	}
}

}  // namespace mitiru::module::detail

/// aggregate のフィールドを列挙せず host へ申告する。
/// グローバル scope で完全修飾名を渡す。
#define MITIRU_REFLECT_AUTO(Type)                                                           \
	namespace mitiru { namespace module { namespace detail {                                \
		template<> struct ReflectionOf<Type> {                                              \
			static void fillApi(::mitiru::module::ModuleApi* api) {                         \
				static_assert(std::is_aggregate_v<Type>,                                    \
					"MITIRU_REFLECT_AUTO(" #Type "): aggregate 型のみ対応。"                 \
					"ユーザー定義コンストラクタがあるとフィールドを自動列挙できない。");      \
				static_assert(std::is_trivially_copyable_v<Type>,                           \
					"MITIRU_REFLECT_AUTO(" #Type "): flat POD (trivially_copyable) のみ"     \
					"対応。std::vector/std::string 等を含めないこと。");                     \
				const auto _mitiruAutoFields =                                              \
					::mitiru::module::detail::collectFields<Type>(0, "", #Type);            \
				::mitiru::module::detail::registerReflection(                               \
					api, _mitiruAutoFields.data(),                                          \
					static_cast<std::int32_t>(_mitiruAutoFields.size()));                   \
				::mitiru::module::detail::warnIfGameMemoryPadding<Type>(#Type);            \
			}                                                                               \
		};                                                                                  \
	} } }

/// enum の表示名を順番に登録する。
/// グローバル scope で完全修飾名を渡す。
#define MITIRU_ENUM(Type, ...)                                                              \
	namespace mitiru { namespace module { namespace detail {                                \
		template<> struct EnumNames<Type> {                                                 \
			static const char* joined() noexcept {                                          \
				static const std::string _mitiruEnumNames =                                 \
					::mitiru::module::detail::joinEnumNames({ __VA_ARGS__ });                \
				return _mitiruEnumNames.c_str();                                             \
			}                                                                                \
		};                                                                                   \
	} } }

#define MITIRU_AUTOREFLECT_CONCAT_IMPL(a, b) a##b
#define MITIRU_AUTOREFLECT_CONCAT(a, b) MITIRU_AUTOREFLECT_CONCAT_IMPL(a, b)

/// 数値フィールドの値域 (ClampMin/Max 相当) を登録する。`observe::Oracle` が範囲外検出に使う。
/// `field` には `Type` の直接のメンバ名を渡す。ネストしたフィールドは対象外。
#define MITIRU_FIELD_RANGE(Type, field, minV, maxV)                                         \
	namespace                                                                                \
	{                                                                                        \
	struct MITIRU_AUTOREFLECT_CONCAT(FieldRangeInit_, __LINE__)                              \
	{                                                                                        \
		MITIRU_AUTOREFLECT_CONCAT(FieldRangeInit_, __LINE__)()                               \
		{                                                                                    \
			::mitiru::module::detail::registerFieldRange(                                   \
				#Type, #field, static_cast<float>(minV), static_cast<float>(maxV));         \
		}                                                                                    \
	} MITIRU_AUTOREFLECT_CONCAT(g_fieldRangeInit_, __LINE__);                                \
	}

/// inspector のスライダー範囲 (UIMin/Max 相当) を登録する。`MITIRU_FIELD_RANGE` の値域とは
/// 独立で、未指定なら inspector は値域をスライダー範囲としてそのまま使う。
#define MITIRU_FIELD_UI_RANGE(Type, field, minV, maxV)                                      \
	namespace                                                                                \
	{                                                                                        \
	struct MITIRU_AUTOREFLECT_CONCAT(FieldUiRangeInit_, __LINE__)                            \
	{                                                                                        \
		MITIRU_AUTOREFLECT_CONCAT(FieldUiRangeInit_, __LINE__)()                             \
		{                                                                                    \
			::mitiru::module::detail::registerFieldUiRange(                                 \
				#Type, #field, static_cast<float>(minV), static_cast<float>(maxV));         \
		}                                                                                    \
	} MITIRU_AUTOREFLECT_CONCAT(g_fieldUiRangeInit_, __LINE__);                              \
	}

/// inspector の表示グループ名 (Godot `@export_group` 相当) を登録する。同じ名前を付けた
/// フィールド同士が inspector 上でまとめて折り畳み表示される。
#define MITIRU_FIELD_GROUP(Type, field, name)                                               \
	namespace                                                                                \
	{                                                                                        \
	struct MITIRU_AUTOREFLECT_CONCAT(FieldGroupInit_, __LINE__)                              \
	{                                                                                        \
		MITIRU_AUTOREFLECT_CONCAT(FieldGroupInit_, __LINE__)()                               \
		{                                                                                    \
			::mitiru::module::detail::registerFieldGroup(#Type, #field, name);              \
		}                                                                                    \
	} MITIRU_AUTOREFLECT_CONCAT(g_fieldGroupInit_, __LINE__);                                \
	}
