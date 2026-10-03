#pragma once

/// @file JoltGameWorld.hpp
/// @brief game DLL が自分で持つ Jolt の world。保存と復元を host へ預け、巻き戻し・分岐・セーブ・
///        ホットリロードで GameMemory と一緒に戻せる (ADR 0054)。
/// @details 使い方は 3 つ。
/// 1. 場面の作り方を「GameMemory を読んで body を足す関数」として渡し、static に 1 つ置く。
/// 2. ファイルスコープで MITIRU_SIDE_STATE("jolt", 1, world) と書く。
/// 3. update の先頭で ensureBuilt(this)、続けて step(dt)。描く物は body から GameMemory へ写す。
///
/// 決定論の前提: 同じ binary、body を足す順が同じ、1 スレッドで解く (JobSystemSingleThreaded)、
/// 浮動小数点の縮約なし (Jolt は CROSS_PLATFORM_DETERMINISTIC で build 済み)。
/// body を作るのは組み立ての中だけにする。弾や破片のように出し入れする物は、組み立てで addReserveBody して
/// 置いておき、spawn / despawn で world に入れたり外したりする。作った body は消さないので BodyID が変わらず、
/// 保存する bytes にはどの body が world に入っているかと、外にある body の状態も入る。巻き戻すと出し入れも戻る。
/// 組み立ての後に body を作った world や、組が違う bytes は戻さずに断る。

#ifdef MITIRU_HAS_JOLT

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorder.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>

#include <mitiru/physics/detail/JoltBackend.hpp>

namespace mitiru::physics3d
{

namespace detail::jolt
{

/// @brief 呼び手の領域へ直接書き、呼び手の bytes から直接読む StateRecorder (毎回の確保をしない)。
/// @details 書くときは cap を超えた分を捨てて数だけ進め、必要量を size() で返す (host が広げて呼び直す)。
class SpanStateRecorder final : public JPH::StateRecorder
{
public:
	SpanStateRecorder(std::uint8_t* dst, std::uint64_t cap) noexcept : m_dst(dst), m_cap(cap) {}
	SpanStateRecorder(const std::uint8_t* src, std::uint64_t size) noexcept : m_src(src), m_cap(size) {}

	void WriteBytes(const void* data, size_t n) override
	{
		if (m_dst != nullptr && m_pos + n <= m_cap) { std::memcpy(m_dst + m_pos, data, n); }
		m_pos += n;
	}

	void ReadBytes(void* data, size_t n) override
	{
		if (m_src == nullptr || m_pos + n > m_cap) { m_failed = true; std::memset(data, 0, n); return; }
		std::memcpy(data, m_src + m_pos, n);
		m_pos += n;
	}

	bool IsEOF() const override { return m_pos >= m_cap; }
	bool IsFailed() const override { return m_failed; }

	[[nodiscard]] std::uint64_t size() const noexcept { return m_pos; }

private:
	std::uint8_t*       m_dst = nullptr;
	const std::uint8_t* m_src = nullptr;
	std::uint64_t       m_cap = 0;
	std::uint64_t       m_pos = 0;
	bool                m_failed = false;
};

}  // namespace detail::jolt

/// @brief body の回転を Screen::drawMesh の rotDeg ({x, y, z} 度。Y → X → Z の順に掛ける R = Ry Rx Rz) へ直す。
[[nodiscard]] inline sgc::Vec3f drawMeshRotationDeg(JPH::QuatArg q) noexcept
{
	constexpr float kToDeg = 57.29577951308232f;
	const JPH::Mat44 r = JPH::Mat44::sRotation(q);
	const float sinX = -r(1, 2);
	if (std::fabs(sinX) > 0.99999f)
	{
		// x が ±90 度では y と z が区別できないので、z を 0 とし、残りの回転を y で表す。
		const float x = sinX > 0.0f ? 90.0f : -90.0f;
		return {x, std::atan2(-r(2, 0), r(0, 0)) * kToDeg, 0.0f};
	}
	return {std::asin(sinX) * kToDeg, std::atan2(r(0, 2), r(2, 2)) * kToDeg, std::atan2(r(1, 0), r(1, 1)) * kToDeg};
}

class JoltGameWorld
{
public:
	/// @brief 場面を組み立てる関数。memory は GameMemory (何を置くかを決める進行データを読む)。
	using BuildFn = void (*)(JoltGameWorld& world, const void* memory);

