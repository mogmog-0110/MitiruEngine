#pragma once
/// @file WorldTags.hpp
/// @brief 敵の頭上の HP バーと、ダメージ数字のような浮かぶ文字を、ワールドの位置から UI へ送る。
///
/// 画面のどこに出すかは C++ が `pick3d::worldToScreen` で決め、`hud.set` で座標を送る。UI (RML) はその座標に
/// 絶対配置で並べるだけで、動きも C++ が送る経過 (0..1) から作る。UI の要素は送った数だけでき、数は上限で止まる。
/// 使い方は docs/UI_WORLD_TAGS.md。
///
///   WorldTagFrame tags(view);                       // 毎フレームのコンテナ (状態を持たない)
///   for (const Enemy& e : enemies) { tags.add(e.id, {e.x, e.y + 1.8f, e.z}, e.hp / e.maxHp); }
///   tags.push(hud, "tags");                         // view.tags
///
///   FloatingNumbers<16> numbers;                    // GameMemory のメンバー (int と float だけ)
///   numbers.spawn({x, y, z}, damage);               // 当たった瞬間
///   numbers.update(dt);
///   numbers.push(hud, "damage", view);              // view.damage

#include <mitiru/render/Picking3D.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace mitiru::ui
{

namespace detail
{

/// statePush の文字列は 3968 バイトまでなので、JSON は 3900 バイトまでにする
inline constexpr std::size_t kMaxTagJsonBytes = 3900;

/// 上限を超える行は追加せず、false を返す
inline bool appendRow(std::string& json, const char* row)
{
	const std::size_t len = std::char_traits<char>::length(row);
	if (json.size() + len + 2 > kMaxTagJsonBytes) { return false; }
	if (json.size() > 1) { json += ','; }
	json += row;
	return true;
}

/// worldToScreen を通った点を、カメラの前方への距離で並べ替える
inline float depthOf(const pick3d::CameraView& view, const sgc::Vec3f& world)
{
	const pick3d::detail::Basis b = pick3d::detail::basisOf(view);
	const sgc::Vec3f v{world.x - view.eye.x, world.y - view.eye.y, world.z - view.eye.z};
	return v.dot(b.forward);
}

} // namespace detail

/// 1 フレームぶんの追従タグ。敵の位置はゲームの状態にあるため、毎フレーム並べ直す
class WorldTagFrame
{
public:
	/// 同時に出す上限。超えたときは、小さく読みにくい遠くのものから除く
	static constexpr int kDefaultMax = 24;

	explicit WorldTagFrame(const pick3d::CameraView& view, int maxTags = kDefaultMax)
		: view_(view), max_(std::max(1, maxTags)) {}

	/// world は HP バーを置く点、hp01 は 0..1。カメラの後ろの点は数えない。
	/// 画面外の点は、画面に入るとき UI の表示が遅れないように数える
	bool add(int id, const sgc::Vec3f& world, float hp01)
	{
		Item item;
		if (!pick3d::worldToScreen(view_, world, item.x, item.y)) { return false; }
		item.id = id;
		item.hp = std::clamp(hp01, 0.0f, 1.0f) * 100.0f;
		item.depth = detail::depthOf(view_, world);
		items_.push_back(item);
		return true;
	}

	/// `view.<name>` に `{"id","x","y","hp","s"}` の配列を送る。hp は 0..100、s は近いほど大きい 0.5..1.5。
	/// 手前の要素が上に重なるよう、奥から手前の順にする。
	/// 数か文字列の上限で一部を送れなかったときは false を返す
	template <class Hud>
	bool push(Hud& hud, const char* name) const
	{
		std::vector<Item> kept = items_;
		bool complete = true;
		std::sort(kept.begin(), kept.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
		if (static_cast<int>(kept.size()) > max_)
		{
			kept.resize(static_cast<std::size_t>(max_));
			complete = false;
		}
		std::sort(kept.begin(), kept.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });

		std::string json = "[";
		for (const Item& it : kept)
		{
			char row[160];
			const float scale = std::clamp(kReferenceDepth / std::max(it.depth, 0.1f), 0.5f, 1.5f);
			std::snprintf(row, sizeof(row), "{\"id\":%d,\"x\":%.1f,\"y\":%.1f,\"hp\":%.1f,\"s\":%.2f}",
			              it.id, static_cast<double>(it.x), static_cast<double>(it.y), static_cast<double>(it.hp),
			              static_cast<double>(scale));
			if (!detail::appendRow(json, row)) { complete = false; break; }
		}
		json += ']';
		char key[128];
		std::snprintf(key, sizeof(key), "view.%s", name);
		hud.set(key, json.c_str());
		return complete;
	}

	[[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

private:
	static constexpr float kReferenceDepth = 12.0f;   ///< この距離で等倍

	struct Item
	{
		int id = 0;
		float x = 0.0f, y = 0.0f, hp = 0.0f, depth = 0.0f;
	};

	pick3d::CameraView view_;
	int max_;
	std::vector<Item> items_;
};

/// 出てから消えるまでの短い文字。状態は int と float だけで、GameMemory に置ける。
/// 満杯のときは最も古いものを消し、新しいものを入れる。同時に出る上限は N
template <int N>
struct FloatingNumbers
{
	struct Slot
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;   ///< 出た時のワールドの位置
		float age = -1.0f;                    ///< 出てからの秒。負なら空き
		int value = 0;
		int kind = 0;                         ///< UI の class を替える種類 (0 = 通常、1 = 会心など)
		int seq = 0;                          ///< 出た順。同じ位置に重なる物を横にずらす
	};

	Slot slots[N] = {};
	int next = 0;       ///< 次に書く場所
	int spawned = 0;    ///< これまでに出した数
	float life = 0.9f;  ///< 出てから消えるまでの秒

	void spawn(const sgc::Vec3f& world, int value, int kind = 0) noexcept
	{
		Slot& s = slots[next];
		s.x = world.x;
		s.y = world.y;
		s.z = world.z;
		s.age = 0.0f;
		s.value = value;
		s.kind = kind;
		s.seq = spawned;
		next = (next + 1) % N;
		++spawned;
	}

	void update(float dt) noexcept
	{
		for (Slot& s : slots)
		{
			if (s.age < 0.0f) { continue; }
			s.age += dt;
			if (s.age >= life) { s.age = -1.0f; }
		}
	}

	[[nodiscard]] int active() const noexcept
	{
		int n = 0;
		for (const Slot& s : slots) { n += s.age >= 0.0f ? 1 : 0; }
		return n;
	}

	/// `view.<name>` に `{"x","y","v","k","t"}` の配列を送る。t は 0..1 の経過。
	/// 新しいものが上に重なるよう、後ろに置く。
	/// x は出た順に 0、右、左の順で、最大 ±spread px までずらす。
	/// UI は t から上への移動量と透明度を作る。
	/// 文字列の上限で一部を送れなかったときは false を返す
	template <class Hud>
	bool push(Hud& hud, const char* name, const pick3d::CameraView& view, float spread = 16.0f) const
	{
		std::vector<const Slot*> alive;
		for (const Slot& s : slots)
		{
			if (s.age >= 0.0f) { alive.push_back(&s); }
		}
		std::sort(alive.begin(), alive.end(), [](const Slot* a, const Slot* b) { return a->seq < b->seq; });

		std::string json = "[";
		bool complete = true;
		for (const Slot* s : alive)
		{
			float sx = 0.0f, sy = 0.0f;
			if (!pick3d::worldToScreen(view, {s->x, s->y, s->z}, sx, sy)) { continue; }
			static constexpr int kOrder[5] = { 0, 1, -1, 2, -2 };   // 1 つめは真上、あとは右・左と交互に広がる
			const float shift = static_cast<float>(kOrder[s->seq % 5]) * spread * 0.5f;
			char row[128];
			std::snprintf(row, sizeof(row), "{\"x\":%.1f,\"y\":%.1f,\"v\":%d,\"k\":%d,\"t\":%.3f}",
			              static_cast<double>(sx + shift), static_cast<double>(sy), s->value, s->kind,
			              static_cast<double>(life > 0.0f ? std::clamp(s->age / life, 0.0f, 1.0f) : 1.0f));
			if (!detail::appendRow(json, row)) { complete = false; break; }
		}
		json += ']';
		char key[128];
		std::snprintf(key, sizeof(key), "view.%s", name);
		hud.set(key, json.c_str());
		return complete;
	}
};

} // namespace mitiru::ui
