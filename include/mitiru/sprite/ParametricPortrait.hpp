#pragma once

/// @file ParametricPortrait.hpp
/// @brief 量子化した実数値入力で選択する sprite variant の N 次元 grid (G-01)
///
/// **動機。** 育成シム系の character は連続値の stat (motivation・weight・jump)
/// で見た目が駆動される。素朴に書くと各 stat を
/// {-1, 0, +1} に map し、`1_0_-1.PNG` のような filename を組み立てる
/// コードになり、どの game もこの pattern を再発明する。
/// `ParametricPortrait` が量子化・path-template の置換・per-path の texture
/// cache を所有するので、game は `select(values)` を呼ぶだけでよい。
///
/// **設計判断:**
/// - Header-only。I/O 依存無し。caller が `LoaderFn` callback を供給する。
/// - cache に `mutable std::mutex` を持つので `select()` はどの thread からでも安全。
/// - Override flag は挿入順 priority (先に追加されたものが先に match)。
/// - 空 / 失敗した load は stderr に報告し空の `Texture` を返す。game は asset
///   欠落で crash してはならない。
///
/// **使い方 (3 軸 portrait):**
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
/// **量子化ルール:**
///   - value < axis.lowThreshold  → "-1"
///   - value > axis.highThreshold → "1"
///   - それ以外                   → "0"
///   - threshold と等しい場合は中央 (0) として扱う

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

/// @brief 量子化した実数値入力に基づき、variant の N 次元 grid から
///        texture を 1 つ選ぶ。
/// @tparam N_AXES 独立した量子化軸の数 (例: 上の portrait 例なら 3)。
template <std::size_t N_AXES>
class ParametricPortrait
{
public:
	/// @brief Loader callback の型。
	/// @details texture が初めて必要になった時、解決済みの絶対 path で呼ばれる。
	///          失敗を伝えるには空 (default 構築) の `Texture` を返す。portrait は
	///          stderr に log を出し空の ref を返す。
	using LoaderFn = std::function<mitiru::render::Texture(const std::filesystem::path&)>;

	/// @brief 1 つの量子化軸。
	struct Axis
	{
		float       lowThreshold;   ///< value < low  → "-1"
		float       highThreshold;  ///< value > high → "1"
		std::string name;           ///< pathTemplate で使う placeholder 名。例 "motivation"
	};

	/// @brief portrait grid を構築する。
	/// @param axes           N_AXES 個の量子化軸の配列。template-placeholder 順。
	/// @param pathTemplate   baseDir からの相対 path。placeholder `{axisName}` が
	///                       "-1"・"0"・"1" に置換される。
	///                       例: `"raising/{motivation}_{weight}_{jump}.PNG"`
	/// @param baseDir        load 前に解決済み path 全てに前置される root directory。
	/// @param loader         texture 読み込みのため (unique path ごとに 1 回) 呼ばれる callback。
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

	// ── override flag ────────────────────────────────────────────────

	/// @brief override を登録する: `flag` が有効な時、`select()` は通常計算される
	///        portrait の代わりに `overridePath` (baseDir からの相対) の texture
	///        を返す。
	/// @details 複数の override は挿入順に検査され、最初に有効な flag が勝つ。
	void addOverride(std::string_view flag, std::string_view overridePath)
	{
		std::lock_guard lock(m_mutex);
		// 重複を避ける — flag が既に登録済みなら path を更新する。
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

	/// @brief override flag を有効 / 無効にする。
	/// @details `addOverride` で登録されていない flag なら no-op。
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

	// ── 中核の選択 ────────────────────────────────────────────────

	/// @brief `values` を量子化し path を解決、cache-hit か load して ref を返す。
	/// @param values  軸ごとに 1 つの float。`axes` 配列と同じ順序。
	/// @return cache 済み texture への const ref。ref はこの ParametricPortrait の
	///         生存期間にわたり安定。loader が失敗すると空の Texture を返す
	///         (かつ stderr に log)。
	const mitiru::render::Texture& select(std::array<float, N_AXES> values) const
	{
		std::lock_guard lock(m_mutex);

		// まず override flag を検査する (挿入順 = priority 順)。
		for (const auto& entry : m_overrides)
		{
			if (entry.active)
			{
				return loadCached(entry.path);
			}
		}

		// 量子化した軸の値から path を組み立てる。
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

	// ── helper (全て lock 下で呼ばれる) ──────────────────────────────

	/// @brief 単一の軸の値を量子化する。
	static std::string quantize(float value, const Axis& axis)
	{
		if (value < axis.lowThreshold)  return "-1";
		if (value > axis.highThreshold) return "1";
		return "0";
	}

	/// @brief 全ての軸 placeholder を置換して相対 path 文字列を組み立てる。
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

	/// @brief `str` 中の `placeholder` の全出現を `value` に置換する。
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

	/// @brief cache 検索 + load。m_cache 内への ref (安定 address) を返す。
	/// @details m_mutex 下で呼ぶこと。
	const mitiru::render::Texture& loadCached(const std::string& relativePath) const
	{
		auto it = m_cache.find(relativePath);
		if (it != m_cache.end())
		{
			return it->second;
		}

		// 返す address を安定させるため、無条件に load して insert する。
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

	// ── データ ─────────────────────────────────────────────────────────

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
