#pragma once

/// @file Reflect.hpp
/// @brief GameMemory バイト列 → 構造化 JSON (host 側)
/// @details
/// game が `MITIRU_REFLECT` で申告した FieldDescriptor 表を使い、host が GameMemory の
/// 生バイト列 (現フレーム or rewind ring の過去フレーム) を nlohmann::json に変換する。
/// AI が全フィールドを構造的に読めるようになる。host は layout を内蔵せず、記述子だけで動く。
/// 純関数・bounds-check 付き・例外を投げない。
///
/// 対応: スカラー(i8..u64/f32/f64/bool) / FixedString("str") / FixedVec("vec"、scalar 要素 or
/// 1 段ネスト struct 要素) / 直 nested struct("struct")。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/module/Reflection.hpp>
#include <mitiru/util/Hash.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/NumberAppend.hpp>

namespace mitiru::observe
{

namespace detail
{

/// @brief tag のスカラーを p から読み、JSON リテラルとして out へ直接追記する。
///        readScalar と同じ tag 解釈だが nlohmann::json を経由しない (C5)。
inline void appendScalarJson(std::string& out, const std::uint8_t* p, const char* tag)
{
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };
	if (eq("f32"))  { float v;          std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("f64"))  { double v;         std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("i32"))  { std::int32_t v;   std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("u32"))  { std::uint32_t v;  std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("i64"))  { std::int64_t v;   std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("u64"))  { std::uint64_t v;  std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("i16"))  { std::int16_t v;   std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("u16"))  { std::uint16_t v;  std::memcpy(&v, p, sizeof(v)); appendNumber(out, v); return; }
	if (eq("i8"))   { std::int8_t v;    std::memcpy(&v, p, sizeof(v)); appendNumber(out, static_cast<int>(v)); return; }
	if (eq("u8"))   { std::uint8_t v;   std::memcpy(&v, p, sizeof(v)); appendNumber(out, static_cast<unsigned>(v)); return; }
	if (eq("bool")) { std::uint8_t v;   std::memcpy(&v, p, sizeof(v)); out += (v != 0) ? "true" : "false"; return; }
	out += "null";
}

/// @brief tag のスカラーを p から読む (memcpy で alignment 安全)。未知 tag は null。
inline nlohmann::json readScalar(const std::uint8_t* p, const char* tag)
{
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };
	// 非有限値は JSON の null にまとめず "NaN"/"Inf"/"-Inf" 文字列にする (appendScalarJson と対応)。
	if (eq("f32"))  { float v;  std::memcpy(&v, p, sizeof(v)); if (!std::isfinite(v)) { return std::string(nonFiniteJsonName(v)); } return v; }
	if (eq("f64"))  { double v; std::memcpy(&v, p, sizeof(v)); if (!std::isfinite(v)) { return std::string(nonFiniteJsonName(v)); } return v; }
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

/// @brief JSON 値を tag のスカラーとして p へ書く (memcpy で alignment 安全)。
///        型不一致 (例: 文字列を f32 へ) や未知 tag は false。
inline bool writeScalar(std::uint8_t* p, const char* tag, const nlohmann::json& v)
{
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };
	// "NaN"/"Inf"/"-Inf" 文字列は readScalar が非有限値を書き出す形なので、書き戻しでも受ける。
	if (eq("f32"))
	{
		double nf = 0.0;
		if (v.is_string() && parseNonFiniteJsonName(v.get<std::string>(), nf))
		{
			float x = static_cast<float>(nf); std::memcpy(p, &x, sizeof(x)); return true;
		}
		if (!v.is_number()) { return false; }
		float x = v.get<float>(); std::memcpy(p, &x, sizeof(x)); return true;
	}
	if (eq("f64"))
	{
		double nf = 0.0;
		if (v.is_string() && parseNonFiniteJsonName(v.get<std::string>(), nf))
		{
			std::memcpy(p, &nf, sizeof(nf)); return true;
		}
		if (!v.is_number()) { return false; }
		double x = v.get<double>(); std::memcpy(p, &x, sizeof(x)); return true;
	}
	if (eq("bool")) { if (!v.is_boolean()) { return false; } std::uint8_t x = v.get<bool>() ? 1 : 0; std::memcpy(p, &x, sizeof(x)); return true; }
	if (!v.is_number_integer()) { return false; }
	const long long iv = v.get<long long>();
	// static_cast だけだと範囲外の値がエラーにならずに wrap する (例: i8 に 300 を書くと 44 になる)。
	// reflectWriteField の呼び出し元は書き込み API (branch editor の PUT /api/ai/state 等)
	// なので、範囲外は書き込み失敗として reject し、値の破損より先にエラーを見せる。
	const auto inRange = [&](long long lo, long long hi) { return iv >= lo && iv <= hi; };
	if (eq("i8"))  { if (!inRange(INT8_MIN, INT8_MAX))   { return false; } auto x = static_cast<std::int8_t>(iv);   std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u8"))  { if (!inRange(0, UINT8_MAX))         { return false; } auto x = static_cast<std::uint8_t>(iv);  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i16")) { if (!inRange(INT16_MIN, INT16_MAX)) { return false; } auto x = static_cast<std::int16_t>(iv);  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u16")) { if (!inRange(0, UINT16_MAX))        { return false; } auto x = static_cast<std::uint16_t>(iv); std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i32")) { if (!inRange(INT32_MIN, INT32_MAX)) { return false; } auto x = static_cast<std::int32_t>(iv);  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u32")) { if (!inRange(0, UINT32_MAX))        { return false; } auto x = static_cast<std::uint32_t>(iv); std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i64")) { auto x = static_cast<std::int64_t>(iv);  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u64")) { if (iv < 0) { return false; } auto x = static_cast<std::uint64_t>(iv); std::memcpy(p, &x, sizeof(x)); return true; }
	return false;
}

}  // namespace detail

