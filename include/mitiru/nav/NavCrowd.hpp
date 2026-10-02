#pragma once

/// @file NavCrowd.hpp
/// @brief 50〜200 体の敵を互いに避けながらナビメッシュ上で歩かせる群衆 (DetourCrowd)。GameMemory の外に置き、
///        MITIRU_SIDE_STATE (ADR 0054) で巻き戻す。mitiru_nav をリンクした DLL 向け
/// @details JoltGameWorld と同じ 3 段階で使う。
/// 1. レベルを読み agent を追加する関数を渡して static に 1 つ置き、MITIRU_SIDE_STATE("crowd", 1, crowd) と書く
/// 2. update の先頭で ensureBuilt(this) を呼び、目的地を setTarget、扉を navMesh().setObstacle で設定し、
///    最後に update(dt) を呼ぶ
/// 3. position(id) から描画対象を GameMemory へ写す
///
/// dtCrowd::update の結果は agent、ナビメッシュ、dtPathQueue、毎回作り直す作業域の 4 つで決まる。
/// agent の全状態は CrowdAgentImage に書く。
/// DynamicNavMesh は障害物の枠と世代だけで決まる形に保つ。
/// 複数フレームにまたがる dtPathQueue は private で bytes にできないため、dtCrowd には経路を探させない。
/// update の前に dtCrowd::checkPathValidity と同じ条件を調べ、該当する agent の経路を最後まで探して通路へ入れる。
/// dtCrowd に渡す時点では全 agent が条件から外れ、待ち行列は使われない。
/// 近くの agent の格子、回避候補、探索の node pool は使う前に空にする。
/// 乱数と時刻は使わない。
/// update ごとに待ち行列が空であることを確かめ、違反は invariantBreaks() で数える。
/// off-mesh connection の途中の状態は private のため、飛び降りと梯子は生成時に作らない。

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <DetourCrowd.h>

#include "mitiru/nav/DynamicNavMesh.hpp"
#include "mitiru/nav/detail/CrowdAgentImage.hpp"

namespace mitiru::nav
{

struct CrowdAgentConfig
{
	float radius = 0.4f;            ///< 焼いた agentRadius と揃える (ナビメッシュはこの幅だけ縮めてある)
	float height = 1.8f;
	float maxSpeed = 3.5f;
	float maxAcceleration = 10.0f;
	float separationWeight = 2.0f;  ///< 近くの agent から離れる強さ
};

struct NavCrowdSettings
{
	int maxAgents = 256;
	float maxAgentRadius = 0.6f;    ///< 近くの agent を探す格子の大きさと、ナビメッシュに乗せ直す範囲に使う
	int maxReplansPerUpdate = 16;   ///< 1 回の update で経路を探し直す agent の数の上限。残りは次の update へ回す
};

enum class CrowdMove : std::uint8_t
{
	Inactive,
	Idle,       ///< 目的地が無い
	Moving,     ///< 目的地へ向かっている (届かない目的地なら届く所まで: partial)
	Velocity,   ///< setVelocity で決めた速度で歩く
	Failed,     ///< 目的地への経路が無い
	OffMesh,    ///< ナビメッシュの外に出て動けない
};

struct CrowdAgentView
{
	sgc::Vec3f position{};
	sgc::Vec3f velocity{};
	sgc::Vec3f target{};
	CrowdMove move = CrowdMove::Inactive;
	bool partial = false;   ///< 目的地へは届かず、届く所で一番近い所へ向かっている
};

class NavCrowd
{
public:
	using BuildFn = void (*)(NavCrowd& crowd, const void* memory);
	static constexpr int kMaxPath = 256;   ///< dtCrowd が通路に持てる面の数 (dtCrowd::init が決める値)

	explicit NavCrowd(BuildFn build, const NavCrowdSettings& settings = {}) noexcept : m_build(build), m_settings(settings) {}
	NavCrowd(const NavCrowd&) = delete;
	NavCrowd& operator=(const NavCrowd&) = delete;

