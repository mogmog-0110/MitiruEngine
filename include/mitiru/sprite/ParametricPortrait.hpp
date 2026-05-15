#pragma once

/// @file ParametricPortrait.hpp
/// @brief N-dimensional grid of sprite variants selected by quantized real-valued inputs (G-01)
///
/// **Motivation.** Characters in pandd-dodo have appearance driven by continuous
/// stats (motivation, weight, jump). The original code in
/// `CharacterAttributes::getMotivationAppearance()` mapped each stat → {-1, 0, +1}
/// and assembled a filename like `1_0_-1.PNG`. Every game re-invents this pattern.
/// `ParametricPortrait` owns the quantization, path-template substitution, and
/// per-path texture caching so the game only needs to call `select(values)`.
///
/// **Design decisions:**
/// - Header-only; no I/O dependency. The caller supplies a `LoaderFn` callback.
/// - `mutable std::mutex` on the cache so `select()` is safe to call from any thread.
/// - Override flags use insertion-order priority (first-added first-matched).
/// - Empty/failed loads are reported to stderr and return an empty `Texture`; the
///   game must not crash on a missing asset.
///
/// **Usage (3-axis pandd-dodo portrait):**
/// ```cpp
///   using mitiru::sprite::ParametricPortrait;
///   using mitiru::render::Texture;
///
///   ParametricPortrait<3> portrait(
///       { ParametricPortrait<3>::Axis{-20.f, 20.f, "motivation"},
///         ParametricPortrait<3>::Axis{-10.f, 10.f, "weight"},
///         ParametricPortrait<3>::Axis{ -5.f,  5.f, "jump"} },
///       "raising/{motivation}_{weight}_{jump}.PNG",
///       baseDir,
///       [](const std::filesystem::path& p) { return stbLoadTexture(p); });
///
///   portrait.addOverride("gameover", "special/gameover.PNG");
///   portrait.setOverrideFlag("gameover", true);
///
///   const Texture& tex = portrait.select({motivationStat, weightStat, jumpStat});
/// ```
///
/// **Quantization rule:**
///   - value < axis.lowThreshold  → "-1"
///   - value > axis.highThreshold → "1"
///   - otherwise                  → "0"
///   - equal to a threshold is treated as middle (0)

#include <array>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdio>

#include <mitiru/render/Texture.hpp>

namespace mitiru::sprite
{

/// @brief Picks one texture from an N-dimensional grid of variants based on
///        quantized real-valued inputs.
/// @tparam N_AXES Number of independent quantization axes (e.g. 3 for pandd-dodo portraits).
template <std::size_t N_AXES>
class ParametricPortrait
{
public:
	/// @brief Loader callback type.
	/// @details Called with the resolved absolute path when a texture is first
	///          needed. Return an empty (default-constructed) `Texture` to signal
	///          failure; the portrait will log to stderr and return an empty ref.
	using LoaderFn = std::function<mitiru::render::Texture(const std::filesystem::path&)>;

	/// @brief One quantization axis.
	struct Axis
	{
		float       lowThreshold;   ///< value < low  → "-1"
		float       highThreshold;  ///< value > high → "1"
		std::string name;           ///< placeholder name used in pathTemplate, e.g. "motivation"
	};

	/// @brief Construct a portrait grid.
	/// @param axes           Array of N_AXES quantization axes, in template-placeholder order.
	/// @param pathTemplate   Path relative to baseDir; placeholders `{axisName}` are
	///                       replaced with "-1", "0", or "1".
	///                       Example: `"raising/{motivation}_{weight}_{jump}.PNG"`
	/// @param baseDir        Root directory prepended to every resolved path before loading.
	/// @param loader         Callback invoked (once per unique path) to load a texture.
	ParametricPortrait(
		std::array<Axis, N_AXES>  axes,
		std::string               pathTemplate,
		std::filesystem::path     baseDir,
		LoaderFn                  loader)
		: m_axes(std::move(axes))
		, m_pathTemplate(std::move(pathTemplate))
		, m_baseDir(std::move(baseDir))
		, m_loader(std::move(loader))
	{
	}

	ParametricPortrait(const ParametricPortrait&)            = delete;
	ParametricPortrait& operator=(const ParametricPortrait&) = delete;

	// ── override flags ────────────────────────────────────────────────

