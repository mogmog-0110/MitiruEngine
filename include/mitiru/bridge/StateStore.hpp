#pragma once

/// @file StateStore.hpp
/// @brief game が hud.set で送った値の最新を key ごとに持つ表。
///
/// UI (RmlUi) へは FrameIntents から直接写すので、ここは観測の側の写し。replay-as-test の
/// 状態の比較 (`--replay-test`) と palette の表示状態がここを読む。

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mitiru::bridge
{

/// @brief std::string キーの map を string_view で引くための透過ハッシュ。
struct TransparentStringHash
{
	using is_transparent = void;
	[[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
	{
		return std::hash<std::string_view>{}(sv);
	}
};

class StateStore
{
public:
	using json = ::nlohmann::json;
	using StateMap = std::unordered_map<std::string, json, TransparentStringHash, std::equal_to<>>;

	/// @brief 値を入れる。前と同じ値なら何もせず false を返す。
	template <typename T>
	bool set(std::string_view key, const T& value)
	{
		json encoded = value;
		if (const auto it = m_state.find(key); it != m_state.end())
		{
			if (it->second == encoded) { return false; }
			it->second = std::move(encoded);
			return true;
		}
		m_state.emplace(std::string(key), std::move(encoded));
		return true;
	}

	template <typename T>
	[[nodiscard]] std::optional<T> get(std::string_view key) const
	{
		const auto it = m_state.find(key);
		if (it == m_state.end()) { return std::nullopt; }
		try
		{
			return it->second.get<T>();
		}
		catch (const json::exception&)
		{
			return std::nullopt;
		}
	}

	[[nodiscard]] std::optional<json> getJson(std::string_view key) const
	{
		const auto it = m_state.find(key);
		if (it == m_state.end()) { return std::nullopt; }
		return it->second;
	}

	[[nodiscard]] bool has(std::string_view key) const { return m_state.contains(key); }

	void clear() { m_state.clear(); }

	/// @brief 全 key を 1 つの JSON object にする (`{"view.hp": 80, ...}`)。
	/// @details game 由来の文字列に UTF-8 でない byte があっても throw せず U+FFFD にする。
	[[nodiscard]] std::string snapshotJson(int indent = -1) const
	{
		json doc = json::object();
		for (const auto& [key, value] : m_state) { doc[key] = value; }
		return doc.dump(indent, ' ', false, json::error_handler_t::replace);
	}

private:
	StateMap m_state;
};

} // namespace mitiru::bridge
