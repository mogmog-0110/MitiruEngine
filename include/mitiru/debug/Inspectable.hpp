#pragma once

/// @file Inspectable.hpp
/// @brief 「inspector window として開ける」object の登録 API
/// @details
/// engine の atomic-tools 哲学 (= 必要なものしか画面に出さない) を構造的に
/// 実現するための核。ユーザコードが任意の object を「inspect 可能なもの」
/// として **名前付きで登録** すると、command palette がそれを列挙し、
/// ユーザが選んだ時にだけ **その 1 つだけ** のウィンドウが開く。
///
/// 二段階のライフタイム:
///
/// **Global** — `registerInspectable(name, title, fn)` を起動時に呼ぶ。
/// engine 終了まで生きる。engine 自身が "input" / "log" / "perf" などを
/// この経路で予め登録する。
///
/// **Local** — `LocalInspectable` を object メンバーとして抱える。
/// コンストラクタで auto-register、デストラクタで auto-unregister。
/// `Player` クラスが `LocalInspectable m_inspector{...}` を持てば、
/// その Player が生きてる間だけ palette に出る。RAII で leak しない。
///
/// 登録された Inspectable はフレーム毎にスナップショット (`nlohmann::json`) を
/// 生成する関数を持つ。引数なしで呼ばれて任意の JSON を返せばよい。
///
/// @code
/// // global
/// mitiru::debug::registerInspectable("audio", "Audio mixer",
///     []{ return nlohmann::json{{"masterVolume", 0.8f}}; });
///
/// // local (typical pattern)
/// class Player {
///     mitiru::debug::LocalInspectable m_inspector{
///         "player1", "Player #1",
///         [this]{ return nlohmann::json{{"hp", m_hp}, {"x", m_pos.x}}; }};
/// };
/// @endcode

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::debug
{

/// @brief 1 つの inspect 可能 object の記述
struct Inspectable
{
	std::string                       name;   ///< unique id; palette + CLI が参照
	std::string                       title;  ///< 人間用ラベル (palette / window title)
	std::function<nlohmann::json()>   snapshot;  ///< 毎回呼ばれて current state を返す

	[[nodiscard]] nlohmann::json safeSnapshot() const
	{
		try
		{
			return snapshot ? snapshot() : nlohmann::json{};
		}
		catch (...)
		{
			return nlohmann::json{{"error", "inspectable threw"}};
		}
	}
};

/// @brief プロセスローカルなレジストリ (thread-safe)
/// @details engine 側がフレーム毎に `allMeta()` で palette 列挙用メタを集め、
///          `snapshotByName(name)` で特定 inspectable の最新 state を取得する。
///          ユーザコードは直接触らず `registerInspectable` / `LocalInspectable`
///          を経由するのが意図。
class InspectableRegistry
{
public:
	using Handle = std::uint64_t;

	/// @brief 登録。token を返す。
	static Handle add(Inspectable insp)
	{
		std::lock_guard<std::mutex> lock(mutex());
		auto& entries = data();
		const Handle h = ++nextHandle();
		entries.push_back({h, std::move(insp)});
		return h;
	}

	/// @brief Handle で削除 (RAII LocalInspectable が使う)
	static void remove(Handle h) noexcept
	{
		if (h == 0) { return; }
		std::lock_guard<std::mutex> lock(mutex());
		auto& entries = data();
		entries.erase(
			std::remove_if(entries.begin(), entries.end(),
				[h](const Entry& e) { return e.handle == h; }),
			entries.end());
	}

	/// @brief palette 列挙用 — name + title の組のリストを返す
	[[nodiscard]] static std::vector<std::pair<std::string, std::string>> allMeta()
	{
		std::lock_guard<std::mutex> lock(mutex());
		std::vector<std::pair<std::string, std::string>> out;
		out.reserve(data().size());
		for (const auto& e : data())
		{
			out.push_back({e.insp.name, e.insp.title});
		}
		return out;
	}

	/// @brief name に紐づく Inspectable のスナップショットを返す
	[[nodiscard]] static std::optional<nlohmann::json> snapshotByName(
		const std::string& name)
	{
		std::lock_guard<std::mutex> lock(mutex());
		for (const auto& e : data())
		{
			if (e.insp.name == name)
			{
				return e.insp.safeSnapshot();
			}
		}
		return std::nullopt;
	}

	/// @brief 全登録 inspectable の name → state map を返す (snapshot writer 用)
	[[nodiscard]] static nlohmann::json allAsJsonMap()
	{
		std::lock_guard<std::mutex> lock(mutex());
		nlohmann::json out = nlohmann::json::object();
		for (const auto& e : data())
		{
			out[e.insp.name] = nlohmann::json{
				{"title", e.insp.title},
				{"state", e.insp.safeSnapshot()},
			};
		}
		return out;
	}

	/// @brief テスト用 — 全エントリ削除
	static void clear()
	{
		std::lock_guard<std::mutex> lock(mutex());
		data().clear();
	}

	/// @brief 現在の登録数
	[[nodiscard]] static std::size_t size()
	{
		std::lock_guard<std::mutex> lock(mutex());
		return data().size();
	}

private:
	struct Entry { Handle handle; Inspectable insp; };

	static std::mutex& mutex()
	{
		static std::mutex m;
		return m;
	}
	static std::vector<Entry>& data()
	{
		static std::vector<Entry> v;
		return v;
	}
	static Handle& nextHandle()
	{
		static Handle h{0};
		return h;
	}
};

/// @brief Global 登録ヘルパー。
/// @details 削除する手段は提供しない (engine 終了まで生きる前提)。
///          object lifetime に紐付けたい場合は LocalInspectable を使う。
inline InspectableRegistry::Handle registerInspectable(
	std::string name,
	std::string title,
	std::function<nlohmann::json()> snapshot)
{
	return InspectableRegistry::add({
		std::move(name), std::move(title), std::move(snapshot),
	});
}

/// @brief RAII で auto-register / auto-unregister する Inspectable
/// @details object のメンバーに置く想定。
/// @code
/// class Player {
///     mitiru::debug::LocalInspectable m_inspector{
///         "player1", "Player #1",
///         [this]{ return nlohmann::json{{"hp", m_hp}}; }};
/// };
/// @endcode
class LocalInspectable
{
public:
	LocalInspectable(std::string name, std::string title,
	                 std::function<nlohmann::json()> snapshot)
		: m_handle(InspectableRegistry::add({
			std::move(name), std::move(title), std::move(snapshot)}))
	{
	}

	~LocalInspectable()
	{
		InspectableRegistry::remove(m_handle);
	}

	LocalInspectable(const LocalInspectable&) = delete;
	LocalInspectable& operator=(const LocalInspectable&) = delete;

	LocalInspectable(LocalInspectable&& other) noexcept
		: m_handle(other.m_handle)
	{
		other.m_handle = 0;
	}
	LocalInspectable& operator=(LocalInspectable&& other) noexcept
	{
		if (this != &other)
		{
			InspectableRegistry::remove(m_handle);
			m_handle = other.m_handle;
			other.m_handle = 0;
		}
		return *this;
	}

	[[nodiscard]] InspectableRegistry::Handle handle() const noexcept
	{
		return m_handle;
	}

private:
	InspectableRegistry::Handle m_handle{0};
};

}  // namespace mitiru::debug
