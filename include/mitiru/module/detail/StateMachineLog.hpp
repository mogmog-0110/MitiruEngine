#pragma once

/// @file StateMachineLog.hpp
/// @brief 状態機械の資産 (debug/StateMachineInspect.hpp の JSON) が指す GameMemory の範囲を毎フレーム見て、変わった所を覚える
/// @details ツール窓へ届く snapshot は 10 Hz なので、窓が自分で見ていると 1 フレームだけの状態や素早い往復を取りこぼす。
///          host が更新のたびに見て、遷移の記録 (フレーム・前・後) と、組ごとの回数を持つ。状態の名前や図は知らない。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/InspectAssetsHost.hpp>

namespace mitiru::module::detail
{

struct MachineSlot
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;   ///< 1 / 2 / 4。GameMemory に収まらない宣言は 0 (順番を保つため)

	friend bool operator==(const MachineSlot&, const MachineSlot&) = default;
};

inline constexpr std::size_t kMaxStateMachines = 32;

/// @brief 状態機械の資産の machines のうち、GameMemory の中に収まる範囲。収まらない物は大きさ 0 にして順番を保つ
[[nodiscard]] inline std::vector<MachineSlot> machineSlotsOf(const std::vector<CopiedInspectAsset>& assets,
                                                             std::uint32_t memorySize)
{
	std::vector<MachineSlot> out;
	for (const CopiedInspectAsset& a : assets)
	{
		if (a.kind != "statemachine") { continue; }
		const auto j = nlohmann::json::parse(a.bytes.begin(), a.bytes.end(), nullptr, false);
		if (!j.is_object() || !j.contains("machines") || !j["machines"].is_array()) { return out; }
		for (const auto& m : j["machines"])
		{
			if (out.size() >= kMaxStateMachines) { break; }
			const std::uint64_t offset = m.value("offset", std::uint64_t{0});
			const std::uint64_t size = m.value("size", std::uint64_t{0});
			const bool sizeOk = size == 1 || size == 2 || size == 4;
			const bool fits = sizeOk && offset <= memorySize && size <= memorySize - offset;
			out.push_back({fits ? static_cast<std::uint32_t>(offset) : 0u, fits ? static_cast<std::uint32_t>(size) : 0u});
		}
		return out;
	}
	return out;
}

class StateMachineLog
{
public:
	static constexpr std::size_t kMaxEvents = 192;   ///< 機械ごとに覚える遷移の数 (古い順に捨てる)
	static constexpr std::size_t kMaxEdges = 128;    ///< 機械ごとに数える (前, 後) の組の数

	/// @brief 見る範囲を替える。前と同じ範囲なら記録を残し、違えば捨てる (資産の読み直しで履歴を消さないため)
	void reset(std::vector<MachineSlot> slots)
	{
		if (slots == m_slots) { return; }
		m_slots = std::move(slots);
		m_machines.assign(m_slots.size(), Machine{});
		m_frame = 0;
	}

	[[nodiscard]] bool empty() const noexcept { return m_slots.empty(); }
	[[nodiscard]] std::uint64_t frame() const noexcept { return m_frame; }

	/// @brief update が 1 回終わるたびに呼ぶ。GameMemory の先頭 base から memorySize byte が読める
	void sample(const std::uint8_t* base, std::uint32_t memorySize) noexcept
	{
		if (m_slots.empty() || base == nullptr) { return; }
		++m_frame;
		for (std::size_t i = 0; i < m_slots.size(); ++i)
		{
			const MachineSlot& s = m_slots[i];
			if (s.size == 0 || std::uint64_t{s.offset} + s.size > memorySize) { continue; }
			std::uint32_t raw = 0;
			std::memcpy(&raw, base + s.offset, s.size);
			note(m_machines[i], raw);
		}
	}

	/// @brief {frame, machines:[{valid, cur, changes, since, events:[[frame, from, to]], edges:[[from, to, count, last]]}]}
	[[nodiscard]] nlohmann::json toJson() const
	{
		nlohmann::json machines = nlohmann::json::array();
		for (std::size_t i = 0; i < m_machines.size(); ++i)
		{
			const Machine& m = m_machines[i];
			nlohmann::json events = nlohmann::json::array();
			for (const Event& e : m.events) { events.push_back({e.frame, e.from, e.to}); }
			nlohmann::json edges = nlohmann::json::array();
			for (const Edge& e : m.edges) { edges.push_back({e.from, e.to, e.count, e.last}); }
			machines.push_back({{"valid", m_slots[i].size != 0 && m.seen}, {"cur", m.cur}, {"changes", m.changes},
			                    {"since", m.since}, {"events", std::move(events)}, {"edges", std::move(edges)}});
		}
		return {{"frame", m_frame}, {"machines", std::move(machines)}};
	}

private:
	struct Event
	{
		std::uint64_t frame = 0;
		std::uint32_t from = 0;
		std::uint32_t to = 0;
	};
	struct Edge
	{
		std::uint32_t from = 0;
		std::uint32_t to = 0;
		std::uint64_t count = 0;
		std::uint64_t last = 0;
	};
	struct Machine
	{
		bool seen = false;
		std::uint32_t cur = 0;
		std::uint64_t changes = 0;
		std::uint64_t since = 0;
		std::deque<Event> events;
		std::vector<Edge> edges;
	};

	void note(Machine& m, std::uint32_t raw) const
	{
		if (!m.seen)
		{
			m = Machine{};
			m.seen = true;
			m.cur = raw;
			m.since = m_frame;
			return;
		}
		if (raw == m.cur) { return; }
		m.events.push_back({m_frame, m.cur, raw});
		if (m.events.size() > kMaxEvents) { m.events.pop_front(); }
		auto it = std::find_if(m.edges.begin(), m.edges.end(), [&](const Edge& e) { return e.from == m.cur && e.to == raw; });
		if (it != m.edges.end()) { ++it->count; it->last = m_frame; }
		else if (m.edges.size() < kMaxEdges) { m.edges.push_back({m.cur, raw, 1, m_frame}); }
		++m.changes;
		m.since = m_frame;
		m.cur = raw;
	}

	std::vector<MachineSlot> m_slots;
	std::vector<Machine> m_machines;
	std::uint64_t m_frame = 0;
};

}  // namespace mitiru::module::detail
