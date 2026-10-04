#pragma once

/// @file ModuleReflection.hpp
/// @brief host が load した game から受け取った GameMemory の記述 (反射の全件と形の hash)。
/// @details 記述は ModuleApi に載せず、DLL の別 export (`mitiru_module_layout_hash` /
/// `mitiru_module_reflect_fields` / `_schemas`) だけから読む (ABI v45)。件数の上限が無く、
/// ModuleApi が反射の配列で大きくならない。静的リンク (`Engine::runModuleStatic`) には GetProcAddress で
/// 引く export が無いので、`MITIRU_GAME` 系の登録が同じ関数を `linkedReflectionExports()` に置く。
/// float の位置と詰め物の byte 数 (ABI v52、LayoutWalk.hpp) は反射の宣言が無くても `MITIRU_GAME` が出す。

#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/module/LayoutWalk.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/Reflection.hpp>

namespace mitiru::module
{

constexpr const char* kLayoutHashSymbol     = "mitiru_module_layout_hash";
constexpr const char* kReflectFieldsSymbol  = "mitiru_module_reflect_fields";
constexpr const char* kReflectSchemasSymbol = "mitiru_module_reflect_schemas";
constexpr const char* kFloatOffsetsSymbol   = "mitiru_module_float_offsets";
constexpr const char* kPaddingBytesSymbol   = "mitiru_module_padding_bytes";
using ModuleLayoutHashFn     = std::uint64_t (*)();
/// out == nullptr なら数えるだけ。返り値は全件数 (cap より多いこともある)。
using ModuleReflectFieldsFn  = std::int32_t (*)(FieldDescriptor* out, std::int32_t cap);
using ModuleReflectSchemasFn = std::int32_t (*)(ReflectSchema* out, std::int32_t cap);
/// 位置の bit の意味は kFloatOffsetDouble。数え方は fields と同じ。
using ModuleFloatOffsetsFn   = std::int32_t (*)(std::uint32_t* out, std::int32_t cap);
using ModulePaddingBytesFn   = std::uint32_t (*)();

/// host が受け取る float の位置の上限。これを超える分は NaN を見ない (Debug の見張りを 1 フレーム数 ms に抑える)。
inline constexpr std::int32_t kMaxWatchedFloatOffsets = 1 << 20;

/// @brief 反射を引く 3 関数。DLL では GetProcAddress で、静的リンクでは linkedReflectionExports() から得る。
struct ReflectionExports
{
	ModuleLayoutHashFn     layoutHash = nullptr;
	ModuleReflectFieldsFn  fields     = nullptr;
	ModuleReflectSchemasFn schemas    = nullptr;
	ModuleFloatOffsetsFn   floatOffsets = nullptr;
	ModulePaddingBytesFn   paddingBytes = nullptr;
};

/// @brief 同じバイナリにリンクされた game の反射。DLL の中にも 1 つずつあるが、host は DLL では読まない。
inline ReflectionExports& linkedReflectionExports() noexcept
{
	static ReflectionExports exports{};
	return exports;
}

struct ModuleReflection
{
	std::vector<FieldDescriptor> fields;
	std::vector<ReflectSchema>   schemas;
	std::uint64_t                layoutFingerprint = 0;  ///< `mitiru_module_layout_hash`。0 = export 無し
	std::vector<std::uint32_t>   floatOffsets;           ///< `mitiru_module_float_offsets`。上限は kMaxWatchedFloatOffsets
	std::uint32_t                paddingBytes = kPaddingBytesUnknown;  ///< `mitiru_module_padding_bytes`

	[[nodiscard]] const FieldDescriptor* fieldsData() const noexcept { return fields.data(); }
	[[nodiscard]] std::int32_t fieldCount() const noexcept { return static_cast<std::int32_t>(fields.size()); }
	[[nodiscard]] const ReflectSchema* schemasData() const noexcept { return schemas.data(); }
	[[nodiscard]] std::int32_t schemaCount() const noexcept { return static_cast<std::int32_t>(schemas.size()); }

	/// @brief 反射記述子から作る layout hash (0 = 反射なし)。
	[[nodiscard]] std::uint64_t reflectHash() const noexcept
	{
		return layoutHash(fields.data(), fieldCount(), schemas.data(), schemaCount());
	}

	/// @brief セーブの照合に使う値。反射があればその hash (名前まで見る)、無ければ形の hash。
	[[nodiscard]] std::uint64_t identity() const noexcept
	{
		const std::uint64_t h = reflectHash();
		return (h != 0) ? h : layoutFingerprint;
	}

	/// @brief 3 関数から全件を集める。関数が欠けている項目は空のまま。
	[[nodiscard]] static ModuleReflection fromExports(const ReflectionExports& e)
	{
		ModuleReflection r;
		if (e.layoutHash != nullptr) { r.layoutFingerprint = e.layoutHash(); }
		if (e.fields != nullptr)
		{
			const std::int32_t n = e.fields(nullptr, 0);
			if (n > 0) { r.fields.resize(static_cast<std::size_t>(n)); (void)e.fields(r.fields.data(), n); }
		}
		if (e.schemas != nullptr)
		{
			const std::int32_t n = e.schemas(nullptr, 0);
			if (n > 0) { r.schemas.resize(static_cast<std::size_t>(n)); (void)e.schemas(r.schemas.data(), n); }
		}
		if (e.floatOffsets != nullptr)
		{
			const std::int32_t all = e.floatOffsets(nullptr, 0);
			const std::int32_t n = all < kMaxWatchedFloatOffsets ? all : kMaxWatchedFloatOffsets;
			if (n > 0) { r.floatOffsets.resize(static_cast<std::size_t>(n)); (void)e.floatOffsets(r.floatOffsets.data(), n); }
		}
		if (e.paddingBytes != nullptr) { r.paddingBytes = e.paddingBytes(); }
		return r;
	}
};

/// @brief GameMemory に詰め物があるときに host が 1 回だけ出す知らせ。
[[nodiscard]] inline std::string paddingWarningText(std::uint32_t bytes)
{
	return "GameMemory に暗黙の詰め物が " + std::to_string(bytes) + " byte あります。詰め物の byte は代入で値が決まらず、"
		"同じ状態でも巻き戻し・リプレイ・セーブの照合が食い違うことがあります。GameMemory の型を書いたファイルに "
		"MITIRU_ASSERT_NO_PADDING(型) を 1 行置くと、コンパイルで隙間の位置が出ます。フィールドを大きい順に"
		"並べ替えるか、std::uint8_t _pad[n]{} で埋めてください。";
}

/// @brief 2 つの記述を比べ、同じ GameMemory の形とみなせないか。どちらかに情報が無い項目は比べない。
[[nodiscard]] inline bool layoutDiffers(const ModuleReflection& a, const ModuleReflection& b) noexcept
{
	const std::uint64_t ra = a.reflectHash(), rb = b.reflectHash();
	if (ra != 0 && rb != 0 && ra != rb) { return true; }
	return a.layoutFingerprint != 0 && b.layoutFingerprint != 0 && a.layoutFingerprint != b.layoutFingerprint;
}

}  // namespace mitiru::module
