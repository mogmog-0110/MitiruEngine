#pragma once

/// @file BehaviorTree.hpp
/// @brief 敵の行動を決めるビヘイビアツリー。木の形を持つ BtTree は読み込み後に変更せず、敵ごとの実行状態を
///        持つ BtState は固定長の POD とする。BtState を GameMemory に置けば、巻き戻しと記録／再生の対象になる。
///        tick はヒープ、乱数、時刻を使わない。
/// @details ノードは前順で並べ、各ノードに部分木の大きさを持たせる。子は親の直後から部分木の大きさずつ飛んでたどる。
///          時間は tick の整数で数える。
///
///          Action と Condition の処理はゲームが書く。tickBehaviorTree に渡す関数オブジェクトは BtLeafCall を受け取り、
///          leaf の番号に応じて BtStatus を返す。途中で打ち切られた Action には event = Halt で 1 度だけ知らせ、
///          返り値は使わない。組み方は docs/GAME_AI.md にある。

#include <cstdint>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::gameai
{

using Vec3 = action::Vec3;

inline constexpr int           kBtMaxNodes = 64;
inline constexpr std::uint16_t kBtNoNode   = 0xFFFFu;

enum class BtStatus : std::uint8_t
{
	Idle,
	Running,
	Success,
	Failure,
};

enum class BtKind : std::uint8_t
{
	Sequence,          ///< 子を順に。失敗した所で失敗。走っている子から続ける
	Selector,          ///< 子を順に。成功した所で成功。走っている子から続ける
	ReactiveSequence,  ///< 毎 tick 最初の子から評価し直す。前の条件が崩れたら走っている子を打ち切る
	ReactiveSelector,  ///< 毎 tick 最初の子から評価し直す。優先の高い子が通れば低い子を打ち切る
	Parallel,          ///< 子を全部回す。param 個成功で成功 (0 なら全部)。成功に届かなくなったら失敗
	Inverter,
	Succeeder,         ///< 子が終われば結果に依らず成功
	Cooldown,          ///< 子が成功したら param tick の間は子を回さず失敗。打ち切りでも記録は消えない
	Repeat,            ///< 子の成功を param 回まで繰り返す (0 なら失敗するまで)。1 tick に 1 回まで
	Timeout,           ///< 入ってから param tick を過ぎたら子を打ち切って失敗
	Wait,              ///< 葉。param tick 待って成功
	Action,            ///< 葉。ゲームの関数。Running を返してよい
	Condition,         ///< 葉。ゲームの関数。Running を返したら失敗とみなす
};

struct BtNode
{
	BtKind        kind  = BtKind::Action;
	std::uint8_t  pad   = 0;
	std::uint16_t size  = 1;  ///< 自分を含む部分木のノード数
	std::uint16_t leaf  = 0;  ///< Action / Condition の番号 (ゲームが決める)
	std::uint16_t childCount = 0;
	std::int32_t  param = 0;  ///< tick 数、回数、成功の数、葉への引数 (種類ごと)
};

/// @brief 木の形。作成後は変更せず、GameMemory ではなく DLL の static 領域や読み込んだデータに置く
struct BtTree
{
	BtNode        nodes[kBtMaxNodes]{};
	std::uint16_t count = 0;
	std::uint16_t pad   = 0;
	std::uint32_t hash  = 0;  ///< 形の指紋。build が nodes から求める
};

/// @brief 敵 1 体の実行状態。GameMemory に置く
struct BtState
{
	BtStatus      status[kBtMaxNodes]{};  ///< 各ノードの直近の結果
	std::int32_t  mem[kBtMaxNodes]{};     ///< ノードごとの記憶 (子の位置、回数、開始 tick、葉の自由欄)
	std::uint32_t ticks       = 0;
	std::uint32_t tree        = 0;          ///< この状態を作った木の BtTree::hash
	std::uint16_t runningLeaf = kBtNoNode;  ///< 直近の tick で Running を返した葉のノード番号
	BtStatus      root        = BtStatus::Idle;
	std::uint8_t  pad         = 0;
};

/// @brief 型付きの小さな黒板。キーはゲーム側の enum で決め、配列の添字に使う
template <int Ints = 8, int Floats = 8, int Vecs = 4>
struct Blackboard
{
	std::int32_t  ints[Ints]{};
	float         floats[Floats]{};
	Vec3          vecs[Vecs]{};
	std::uint32_t flags = 0;

	constexpr void setFlag(int bit, bool on) noexcept
	{
		flags = on ? (flags | (1u << bit)) : (flags & ~(1u << bit));
	}
	[[nodiscard]] constexpr bool flag(int bit) const noexcept { return ((flags >> bit) & 1u) != 0u; }
};

enum class BtLeafEvent : std::uint8_t
{
	Tick,
	Halt,
};

struct BtLeafCall
{
	std::int32_t* memory = nullptr;  ///< この葉の自由欄 (BtState::mem)。入り直すと 0 に戻る
	std::int32_t  param  = 0;
	std::uint32_t now    = 0;        ///< tickBehaviorTree に渡した tick
	std::uint16_t leaf   = 0;
	std::uint16_t node   = 0;
	BtKind        kind   = BtKind::Action;
	BtLeafEvent   event  = BtLeafEvent::Tick;
	bool          entered = false;   ///< 前の tick に Running でなかった (今回から始まる)
};

static_assert(std::is_trivially_copyable_v<BtNode>);
static_assert(std::is_trivially_copyable_v<BtTree>);
static_assert(std::is_trivially_copyable_v<BtState>);
static_assert(std::is_trivially_copyable_v<Blackboard<>>);

[[nodiscard]] constexpr bool btIsComposite(BtKind k) noexcept { return k <= BtKind::Parallel; }
[[nodiscard]] constexpr bool btIsDecorator(BtKind k) noexcept { return k >= BtKind::Inverter && k <= BtKind::Timeout; }
[[nodiscard]] constexpr bool btIsLeaf(BtKind k) noexcept { return k >= BtKind::Wait; }

/// @brief 複合ノードとデコレータは open で開き、子を追加して end で閉じる。葉はそのまま追加する。build 後に error が nullptr なら成功
class BtBuilder
{
public:
	const char* error = nullptr;

	constexpr BtBuilder& open(BtKind kind, std::int32_t param = 0) noexcept
	{
		if (btIsLeaf(kind)) { fail("open に葉を渡した (action / condition / wait を使う)"); return *this; }
		return push(kind, 0, param, true);
	}
	constexpr BtBuilder& action(std::uint16_t leaf, std::int32_t param = 0) noexcept
	{
		return push(BtKind::Action, leaf, param, false);
	}
	constexpr BtBuilder& condition(std::uint16_t leaf, std::int32_t param = 0) noexcept
	{
		return push(BtKind::Condition, leaf, param, false);
	}
	constexpr BtBuilder& wait(std::int32_t frames) noexcept { return push(BtKind::Wait, 0, frames, false); }

	constexpr BtBuilder& end() noexcept
	{
		if (m_depth == 0) { fail("end が open より多い"); return *this; }
		const std::uint16_t at = m_open[--m_depth];
		BtNode& n = m_tree.nodes[at];
		n.size = static_cast<std::uint16_t>(m_tree.count - at);
		if (btIsDecorator(n.kind) && n.childCount != 1) { fail("飾り (inverter / cooldown 等) の子は 1 つ"); }
		if (btIsComposite(n.kind) && n.childCount == 0) { fail("子の無い sequence / selector / parallel"); }
		if (n.kind == BtKind::Parallel && (n.param < 0 || n.param > n.childCount)) { fail("parallel の成功数が子の数を超える"); }
		return *this;
	}

	[[nodiscard]] constexpr BtTree build() noexcept
	{
		if (m_depth != 0) { fail("閉じていない open がある"); }
		if (m_tree.count == 0) { fail("ノードが無い"); }
		if (m_roots > 1) { fail("根が 2 つ以上ある (全体を 1 つの複合で包む)"); }
		if (error != nullptr) { return BtTree{}; }
		BtTree t = m_tree;
		t.hash = hashOf(t);
		return t;
	}

private:
	constexpr BtBuilder& push(BtKind kind, std::uint16_t leaf, std::int32_t param, bool opens) noexcept
	{
		if (m_tree.count >= kBtMaxNodes) { fail("ノードが kBtMaxNodes を超える"); return *this; }
		if (m_depth > 0) { ++m_tree.nodes[m_open[m_depth - 1]].childCount; }
		else { ++m_roots; }
		const auto at = m_tree.count++;
		m_tree.nodes[at] = BtNode{kind, 0, 1, leaf, 0, param};
		if (opens) { m_open[m_depth++] = at; }
		return *this;
	}
	constexpr void fail(const char* why) noexcept
	{
		if (error == nullptr) { error = why; }
	}
	/// @brief FNV-1a。0 は「まだどの木でも回していない状態」に取っておく
	[[nodiscard]] static constexpr std::uint32_t hashOf(const BtTree& t) noexcept
	{
		std::uint32_t h = 2166136261u;
		auto mix = [&h](std::uint32_t v) { for (int b = 0; b < 4; ++b) { h = (h ^ ((v >> (8 * b)) & 0xFFu)) * 16777619u; } };
		mix(t.count);
		for (int i = 0; i < t.count; ++i)
		{
			const BtNode& n = t.nodes[i];
			mix(static_cast<std::uint32_t>(n.kind) | (static_cast<std::uint32_t>(n.size) << 16));
			mix(n.leaf | (static_cast<std::uint32_t>(n.childCount) << 16));
			mix(static_cast<std::uint32_t>(n.param));
		}
		return h == 0u ? 1u : h;
	}

	BtTree        m_tree{};
	std::uint16_t m_open[kBtMaxNodes]{};
	int           m_depth = 0;
	int           m_roots = 0;
};

} // namespace mitiru::gameai