	/// @brief Register an override: when `flag` is active, `select()` returns
	///        the texture at `overridePath` (relative to baseDir) instead of the
	///        normally computed portrait.
	/// @details Multiple overrides are checked in insertion order; the first
	///          active flag wins.
	void addOverride(std::string_view flag, std::string_view overridePath)
	{
		std::lock_guard lock(m_mutex);
		// Avoid duplicates — update path if flag already registered.
		for (auto& entry : m_overrides)
		{
			if (entry.flag == flag)
			{
				entry.path = std::string(overridePath);
				return;
			}
		}
		m_overrides.push_back({std::string(flag), std::string(overridePath), false});
	}

	/// @brief Activate or deactivate an override flag.
	/// @details No-op if the flag was never registered via `addOverride`.
	void setOverrideFlag(std::string_view flag, bool on)
	{
		std::lock_guard lock(m_mutex);
		for (auto& entry : m_overrides)
		{
			if (entry.flag == flag)
			{
				entry.active = on;
				return;
			}
		}
	}

	// ── core selection ────────────────────────────────────────────────

	/// @brief Quantize `values`, resolve the path, cache-hit or load, return ref.
	/// @param values  One float per axis, in the same order as the `axes` array.
	/// @return Const ref to the cached texture. The ref is stable for the
	///         lifetime of this ParametricPortrait. An empty Texture is returned
	///         (and logged to stderr) if the loader fails.
	const mitiru::render::Texture& select(std::array<float, N_AXES> values) const
	{
		std::lock_guard lock(m_mutex);

		// Check override flags first (insertion order = priority order).
		for (const auto& entry : m_overrides)
		{
			if (entry.active)
			{
				return loadCached(entry.path);
			}
		}

		// Build path from quantized axis values.
		const std::string resolvedPath = buildPath(values);
		return loadCached(resolvedPath);
	}

private:
	// ── override entry ────────────────────────────────────────────────

	struct OverrideEntry
	{
		std::string flag;
		std::string path;
		bool        active = false;
	};

	// ── helpers (all called under lock) ──────────────────────────────

	/// @brief Quantize a single axis value.
	static std::string quantize(float value, const Axis& axis)
	{
		if (value < axis.lowThreshold)  return "-1";
		if (value > axis.highThreshold) return "1";
		return "0";
	}

	/// @brief Build the relative path string by substituting all axis placeholders.
	std::string buildPath(const std::array<float, N_AXES>& values) const
	{
		std::string result = m_pathTemplate;
		for (std::size_t i = 0; i < N_AXES; ++i)
		{
			const std::string placeholder = "{" + m_axes[i].name + "}";
			const std::string replacement = quantize(values[i], m_axes[i]);
			replacePlaceholder(result, placeholder, replacement);
		}
		return result;
	}

	/// @brief Replace every occurrence of `placeholder` in `str` with `value`.
	static void replacePlaceholder(std::string& str,
	                               const std::string& placeholder,
	                               const std::string& value)
	{
		std::size_t pos = 0;
		while ((pos = str.find(placeholder, pos)) != std::string::npos)
		{
			str.replace(pos, placeholder.size(), value);
			pos += value.size();
		}
	}

	/// @brief Cache-lookup + load. Returns ref into m_cache (stable address).
	/// @details Must be called under m_mutex.
	const mitiru::render::Texture& loadCached(const std::string& relativePath) const
	{
		auto it = m_cache.find(relativePath);
		if (it != m_cache.end())
		{
			return it->second;
		}

		// Load and insert unconditionally so we have a stable address to return.
		const std::filesystem::path fullPath = m_baseDir / relativePath;
		mitiru::render::Texture tex;
		if (m_loader)
		{
			tex = m_loader(fullPath);
		}

		if (!tex.valid())
		{
			std::fprintf(stderr,
				"[ParametricPortrait] loader returned empty texture for: %s\n",
				fullPath.string().c_str());
		}

		auto [inserted, _] = m_cache.emplace(relativePath, std::move(tex));
		return inserted->second;
	}

	// ── data ─────────────────────────────────────────────────────────

	std::array<Axis, N_AXES>                               m_axes;
	std::string                                            m_pathTemplate;
	std::filesystem::path                                  m_baseDir;
	LoaderFn                                               m_loader;

	std::vector<OverrideEntry>                             m_overrides;

	mutable std::mutex                                     m_mutex;
	mutable std::unordered_map<std::string,
	                           mitiru::render::Texture>    m_cache;
};

} // namespace mitiru::sprite