	/// @brief 無ければ作り、build でレベルを読んで agent を追加する。ホットリロード直後と restart 直後も同じ手順を通る
	void ensureBuilt(const void* memory)
	{
		if (m_core) return;
		m_core = std::make_unique<Core>();
		m_error.clear();
		if (m_build != nullptr) m_build(*this, memory);
	}

	/// @brief 群衆とナビメッシュを破棄する。次の ensureBuilt で作り直す (game の init / restart で呼ぶ)
	void reset() noexcept { m_core.reset(); }
	[[nodiscard]] bool built() const noexcept { return m_core != nullptr; }
	[[nodiscard]] bool ready() const noexcept { return m_core && m_core->crowd; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief .navcache を読み、群衆を作る。build から呼ぶ
	bool load(std::span<const std::uint8_t> navcache)
	{
		if (!m_core) m_core = std::make_unique<Core>();
		m_core->crowd.reset();
		return m_core->nav.load(navcache, &m_error) && initCrowd();
	}

	bool loadFile(const char* path)
	{
		if (!m_core) m_core = std::make_unique<Core>();
		m_core->crowd.reset();
		return m_core->nav.loadFile(path, &m_error) && initCrowd();
	}

	/// @brief 扉などの障害物を置くナビメッシュ。問い合わせには navMesh().query() を使う
	[[nodiscard]] DynamicNavMesh& navMesh() noexcept { return m_core->nav; }
	[[nodiscard]] int maxAgents() const noexcept { return m_settings.maxAgents; }

	/// @return agent の番号。追加できなければ -1
	int addAgent(const sgc::Vec3f& position, const CrowdAgentConfig& config = {})
	{
		if (!ready()) return -1;
		const dtCrowdAgentParams p = agentParams(config);
		const float pos[3] = {position.x, position.y, position.z};
		const int id = m_core->crowd->addAgent(pos, &p);
		if (id >= 0) m_core->extra[static_cast<std::size_t>(id)] = AgentExtra{m_core->nav.epoch(), 0};
		return id;
	}

	void removeAgent(int id)
	{
		if (agentAt(id) != nullptr) m_core->crowd->removeAgent(id);
	}

	/// @brief 目的地までの経路をその場で探す。到達できない場合は到達できる地点まで進む (CrowdAgentView::partial)
	/// @return 経路が見つかれば true
	bool setTarget(int id, const sgc::Vec3f& goal)
	{
		dtCrowdAgent* a = agentAt(id);
		if (a == nullptr) return false;
		float nearest[3];
		const dtPolyRef ref = m_core->nav.query().nearestRef(goal, nearest);
		if (ref == 0) return false;
		a->targetRef = ref;
		copy3(a->targetPos, nearest);
		a->targetState = DT_CROWDAGENT_TARGET_FAILED;
		if (a->state == DT_CROWDAGENT_STATE_WALKING) plan(*a);
		m_core->extra[static_cast<std::size_t>(id)] = AgentExtra{m_core->nav.epoch(), 0};
		return a->targetState == DT_CROWDAGENT_TARGET_VALID;
	}

	/// @brief 経路を使わず指定した速度で歩かせる。ほかの agent を避け、ナビメッシュの縁で止まる
	void setVelocity(int id, const sgc::Vec3f& v)
	{
		const float vel[3] = {v.x, v.y, v.z};
		if (agentAt(id) != nullptr) m_core->crowd->requestMoveVelocity(id, vel);
	}

	void stop(int id)
	{
		if (agentAt(id) != nullptr) m_core->crowd->resetMoveTarget(id);
	}

	[[nodiscard]] CrowdAgentView view(int id) const
	{
		const dtCrowdAgent* a = agentAt(id);
		if (a == nullptr) return {};
		CrowdAgentView v;
		v.position = {a->npos[0], a->npos[1], a->npos[2]};
		v.velocity = {a->vel[0], a->vel[1], a->vel[2]};
		v.move = moveOf(*a);
		if (v.move == CrowdMove::Moving || v.move == CrowdMove::Failed) v.target = {a->targetPos[0], a->targetPos[1], a->targetPos[2]};
		v.partial = a->partial;
		return v;
	}

	[[nodiscard]] sgc::Vec3f position(int id) const { return view(id).position; }

	/// @brief 障害物を反映し、必要な経路を探し直してから、全 agent を dt だけ進める
	void update(float dt)
	{
		if (!ready() || !(dt > 0.0f)) return;
		m_replans = 0;
		m_core->nav.sync();
		prepare(dt);
		m_core->crowd->update(dt, nullptr);
		countInvariantBreaks();
	}

	[[nodiscard]] int replansLastUpdate() const noexcept { return m_replans; }
	[[nodiscard]] std::uint64_t invariantBreaks() const noexcept { return m_invariantBreaks; }

	// ── host へ預ける窓口 (MITIRU_SIDE_STATE) ──

	/// @brief [見出し][ナビメッシュの状態][agent ごとの CrowdAgentImage と通路の面の列] を dst へ書き、必要な byte 数を返す
	std::uint64_t saveState(const void* memory, void* dst, std::uint64_t cap)
	{
		ensureBuilt(memory);
		if (!ready()) return 0;
		const std::uint64_t need = imageSize();
		if (cap >= need) writeImage(static_cast<std::uint8_t*>(dst));
		return need;
	}

	/// @brief saveState の bytes から戻す。レベルや群衆の大きさが異なる場合は変更せず false を返す
	bool restoreState(void* memory, const void* src, std::uint64_t size)
	{
		ensureBuilt(memory);
		if (!ready() || src == nullptr) return false;
		return readImage(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(src), static_cast<std::size_t>(size)));
	}

private:
	struct AgentExtra
	{
		std::uint32_t checkedEpoch = 0;
		std::uint8_t pending = 0;   ///< 探し直しが上限で次の update へ回った
	};
	struct CrowdDeleter { void operator()(dtCrowd* p) const noexcept { dtFreeCrowd(p); } };
	struct Core
	{
		DynamicNavMesh nav;
		std::unique_ptr<dtCrowd, CrowdDeleter> crowd;   ///< nav を指すので nav より先に壊れるよう後ろに置く
		std::vector<AgentExtra> extra;
		dtPolyRef path[kMaxPath]{};
	};
	struct ImageHeader
	{
		std::uint32_t magic;
		std::uint32_t version;
		std::uint32_t maxAgents;
		std::uint32_t agentCount;
		std::uint64_t navImageSize;
	};
	static constexpr std::uint32_t kImageMagic = 0x4452434Du;   // "MCRD"
	static constexpr std::uint32_t kImageVersion = 1;
	// dtCrowd::checkPathValidity と同じ値。通路の先を確かめる 10 面と、終点の手前で探し直すまでの 1.0 秒
	static constexpr int kCheckLookAhead = 10;
	static constexpr float kTargetReplanDelay = 1.0f;

