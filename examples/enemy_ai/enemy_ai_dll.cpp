// enemy_ai。「敵の AI の部品」(mitiru/gameai/) の見本。ビヘイビアツリー、攻撃トークン、視界と物音、ナビメッシュの経路、ぶつからない歩き方
// 実行すると: 箱の庭で、見つけた敵が橙のプレイヤーを囲む。同時に攻めるのは 2 体までで、赤く光ってから突く。壁の向こうの敵にはプレイヤーが見えず、手を叩くと物音を聞いて最後に知った位置を探しに来る (紫)
// 関連 API: loadBehaviorTreeJsonFile / tickBehaviorTree / AttackTokenPool / checkSight / listen / requestPath / followPath / computeAvoidVelocity /
//   MITIRU_INSPECT_ASSETS

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <mitiru.hpp>
#include <mitiru/action/CharacterController.hpp>
#include <mitiru/gameai/AttackTimeline.hpp>
#include <mitiru/gameai/AttackTokens.hpp>
#include <mitiru/gameai/Avoidance.hpp>
#include <mitiru/gameai/BehaviorTreeJson.hpp>
#include <mitiru/gameai/CombatSteering.hpp>
#include <mitiru/gameai/NavPath.hpp>
#include <mitiru/gameai/Perception.hpp>
#include <mitiru/module/ReflectEngineTypes.hpp>
#include <mitiru/nav/NavMeshBake.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;
namespace ai  = mitiru::gameai;
using BtStatus = ai::BtStatus;

struct Block { act::Vec3 lo, hi; };
constexpr Block kBlocks[] = {
	{{-12, -1, -12}, {12, 0, 12}},                             // 床
	{{-7, 0, -1}, {3, 2.5f, -0.4f}},                           // 真ん中の壁。この陰に入ると見えない
	{{5, 0, 3}, {6, 2.5f, 4}}, {{-6, 0, 4}, {-5, 2.5f, 5}},    // 柱
	{{4, 0, -8}, {10, 2.5f, -7.4f}},                           // 奥の壁
};
constexpr int kCount = 8;
constexpr act::Vec3 kHomes[kCount] = {{-9, 0, -9}, {-3, 0, -6}, {2, 0, -10}, {8, 0, -4}, {9, 0, 6}, {0, 0, 9}, {-9, 0, 8}, {-10, 0, 0}};

enum Leaf : std::uint16_t { kSees, kHasToken, kAttack, kCircle, kRemembers, kSearch, kGuard };
constexpr ai::BtLeafName kLeafNames[] = {{"seesTarget", kSees}, {"hasToken", kHasToken}, {"attack", kAttack}, {"circle", kCircle},
                                         {"remembers", kRemembers}, {"search", kSearch}, {"guard", kGuard}};
constexpr act::CharacterConfig kBody{};
constexpr ai::AttackFrames     kSwing{30, 8, 30, 0};       // 構え (予告) 0.5 秒、突き、硬直
constexpr ai::SpacingConfig    kRing{4.0f, 0.5f, 3.0f, 2.5f, 1.6f};
constexpr ai::SightConfig      kSight{12.0f, 55.0f, 1.5f, act::kAllLayers};

// 地形とナビメッシュは kBlocks、行動ツリーは assets の JSON から作る
// 作成後は変わらないためゲーム状態には入れず、ホットリロードを含む DLL の読み込みごとに同じ表から作り直す
struct Level { act::CollisionLevel collision; nav::NavMesh nav; std::vector<std::uint8_t> navBlob; ai::BtTree tree{}; std::string error; };
Level& level()
{
	static Level L;
	static const bool built = [] {
		act::CollisionLevelBuilder b;
		std::vector<act::Vec3> v;
		std::vector<std::uint32_t> idx;
		for (const Block& k : kBlocks)
		{
			b.addBox(k.lo, k.hi, 0);
			for (int c : act::kBoxTriangleCorners) { idx.push_back(static_cast<std::uint32_t>(v.size())); v.push_back(act::boxCorner(k.lo, k.hi, c)); }
		}
		L.collision = b.build();
		const nav::NavBakeResult baked = nav::bakeNavMesh(v, idx);
		std::string navError, treeError;
		L.navBlob = baked.blob;
		if (baked.blob.empty() || !L.nav.load(baked.blob, &navError)) { L.error = "ナビメッシュを作れない: " + baked.error + navError; }
		L.tree = ai::loadBehaviorTreeJsonFile("enemy_ai/assets/enemy.json", kLeafNames, &treeError);
		if (!treeError.empty()) { L.error += treeError; }
		return true;
	}();
	(void)built;
	return L;
}

