// objects_kitchen。クラスとコンポーネントで書く game の最小形 (MITIRU_GAME_OBJECTS、ADR 0040)。
// 実行すると: 鉄板が day の数だけ並び、生地が焼けていく。← → で鉄板を選び、SPACE で出す (焼き加減で値段が変わる)。
//             ↑ で次の日へ (鉄板が 1 枚増える)。S セーブ / L ロード / R さいしょから。
// 関連 API: MITIRU_GAME_OBJECTS(Game, Progress) / Hud::save / load / requestRestart
//   進行データ (日数・所持金・乱数) だけが flat POD で、セーブとロードと録画の対象になる。鉄板や
//   コンポーネントは普通の C++ (仮想関数 + std::vector<std::unique_ptr>) で、進行データから組み立て直す。
//   ロードすると所持金と日数は戻り、鉄板の焼き加減は「その日の頭」からになる (焼き加減は進行データに
//   置いていないため)。何を進行データに置くかが、この形の設計の中心になる。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>

#include "../common/chapter_hud.hpp"   // theme の色

using namespace mitiru;

constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;
constexpr int   kMaxPans = 6;

// ── 進行データ (flat POD = GameMemory) ─────────────────────────────────────────
struct KitchenProgress
{
	int           day      = 1;
	int           money    = 0;
	int           served   = 0;
	int           cursor   = 0;          // 選んでいる鉄板
	std::uint32_t rngState = 0x2545F491u; // 乱数はここに置く (場面の中に別の乱数源を持たない = replay が通る)
	bool          pendingSave = false;
	bool          pendingLoad = false;
	std::uint8_t  _pad[2]     = {};   // 末尾の隙間を明示する (不定値が bytes に混ざると録画と一致しなくなる)

	std::uint32_t nextRandom()
	{
		rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
		return rngState;
	}
};
MITIRU_REFLECT_AUTO(KitchenProgress);

// ── コンポーネント ───────────────────────────────────────────────────────────
struct GameObject;

struct Component
{
	virtual ~Component() = default;
	virtual void update(GameObject& /*owner*/, KitchenProgress& /*progress*/, float /*dt*/) {}
	virtual void draw(const GameObject& /*owner*/, Screen& /*screen*/) const {}
};

struct GameObject
{
	float x = 0.0f, y = 0.0f, w = 150.0f, h = 150.0f;
	std::vector<std::unique_ptr<Component>> components;

	template <class T, class... Args>
	T& add(Args&&... args)
	{
		components.push_back(std::make_unique<T>(std::forward<Args>(args)...));
		return static_cast<T&>(*components.back());
	}
	template <class T>
	T* get()
	{
		for (auto& c : components) { if (auto* hit = dynamic_cast<T*>(c.get())) { return hit; } }
		return nullptr;
	}
	void update(KitchenProgress& progress, float dt) { for (auto& c : components) { c->update(*this, progress, dt); } }
	void draw(Screen& screen) const { for (const auto& c : components) { c->draw(*this, screen); } }
};

// 乗っている生地を焼く。焼き加減 0 (生) → 1 (ちょうど) → 2 (焦げ)。
struct Cooker final : Component
{
	float heatPerSecond;
	float doneness = 0.0f;
	explicit Cooker(float heat) : heatPerSecond(heat) {}

	void update(GameObject&, KitchenProgress&, float dt) override
	{
		doneness += heatPerSecond * dt;
		if (doneness > 2.0f) { doneness = 2.0f; }
	}
	int price() const   // ちょうど (1.0) に近いほど高い
	{
		const float off = doneness > 1.0f ? doneness - 1.0f : 1.0f - doneness;
		const int   p   = static_cast<int>(100.0f * (1.0f - off));
		return p > 0 ? p : 0;
	}
	void draw(const GameObject& owner, Screen& s) const override
	{
		const float t = doneness > 1.0f ? 1.0f : doneness;
		const float burn = doneness > 1.0f ? doneness - 1.0f : 0.0f;
		const Color batter{ 0.96f - 0.35f * t - 0.45f * burn, 0.88f - 0.40f * t - 0.40f * burn, 0.62f - 0.40f * t - 0.18f * burn, 1.0f };
		s.drawRoundedRect(Rect{ owner.x + 18.0f, owner.y + 18.0f, owner.w - 36.0f, owner.h - 36.0f }, batter, 56.0f);
	}
};