	static void copy3(float* dst, const float* src) noexcept { std::memcpy(dst, src, sizeof(float) * 3); }

	[[nodiscard]] dtCrowdAgent* agentAt(int id) const
	{
		if (!ready() || id < 0 || id >= m_settings.maxAgents) return nullptr;
		dtCrowdAgent* a = m_core->crowd->getEditableAgent(id);
		return (a != nullptr && a->active) ? a : nullptr;
	}

	[[nodiscard]] static CrowdMove moveOf(const dtCrowdAgent& a) noexcept
	{
		if (a.state != DT_CROWDAGENT_STATE_WALKING) return CrowdMove::OffMesh;
		switch (a.targetState)
		{
		case DT_CROWDAGENT_TARGET_NONE:     return CrowdMove::Idle;
		case DT_CROWDAGENT_TARGET_VELOCITY: return CrowdMove::Velocity;
		case DT_CROWDAGENT_TARGET_FAILED:   return CrowdMove::Failed;
		default:                            return CrowdMove::Moving;
		}
	}

	[[nodiscard]] static dtCrowdAgentParams agentParams(const CrowdAgentConfig& c) noexcept
	{
		dtCrowdAgentParams p{};
		p.radius = c.radius;
		p.height = c.height;
		p.maxAcceleration = c.maxAcceleration;
		p.maxSpeed = c.maxSpeed;
		p.collisionQueryRange = c.radius * 5.0f;
		p.pathOptimizationRange = c.radius * 30.0f;
		p.separationWeight = c.separationWeight;
		p.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION
			| DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
		p.obstacleAvoidanceType = 0;
		p.queryFilterType = 0;
		p.userData = nullptr;
		return p;
	}