// 敵 1 体の状態は、まとめてコピーできるフラットな POD にする
// 巻き戻しと記録、再生にそのまま使える
struct Enemy
{
	act::CharacterState  body{};
	ai::BtState          bt{};
	ai::PerceptionMemory mind{};
	ai::AttackTimeline   swing{};
	ai::PathFollower<16> path{};
	act::Vec3            home{}, want{}, facing{0, 0, 1};
};

struct EnemyAi;
struct Leaves { EnemyAi& g; int i; BtStatus operator()(const ai::BtLeafCall& c); };

struct EnemyAi
{
	Enemy                      e[kCount]{};
	ai::AttackTokenPool<4, 16> tokens{};
	ai::NoiseBoard<8>          noise{};
	act::CharacterState        hero{};
	std::uint32_t              frame = 0, hitUntil = 0;
	std::int32_t               attackers = 0, seeing = 0, remembering = 0;
	float                      minGap = 0.0f;

	void init()
	{
		(void)level();
		EnemyAi& self = *this;
		self = EnemyAi{};
		tokens.capacity = 2;   // 同時に攻めるのは 2 体まで
		hero.position = {0, kBody.skinWidth, 5};
		for (int i = 0; i < kCount; ++i) { e[i].home = kHomes[i]; e[i].body.position = kHomes[i] + act::Vec3{0, kBody.skinWidth, 0}; e[i].facing = act::normalizeOr(-kHomes[i], {0, 0, 1}); }
	}

	void update(Input in, float dt)
	{
		++frame;
		const act::CollisionWorld world(&level().collision);
		const Stick     m = in.move();   // 上で奥 (-z) へ。move の y は下が正
		const act::Vec3 heroWant{m.x * 4.0f, 0, m.y * 4.0f};
		act::stepCharacter(hero, kBody, world, {heroWant, 0.0f}, dt);
		if (in.pressed(Key::Space)) { noise.emit({hero.position + act::Vec3{0, 1, 0}, 14.0f, 1.0f, frame, 0}); }
		for (int i = 0; i < kCount; ++i)
		{
			Enemy& me = e[i];
			const act::Vec3 eye = me.body.position + act::Vec3{0, 1.6f, 0};
			const ai::SightResult s = ai::checkSight(world, eye, me.facing, hero.position + act::Vec3{0, 1.2f, 0}, kSight);
			ai::updatePerceptionMemory(me.mind, {}, s, hero.position, kSight.range, ai::listen(noise, &world, eye, frame, {}), frame, dt);
			Leaves leaves{*this, i};
			ai::tickBehaviorTree(level().tree, me.bt, frame, leaves);
		}
		tokens.releaseExpired(frame, 120);
		tokens.arbitrate(frame);
		walk(world, heroWant, dt);
	}