#include <mitiru/gameai/detail/BehaviorTree_impl.hpp>

namespace mitiru::gameai
{

/// @brief leaves は BtStatus(const BtLeafCall&) として呼べるもの
template <class Leaves>
BtStatus tickBehaviorTree(const BtTree& tree, BtState& state, std::uint32_t now, Leaves&& leaves)
{
	if (tree.count == 0) { return BtStatus::Failure; }
	// ホットリロードで木の形が変わったら、古いノード番号の記憶は意味を失うので最初からにする。
	// 古い木で走っていた葉には Halt が届かないので、葉が外に持つもの (トークン等) は期限で回収させる
	if (state.tree != tree.hash)
	{
		state = BtState{};
		state.tree = tree.hash;
	}
	bt_detail::Runner<Leaves> r{tree, state, now, leaves};
	state.runningLeaf = kBtNoNode;
	++state.ticks;
	state.root = r.tick(0);
	return state.root;
}

/// @brief 実行中の枝をすべて打ち切り、最初からやり直せる状態に戻す。Cooldown の記録は残す
template <class Leaves>
void haltBehaviorTree(const BtTree& tree, BtState& state, std::uint32_t now, Leaves&& leaves)
{
	if (tree.count == 0) { return; }
	bt_detail::Runner<Leaves> r{tree, state, now, leaves};
	r.halt(0);
	state.runningLeaf = kBtNoNode;
	state.root        = BtStatus::Idle;
}

} // namespace mitiru::gameai