	bool initCrowd()
	{
		Core& c = *m_core;
		c.crowd.reset(dtAllocCrowd());
		if (!c.crowd || !c.crowd->init(m_settings.maxAgents, m_settings.maxAgentRadius, c.nav.detour()))
		{
			c.crowd.reset();
			m_error = "dtCrowd を作れない";
			return false;
		}
		c.crowd->getEditableFilter(0)->setIncludeFlags(1);
		c.crowd->getEditableFilter(0)->setExcludeFlags(0);
		// 回避候補を減らし、200 体を 1 フレームで処理する
		dtObstacleAvoidanceParams q = *c.crowd->getObstacleAvoidanceParams(0);
		q.velBias = 0.5f;
		q.adaptiveDivs = 5;
		q.adaptiveRings = 2;
		q.adaptiveDepth = 2;
		c.crowd->setObstacleAvoidanceParams(0, &q);
		c.extra.assign(static_cast<std::size_t>(m_settings.maxAgents), AgentExtra{});
		return true;
	}

	[[nodiscard]] static bool followsPath(const dtCrowdAgent& a) noexcept
	{
		return a.targetState != DT_CROWDAGENT_TARGET_NONE && a.targetState != DT_CROWDAGENT_TARGET_VELOCITY;
	}

	/// @brief 通路の先頭の面が無効なら、dtCrowd::checkPathValidity と同じ手順で近くの面へ移す
	/// @return 別の面へ移したら true。面が見つからなければ false を返し、dtCrowd が動けない状態にする
	bool fixStart(dtCrowdAgent& a, const dtQueryFilter* f)
	{
		dtNavMeshQuery* q = m_core->nav.query().detour();
		if (q->isValidPolyRef(a.corridor.getFirstPoly(), f)) return false;
		float nearest[3];
		copy3(nearest, a.npos);
		dtPolyRef ref = 0;
		q->findNearestPoly(a.npos, m_core->crowd->getQueryHalfExtents(), f, &ref, nearest);
		if (ref == 0) return false;
		a.corridor.fixPathStart(ref, nearest);
		a.boundary.reset();
		copy3(a.npos, nearest);
		return true;
	}

	/// @brief ナビメッシュの変更後に、通路と目的地の面がまだ有効かを確かめる
	/// @return 経路を探し直す必要があれば true
	bool revalidate(dtCrowdAgent& a)
	{
		dtNavMeshQuery* q = m_core->nav.query().detour();
		const dtQueryFilter* f = m_core->crowd->getFilter(a.params.queryFilterType);
		const bool moved = fixStart(a, f);
		if (!followsPath(a)) return false;
		if (a.targetState != DT_CROWDAGENT_TARGET_FAILED && !q->isValidPolyRef(a.targetRef, f))
		{
			float nearest[3];
			copy3(nearest, a.targetPos);
			a.targetRef = 0;
			q->findNearestPoly(a.targetPos, m_core->crowd->getQueryHalfExtents(), f, &a.targetRef, nearest);
			copy3(a.targetPos, nearest);
			if (a.targetRef == 0)
			{
				a.corridor.reset(a.corridor.getFirstPoly(), a.npos);
				a.partial = false;
				a.targetState = DT_CROWDAGENT_TARGET_NONE;
				return false;
			}
			return true;
		}
		return moved || !a.corridor.isValid(a.corridor.getPathCount(), q, f);
	}

	/// @brief dtCrowd が短い通路の再探索を求める条件を、先に判定する
	[[nodiscard]] static bool wantsRefresh(const dtCrowdAgent& a, float dt) noexcept
	{
		if (a.targetState != DT_CROWDAGENT_TARGET_VALID && a.targetState != DT_CROWDAGENT_TARGET_FAILED) return true;
		return a.targetState == DT_CROWDAGENT_TARGET_VALID && a.targetReplanTime + dt > kTargetReplanDelay
			&& a.corridor.getPathCount() < kCheckLookAhead && a.corridor.getLastPoly() != a.targetRef;
	}