	// 望む速度を衝突しない速度に直してから歩かせる
	// プレイヤーは避けないため、敵だけが避ける
	void walk(const act::CollisionWorld& world, const act::Vec3& heroWant, float dt)
	{
		ai::AvoidAgent agents[kCount + 1];
		for (int i = 0; i < kCount; ++i) { agents[i] = {e[i].body.position, ai::flatten(e[i].body.velocity), e[i].want, 0.4f, act::maxf(3.0f, act::length(e[i].want)), 1.0f}; }
		agents[kCount] = {hero.position, ai::flatten(hero.velocity), heroWant, 0.4f, 4.0f, 0.0f};
		attackers = seeing = remembering = 0;
		minGap = 1e9f;
		for (int i = 0; i < kCount; ++i)
		{
			Enemy& me = e[i];
			act::stepCharacter(me.body, kBody, world, {ai::computeAvoidVelocity(agents, i, {}), 0.0f}, dt);
			const bool sees = sawRecently(me);
			const act::Vec3 look = sees ? hero.position - me.body.position : me.body.velocity;
			if (act::lengthSq(ai::flatten(look)) > 0.04f) { me.facing = act::normalizeOr(ai::flatten(look), me.facing); }
			const ai::AttackPhase p = me.swing.phase(frame, kSwing);
			attackers   += (p == ai::AttackPhase::Windup || p == ai::AttackPhase::Active || p == ai::AttackPhase::Recovery) ? 1 : 0;
			seeing      += sees ? 1 : 0;
			remembering += (!sees && me.mind.hasLastKnown != 0) ? 1 : 0;
			for (int j = 0; j < i; ++j) { minGap = act::minf(minGap, ai::distanceXZ(me.body.position, e[j].body.position) - 0.8f); }
		}
	}

	[[nodiscard]] bool sawRecently(const Enemy& me) const
	{
		return me.mind.level == ai::AwarenessLevel::Alert && frame - me.mind.lastSeenFrame < 30;
	}

	// 経路はナビメッシュに問い合わせ、終点が動いたときか 1.5 秒ごとに取り直す
	BtStatus walkTo(Enemy& me, const act::Vec3& goal)
	{
		if (me.path.shouldRepath(goal, frame, 1.0f, 90)) { ai::requestPath(level().nav, me.path, me.body.position, goal, frame); }
		me.want = ai::followPath(me.path, me.body.position, {});
		if (me.path.state == ai::PathState::Following) { return BtStatus::Running; }
		return me.path.state == ai::PathState::Arrived ? BtStatus::Success : BtStatus::Failure;
	}

	// 構えの間に間合いを詰め、突きの最初の tick で届いていれば当たりとする
	BtStatus attack(Enemy& me, const ai::BtLeafCall& c, std::uint32_t id)
	{
		if (c.entered) { me.swing.begin(1, c.now); }
		const ai::AttackPhase p   = me.swing.phase(c.now, kSwing);
		const act::Vec3       to  = ai::flatten(hero.position - me.body.position);
		const act::Vec3       dir = act::normalizeOr(to, me.facing);
		me.want = p == ai::AttackPhase::Windup ? (act::length(to) > 1.6f ? dir * 4.0f : act::Vec3{})
		        : p == ai::AttackPhase::Active ? dir * 7.0f : act::Vec3{};
		if (me.swing.entered(c.now, kSwing, ai::AttackPhase::Active) && act::length(to) < 2.2f) { hitUntil = c.now + 12; }
		if (p != ai::AttackPhase::Done) { return BtStatus::Running; }
		me.swing.cancel();
		tokens.release(id);
		return BtStatus::Success;
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));
		s.camera3D({0, 19, 15}, {0, 0, 1}, 50);
		s.light3D({-0.5f, -1.0f, -0.35f}, hex(0xFFFBF2));
		for (const Block& k : kBlocks) { s.drawMesh("cube", (k.lo + k.hi) * 0.5f, k.hi - k.lo, {0, 0, 0}, hex(k.lo.y < 0 ? 0xC9D1DC : 0x8C9BB0)); }
		s.drawMesh("cube", hero.position + act::Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, frame < hitUntil ? theme::kRed : theme::kOrange);
		for (const Enemy& me : e)
		{
			const ai::AttackPhase p = me.swing.phase(frame, kSwing);
			const Color body = p == ai::AttackPhase::Windup ? theme::kRed : p == ai::AttackPhase::Active ? hex(0xFFFFFF)
			                 : sawRecently(me) ? theme::kBlue : me.mind.hasLastKnown != 0 ? hex(0x8E5BD9) : hex(0x5B6577);
			s.drawMesh("cube", me.body.position + act::Vec3{0, 0.8f, 0}, {0.7f, 1.6f, 0.7f}, {0, 0, 0}, body);
			s.drawMesh("cube", me.body.position + act::Vec3{0, 1.25f, 0} + me.facing * 0.38f, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, theme::kInk);
		}
		char line[64];
		std::snprintf(line, sizeof line, "攻めている敵 %d / %d", attackers, tokens.capacity);
		s.text(line, 16.0f, 64.0f, theme::kInk, 18);
		if (!level().error.empty()) { s.text(level().error, 16.0f, 92.0f, theme::kRed, 18); }
		chapterTitle(s, "敵の AI");
		chapterControls(s, "矢印: あるく　Space: 手を叩く (物音)");
	}
};