/// @brief GameMemory バイト列を記述子に従い構造化 JSON へ。bounds 外フィールドは黙って skip。
/// @param bytes      GameMemory の先頭 (現フレーム or ring.at(offset))
/// @param size       GameMemory のバイト数 (memorySize)
/// @param fields     FieldDescriptor 表 (ModuleReflection::fields)
/// @param fieldCount fields の数
/// @param schemas    FixedVec<struct,N> 用の要素スキーマ表 (ModuleReflection::schemas)
/// @param schemaCount schemas の数
[[nodiscard]] inline nlohmann::json reflectToJson(
	const std::uint8_t*                  bytes,
	std::uint32_t                        size,
	const mitiru::module::FieldDescriptor* fields,
	std::int32_t                         fieldCount,
	const mitiru::module::ReflectSchema* schemas,
	std::int32_t                         schemaCount)
{
	MITIRU_ZONE_NAMED("observe::reflectToJson");
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

/// @brief reflectToJson と同じフィールド解釈規約で、nlohmann の DOM を経由せず JSON 文字列を
///        直接 out へ追記する (C5)。to_chars ベースの直書きにより DOM 構築 + dump の 2 段階を
///        1 段にする。出力バイト列は DOM 版と一致しないが (キー順序・数値表現は同一)、
///        parse すれば同じ構造になる。
inline void reflectAppendJson(
	std::string&                          out,
	const std::uint8_t*                   bytes,
	std::uint32_t                         size,
	const mitiru::module::FieldDescriptor* fields,
	std::int32_t                          fieldCount,
	const mitiru::module::ReflectSchema*  schemas,
	std::int32_t                          schemaCount)
{
	out += '{';
	if (bytes == nullptr || fields == nullptr) { out += '}'; return; }

	bool first = true;
	const auto beginField = [&](const char* name) {
		if (!first) { out += ','; }
		first = false;
		out += '"';
		out += name;
		out += "\":";
	};

	for (std::int32_t fi = 0; fi < fieldCount; ++fi)
	{
		const auto& f = fields[fi];
		if (f.name[0] == '\0') { continue; }
		const char* tag = f.typeTag;

		if (std::strcmp(tag, "vec") == 0)
		{
			std::uint32_t cnt = 0;
			if (static_cast<std::uint64_t>(f.offset) + f.countOffset + sizeof(cnt) <= size)
			{
				std::memcpy(&cnt, bytes + f.offset + f.countOffset, sizeof(cnt));
			}
			if (cnt > f.elemCount) { cnt = f.elemCount; }

			beginField(f.name);
			out += '[';
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			bool elemFirst = true;
			for (std::uint32_t i = 0; i < cnt; ++i)
			{
				const std::uint64_t elemEnd =
					static_cast<std::uint64_t>(f.offset) + static_cast<std::uint64_t>(i + 1) * f.elemSize;
				if (elemEnd > size) { break; }
				if (!elemFirst) { out += ','; }
				elemFirst = false;
				const std::uint8_t* ep = bytes + f.offset + static_cast<std::size_t>(i) * f.elemSize;
				if (sch != nullptr)
				{
					reflectAppendJson(out, ep, f.elemSize, sch->fields, sch->fieldCount, schemas, schemaCount);
				}
				else
				{
					detail::appendScalarJson(out, ep, f.elemType);
				}
			}
			out += ']';
		}
		else if (std::strcmp(tag, "str") == 0)
		{
			if (static_cast<std::uint64_t>(f.offset) + f.elemCount > size) { continue; }
			const char* s = reinterpret_cast<const char*>(bytes + f.offset);
			std::size_t len = 0;
			while (len < f.elemCount && s[len] != '\0') { ++len; }
			beginField(f.name);
			out += '"';
			out += jsonEscape(std::string_view(s, len));
			out += '"';
		}
		else if (std::strcmp(tag, "struct") == 0)
		{
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			if (sch == nullptr || static_cast<std::uint64_t>(f.offset) + f.elemSize > size) { continue; }
			beginField(f.name);
			reflectAppendJson(out, bytes + f.offset, f.elemSize, sch->fields, sch->fieldCount, schemas, schemaCount);
		}
		else  // スカラー
		{
			if (static_cast<std::uint64_t>(f.offset) + f.elemSize > size) { continue; }
			beginField(f.name);
			detail::appendScalarJson(out, bytes + f.offset, tag);
		}
	}
	out += '}';
}

/// @brief dirty 検知用の鍵。bytes だけでなく記述子表も混ぜる (hot reload で GameMemory の bytes が
/// 同じまま field の名前や offset が変わることがある)。
[[nodiscard]] inline std::uint64_t reflectCacheKey(
	const std::uint8_t* bytes, std::uint32_t size,
	const mitiru::module::FieldDescriptor* fields, std::int32_t fieldCount,
	const mitiru::module::ReflectSchema* schemas, std::int32_t schemaCount) noexcept
{
	std::uint64_t h = (bytes != nullptr) ? ::mitiru::util::Hash::fnv1a(bytes, size) : 0;
	if (fields != nullptr && fieldCount > 0)
	{
		h ^= ::mitiru::util::Hash::fnv1a(reinterpret_cast<const std::uint8_t*>(fields), static_cast<std::uint32_t>(fieldCount) * sizeof(mitiru::module::FieldDescriptor)) * 0x9E3779B97F4A7C15ull;
	}
	if (schemas != nullptr && schemaCount > 0)
	{
		h ^= ::mitiru::util::Hash::fnv1a(reinterpret_cast<const std::uint8_t*>(schemas), static_cast<std::uint32_t>(schemaCount) * sizeof(mitiru::module::ReflectSchema)) * 0xC2B2AE3D27D4EB4Full;
	}
	return h;
}

/// @brief GameMemory バイト列を JSON 文字列に変換する。直前の呼び出しと FNV-1a ハッシュが
///        一致する間は保持済み文字列をそのまま返し、reflectAppendJson を呼ばない (C5)。
///        `/api/ai/state` 系は同一内容のフレームで繰り返し polling されうる。
class ReflectJsonStringCache
{
public:
	[[nodiscard]] const std::string& get(
		const std::uint8_t*                   bytes,
		std::uint32_t                         size,
		const mitiru::module::FieldDescriptor* fields,
		std::int32_t                          fieldCount,
		const mitiru::module::ReflectSchema*  schemas,
		std::int32_t                          schemaCount)
	{
		MITIRU_ZONE_NAMED("observe::ReflectJsonStringCache::get");
		const std::uint64_t h = reflectCacheKey(bytes, size, fields, fieldCount, schemas, schemaCount);
		if (!m_valid || h != m_hash || bytes == nullptr)
		{
			m_json.clear();
			reflectAppendJson(m_json, bytes, size, fields, fieldCount, schemas, schemaCount);
			m_hash  = h;
			m_valid = (bytes != nullptr);
		}
		return m_json;
	}

private:
	std::string   m_json;
	std::uint64_t m_hash  = 0;
	bool          m_valid = false;
};

/// @brief reflectToJson (DOM 版) を FNV-1a ハッシュで dirty 検知しながら返す。diff は
///        値の構造比較が要るため文字列ではなく DOM (nlohmann::json) を保持する (C5)。
class ReflectJsonCache
{
public:
	[[nodiscard]] const nlohmann::json& get(
		const std::uint8_t*                   bytes,
		std::uint32_t                         size,
		const mitiru::module::FieldDescriptor* fields,
		std::int32_t                          fieldCount,
		const mitiru::module::ReflectSchema*  schemas,
		std::int32_t                          schemaCount)
	{
		MITIRU_ZONE_NAMED("observe::ReflectJsonCache::get");
		const std::uint64_t h = reflectCacheKey(bytes, size, fields, fieldCount, schemas, schemaCount);
		if (!m_valid || h != m_hash)
		{
			m_json  = reflectToJson(bytes, size, fields, fieldCount, schemas, schemaCount);
			m_hash  = h;
			m_valid = true;
		}
		return m_json;
	}

private:
	nlohmann::json m_json  = nlohmann::json::object();
	std::uint64_t  m_hash  = 0;
	bool           m_valid = false;
};

/// @brief dotted path が指す 1 フィールドの絶対 byte offset/size を、書き込まずに求める。
///        reflectWriteField と同じ経路解決規約 (struct を "outer.inner" で辿る、
///        str/vec は非対応) を共有する。呼び出し側はこれで判明した範囲だけを退避すれば、
///        GameMemory 全体をコピーせずに write→読み取り→復元ができる (C8)。
[[nodiscard]] inline bool reflectFieldByteSpan(
	const mitiru::module::FieldDescriptor* fields,
	std::int32_t                           fieldCount,
	const mitiru::module::ReflectSchema*   schemas,
	std::int32_t                           schemaCount,
	std::uint32_t                          size,
	const std::string&                     path,
	std::uint32_t&                         outOffset,
	std::uint32_t&                         outSize)
{
	if (fields == nullptr) { return false; }
	const auto dot = path.find('.');
	const std::string head = (dot == std::string::npos) ? path : path.substr(0, dot);
	const std::string rest = (dot == std::string::npos) ? std::string{} : path.substr(dot + 1);

	for (std::int32_t fi = 0; fi < fieldCount; ++fi)
	{
		const auto& f = fields[fi];
		if (head != f.name) { continue; }

		if (std::strcmp(f.typeTag, "struct") == 0)
		{
			if (rest.empty()) { return false; }
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			if (sch == nullptr || static_cast<std::uint64_t>(f.offset) + f.elemSize > size) { return false; }
			std::uint32_t innerOffset = 0, innerSize = 0;
			if (!reflectFieldByteSpan(sch->fields, sch->fieldCount, schemas, schemaCount,
			                          f.elemSize, rest, innerOffset, innerSize))
			{ return false; }
			outOffset = f.offset + innerOffset;
			outSize   = innerSize;
			return true;
		}

		if (!rest.empty()) { return false; }
		if (static_cast<std::uint64_t>(f.offset) + f.elemSize > size) { return false; }
		outOffset = f.offset;
		outSize   = f.elemSize;
		return true;
	}
	return false;
}

/// @brief dotted path (例 "outer.inner") で reflect field を辿り、GameMemory へ 1 個だけ書く。
/// @details reflectToJson の書き込み対応。str/vec は非対応 (inspector からの書き戻しは
///          スカラーのみ、3-3)。呼び出し側が書き込み前後で GameMemory を退避/復元する
///          "分岐" 経路であることが前提 (live へ直接残さない、決定論を壊さない)。
/// @param path  トップフィールド名、または "struct" フィールドを挟んだ "outer.inner"
/// @param err   失敗理由 (unknown field / kind mismatch / out of bounds)
[[nodiscard]] inline bool reflectWriteField(
	std::uint8_t*                          bytes,
	std::uint32_t                          size,
	const mitiru::module::FieldDescriptor* fields,
	std::int32_t                           fieldCount,
	const mitiru::module::ReflectSchema*   schemas,
	std::int32_t                           schemaCount,
	const std::string&                     path,
	const nlohmann::json&                  value,
	std::string&                           err)
{
	if (bytes == nullptr || fields == nullptr) { err = "GameMemory 未初期化"; return false; }
	const auto dot = path.find('.');
	const std::string head = (dot == std::string::npos) ? path : path.substr(0, dot);
	const std::string rest = (dot == std::string::npos) ? std::string{} : path.substr(dot + 1);

	for (std::int32_t fi = 0; fi < fieldCount; ++fi)
	{
		const auto& f = fields[fi];
		if (head != f.name) { continue; }

		if (std::strcmp(f.typeTag, "struct") == 0)
		{
			if (rest.empty()) { err = "field '" + path + "' は struct です (outer.inner で指定)"; return false; }
			const auto* sch = detail::findSchema(schemas, schemaCount, f.elemType);
			if (sch == nullptr || static_cast<std::uint64_t>(f.offset) + f.elemSize > size)
			{ err = "field '" + path + "' のスキーマが見つかりません"; return false; }
			return reflectWriteField(bytes + f.offset, f.elemSize, sch->fields, sch->fieldCount,
			                          schemas, schemaCount, rest, value, err);
		}

		if (!rest.empty()) { err = "unknown field '" + path + "'"; return false; }
		if (static_cast<std::uint64_t>(f.offset) + f.elemSize > size)
		{ err = "field '" + path + "' が GameMemory の範囲外です"; return false; }
		if (!detail::writeScalar(bytes + f.offset, f.typeTag, value))
		{ err = "field '" + path + "' の型が一致しません (期待: " + std::string(f.typeTag) + ")"; return false; }
		return true;
	}
	err = "unknown field '" + path + "'";
	return false;
}

/// @brief 名前 + 型が一致する field だけ旧 GameMemory バイト列から新 GameMemory バイト列へ
/// 値を引き継ぐ (P9: hot reload でフィールド追加/並べ替えが起きても state を保つ)。newBytes は
/// 呼び出し側が事前に on_init 済みであること (一致しない field は新側の初期値のまま残る)。
/// FixedVec/直ネスト struct は elemType (要素型名) も一致する場合だけ 1 段ネストで再帰する。
inline void migrateReflectedMemory(
	const std::uint8_t*                    oldBytes,   std::uint32_t oldSize,
	const module::FieldDescriptor*         oldFields,  std::int32_t  oldFieldCount,
	const module::ReflectSchema*           oldSchemas, std::int32_t  oldSchemaCount,
	std::uint8_t*                          newBytes,   std::uint32_t newSize,
	const module::FieldDescriptor*         newFields,  std::int32_t  newFieldCount,
	const module::ReflectSchema*           newSchemas, std::int32_t  newSchemaCount)
{
	if (oldBytes == nullptr || newBytes == nullptr || oldFields == nullptr || newFields == nullptr) { return; }

	for (std::int32_t ni = 0; ni < newFieldCount; ++ni)
	{
		const auto& nf = newFields[ni];
		if (nf.name[0] == '\0') { continue; }

		const module::FieldDescriptor* of = nullptr;
		for (std::int32_t oi = 0; oi < oldFieldCount; ++oi)
		{
			if (std::strcmp(oldFields[oi].name, nf.name) == 0) { of = &oldFields[oi]; break; }
		}
		// 新規 field (旧に無い) / 型変更 (typeTag 不一致) は引き継がず新側の初期値のまま残す。
		if (of == nullptr || std::strcmp(of->typeTag, nf.typeTag) != 0) { continue; }
		if (static_cast<std::uint64_t>(nf.offset) + nf.elemSize > newSize ||
		    static_cast<std::uint64_t>(of->offset) + of->elemSize > oldSize) { continue; }

		if (std::strcmp(nf.typeTag, "vec") == 0)
		{
			if (std::strcmp(of->elemType, nf.elemType) != 0) { continue; }  // 要素型変更
			std::uint32_t oldCnt = 0;
			if (static_cast<std::uint64_t>(of->offset) + of->countOffset + sizeof(oldCnt) <= oldSize)
			{ std::memcpy(&oldCnt, oldBytes + of->offset + of->countOffset, sizeof(oldCnt)); }
			const std::uint32_t n = (std::min)({oldCnt, of->elemCount, nf.elemCount});
			const auto* nsch = detail::findSchema(newSchemas, newSchemaCount, nf.elemType);
			const auto* osch = detail::findSchema(oldSchemas, oldSchemaCount, of->elemType);
			for (std::uint32_t e = 0; e < n; ++e)
			{
				const std::uint8_t* op = oldBytes + of->offset + static_cast<std::size_t>(e) * of->elemSize;
				std::uint8_t*       np = newBytes + nf.offset + static_cast<std::size_t>(e) * nf.elemSize;
				if (nsch != nullptr && osch != nullptr)
				{
					migrateReflectedMemory(op, of->elemSize, osch->fields, osch->fieldCount, oldSchemas, oldSchemaCount,
						np, nf.elemSize, nsch->fields, nsch->fieldCount, newSchemas, newSchemaCount);
				}
				else if (nsch == nullptr && osch == nullptr && of->elemSize == nf.elemSize)
				{
					std::memcpy(np, op, nf.elemSize);
				}
			}
			if (static_cast<std::uint64_t>(nf.offset) + nf.countOffset + sizeof(n) <= newSize)
			{ std::memcpy(newBytes + nf.offset + nf.countOffset, &n, sizeof(n)); }
		}
		else if (std::strcmp(nf.typeTag, "struct") == 0)
		{
			if (std::strcmp(of->elemType, nf.elemType) != 0) { continue; }
			const auto* nsch = detail::findSchema(newSchemas, newSchemaCount, nf.elemType);
			const auto* osch = detail::findSchema(oldSchemas, oldSchemaCount, of->elemType);
			if (nsch == nullptr || osch == nullptr) { continue; }
			migrateReflectedMemory(oldBytes + of->offset, of->elemSize, osch->fields, osch->fieldCount, oldSchemas, oldSchemaCount,
				newBytes + nf.offset, nf.elemSize, nsch->fields, nsch->fieldCount, newSchemas, newSchemaCount);
		}
		else if (std::strcmp(nf.typeTag, "str") == 0)
		{
			const std::uint32_t n = (std::min)(of->elemCount, nf.elemCount);
			std::memcpy(newBytes + nf.offset, oldBytes + of->offset, n);
			if (nf.elemCount > n) { newBytes[nf.offset + n] = '\0'; }  // 縮小コピー時の終端保証
		}
		else  // スカラー
		{
			if (of->elemSize != nf.elemSize) { continue; }
			std::memcpy(newBytes + nf.offset, oldBytes + of->offset, nf.elemSize);
		}
	}
}

}  // namespace mitiru::observe