	/// @brief 通路の先頭から目的地まで経路を最後まで探して通路へ入れ、dtCrowd の待ち行列を使わない
	void plan(dtCrowdAgent& a)
	{
		dtNavMeshQuery* q = m_core->nav.query().detour();
		const dtQueryFilter* f = m_core->crowd->getFilter(a.params.queryFilterType);
		dtPolyRef* path = m_core->path;
		const dtPolyRef start = a.corridor.getFirstPoly();
		int n = 0;
		const dtStatus st = q->findPath(start, a.targetRef, a.npos, a.targetPos, f, path, &n, kMaxPath);
		a.targetPathqRef = DT_PATHQ_INVALID;
		a.targetReplan = false;
		a.targetReplanTime = 0.0f;
		a.boundary.reset();
		if (dtStatusFailed(st) || n == 0)
		{
			a.corridor.reset(start, a.npos);
			a.partial = false;
			a.targetState = DT_CROWDAGENT_TARGET_FAILED;
			return;
		}
		float end[3];
		copy3(end, a.targetPos);
		const dtPolyRef last = path[n - 1];
		if (last != a.targetRef) q->closestPointOnPoly(last, a.targetPos, end, nullptr);
		a.corridor.setCorridor(end, path, n);
		a.partial = last != a.targetRef;
		a.targetState = DT_CROWDAGENT_TARGET_VALID;
	}

	/// @brief 再探索を次の update へ回す。無効な面を通路から外し、dtCrowd から再探索を求められない状態にする
	void defer(dtCrowdAgent& a)
	{
		const dtQueryFilter* f = m_core->crowd->getFilter(a.params.queryFilterType);
		a.corridor.trimInvalidPath(a.corridor.getFirstPoly(), a.npos, m_core->nav.query().detour(), f);
		a.targetReplanTime = 0.0f;
	}

	/// @brief dtCrowd::update の前に、再探索が必要な agent を番号順に上限まで処理する
	void prepare(float dt)
	{
		const std::uint32_t epoch = m_core->nav.epoch();
		int budget = m_settings.maxReplansPerUpdate;
		for (int i = 0; i < m_settings.maxAgents; ++i)
		{
			dtCrowdAgent& a = *m_core->crowd->getEditableAgent(i);
			if (!a.active || a.state != DT_CROWDAGENT_STATE_WALKING) continue;
			AgentExtra& x = m_core->extra[static_cast<std::size_t>(i)];
			bool replan = x.pending != 0;
			if (x.checkedEpoch != epoch) { replan = revalidate(a) || replan; x.checkedEpoch = epoch; }
			if (!followsPath(a)) { x.pending = 0; continue; }
			if (!replan && !wantsRefresh(a, dt)) continue;
			x.pending = budget > 0 ? 0 : 1;
			if (budget-- > 0) { plan(a); ++m_replans; }
			else defer(a);
		}
	}

	void countInvariantBreaks()
	{
		for (int i = 0; i < m_settings.maxAgents; ++i)
		{
			const dtCrowdAgent& a = *m_core->crowd->getAgent(i);
			if (!a.active) continue;
			const bool queued = a.targetState == DT_CROWDAGENT_TARGET_REQUESTING
				|| a.targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE || a.targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_PATH;
			if (queued || a.state == DT_CROWDAGENT_STATE_OFFMESH) ++m_invariantBreaks;
		}
	}

	[[nodiscard]] std::uint64_t imageSize() const
	{
		std::uint64_t n = sizeof(ImageHeader) + m_core->nav.imageSize();
		for (int i = 0; i < m_settings.maxAgents; ++i)
		{
			const dtCrowdAgent& a = *m_core->crowd->getAgent(i);
			if (a.active) n += sizeof(detail::CrowdAgentImage) + sizeof(dtPolyRef) * static_cast<std::uint64_t>(a.corridor.getPathCount());
		}
		return n;
	}