	struct Settings
	{
		JPH::uint     maxBodies             = 2048;
		JPH::uint     maxBodyPairs          = 8192;
		JPH::uint     maxContactConstraints = 8192;
		std::uint32_t tempAllocatorBytes    = 16u * 1024u * 1024u;
		JPH::Vec3     gravity               = JPH::Vec3(0.0f, -9.81f, 0.0f);
	};

	explicit JoltGameWorld(BuildFn build) noexcept : m_build(build) {}
	JoltGameWorld(BuildFn build, const Settings& settings) noexcept : m_build(build), m_settings(settings) {}

	JoltGameWorld(const JoltGameWorld&) = delete;
	JoltGameWorld& operator=(const JoltGameWorld&) = delete;

	/// @brief world が無ければ作り、build で場面を組み立てる (ホットリロード直後・restart 直後も同じ経路)。
	void ensureBuilt(const void* memory)
	{
		if (m_core) { return; }
		m_core = std::make_unique<Core>(m_settings);
		m_core->system.SetContactListener(m_contactListener);
		if (m_build != nullptr) { m_build(*this, memory); }
		m_core->system.GetBodies(m_ids);
		m_bodySetHash = computeBodySetHash();
	}

	/// @brief world を捨てる。次の ensureBuilt が作り直す (game の init / restart で呼ぶ)。
	void reset() noexcept { m_core.reset(); }

	[[nodiscard]] bool built() const noexcept { return m_core != nullptr; }

	/// @brief 固定 dt で 1 回進める。collisionSteps は 1 フレームを何回に分けて解くか。
	void step(float dt, int collisionSteps = 1)
	{
		if (!m_core || dt <= 0.0f) { return; }
		m_core->system.Update(dt, collisionSteps, &m_core->temp, &m_core->jobs);
	}

	[[nodiscard]] JPH::PhysicsSystem& system() noexcept { return m_core->system; }
	[[nodiscard]] JPH::BodyInterface& bodies() noexcept { return m_core->system.GetBodyInterfaceNoLock(); }
	/// @brief soft body の SkinVertices などが使う一時領域
	[[nodiscard]] JPH::TempAllocator& tempAllocator() noexcept { return m_core->temp; }

	/// @brief 接触の知らせを受ける物 (破壊の衝撃を測るなど)。world を作り直すたびに付ける。
	/// @details 呼ばれるのは step の中だけで、1 スレッドで解くので呼ばれる順も毎回同じ。知らせから決めた結果は
	///          GameMemory か body の状態に残し、受け手の中に step をまたぐ状態を持たない (巻き戻しで戻らないため)。
	void setContactListener(JPH::ContactListener* listener) noexcept
	{
		m_contactListener = listener;
		if (m_core) { m_core->system.SetContactListener(listener); }
	}

	/// @brief body を作って world に足す。組み立て (build) の中でだけ呼ぶ。
	JPH::BodyID addBody(const JPH::BodyCreationSettings& settings, JPH::EActivation activation = JPH::EActivation::Activate)
	{
		return bodies().CreateAndAddBody(settings, activation);
	}

	/// @brief ragdoll を作って world に足す。world が持ち、world と一緒に消える。組み立ての中でだけ呼ぶ。
	/// @param group 衝突の group 番号。ragdoll ごとに変える (同じ ragdoll の親子は当たらない)
	JPH::Ragdoll* addRagdoll(const JPH::RagdollSettings& settings, JPH::CollisionGroup::GroupID group)
	{
		JPH::Ref<JPH::Ragdoll> ragdoll = settings.CreateRagdoll(group, 0, &m_core->system);
		ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
		m_core->ragdolls.push_back(ragdoll);
		return ragdoll.GetPtr();
	}

	/// @brief soft body (布など) を作って world に足す。組み立ての中でだけ呼ぶ。頂点の位置と速度は保存の bytes に入る
	JPH::BodyID addSoftBody(const JPH::SoftBodyCreationSettings& settings,
	                        JPH::EActivation activation = JPH::EActivation::Activate)
	{
		return bodies().CreateAndAddSoftBody(settings, activation);
	}

