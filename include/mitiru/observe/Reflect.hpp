#pragma once

/// @file Reflect.hpp
/// @brief GameMemory バイト列 → 構造化 JSON (host 側、ADR 0018)
/// @details
/// game が `MITIRU_REFLECT` で申告した FieldDescriptor 表を使い、host が GameMemory の
/// 生バイト列 (現フレーム or time-travel ring の過去フレーム) を nlohmann::json に変換する。
/// AI が全フィールドを構造的に読めるようになる。host は layout を内蔵せず、記述子だけで動く
/// (ADR 0005 整合)。純関数・bounds-check 付き・例外を投げない。
///
/// 対応: スカラー(i8..u64/f32/f64/bool) / FixedString("str") / FixedVec("vec"、scalar 要素 or
/// 1 段ネスト struct 要素) / 直 nested struct("struct")。

#include <cstdint>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/module/Reflection.hpp>

namespace mitiru::observe
{

namespace detail
{

/// @brief tag のスカラーを p から読む (memcpy で alignment 安全)。未知 tag は null。
inline nlohmann::json readScalar(const std::uint8_t* p, const char* tag)
{
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };
	if (eq("f32"))  { float v;              std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("f64"))  { double v;             std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i32"))  { std::int32_t v;       std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("u32"))  { std::uint32_t v;      std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i64"))  { std::int64_t v;       std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("u64"))  { std::uint64_t v;      std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i16"))  { std::int16_t v;       std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("u16"))  { std::uint16_t v;      std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i8"))   { std::int8_t v;        std::memcpy(&v, p, sizeof(v)); return static_cast<int>(v); }
	if (eq("u8"))   { std::uint8_t v;       std::memcpy(&v, p, sizeof(v)); return static_cast<unsigned>(v); }
	if (eq("bool")) { std::uint8_t v;       std::memcpy(&v, p, sizeof(v)); return v != 0; }
	return nullptr;
}

/// @brief schemas から typeName==name のスキーマを探す。無ければ nullptr。
inline const mitiru::module::ReflectSchema* findSchema(
	const mitiru::module::ReflectSchema* schemas, std::int32_t schemaCount, const char* name)
{
	if (schemas == nullptr || name == nullptr || name[0] == '\0') { return nullptr; }
	for (std::int32_t i = 0; i < schemaCount; ++i)
	{
		if (std::strcmp(schemas[i].typeName, name) == 0) { return &schemas[i]; }
	}
	return nullptr;
}

}  // namespace detail

/// @brief GameMemory バイト列を記述子に従い構造化 JSON へ。bounds 外フィールドは黙って skip。
/// @param bytes      GameMemory の先頭 (現フレーム or ring.at(offset))
/// @param size       GameMemory のバイト数 (memorySize)
/// @param fields     FieldDescriptor 表 (ModuleApi.reflectFields)
/// @param fieldCount fields の数
/// @param schemas    FixedVec<struct,N> 用の要素スキーマ表 (ModuleApi.reflectSchemas)
/// @param schemaCount schemas の数
[[nodiscard]] inline nlohmann::json reflectToJson(
	const std::uint8_t*                  bytes,
	std::uint32_t                        size,
	const mitiru::module::FieldDescriptor* fields,
	std::int32_t                         fieldCount,
	const mitiru::module::ReflectSchema* schemas,
	std::int32_t                         schemaCount)
{
	nlohmann::json obj = nlohmann::json::object();
	if (bytes == nullptr || fields == nullptr) { return obj; }

	for (std::int32_t fi = 0; fi < fieldCount; ++fi)
	{
		const auto& f = fields[fi];
		if (f.name[0] == '\0') { continue; }
		const char* tag = f.typeTag;

		if (std::strcmp(tag, "vec") == 0)
		{
			// live count を countOffset から読む (容量 elemCount で clamp)。
			std::uint32_t cnt = 0;
			if (static_cast<std::uint64_t>(f.offset) + f.countOffset + sizeof(cnt) <= size)
			{
				std::memcpy(&cnt, bytes + f.offset + f.countOffset, sizeof(cnt));
			}
			if (cnt > f.elemCount) { cnt = f.elemCount; }

			nlohmann::json arr = nlohmann::json::array();
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			for (std::uint32_t i = 0; i < cnt; ++i)
			{
				const std::uint64_t elemEnd =
					static_cast<std::uint64_t>(f.offset) + static_cast<std::uint64_t>(i + 1) * f.elemSize;
				if (elemEnd > size) { break; }
				const std::uint8_t* ep = bytes + f.offset + static_cast<std::size_t>(i) * f.elemSize;
				if (sch != nullptr)
				{
					arr.push_back(reflectToJson(ep, f.elemSize, sch->fields, sch->fieldCount,
					                            schemas, schemaCount));
				}
				else
				{
					arr.push_back(detail::readScalar(ep, f.elemType));  // elemType = scalar tag
				}
			}
			obj[f.name] = std::move(arr);
		}
		else if (std::strcmp(tag, "str") == 0)
		{
			if (static_cast<std::uint64_t>(f.offset) + f.elemCount > size) { continue; }
			const char* s = reinterpret_cast<const char*>(bytes + f.offset);
			std::size_t len = 0;
			while (len < f.elemCount && s[len] != '\0') { ++len; }
			obj[f.name] = std::string(s, len);
		}
		else if (std::strcmp(tag, "struct") == 0)
		{
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			if (sch != nullptr &&
			    static_cast<std::uint64_t>(f.offset) + f.elemSize <= size)
			{
				obj[f.name] = reflectToJson(bytes + f.offset, f.elemSize, sch->fields,
				                            sch->fieldCount, schemas, schemaCount);
			}
		}
		else  // スカラー
		{
			if (static_cast<std::uint64_t>(f.offset) + f.elemSize > size) { continue; }
			obj[f.name] = detail::readScalar(bytes + f.offset, tag);
		}
	}
	return obj;
}

}  // namespace mitiru::observe