	void writeImage(std::uint8_t* dst) const
	{
		ImageHeader h{kImageMagic, kImageVersion, static_cast<std::uint32_t>(m_settings.maxAgents), 0, m_core->nav.imageSize()};
		for (int i = 0; i < m_settings.maxAgents; ++i) h.agentCount += m_core->crowd->getAgent(i)->active ? 1u : 0u;
		std::memcpy(dst, &h, sizeof(h));
		dst += sizeof(h);
		m_core->nav.writeImage(dst);
		dst += h.navImageSize;
		for (int i = 0; i < m_settings.maxAgents; ++i)
		{
			const dtCrowdAgent& a = *m_core->crowd->getAgent(i);
			if (!a.active) continue;
			detail::CrowdAgentImage img = detail::captureAgent(a, static_cast<std::uint32_t>(i));
			img.checkedEpoch = m_core->extra[static_cast<std::size_t>(i)].checkedEpoch;
			img.pending = m_core->extra[static_cast<std::size_t>(i)].pending;
			std::memcpy(dst, &img, sizeof(img));
			dst += sizeof(img);
			const std::size_t pathBytes = sizeof(dtPolyRef) * static_cast<std::size_t>(img.npath);
			std::memcpy(dst, a.corridor.getPath(), pathBytes);
			dst += pathBytes;
		}
	}

	bool readImage(std::span<const std::uint8_t> src)
	{
		ImageHeader h{};
		if (src.size() < sizeof(h)) return false;
		std::memcpy(&h, src.data(), sizeof(h));
		if (h.magic != kImageMagic || h.version != kImageVersion || h.maxAgents != static_cast<std::uint32_t>(m_settings.maxAgents)) return false;
		if (src.size() - sizeof(h) < h.navImageSize || !agentsFit(src.subspan(sizeof(h) + h.navImageSize), h.agentCount)) return false;
		if (!m_core->nav.readImage(src.subspan(sizeof(h), static_cast<std::size_t>(h.navImageSize)))) return false;
		std::vector<std::uint8_t> keep(static_cast<std::size_t>(m_settings.maxAgents), 0);
		const std::uint8_t* p = src.data() + sizeof(h) + h.navImageSize;
		for (std::uint32_t k = 0; k < h.agentCount; ++k)
		{
			detail::CrowdAgentImage img;
			std::memcpy(&img, p, sizeof(img));
			p += sizeof(img);
			std::memcpy(m_core->path, p, sizeof(dtPolyRef) * static_cast<std::size_t>(img.npath));
			p += sizeof(dtPolyRef) * static_cast<std::size_t>(img.npath);
			detail::restoreAgent(*m_core->crowd->getEditableAgent(static_cast<int>(img.index)), img, m_core->path);
			m_core->extra[img.index] = AgentExtra{img.checkedEpoch, img.pending};
			keep[img.index] = 1;
		}
		for (int i = 0; i < m_settings.maxAgents; ++i)
		{
			if (keep[static_cast<std::size_t>(i)] == 0) m_core->crowd->removeAgent(i);
		}
		return true;
	}

	/// @brief 書き換える前に、agent の記録が src に収まり、番号と数が範囲内かをすべて確かめる
	[[nodiscard]] bool agentsFit(std::span<const std::uint8_t> src, std::uint32_t count) const
	{
		std::size_t at = 0;
		for (std::uint32_t k = 0; k < count; ++k)
		{
			detail::CrowdAgentImage img;
			if (src.size() - at < sizeof(img)) return false;
			std::memcpy(&img, src.data() + at, sizeof(img));
			if (img.index >= static_cast<std::uint32_t>(m_settings.maxAgents) || !detail::plausible(img, kMaxPath)) return false;
			at += sizeof(img) + sizeof(dtPolyRef) * static_cast<std::size_t>(img.npath);
			if (at > src.size()) return false;
		}
		return at == src.size();
	}

	BuildFn m_build = nullptr;
	NavCrowdSettings m_settings;
	std::unique_ptr<Core> m_core;
	std::string m_error;
	int m_replans = 0;
	std::uint64_t m_invariantBreaks = 0;
};

} // namespace mitiru::nav
