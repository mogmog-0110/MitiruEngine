#pragma once

/// @file FieldAttr.hpp
/// @brief `MITIRU_FIELD_RANGE` / `MITIRU_FIELD_UI_RANGE` / `MITIRU_FIELD_GROUP` の登録簿を
///        1 本の属性リストへ統合する。今までは「値域 (range)」と「表示グループ (group)」が
///        それぞれ別の静的レジストリ・別の文字列書式で `FieldDescriptor::elemType` に密輸
///        されていた (AutoReflect.hpp の旧 `fieldRangeRegistry`)。ここでは登録の受け皿を
///        `FieldAttr` 1 種類にまとめ、elemType へ書き出す文字列合成も 1 箇所
///        (`buildElemTypeTag`) に集約する。ABI は変えない。`FieldDescriptor` の POD レイアウト
///        は既存のまま (offset 不変) で、この統合は host 側の反射収集コードの内部整理にとどまる。
///        elemType の合成書式は次のとおり (scalar フィールドのみ対象。vec/struct/enum は別経路
///        で elemType を使うため group を除き対象外):
///        "range:<min>:<max>" (値域のみ、旧書式そのまま = 後方互換) /
///        "range:<min>:<max>;ui:<a>:<b>" (値域 + UI スライダー範囲) /
///        "ui:<a>:<b>" (UI 範囲のみ。値域未宣言なのでオラクルは範囲外検出をしない) に加え、
///        ";group:<name>" をどれにも追記できる。`observe::Oracle::parseRangeTag` は先頭が
///        "range:" の場合だけ値を読むので、後ろに ";ui:..." や ";group:..." が付いても
///        値域判定には影響しない。

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mitiru/module/Reflection.hpp>  // detail::copyTag

namespace mitiru::module::detail
{

/// @brief 1 フィールドに付けられる属性の種類。
enum class FieldAttrKind : std::uint8_t
{
	Range,    // 値域 (ClampMin/Max 相当)。observe::Oracle が範囲外検出に使う
	UiRange,  // inspector のスライダー範囲 (UIMin/Max 相当)。値域と独立
	Group,    // inspector の表示グループ名 (@export_group 相当)
};

/// @brief 属性 1 個。POD ではない (host プロセス内でしか使わず、DLL 境界は渡らない)。
struct FieldAttr
{
	FieldAttrKind kind;
	float         a = 0.0f;
	float         b = 0.0f;
	std::string   text;  // Group のときだけ使う
};

/// @brief "TypeName.fieldName" をキーに属性リストを積む登録簿。
inline std::unordered_map<std::string, std::vector<FieldAttr>>& fieldAttrRegistry()
{
	static std::unordered_map<std::string, std::vector<FieldAttr>> registry;
	return registry;
}

inline void registerFieldAttr(const char* typeName, const char* fieldName, FieldAttr attr)
{
	fieldAttrRegistry()[std::string(typeName) + "." + fieldName].push_back(std::move(attr));
}

/// @brief key ("Type.field") に登録済みの属性リストを引く。無ければ nullptr。
[[nodiscard]] inline const std::vector<FieldAttr>* findFieldAttrs(const std::string& key)
{
	const auto& reg = fieldAttrRegistry();
	const auto  it  = reg.find(key);
	return (it != reg.end()) ? &it->second : nullptr;
}

/// @brief 属性リストから種類 kind の 1 個目を探す。無ければ nullptr。
[[nodiscard]] inline const FieldAttr* findFieldAttrOfKind(
	const std::vector<FieldAttr>& attrs, FieldAttrKind kind)
{
	for (const auto& a : attrs) { if (a.kind == kind) { return &a; } }
	return nullptr;
}

/// @brief key に登録された Range/UiRange/Group を、ファイル冒頭の書式で 1 本の文字列に
///        合成する。何も登録が無ければ空文字列。
[[nodiscard]] inline std::string buildElemTypeTag(const std::string& key)
{
	const auto* attrs = findFieldAttrs(key);
	if (attrs == nullptr || attrs->empty()) { return {}; }

	const auto* range   = findFieldAttrOfKind(*attrs, FieldAttrKind::Range);
	const auto* uirange = findFieldAttrOfKind(*attrs, FieldAttrKind::UiRange);
	const auto* group   = findFieldAttrOfKind(*attrs, FieldAttrKind::Group);

	std::string tag;
	char        buf[32];
	if (range != nullptr)
	{
		std::snprintf(buf, sizeof(buf), "range:%g:%g",
			static_cast<double>(range->a), static_cast<double>(range->b));
		tag += buf;
	}
	if (uirange != nullptr)
	{
		std::snprintf(buf, sizeof(buf), "ui:%g:%g",
			static_cast<double>(uirange->a), static_cast<double>(uirange->b));
		if (!tag.empty()) { tag += ';'; }
		tag += buf;
	}
	if (group != nullptr)
	{
		if (!tag.empty()) { tag += ';'; }
		tag += "group:" + group->text;
	}
	return tag;
}

/// @brief `buildElemTypeTag` の結果を `FieldDescriptor::elemType` へ書く。32 byte を超える
///        組み合わせ (長い group 名 + range + ui) は既存の `copyTag` と同じく黙って切り詰まる
///        (AutoReflect.hpp 1-8 節が指摘する既知の制約であり、ここで新たに広げてはいない)。
inline void writeElemTypeTag(char* elemType, std::size_t cap, const std::string& key)
{
	const std::string tag = buildElemTypeTag(key);
	if (!tag.empty()) { copyTag(elemType, cap, tag.c_str()); }
}

}  // namespace mitiru::module::detail
