#pragma once

/// @file StateMachineInspect.hpp
/// @brief ゲームが持つ状態機械を、ツール窓 (--page statemachine) に名前つきの図として見せるための宣言 (読み取り専用)。
/// @details 状態の番号そのものは GameMemory の flat POD の中にある (列挙型 1 つ、または列挙型の番号を入れた float)。
///          ゲーム DLL はそれがどこにあるか (offsetof) と、番号の名前、通れる遷移を MITIRU_INSPECT_ASSETS の kind "statemachine"
///          で渡す。host は JSON が指す範囲を毎フレーム見て、変わった所を記録して窓へ送るだけで、状態機械の中身を知らない。
///          図は宣言した遷移から描き、宣言に無い遷移が起きたら窓が赤で出す。宣言そのものの確かめにもなる。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::debug
{

inline constexpr const char* kStateMachineInspectKind = "statemachine";

/// @brief 状態の番号が GameMemory の中でどう入っているか
enum class StateValueType : std::uint8_t
{
	U8,
	U16,
	U32,
	I32,
	F32,   ///< 計器 (Probe) が番号を float で持っている形。四捨五入して番号にする
};

[[nodiscard]] constexpr std::uint32_t stateValueSize(StateValueType t) noexcept
{
	switch (t)
	{
	case StateValueType::U8: return 1;
	case StateValueType::U16: return 2;
	default: return 4;
	}
}

[[nodiscard]] constexpr const char* stateValueName(StateValueType t) noexcept
{
	switch (t)
	{
	case StateValueType::U8: return "u8";
	case StateValueType::U16: return "u16";
	case StateValueType::U32: return "u32";
	case StateValueType::I32: return "i32";
	default: return "f32";
	}
}

/// @brief host が写した生の値 (先頭から size byte を little endian で並べた物) を、宣言の型で状態の番号にする
[[nodiscard]] inline long long decodeStateValue(std::string_view type, std::uint32_t raw) noexcept
{
	if (type == "i32") { return static_cast<std::int32_t>(raw); }
	if (type == "f32")
	{
		float f = 0.0f;
		std::memcpy(&f, &raw, sizeof(f));
		// NaN や桁あふれの float を整数にすると未定義なので、範囲の外は 0 番に倒す
		if (!(f > -2.0e9f && f < 2.0e9f)) { return 0; }
		return static_cast<long long>(f + (f < 0.0f ? -0.5f : 0.5f));
	}
	return static_cast<long long>(raw);
}

struct StateDecl
{
	int         value = 0;
	const char* name = "";
};

struct EdgeDecl
{
	int         from = 0;
	int         to = 0;
	const char* label = "";   ///< 遷移の理由。空なら出さない
};

struct MachineDecl
{
	const char*            id = "";       ///< 窓の中で機械を区別する名前 (英数字)
	const char*            label = "";    ///< 窓に出す名前
	std::size_t            offset = 0;    ///< GameMemory の先頭からの位置 (offsetof)
	StateValueType         type = StateValueType::U8;
	std::vector<StateDecl> states;
	std::vector<EdgeDecl>  edges;         ///< 通れる遷移。空なら起きた遷移だけを図に足す
};

/// @brief 状態の置き場。型と位置は C++ の構造体の事実なので、名前や遷移を書いた JSON とは分けて C++ に持つ
struct MachineBinding
{
	const char*    id = "";
	std::size_t    offset = 0;
	StateValueType type = StateValueType::U8;
};

/// @brief 宣言をまとめて JSON にする。DLL が読み込みの時に作り、text() を InspectAsset の data に使う
class StateMachineInspect
{
public:
	/// @brief 外部の JSON ({"machines":[{"id","label","states":[{"v","name"}],"edges":[{"from","to","label"}]}]}) を読み、
	///        id が同じ binding の位置と型を足す。コメントは書かない JSON。binding の無い機械と、機械の無い binding は
	///        error に書いて飛ばす。返すのは読めた機械の数
	std::size_t load(std::string_view jsonText, const std::vector<MachineBinding>& bindings, std::string& error)
	{
		const nlohmann::json file = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
		if (!file.is_object() || !file.contains("machines") || !file["machines"].is_array())
		{
			error = "状態機械の JSON を読めません (machines の配列が無い)";
			return 0;
		}
		std::size_t loaded = 0;
		std::vector<bool> used(bindings.size(), false);
		for (const nlohmann::json& m : file["machines"])
		{
			const std::string id = m.value("id", std::string());
			std::size_t at = bindings.size();
			for (std::size_t i = 0; i < bindings.size(); ++i) { if (id == bindings[i].id) { at = i; } }
			if (at == bindings.size())
			{
				error += (error.empty() ? "" : " / ") + std::string("置き場の無い機械 ") + id;
				continue;
			}
			used[at] = true;
			nlohmann::json out = m;
			out["offset"] = bindings[at].offset;
			out["type"] = stateValueName(bindings[at].type);
			out["size"] = stateValueSize(bindings[at].type);
			m_root["machines"].push_back(std::move(out));
			++loaded;
		}
		for (std::size_t i = 0; i < bindings.size(); ++i)
		{
			if (!used[i]) { error += (error.empty() ? "" : " / ") + std::string("JSON に無い置き場 ") + bindings[i].id; }
		}
		return loaded;
	}

	StateMachineInspect& machine(const MachineDecl& decl)
	{
		nlohmann::json states = nlohmann::json::array();
		for (const StateDecl& s : decl.states) { states.push_back({{"v", s.value}, {"name", s.name}}); }
		nlohmann::json edges = nlohmann::json::array();
		for (const EdgeDecl& e : decl.edges) { edges.push_back({{"from", e.from}, {"to", e.to}, {"label", e.label}}); }
		m_root["machines"].push_back({{"id", decl.id}, {"label", decl.label}, {"offset", decl.offset},
		                              {"type", stateValueName(decl.type)}, {"size", stateValueSize(decl.type)},
		                              {"states", std::move(states)}, {"edges", std::move(edges)}});
		return *this;
	}

	/// @brief 今までに渡した物の JSON。返した文字列は次にこの関数を呼ぶまで変わらない
	[[nodiscard]] const std::string& text()
	{
		m_text = m_root.dump();
		return m_text;
	}

	/// @brief MITIRU_INSPECT_ASSETS の関数で out に書く 1 件。text() を呼んだ後に使う
	[[nodiscard]] module::InspectAsset asset() const noexcept
	{
		module::InspectAsset a{};
		std::memcpy(a.kind, kStateMachineInspectKind, std::strlen(kStateMachineInspectKind));
		std::memcpy(a.name, "statemachine", 12);
		a.data = m_text.data();
		a.size = m_text.size();
		return a;
	}

private:
	nlohmann::json m_root = {{"machines", nlohmann::json::array()}};
	std::string    m_text;
};

}  // namespace mitiru::debug