// 鉄板の見た目。
struct PlateRenderer final : Component
{
	void draw(const GameObject& owner, Screen& s) const override
	{
		s.drawRoundedRect(Rect{ owner.x, owner.y, owner.w, owner.h }, Color{ 0.22f, 0.23f, 0.26f, 1.0f }, 18.0f);
	}
};

// ── 場面 (DLL の中に 1 個だけ生きる普通の C++ オブジェクト) ─────────────────────
struct Kitchen
{
	std::vector<std::unique_ptr<GameObject>> pans;

	// 進行データから場面を組み立てる。初回・ホットリロード後・ロード後・restart 後に呼ばれる。
	// 読むだけ (const): 同じ進行データからは必ず同じ場面ができ、何度呼ばれても進行データは変わらない。
	void build(const KitchenProgress& progress)
	{
		pans.clear();
		const int   count = progress.day < kMaxPans ? progress.day : kMaxPans;
		const float gap   = 40.0f, size = 150.0f;
		const float total = static_cast<float>(count) * size + static_cast<float>(count - 1) * gap;
		for (int i = 0; i < count; ++i)
		{
			auto pan = std::make_unique<GameObject>();
			pan->x = (kScreenW - total) * 0.5f + static_cast<float>(i) * (size + gap);
			pan->y = 300.0f;
			pan->add<PlateRenderer>();
			// 火力は鉄板ごとに少し違う。日数と位置から決める (組み立ては乱数を進めない)。
			const std::uint32_t mix = static_cast<std::uint32_t>(progress.day * 31 + i * 17) * 2654435761u;
			pan->add<Cooker>(0.18f + static_cast<float>((mix >> 16) % 100u) / 100.0f * 0.22f);
			pans.push_back(std::move(pan));
		}
	}

	void update(KitchenProgress& progress, Input in, Hud hud, float dt)
	{
		if (progress.pendingSave) { progress.pendingSave = false; }
		if (progress.pendingLoad) { progress.pendingLoad = false; }

		const int count = static_cast<int>(pans.size());
		if (progress.cursor >= count) { progress.cursor = count > 0 ? count - 1 : 0; }
		if (in.pressed(Key::Right) && progress.cursor + 1 < count) { ++progress.cursor; }
		if (in.pressed(Key::Left) && progress.cursor > 0)          { --progress.cursor; }

		if (in.pressed(Key::Space) && count > 0)
		{
			if (auto* cooker = pans[static_cast<std::size_t>(progress.cursor)]->get<Cooker>())
			{
				progress.money  += cooker->price() + static_cast<int>(progress.nextRandom() % 10u);   // チップ。乱数は進行データのもの
				progress.served += 1;
				cooker->doneness = 0.0f;   // 新しい生地を流す
			}
		}
		if (in.pressed(Key::Up)) { progress.day += 1; build(progress); }   // 次の日 = 場面を組み直す

		if (in.pressed(Key::S)) { hud.save("kitchen"); progress.pendingSave = true; }
		if (in.pressed(Key::L)) { hud.load("kitchen"); progress.pendingLoad = true; }   // host が書き戻して build を呼ぶ
		if (in.pressed(Key::R)) { hud.requestRestart(); }

		for (auto& pan : pans) { pan->update(progress, dt); }
	}

	void draw(const KitchenProgress& progress, Screen& s)
	{
		s.fillScreen(theme::kPaper);
		s.text("objects_kitchen  クラスとコンポーネントで書く (MITIRU_GAME_OBJECTS)", 40.0f, 28.0f, theme::kInk, 26.0f);
		const std::string status = std::to_string(progress.day) + " 日目   所持金 " + std::to_string(progress.money)
			+ "   出した数 " + std::to_string(progress.served);
		s.text(status.c_str(), 40.0f, 78.0f, theme::kInk, 30.0f);

		for (const auto& pan : pans) { pan->draw(s); }
		if (!pans.empty())
		{
			const GameObject& sel = *pans[static_cast<std::size_t>(progress.cursor)];
			s.drawRectFrame(Rect{ sel.x - 8.0f, sel.y - 8.0f, sel.w + 16.0f, sel.h + 16.0f }, theme::kOrange, 4.0f);
		}
		s.text("← → 鉄板を選ぶ   SPACE 出す   ↑ 次の日   S セーブ   L ロード   R さいしょから",
		       40.0f, kScreenH - 56.0f, theme::kInk, 22.0f);
	}
};

MITIRU_GAME_OBJECTS(Kitchen, KitchenProgress)