	/// @brief body を作るが world には入れずに置いておく。組み立ての中でだけ呼ぶ。
	JPH::BodyID addReserveBody(const JPH::BodyCreationSettings& settings)
	{
		const JPH::Body* body = bodies().CreateBody(settings);
		return body != nullptr ? body->GetID() : JPH::BodyID();
	}

	/// @brief body を、位置・向き・速度を決めて world に入れる。入っていれば何もしない。
	void spawn(const JPH::BodyID& id, JPH::RVec3Arg position, JPH::QuatArg rotation, JPH::Vec3Arg linearVelocity,
	           JPH::Vec3Arg angularVelocity = JPH::Vec3::sZero())
	{
		if (id.IsInvalid() || isSpawned(id)) { return; }
		bodies().SetPositionAndRotation(id, position, rotation, JPH::EActivation::DontActivate);
		bodies().AddBody(id, JPH::EActivation::Activate);
		bodies().SetLinearAndAngularVelocity(id, linearVelocity, angularVelocity);
	}

	/// @brief body を world から外す。消さずに置いておくので、また spawn できる。
	void despawn(const JPH::BodyID& id)
	{
		if (!id.IsInvalid() && isSpawned(id)) { bodies().RemoveBody(id); }
	}

	[[nodiscard]] bool isSpawned(const JPH::BodyID& id) const
	{
		return m_core->system.GetBodyInterfaceNoLock().IsAdded(id);
	}

	/// @brief pool の中で world に入っていない最初の body。全部入っていれば無効な ID。
	[[nodiscard]] JPH::BodyID firstFree(std::span<const JPH::BodyID> pool) const
	{
		for (const JPH::BodyID& id : pool)
		{
			if (!isSpawned(id)) { return id; }
		}
		return JPH::BodyID();
	}

	/// @brief 動く body を置く層と動かない body を置く層 (JoltBackend の 2 層の表)。
	static constexpr JPH::ObjectLayer kMovingLayer = detail::jolt::Layers::kMoving;
	static constexpr JPH::ObjectLayer kStaticLayer = detail::jolt::Layers::kNonMoving;

	// ── host へ預ける窓口 (MITIRU_SIDE_STATE) ───────────────────────────────

	/// @brief [組の印 (body の数と ID の hash)] + [world に入っている body の印] + Jolt の全状態 +
	///        [world の外にある body の状態] を dst へ書き、必要な byte 数を返す。
	/// @details Jolt の SaveState は world に入っている body しか書かない。外にある body の状態も書いておくと、
	/// 巻き戻した後にまた spawn したときに、元の実行と同じ状態から入る。
	std::uint64_t saveState(const void* memory, void* dst, std::uint64_t cap)
	{
		ensureBuilt(memory);
		Header h{kMagic, m_core->system.GetNumBodies(), m_bodySetHash};
		auto* out = static_cast<std::uint8_t*>(dst);
		if (cap >= sizeof(h)) { std::memcpy(out, &h, sizeof(h)); }
		detail::jolt::SpanStateRecorder rec(cap >= sizeof(h) ? out + sizeof(h) : nullptr,
		                                    cap >= sizeof(h) ? cap - sizeof(h) : 0);
		writeSpawnedBits(rec);
		m_core->system.SaveState(rec, JPH::EStateRecorderState::All);
		const JPH::BodyLockInterfaceNoLock& locks = m_core->system.GetBodyLockInterfaceNoLock();
		for (const JPH::BodyID& id : m_ids)
		{
			if (isSpawned(id)) { continue; }
			const JPH::BodyLockRead lock(locks, id);
			lock.GetBody().SaveState(rec);
		}
		return sizeof(h) + rec.size();
	}