BtStatus Leaves::operator()(const ai::BtLeafCall& c)
{
	Enemy& me = g.e[i];
	const std::uint32_t id = static_cast<std::uint32_t>(i + 1);   // トークンの id に 0 は使えない
	if (c.event == ai::BtLeafEvent::Halt)
	{
		if (c.leaf == kAttack) { me.swing.cancel(); g.tokens.release(id); }
		if (c.leaf == kSearch) { me.mind.hasLastKnown = 0; }   // 探すのを打ち切ったら諦める
		me.path.clear();
		return BtStatus::Idle;
	}
	switch (c.leaf)
	{
	case kSees:      return g.sawRecently(me) ? BtStatus::Success : BtStatus::Failure;
	case kHasToken:  return g.tokens.holds(id) ? BtStatus::Success : BtStatus::Failure;
	case kRemembers: return me.mind.hasLastKnown != 0 ? BtStatus::Success : BtStatus::Failure;
	case kAttack:    return g.attack(me, c, id);
	case kCircle:    // 間合いを保って回りながら、近い敵ほど高い点でトークンを頼む
		me.want = ai::circleVelocity(me.body.position, g.hero.position, kRing, (i & 1) != 0 ? 1 : -1);
		g.tokens.request(id, 1.0f / (1.0f + ai::distanceXZ(me.body.position, g.hero.position)));
		return BtStatus::Running;
	case kSearch:
	{
		const BtStatus st = g.walkTo(me, me.mind.lastKnown);
		if (st == BtStatus::Success) { me.mind.hasLastKnown = 0; }   // 着いても居なければ忘れて持ち場へ戻る
		return st;
	}
	default:
		if (g.walkTo(me, me.home) != BtStatus::Running) { me.want = {}; }
		return BtStatus::Running;
	}
}

MITIRU_ASSERT_NO_PADDING(EnemyAi);
// 木の形 (JSON) と焼いたナビメッシュを host に渡す。--inspect ai と --inspect nav の窓が、木のノードの名前と床の形を出す
std::int32_t inspectAssets(module::InspectAsset* out, std::int32_t cap)
{
	static const std::string tree = [] {
		std::ifstream f("enemy_ai/assets/enemy.json", std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}();
	const std::vector<std::uint8_t>& nav = level().navBlob;
	const module::InspectAsset all[2] = {{"bt_tree", "enemy", tree.data(), tree.size()}, {"navmesh", "garden", nav.data(), nav.size()}};
	const std::int32_t n = std::min<std::int32_t>(cap, 2);
	std::copy(all, all + n, out);
	return n;
}
MITIRU_INSPECT_ASSETS(inspectAssets);

// 木の状態・知覚・トークンはエンジンの型のまま載せる。--inspect ai の窓が敵ごとに読む
MITIRU_REFLECT(EnemyAi, attackers, seeing, remembering, minGap, hero.position.x, hero.position.z, tokens,
               e[0].bt, e[0].mind, e[1].bt, e[1].mind, e[2].bt, e[2].mind, e[3].bt, e[3].mind,
               e[4].bt, e[4].mind, e[5].bt, e[5].mind, e[6].bt, e[6].mind, e[7].bt, e[7].mind);
MITIRU_GAME(EnemyAi);
