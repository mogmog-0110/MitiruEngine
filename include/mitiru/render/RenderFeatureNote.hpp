#pragma once

/// @file RenderFeatureNote.hpp
/// @brief 3D 描画の機能の ID と、backend が描けない依頼を数える入口 (ADR 0090)。
/// @details 表は RenderFeatures.hpp にある。IRenderer3D.hpp から include できるよう、重い GpuCompat.hpp を引かない ID と計数だけをここに置く。
///          `note` は描画にも ABI にも触れず数えるだけで、有効な段が無い間 (DX11、WebGL、Null) は何もしない。

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mitiru::render::features
{

/// API ではなく描ける範囲の段。表の全行がこの段の数だけ Support を持つ
enum class Tier : std::uint8_t
{
	Dx12,
	DawnCore,
	DawnCompat,
	Count
};

inline constexpr std::size_t kTierCount = static_cast<std::size_t>(Tier::Count);

/// 並びは RenderFeatures.hpp の表の行と同じ。表は id と添字の一致を static_assert で見る
enum class Feature : std::uint8_t
{
	MeshDraw,
	ModelGltf,
	ModelRelease,
	ToonStyle,
	ShadingModels,
	SunShadow,
	Outline,
	Bloom,
	AntiAlias,
	TonemapGrade,
	ColorLut,
	WetSurface,
	CloudLayer,
	HdrOutput,
	SkyAtmosphere,
	VolumetricFog,
	BakedGi,
	ReflectionProbes,
	Ssr,
	Ddgi,
	LocalLights,
	MultiLight,
	PointShadow,
	AmbientOcclusion,
	DepthOfField,
	AutoExposure,
	ContactShadow,
	TaaMotionVectors,
	MotionBlur,
	RenderScale,
	Fsr3,
	Oit,
	Decals,
	GpuParticles,
	VfxTextures,
	HitFeel,
	ImpactFrame,
	OccluderFade,
	Trails,
	ScreenProjection,
	OutdoorWorld,
	Clod,
	ViewSlots,
	Effekseer,
	NeuralFx,
	NeuralStyleDepth,
	SplatLive2d,
	UserMaterial,
	MaterialLayers,
	Light2d,
	MorphTargets,
	HdrEnvironment,
	No3d,
	Count
};

inline constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::Count);

/// ゲームが宣言した機能 (ADR 0090 の 3.2)。declaredMode が false の旧体制では全部を従来どおりの時期に作る。
/// declared は implies の先まで含む (RenderFeatures.hpp の withImplied)。DLL の境界には出ない host の内部の型
struct RenderNeeds
{
	bool declaredMode = false;
	std::bitset<kFeatureCount> declared;

	[[nodiscard]] bool wants(Feature f) const noexcept
	{
		return !declaredMode || declared.test(static_cast<std::size_t>(f));
	}

	/// no_3d を宣言した。3D の描画の層をなにも作らず、3D の依頼は捨てる (2D だけのゲーム)
	[[nodiscard]] bool without3D() const noexcept
	{
		return declaredMode && declared.test(static_cast<std::size_t>(Feature::No3d));
	}
};

namespace detail
{

constexpr std::uint8_t kNoTier = 0xFF;

struct NoteSlots
{
	std::atomic<std::uint8_t> tier{kNoTier};
	std::atomic<bool> declaredMode{false};
	std::atomic<std::uint64_t> declaredBits{0};
	std::array<std::atomic<std::uint32_t>, kFeatureCount> counts{};
	std::array<std::atomic<bool>, kFeatureCount> reported{};
	std::array<std::atomic<bool>, kFeatureCount> used{};
};

inline NoteSlots& slots() noexcept
{
	static NoteSlots s;
	return s;
}

}  // namespace detail

/// @brief host が renderer を作った直後に、その renderer の段を伝える。nullopt の間は数えない
inline void setActiveTier(std::optional<Tier> tier) noexcept
{
	detail::slots().tier.store(tier ? static_cast<std::uint8_t>(*tier) : detail::kNoTier, std::memory_order_relaxed);
}

[[nodiscard]] inline std::optional<Tier> activeTier() noexcept
{
	const std::uint8_t t = detail::slots().tier.load(std::memory_order_relaxed);
	if (t >= kTierCount) { return std::nullopt; }
	return static_cast<Tier>(t);
}

static_assert(kFeatureCount <= 64, "宣言の bit は 64 bit の 1 語に詰める");

/// @brief host が renderer を作った直後と、hot reload で宣言が増えたときに、今の宣言を伝える。端末の知らせが「未宣言」と「非対応」を分けるのに読む
inline void setActiveNeeds(const RenderNeeds& needs) noexcept
{
	auto& s = detail::slots();
	s.declaredBits.store(needs.declared.to_ullong(), std::memory_order_relaxed);
	s.declaredMode.store(needs.declaredMode, std::memory_order_relaxed);
}

[[nodiscard]] inline RenderNeeds activeNeeds() noexcept
{
	auto& s = detail::slots();
	RenderNeeds out;
	out.declaredMode = s.declaredMode.load(std::memory_order_relaxed);
	out.declared = std::bitset<kFeatureCount>(s.declaredBits.load(std::memory_order_relaxed));
	return out;
}

/// @brief この backend が描けない依頼を 1 回受けたと数える。毎フレームの呼び出しから呼んでも軽い (relaxed の加算 1 回)
inline void note(Feature feature) noexcept
{
	auto& s = detail::slots();
	if (s.tier.load(std::memory_order_relaxed) == detail::kNoTier) { return; }
	s.counts[static_cast<std::size_t>(feature)].fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint32_t noteCount(Feature feature) noexcept
{
	return detail::slots().counts[static_cast<std::size_t>(feature)].load(std::memory_order_relaxed);
}

/// @brief 数え始めた行のうち、まだ知らせていない行を返し、知らせた印を付ける
[[nodiscard]] inline std::array<bool, kFeatureCount> takeUnreportedNotes() noexcept
{
	auto& s = detail::slots();
	std::array<bool, kFeatureCount> out{};
	for (std::size_t i = 0; i < kFeatureCount; ++i)
	{
		if (s.counts[i].load(std::memory_order_relaxed) > 0 && !s.reported[i].exchange(true, std::memory_order_relaxed))
		{
			out[i] = true;
		}
	}
	return out;
}

/// @brief ゲームがその行の機能を頼んだと印を付ける (census)。backend が描けるか、宣言したかには関わらず、頼みが入口に届いたら付ける。
///        宣言の突き合わせ (ゲームが使う行は needs に入っているか) に使い、描画にも ABI にも触れない
inline void use(Feature feature) noexcept
{
	detail::slots().used[static_cast<std::size_t>(feature)].store(true, std::memory_order_relaxed);
}

[[nodiscard]] inline std::array<bool, kFeatureCount> usedFeatures() noexcept
{
	auto& s = detail::slots();
	std::array<bool, kFeatureCount> out{};
	for (std::size_t i = 0; i < kFeatureCount; ++i) { out[i] = s.used[i].load(std::memory_order_relaxed); }
	return out;
}

/// @brief 数と知らせた印を消す。試験が場面ごとに使う
inline void resetNotes() noexcept
{
	auto& s = detail::slots();
	for (std::size_t i = 0; i < kFeatureCount; ++i)
	{
		s.counts[i].store(0, std::memory_order_relaxed);
		s.reported[i].store(false, std::memory_order_relaxed);
		s.used[i].store(false, std::memory_order_relaxed);
	}
}

}  // namespace mitiru::render::features
