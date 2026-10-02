#pragma once
// RmlUi の data binding に nlohmann::json の値をそのまま見せる VariableDefinition。
// RmlUi の data model は型を C++ の構造体として事前登録する前提だが、エンジンの HUD 状態は
// hud.set("view.pips", "[{\"on\":1},...]") のように実行時に形が決まる文字列で届く。
// 1 つの定義が値の実際の型 (数値 / 真偽 / 文字列 / 配列 / オブジェクト) を毎回見て振る舞うので、
// 配列なら data-for、オブジェクトなら item.field、スカラなら {{ }} がそのまま使える。

#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/Variant.h>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace mitiru::ui_rml
{

// bind.js の parseValue と同じ規則: '{' か '[' で始まれば JSON、数値・真偽に読めればその型、
// それ以外は文字列のまま。C++ 側の hud.set が送る形を、HTML 版と同じ解釈で受けるため。
inline nlohmann::json parseStateValue(std::string_view raw)
{
	const auto first = raw.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) { return std::string(raw); }
	const char c = raw[first];
	if (c == '{' || c == '[' || c == '-' || (c >= '0' && c <= '9') || raw == "true" || raw == "false")
	{
		auto parsed = nlohmann::json::parse(raw, nullptr, false);
		if (!parsed.is_discarded()) { return parsed; }
	}
	return std::string(raw);
}

class JsonVariableDefinition final : public Rml::VariableDefinition
{
public:
	JsonVariableDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Scalar) {}

	static JsonVariableDefinition& instance()
	{
		static JsonVariableDefinition def;
		return def;
	}

	bool Get(void* ptr, Rml::Variant& out) override
	{
		const auto& j = *static_cast<const nlohmann::json*>(ptr);
		if (j.is_boolean())              { out = j.get<bool>(); }
		else if (j.is_number_integer())  { out = j.get<int>(); }
		else if (j.is_number_float())    { out = j.get<double>(); }
		else if (j.is_string())          { out = j.get<std::string>(); }
		else if (j.is_null())            { out = Rml::String(); }
		else                             { out = j.dump(); }
		return true;
	}

	// UI から状態を書き換える経路は持たない (状態の持ち主は C++、UI は dispatch で頼むだけ)。
	// data-value の双方向束縛を書いても C++ の値は変わらず、次の set で上書きされる。
	bool Set(void*, const Rml::Variant&) override { return false; }

	int Size(void* ptr) override
	{
		const auto& j = *static_cast<const nlohmann::json*>(ptr);
		return j.is_array() ? static_cast<int>(j.size()) : 0;
	}

	Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
	{
		auto& j = *static_cast<nlohmann::json*>(ptr);
		if (j.is_array())
		{
			if (address.index >= 0 && address.index < static_cast<int>(j.size()))
			{
				return Rml::DataVariable(this, &j[static_cast<std::size_t>(address.index)]);
			}
			if (address.name == "size") { return Rml::MakeLiteralIntVariable(static_cast<int>(j.size())); }
			return {};
		}
		if (j.is_object() && !address.name.empty())
		{
			const auto it = j.find(address.name);
			if (it != j.end()) { return Rml::DataVariable(this, &*it); }
		}
		return {};
	}
};

} // namespace mitiru::ui_rml