	/// @brief saveState の bytes を戻す。body の組が今の world と違えば何もせずに false。
	/// @details 先に world に入れる body の組を bytes にそろえ (足す順は ID 順)、それから Jolt の状態を戻す。
	bool restoreState(void* memory, const void* src, std::uint64_t size)
	{
		ensureBuilt(memory);
		Header h{};
		if (src == nullptr || size < sizeof(h)) { return false; }
		std::memcpy(&h, src, sizeof(h));
		if (h.magic != kMagic || h.bodyCount != m_core->system.GetNumBodies() || h.bodyCount != m_ids.size()
		    || h.bodySetHash != computeBodySetHash() || size - sizeof(h) < (m_ids.size() + 31) / 32 * sizeof(std::uint32_t))
		{
			return false;
		}
		detail::jolt::SpanStateRecorder rec(static_cast<const std::uint8_t*>(src) + sizeof(h), size - sizeof(h));
		if (!applySpawnedBits(rec) || !m_core->system.RestoreState(rec)) { return false; }
		const JPH::BodyLockInterfaceNoLock& locks = m_core->system.GetBodyLockInterfaceNoLock();
		for (const JPH::BodyID& id : m_ids)
		{
			if (isSpawned(id)) { continue; }
			const JPH::BodyLockWrite lock(locks, id);
			lock.GetBody().RestoreState(rec);
		}
		return !rec.IsFailed();
	}

private:
	struct Header
	{
		std::uint32_t magic;
		std::uint32_t bodyCount;
		std::uint64_t bodySetHash;
	};
	static constexpr std::uint32_t kMagic = 0x32544C4Au;  // "JLT2" (world の出し入れを含む形)

	/// @brief world に入っている body を、組み立てた順の 1 bit ずつで書く。
	void writeSpawnedBits(detail::jolt::SpanStateRecorder& rec) const
	{
		std::uint32_t word = 0;
		for (std::size_t i = 0; i < m_ids.size(); ++i)
		{
			if (isSpawned(m_ids[i])) { word |= 1u << (i % 32); }
			if (i % 32 == 31 || i + 1 == m_ids.size()) { rec.Write(word); word = 0; }
		}
	}

	/// @brief bytes の印に合わせて body を world に入れる・外す。
	bool applySpawnedBits(detail::jolt::SpanStateRecorder& rec)
	{
		std::uint32_t word = 0;
		for (std::size_t i = 0; i < m_ids.size(); ++i)
		{
			if (i % 32 == 0) { rec.Read(word); }
			const bool want = ((word >> (i % 32)) & 1u) != 0;
			if (want && !isSpawned(m_ids[i])) { bodies().AddBody(m_ids[i], JPH::EActivation::DontActivate); }
			if (!want) { despawn(m_ids[i]); }
		}
		return !rec.IsFailed();
	}

	/// @brief Jolt の登録 (Factory と型) は Runtime が数えて 1 回だけ行う。宣言順に作られ、逆順に壊れる。
	struct Core
	{
		explicit Core(const Settings& s)
			: temp(s.tempAllocatorBytes), jobs(JPH::cMaxPhysicsJobs)
		{
			system.Init(s.maxBodies, 0, s.maxBodyPairs, s.maxContactConstraints, layers, objectVsBroadPhase, objectPairs);
			system.SetGravity(s.gravity);
		}
		detail::jolt::Runtime            runtime;
		JPH::TempAllocatorImpl           temp;
		JPH::JobSystemSingleThreaded     jobs;
		detail::jolt::BroadPhaseLayers   layers;
		detail::jolt::ObjectVsBroadPhase objectVsBroadPhase;
		detail::jolt::ObjectPairs        objectPairs;
		JPH::PhysicsSystem               system;
		std::vector<JPH::Ref<JPH::Ragdoll>> ragdolls;   ///< system より先に壊す (壊すときに body を消すため)

		~Core()
		{
			for (const auto& r : ragdolls) { r->RemoveFromPhysicsSystem(); }
			ragdolls.clear();
		}
	};

	[[nodiscard]] std::uint64_t computeBodySetHash() const
	{
		JPH::BodyIDVector ids;
		m_core->system.GetBodies(ids);
		std::uint64_t h = 0xCBF29CE484222325ull;
		for (const JPH::BodyID& id : ids)
		{
			h = (h ^ id.GetIndexAndSequenceNumber()) * 0x100000001B3ull;
		}
		return h;
	}

	BuildFn                m_build = nullptr;
	JPH::ContactListener*  m_contactListener = nullptr;
	JPH::BodyIDVector      m_ids;   ///< 組み立てで作った全 body (ID 順、world の外に置いた物も含む)
	Settings               m_settings{};
	std::unique_ptr<Core>  m_core;
	std::uint64_t          m_bodySetHash = 0;
};

}  // namespace mitiru::physics3d

#endif  // MITIRU_HAS_JOLT
