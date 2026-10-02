#pragma once

// BehaviorTree.hpp の中身。直接 include しない

#include <cstdint>
#include <type_traits>

namespace mitiru::gameai::bt_detail
{

/// @brief tick 間の経過が frames 以上か。tick が一周しても差を正しく求める
[[nodiscard]] constexpr bool elapsed(std::uint32_t now, std::int32_t since, std::int32_t frames) noexcept
{
	return static_cast<std::int32_t>(now - static_cast<std::uint32_t>(since)) >= frames;
}

template <class Leaves>
struct Runner
{
	const BtTree&                     t;
	BtState&                          s;
	std::uint32_t                     now;
	std::remove_reference_t<Leaves>&  leaves;

	[[nodiscard]] int size(int i) const noexcept { return t.nodes[i].size; }

	BtStatus tick(int i)
	{
		const bool entered = s.status[i] != BtStatus::Running;
		if (entered) { reset(i); }
		const BtStatus st = dispatch(i, entered);
		s.status[i] = st;
		return st;
	}

	/// @brief 部分木へ入る前の状態に戻す。Cooldown の記録は打ち切っても残す
	void reset(int i) noexcept
	{
		const int end = i + size(i);
		for (int j = i; j < end; ++j)
		{
			s.status[j] = BtStatus::Idle;
			if (t.nodes[j].kind != BtKind::Cooldown) { s.mem[j] = 0; }
		}
	}

	void halt(int i)
	{
		const int end = i + size(i);
		for (int j = i; j < end; ++j)
		{
			if (s.status[j] == BtStatus::Running && t.nodes[j].kind == BtKind::Action) { callLeaf(j, BtLeafEvent::Halt, false); }
		}
		reset(i);
	}

	void haltRunning(int first, int end)
	{
		for (int c = first; c < end; c += size(c))
		{
			if (s.status[c] == BtStatus::Running) { halt(c); }
		}
	}

	BtStatus callLeaf(int i, BtLeafEvent ev, bool entered)
	{
		const BtNode& n = t.nodes[i];
		const BtLeafCall call{&s.mem[i], n.param, now, n.leaf, static_cast<std::uint16_t>(i), n.kind, ev, entered};
		return leaves(call);
	}

	BtStatus dispatch(int i, bool entered)
	{
		switch (t.nodes[i].kind)
		{
		case BtKind::Sequence:         return sequential(i, BtStatus::Success);
		case BtKind::Selector:         return sequential(i, BtStatus::Failure);
		case BtKind::ReactiveSequence: return reactive(i, BtStatus::Success);
		case BtKind::ReactiveSelector: return reactive(i, BtStatus::Failure);
		case BtKind::Parallel:         return parallel(i);
		case BtKind::Wait:
			if (entered) { s.mem[i] = static_cast<std::int32_t>(now); }
			return elapsed(now, s.mem[i], t.nodes[i].param) ? BtStatus::Success : BtStatus::Running;
		case BtKind::Action:
		case BtKind::Condition:        return leaf(i, entered);
		default:                       return decorator(i, entered);
		}
	}

	/// @brief Sequence は keepGoing = Success、Selector は keepGoing = Failure。実行中の子から再開する
	BtStatus sequential(int i, BtStatus keepGoing)
	{
		const BtNode& n = t.nodes[i];
		int c = i + 1;
		for (int k = 0; k < s.mem[i]; ++k) { c += size(c); }
		for (int k = s.mem[i]; k < n.childCount; ++k, c += size(c))
		{
			const BtStatus st = tick(c);
			if (st == BtStatus::Running) { s.mem[i] = k; return st; }
			if (st != keepGoing) { return st; }
		}
		return keepGoing;
	}

	/// @brief tick ごとに先頭から実行する。止まった子より後ろで実行中の子は打ち切る
	BtStatus reactive(int i, BtStatus keepGoing)
	{
		const int end = i + size(i);
		for (int c = i + 1; c < end; c += size(c))
		{
			const BtStatus st = tick(c);
			if (st != keepGoing)
			{
				haltRunning(c + size(c), end);
				return st;
			}
		}
		return keepGoing;
	}

	BtStatus parallel(int i)
	{
		const BtNode& n    = t.nodes[i];
		const int     need = n.param == 0 ? n.childCount : n.param;
		const int     end  = i + size(i);
		int ok = 0, ng = 0;
		for (int c = i + 1; c < end; c += size(c))
		{
			BtStatus st = s.status[c];
			if (st != BtStatus::Success && st != BtStatus::Failure) { st = tick(c); }
			ok += st == BtStatus::Success ? 1 : 0;
			ng += st == BtStatus::Failure ? 1 : 0;
		}
		if (ok < need && ng <= n.childCount - need) { return BtStatus::Running; }
		haltRunning(i + 1, end);
		return ok >= need ? BtStatus::Success : BtStatus::Failure;
	}

	BtStatus leaf(int i, bool entered)
	{
		BtStatus st = callLeaf(i, BtLeafEvent::Tick, entered);
		if (st == BtStatus::Idle || (st == BtStatus::Running && t.nodes[i].kind == BtKind::Condition)) { st = BtStatus::Failure; }
		if (st == BtStatus::Running) { s.runningLeaf = static_cast<std::uint16_t>(i); }
		return st;
	}

	BtStatus decorator(int i, bool entered)
	{
		const BtNode& n = t.nodes[i];
		const int     c = i + 1;
		switch (n.kind)
		{
		case BtKind::Inverter:  return invert(tick(c));
		case BtKind::Succeeder: return tick(c) == BtStatus::Running ? BtStatus::Running : BtStatus::Success;
		case BtKind::Cooldown:  return cooldown(i, c);
		case BtKind::Repeat:    return repeat(i, c);
		case BtKind::Timeout:
			if (entered) { s.mem[i] = static_cast<std::int32_t>(now); }
			if (elapsed(now, s.mem[i], n.param)) { halt(c); return BtStatus::Failure; }
			return tick(c);
		default: return BtStatus::Failure;
		}
	}

	[[nodiscard]] static constexpr BtStatus invert(BtStatus st) noexcept
	{
		if (st == BtStatus::Success) { return BtStatus::Failure; }
		if (st == BtStatus::Failure) { return BtStatus::Success; }
		return st;
	}

	/// @brief mem は次に実行できる tick。0 は一度も成功していない状態を表す
	BtStatus cooldown(int i, int c)
	{
		if (s.mem[i] != 0 && !elapsed(now, s.mem[i], 0)) { return BtStatus::Failure; }
		const BtStatus st = tick(c);
		if (st == BtStatus::Success)
		{
			const std::uint32_t ready = now + static_cast<std::uint32_t>(t.nodes[i].param);
			s.mem[i] = static_cast<std::int32_t>(ready == 0u ? 1u : ready);
		}
		return st;
	}

	BtStatus repeat(int i, int c)
	{
		const BtStatus st = tick(c);
		if (st != BtStatus::Success) { return st; }
		++s.mem[i];
		const std::int32_t times = t.nodes[i].param;
		return (times > 0 && s.mem[i] >= times) ? BtStatus::Success : BtStatus::Running;
	}
};

} // namespace mitiru::gameai::bt_detail
