#pragma once

/// @file GpuParticles.hpp
/// @brief GPU パーティクルの放出の記述と、GPU のシミュレーションと同じ規則 (放出の時刻・乱数・刻みの計画)
/// @details 放出はゲームが毎フレーム「出してからの経過秒」つきで渡す。レンダラは経過秒を 60 Hz の
///          固定の刻みに直し、まだ進めていない刻みだけ GPU で進める。経過秒が戻ったら (巻き戻し・分岐)
///          寿命の長さぶん前から進め直すので、同じ経過秒からは同じ粒が出る。乱数は seed と粒の番号の
///          ハッシュで、GameMemory の乱数とは独立している (描画だけに使う)。
///          ここにあるのは CPU と GPU で同じ式で、テストが GPU の読み戻しと突き合わせる基準でもある。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace mitiru::render
{

/// @brief シミュレーションの刻み (Hz)。経過秒をこの刻みの整数に切り捨てて進める
inline constexpr std::uint32_t kParticleStepHz = 60;
/// @brief 全エミッターで分け合う粒の数
inline constexpr std::uint32_t kParticlePoolSize = 65536;
/// @brief エミッター 1 個が持てる粒の上限
inline constexpr std::uint32_t kMaxParticlesPerEmitter = 16384;
/// @brief 1 フレームに受け付けるエミッターの数
inline constexpr int kMaxParticleEmitters = 256;
/// @brief 粒の番号が無いことを表す値
inline constexpr std::uint32_t kParticleNoIndex = 0xFFFFFFFFu;

enum class ParticleBlend : std::uint8_t
{
	Additive = 0,   ///< 火花・光。重なるほど明るく、描く順に依らない
	Alpha = 1,      ///< 煙・土ぼこり・血しぶき。半透明 (WBOIT) で重ねるので並べ替えは要らない
};

enum class ParticleCollision : std::uint8_t
{
	None = 0,
	Bounce = 1,   ///< 深度バッファの面で跳ね返る
	Kill = 2,     ///< 面に当たったら消える
};

enum class ParticleShape : std::uint8_t
{
	SoftDisc = 0,   ///< 縁の柔らかい円
	Spark = 1,      ///< 芯の硬い円 (stretch と組んで火花)
};

/// @brief ゲームが毎フレーム渡すエミッター 1 個 (180 byte の POD)
/// @details 大きさと色は寿命の 0・midPoint・1 の 3 点を直線でつなぐ。key が同じで形 (経過秒と位置と向き
///          以外) が変わったら作り直す。位置と向きは新しく出る粒だけに効く (出た粒はワールドに残る)。
struct ParticleEmitterDesc
{
	std::uint32_t key = 0;     ///< 同じエフェクトの続きを見分ける番号 (ゲームが付ける)
	std::uint32_t seed = 0;
	float age = 0.0f;          ///< 出してからの経過秒
	float duration = 0.0f;     ///< 連続して出し続ける秒。0 以下は止めない
	float position[3] = {0.0f, 0.0f, 0.0f};
	float spawnRadius = 0.0f;  ///< 出る位置を散らす球の半径
	float direction[3] = {0.0f, 1.0f, 0.0f};
	float spreadDeg = 30.0f;   ///< 向きを散らす円錐の半角
	float speedMin = 1.0f;
	float speedMax = 2.0f;
	float lifeMin = 0.5f;
	float lifeMax = 1.0f;
	float rate = 0.0f;         ///< 毎秒の数
	std::uint32_t burst = 16;  ///< 0 秒に出す数
	float gravity[3] = {0.0f, -9.8f, 0.0f};
	float drag = 0.0f;         ///< 刻みごとに速度を 1 / (1 + drag × dt) 倍する
	float size[3] = {0.1f, 0.1f, 0.0f};   ///< 寿命の 0・midPoint・1 での直径
	float color[3][4] = {{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}};
	float midPoint = 0.5f;
	float stretch = 0.0f;      ///< 速度の向きへ伸ばす秒 (速度 × stretch だけ長くなる)
	float softDistance = 0.1f; ///< 面と交わる所を消していく距離。0 なら面で切る
	float lit = 0.0f;          ///< 場面の光 (主光源・環境光・局所光) を受ける割合 0..1
	float bounce = 0.4f;       ///< 跳ね返りの反発 0..1
	ParticleBlend blend = ParticleBlend::Additive;
	ParticleCollision collision = ParticleCollision::None;
	ParticleShape shape = ParticleShape::SoftDisc;
	std::uint8_t reserved0 = 0;
	std::int32_t textureLayer = -1;  ///< VFX テクスチャの層。-1 は shape の手続きの形
	std::uint32_t maxParticles = 0;  ///< 0 は自動 (burst + rate × lifeMax)
};

static_assert(sizeof(ParticleEmitterDesc) == 180, "ParticleEmitterDesc は 180 byte の POD");
static_assert(std::is_standard_layout_v<ParticleEmitterDesc> && std::is_trivially_copyable_v<ParticleEmitterDesc>);

/// @brief GPU に置く粒 1 個 (48 byte)。index は粒の通し番号で、kParticleNoIndex なら空き
struct ParticleGpu
{
	float position[3];
	float age;
	float velocity[3];
	std::uint32_t index;
	float lifetime;
	float random;   ///< 大きさのばらつきに使う 0..1
	float pad[2];
};

static_assert(sizeof(ParticleGpu) == 48, "HLSL の Particle と同じ 48 byte");

/// @brief PCG のハッシュ (HLSL と同じ整数演算なので、どの GPU でも同じ値)
[[nodiscard]] constexpr std::uint32_t particleHash(std::uint32_t v) noexcept
{
	const std::uint32_t state = v * 747796405u + 2891336453u;
	const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

/// @brief 粒 index の channel 番目の乱数 [0, 1)。上位 24 bit だけ使うので float にちょうど入る
[[nodiscard]] constexpr float particleRandom(std::uint32_t seed, std::uint32_t index, std::uint32_t channel) noexcept
{
	return static_cast<float>(particleHash(seed ^ particleHash(index * 16u + channel)) >> 8) * (1.0f / 16777216.0f);
}

[[nodiscard]] inline float particleLifeMax(const ParticleEmitterDesc& d) noexcept
{
	return std::max({d.lifeMin, d.lifeMax, 1.0f / static_cast<float>(kParticleStepHz)});
}

/// @brief 寿命の間に同時に生きうる数 (粒の席の数)。席は index % capacity で使い回す
[[nodiscard]] inline std::uint32_t particleCapacity(const ParticleEmitterDesc& d) noexcept
{
	std::uint64_t n = d.burst;
	if (d.rate > 0.0f)
	{
		const float window = (d.duration > 0.0f) ? std::min(d.duration, particleLifeMax(d)) : particleLifeMax(d);
		n += static_cast<std::uint64_t>(std::ceil(window * d.rate)) + 1u;
	}
	if (d.maxParticles > 0) { n = std::min<std::uint64_t>(n, d.maxParticles); }
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(n, kMaxParticlesPerEmitter));
}

/// @brief 刻み step の時刻までに出た粒の数 (burst は 0 秒、続きは 1 / rate ごと)。シェーダーと同じ式
[[nodiscard]] inline std::uint32_t particleSpawnedAtStep(const ParticleEmitterDesc& d, std::uint32_t step) noexcept
{
	std::uint32_t n = d.burst;
	if (d.rate > 0.0f)
	{
		std::uint32_t cont =
			static_cast<std::uint32_t>(std::floor(static_cast<float>(step) * d.rate / static_cast<float>(kParticleStepHz) + 1e-4f)) + 1u;
		if (d.duration > 0.0f) { cont = std::min(cont, static_cast<std::uint32_t>(std::ceil(d.duration * d.rate))); }
		n += cont;
	}
	return n;
}

[[nodiscard]] inline float particleSpawnTime(const ParticleEmitterDesc& d, std::uint32_t index) noexcept
{
	if (index < d.burst || !(d.rate > 0.0f)) { return 0.0f; }
	return static_cast<float>(index - d.burst) / d.rate;
}

/// @brief 粒 index の出た時の速度 (円錐の中で一様、速さは speedMin..speedMax)。シェーダーと同じ式
inline void particleInitialVelocity(const ParticleEmitterDesc& d, std::uint32_t index, float out[3]) noexcept
{
	const float cosSpread = std::cos(std::clamp(d.spreadDeg, 0.0f, 180.0f) * 0.017453292519943295f);
	const float z = cosSpread + (1.0f - cosSpread) * particleRandom(d.seed, index, 0);
	const float phi = 6.28318530718f * particleRandom(d.seed, index, 1);
	const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
	const float local[3] = {r * std::cos(phi), r * std::sin(phi), z};
	float w[3] = {d.direction[0], d.direction[1], d.direction[2]};
	const float wl = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
	if (wl > 1e-8f) { for (float& c : w) { c /= wl; } } else { w[0] = 0.0f; w[1] = 1.0f; w[2] = 0.0f; }
	// w と垂直な 2 軸 (w が y に近い時は x から作る)
	const float a[3] = {std::fabs(w[1]) < 0.999f ? 0.0f : 1.0f, std::fabs(w[1]) < 0.999f ? 1.0f : 0.0f, 0.0f};
	float u[3] = {a[1] * w[2] - a[2] * w[1], a[2] * w[0] - a[0] * w[2], a[0] * w[1] - a[1] * w[0]};
	const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (float& c : u) { c /= ul; }
	const float v[3] = {w[1] * u[2] - w[2] * u[1], w[2] * u[0] - w[0] * u[2], w[0] * u[1] - w[1] * u[0]};
	const float speed = d.speedMin + (d.speedMax - d.speedMin) * particleRandom(d.seed, index, 2);
	for (int i = 0; i < 3; ++i) { out[i] = (u[i] * local[0] + v[i] * local[1] + w[i] * local[2]) * speed; }
}

[[nodiscard]] inline float particleLifetime(const ParticleEmitterDesc& d, std::uint32_t index) noexcept
{
	return d.lifeMin + (d.lifeMax - d.lifeMin) * particleRandom(d.seed, index, 3);
}

/// @brief 経過秒 age に当たる刻みの番号 (刻み k は時刻 k / kParticleStepHz の姿)
[[nodiscard]] inline std::uint32_t particleTargetStep(float age) noexcept
{
	if (!(age > 0.0f)) { return 0; }
	return static_cast<std::uint32_t>(std::floor(age * static_cast<float>(kParticleStepHz) + 1e-3f));
}

/// @brief このフレームに進める刻みの計画
struct ParticleStepPlan
{
	bool reset = false;          ///< 席を全部空きとみなして first から進める
	std::uint32_t first = 0;     ///< 最初に進める刻み
	std::uint32_t count = 0;     ///< 進める刻みの数 (0 ならそのまま描く)
};

/// @brief 進めた刻み done (まだ無ければ hasState = false) から target まで行く計画を立てる
/// @details 戻った時や遠くへ飛んだ時は、寿命の最大 + 1 刻みだけ前から進め直す。それより前に出た粒は
///          target の時刻には寿命が尽きているので、頭から進めた時と同じ粒が残る (深度で跳ねる粒は除く)。
[[nodiscard]] inline ParticleStepPlan planParticleSteps(const ParticleEmitterDesc& d, bool hasState, std::uint32_t done,
                                                       std::uint32_t target) noexcept
{
	const std::uint32_t window =
		static_cast<std::uint32_t>(std::ceil(particleLifeMax(d) * static_cast<float>(kParticleStepHz))) + 1u;
	ParticleStepPlan p;
	if (hasState && target >= done && target - done <= window)
	{
		p.first = done + 1;
		p.count = target - done;
		return p;
	}
	p.reset = true;
	p.first = (target > window) ? target - window : 0;
	p.count = target - p.first + 1;
	return p;
}

/// @brief 形 (経過秒・位置・向き以外) が変わったかを見るための値
[[nodiscard]] inline std::uint32_t particleShapeHash(const ParticleEmitterDesc& d) noexcept
{
	ParticleEmitterDesc s = d;
	s.age = 0.0f;
	for (float& p : s.position) { p = 0.0f; }
	for (float& p : s.direction) { p = 0.0f; }
	const auto* bytes = reinterpret_cast<const unsigned char*>(&s);
	std::uint32_t h = 2166136261u;
	for (std::size_t i = 0; i < sizeof(s); ++i) { h = (h ^ bytes[i]) * 16777619u; }
	return h;
}

/// @brief 粒のプールの区画を配る (先頭から最初に入る空きへ)。区画は 64 個単位
class ParticlePoolAllocator
{
public:
	explicit ParticlePoolAllocator(std::uint32_t size = kParticlePoolSize) { m_free.push_back({0, size}); }

	/// @return 区画の先頭。入らなければ kParticleNoIndex
	[[nodiscard]] std::uint32_t allocate(std::uint32_t count)
	{
		const std::uint32_t n = roundUp(count);
		for (std::size_t i = 0; i < m_free.size(); ++i)
		{
			if (m_free[i].count < n) { continue; }
			const std::uint32_t first = m_free[i].first;
			m_free[i].first += n;
			m_free[i].count -= n;
			if (m_free[i].count == 0) { m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i)); }
			return first;
		}
		return kParticleNoIndex;
	}

	void release(std::uint32_t first, std::uint32_t count)
	{
		const Range r{first, roundUp(count)};
		const auto it = std::lower_bound(m_free.begin(), m_free.end(), r,
		                                 [](const Range& a, const Range& b) { return a.first < b.first; });
		const auto pos = m_free.insert(it, r);
		mergeAround(static_cast<std::size_t>(pos - m_free.begin()));
	}

	[[nodiscard]] std::uint32_t freeCount() const noexcept
	{
		std::uint32_t n = 0;
		for (const Range& r : m_free) { n += r.count; }
		return n;
	}

	[[nodiscard]] static constexpr std::uint32_t roundUp(std::uint32_t count) noexcept { return (count + 63u) & ~63u; }

private:
	struct Range
	{
		std::uint32_t first;
		std::uint32_t count;
	};

	void mergeAround(std::size_t i)
	{
		if (i + 1 < m_free.size() && m_free[i].first + m_free[i].count == m_free[i + 1].first)
		{
			m_free[i].count += m_free[i + 1].count;
			m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i + 1));
		}
		if (i > 0 && m_free[i - 1].first + m_free[i - 1].count == m_free[i].first)
		{
			m_free[i - 1].count += m_free[i].count;
			m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	std::vector<Range> m_free;
};

} // namespace mitiru::render
