#pragma once

/// @file SkinnedLod.hpp
/// @brief glTF のスキン prim の LOD。読み込みで簡略化の段を作り、描くときに画面の大きさで段を選ぶ。
/// @details 簡略化は meshoptimizer の辺の縮約で、残る頂点は元の頂点そのもの (位置・法線・UV・色・骨の重みを混ぜない)。
///          主に付く骨が違う頂点どうしが接する所の頂点は固定するので、骨の境目の形は崩れない。
///          段ごとに使う頂点だけを詰めて持つので、compute スキニングの量も段に合わせて減る。
///          段は描画だけのもので、ゲームの当たり判定 (AnimPose から出す骨の位置) とは関係しない。
///          作った段は `<モデル>.skinlod` に置き、次からは読む。モデルより古いか、作り方 (手順の版・数・半径) の鍵が違えば
///          作り直す。pack を mount 中は読むだけ。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <meshoptimizer.h>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/MaterialTextures.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief 段の数 (0 = 元のまま)
inline constexpr int kSkinnedLodLevels = 4;

/// @brief 元の prim から作った 1 段。vertices は元の頂点の添字で、indices はその並びへの添字
struct SkinnedLodLevel
{
	std::vector<std::uint32_t> vertices;
	std::vector<std::uint32_t> indices;
};

/// @brief 段を選ぶ境目。screen はキャラの外接球の直径が画面の高さに占める割合で、これを下回ると次の段へ移る
struct SkinnedLodThresholds
{
	float screen[kSkinnedLodLevels - 1] = {0.25f, 0.12f, 0.06f};
	float hysteresis = 0.1f;   ///< 境目の前後でこの割合だけ戻りにくくする (行き来でちらつかない)
};

/// @brief 段の作り方。ratio は元の三角形の数に対する割合 (形の誤差の上限に先に当たれば、そこで止まる)
/// @details 形の誤差の上限は、段 k を使い始める大きさ (screen[k]) のキャラを高さ referenceHeight 画素の画面に描いた時の
///          pixelError 画素をモデルの単位に直した値。prim 1 つの大きさに対する比で決めると、全身にまたがる prim
///          (肌の色の頭と両手など) では誤差が細い腕より大きくなり、腕や指の輪郭が崩れる
struct SkinnedLodBuild
{
	float ratio[kSkinnedLodLevels - 1] = {0.5f, 0.25f, 0.12f};
	float pixelError = 1.0f;
	float referenceHeight = 1080.0f;
	SkinnedLodThresholds thresholds{};
	float minReduction = 0.9f;   ///< 前の段の三角形の数のこの割合より減らなければ、そこで段を打ち切る

	/// @brief 段 level (1..3) の形の誤差の上限 (モデルの単位)。modelRadius はモデル全体の外接球の半径
	[[nodiscard]] constexpr float absoluteError(int level, float modelRadius) const noexcept
	{
		return pixelError * 2.0f * modelRadius / (thresholds.screen[level - 1] * referenceHeight);
	}
};

namespace detail
{

/// @brief 頂点が最も重く付いている骨。重みが無ければ ~0
[[nodiscard]] inline std::uint32_t dominantJoint(const SkinVertexBinding& b) noexcept
{
	std::uint32_t joint = ~0u;
	float best = 0.0f;
	for (int k = 0; k < 4; ++k)
	{
		if (b.weights[k] > best) { best = b.weights[k]; joint = b.joints[k]; }
	}
	return joint;
}

/// @brief 主な骨が違う頂点を含む三角形の頂点を固定する印
[[nodiscard]] inline std::vector<unsigned char> skinBoundaryLocks(std::span<const std::uint32_t> indices,
                                                                  std::span<const SkinVertexBinding> skin,
                                                                  std::size_t vertexCount)
{
	std::vector<unsigned char> lock(vertexCount, 0);
	if (skin.size() != vertexCount) { return lock; }
	for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
	{
		const std::uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
		if (a >= vertexCount || b >= vertexCount || c >= vertexCount) { continue; }
		const std::uint32_t ja = dominantJoint(skin[a]);
		if (ja == dominantJoint(skin[b]) && ja == dominantJoint(skin[c])) { continue; }
		lock[a] = lock[b] = lock[c] = 1;
	}
	return lock;
}

/// @brief 法線・UV・頂点の色 (Vertex3D の 12 バイト目から 9 個の float) を誤差に入れる重み
inline constexpr float kSkinLodAttributeWeights[9] = {0.25f, 0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

/// @brief 簡略化した添字の列から、使う頂点だけを詰めた段を作る (頂点の並びはキャッシュに合わせて並べ直す)
[[nodiscard]] inline SkinnedLodLevel compactLevel(std::vector<std::uint32_t> indices, std::size_t vertexCount)
{
	meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertexCount);
	std::vector<std::uint32_t> remap(vertexCount);
	const std::size_t used = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), indices.size(), vertexCount);
	SkinnedLodLevel level;
	level.vertices.resize(used);
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		if (remap[v] != ~0u) { level.vertices[remap[v]] = static_cast<std::uint32_t>(v); }
	}
	level.indices.resize(indices.size());
	meshopt_remapIndexBuffer(level.indices.data(), indices.data(), indices.size(), remap.data());
	return level;
}

} // namespace detail

/// @brief glTF の全メッシュの頂点 (バインド姿勢) を包む箱の対角の半分。スキンのモデルの段の誤差の上限に使う
[[nodiscard]] inline float skinnedSceneRadius(const GltfSceneData& scene) noexcept
{
	bool any = false;
	sgc::Vec3f lo{0, 0, 0}, hi{0, 0, 0};
	for (const auto& mesh : scene.meshes)
	{
		for (const auto& prim : mesh.primitives)
		{
			for (const auto& v : prim.vertices)
			{
				lo = any ? sgc::Vec3f{std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)} : v.position;
				hi = any ? sgc::Vec3f{std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)} : v.position;
				any = true;
			}
		}
	}
	return any ? (hi - lo).length() * 0.5f : 0.0f;
}

/// @brief 1 つの prim の段 1..3 を作る (段 0 は元のまま)。減らせない形なら少ない段数で返す
/// @param modelRadius モデル全体の外接球の半径 (モデルの単位)。0 以下なら prim 自身の頂点で測る
[[nodiscard]] inline std::vector<SkinnedLodLevel> buildSkinnedLods(std::span<const Vertex3D> vertices,
                                                                   std::span<const std::uint32_t> indices,
                                                                   std::span<const SkinVertexBinding> skin,
                                                                   float modelRadius, const SkinnedLodBuild& build = {})
{
	std::vector<SkinnedLodLevel> levels;
	if (vertices.empty() || indices.size() < 3 || indices.size() % 3 != 0) { return levels; }
	const float radius = (modelRadius > 0.0f)
		? modelRadius
		: 0.5f * meshopt_simplifyScale(&vertices[0].position.x, vertices.size(), sizeof(Vertex3D));
	const auto lock = detail::skinBoundaryLocks(indices, skin, vertices.size());
	std::vector<std::uint32_t> out(indices.size());
	std::size_t previous = indices.size();
	for (int k = 1; k < kSkinnedLodLevels; ++k)
	{
		const auto target = static_cast<std::size_t>(static_cast<float>(indices.size()) * build.ratio[k - 1]) / 3 * 3;
		const std::size_t n = meshopt_simplifyWithAttributes(
			out.data(), indices.data(), indices.size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex3D),
			&vertices[0].normal.x, sizeof(Vertex3D), detail::kSkinLodAttributeWeights, 9, lock.data(), target,
			build.absoluteError(k, radius), meshopt_SimplifyErrorAbsolute, nullptr);
		if (n < 3 || static_cast<float>(n) > static_cast<float>(previous) * build.minReduction) { break; }
		levels.push_back(detail::compactLevel(std::vector<std::uint32_t>(out.begin(), out.begin() + n), vertices.size()));
		previous = n;
	}
	return levels;
}

/// @brief 段を選ぶ。previous は前のフレームの段 (無ければ -1)、levelCount は段 0 を含む数
[[nodiscard]] constexpr int selectSkinnedLod(float screenFraction, int previous, int levelCount,
                                             const SkinnedLodThresholds& t = {}) noexcept
{
	int level = 0;
	for (int k = 1; k < levelCount && k < kSkinnedLodLevels; ++k)
	{
		const float edge = t.screen[k - 1] * ((previous >= k) ? (1.0f + t.hysteresis) : (1.0f - t.hysteresis));
		if (screenFraction < edge) { level = k; }
	}
	return level;
}

/// @brief 段 level の姿勢を何フレームに 1 回作り直すか (遠いキャラの姿勢の評価を間引く。描画だけ)
[[nodiscard]] constexpr int skinnedPoseInterval(int level) noexcept
{
	return level >= 3 ? 4 : (level == 2 ? 2 : 1);
}

/// @brief 1 つのモデルの全 prim の段。prims の並びは描画の側がスキン prim を読む順
struct SkinnedLodFile
{
	std::uint32_t buildKey = 0;   ///< 段を作った手順と数 (skinnedLodBuildKey)。違えば読まずに作り直す
	struct Prim
	{
		std::uint32_t vertexCount = 0;   ///< 元の頂点数 (違えば作り直す)
		std::uint32_t indexCount = 0;
		std::vector<SkinnedLodLevel> levels;
	};
	std::vector<Prim> prims;
};

namespace detail
{

inline constexpr std::uint32_t kSkinLodMagic = 0x444C534Du;   // "MSLD"
inline constexpr std::uint32_t kSkinLodVersion = 2;
/// 簡略化の手順を変えたら上げる (数の上では同じでも、前の手順で作った sidecar を使わせない)
inline constexpr std::uint32_t kSkinLodAlgorithm = 2;

inline void hashBytes(std::uint32_t& h, const void* data, std::size_t n) noexcept
{
	const auto* p = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < n; ++i) { h = (h ^ p[i]) * 16777619u; }
}

inline void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
	const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
	out.insert(out.end(), p, p + 4);
}

inline void putU32s(std::vector<std::uint8_t>& out, const std::vector<std::uint32_t>& v)
{
	putU32(out, static_cast<std::uint32_t>(v.size()));
	const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
	out.insert(out.end(), p, p + v.size() * 4);
}

/// @brief 読み手。足りなければ ok が false になり、以後は 0 を返す
struct ByteReader
{
	std::span<const std::uint8_t> bytes;
	std::size_t pos = 0;
	bool ok = true;

	std::uint32_t u32()
	{
		if (!ok || pos + 4 > bytes.size()) { ok = false; return 0; }
		std::uint32_t v = 0;
		std::memcpy(&v, bytes.data() + pos, 4);
		pos += 4;
		return v;
	}

	std::vector<std::uint32_t> u32s()
	{
		const std::uint32_t n = u32();
		if (!ok || pos + std::size_t{n} * 4 > bytes.size()) { ok = false; return {}; }
		std::vector<std::uint32_t> v(n);
		std::memcpy(v.data(), bytes.data() + pos, std::size_t{n} * 4);
		pos += std::size_t{n} * 4;
		return v;
	}
};

} // namespace detail

/// @brief 段を作る手順・作り方の数・属性の重み・モデルの半径から決まる鍵 (sidecar がどの作り方の結果かを見分ける)
[[nodiscard]] inline std::uint32_t skinnedLodBuildKey(const SkinnedLodBuild& build, float modelRadius) noexcept
{
	std::uint32_t h = 2166136261u;
	detail::hashBytes(h, &detail::kSkinLodAlgorithm, sizeof(detail::kSkinLodAlgorithm));
	detail::hashBytes(h, build.ratio, sizeof(build.ratio));
	detail::hashBytes(h, &build.pixelError, sizeof(float));
	detail::hashBytes(h, &build.referenceHeight, sizeof(float));
	detail::hashBytes(h, build.thresholds.screen, sizeof(build.thresholds.screen));
	detail::hashBytes(h, &build.minReduction, sizeof(float));
	detail::hashBytes(h, detail::kSkinLodAttributeWeights, sizeof(detail::kSkinLodAttributeWeights));
	detail::hashBytes(h, &modelRadius, sizeof(float));
	return h;
}

[[nodiscard]] inline std::vector<std::uint8_t> encodeSkinnedLodFile(const SkinnedLodFile& file)
{
	std::vector<std::uint8_t> out;
	detail::putU32(out, detail::kSkinLodMagic);
	detail::putU32(out, detail::kSkinLodVersion);
	detail::putU32(out, file.buildKey);
	detail::putU32(out, static_cast<std::uint32_t>(file.prims.size()));
	for (const auto& p : file.prims)
	{
		detail::putU32(out, p.vertexCount);
		detail::putU32(out, p.indexCount);
		detail::putU32(out, static_cast<std::uint32_t>(p.levels.size()));
		for (const auto& l : p.levels)
		{
			detail::putU32s(out, l.vertices);
			detail::putU32s(out, l.indices);
		}
	}
	return out;
}

/// @brief 読めなければ空 (prims が空) を返す。添字が範囲の外を指す段も捨てる
[[nodiscard]] inline SkinnedLodFile decodeSkinnedLodFile(std::span<const std::uint8_t> bytes)
{
	detail::ByteReader r{bytes};
	SkinnedLodFile file;
	if (r.u32() != detail::kSkinLodMagic || r.u32() != detail::kSkinLodVersion) { return {}; }
	file.buildKey = r.u32();
	const std::uint32_t primCount = r.u32();
	for (std::uint32_t i = 0; r.ok && i < primCount; ++i)
	{
		SkinnedLodFile::Prim p;
		p.vertexCount = r.u32();
		p.indexCount = r.u32();
		const std::uint32_t levels = r.u32();
		for (std::uint32_t k = 0; r.ok && k < levels && k < kSkinnedLodLevels - 1; ++k)
		{
			SkinnedLodLevel l{r.u32s(), r.u32s()};
			const bool inRange = std::all_of(l.vertices.begin(), l.vertices.end(),
			                                 [&](std::uint32_t v) { return v < p.vertexCount; }) &&
			                     std::all_of(l.indices.begin(), l.indices.end(),
			                                 [&](std::uint32_t v) { return v < l.vertices.size(); });
			if (!inRange) { return {}; }
			p.levels.push_back(std::move(l));
		}
		file.prims.push_back(std::move(p));
	}
	return r.ok ? file : SkinnedLodFile{};
}

/// @brief モデル 1 つぶんの段を、sidecar から読むか作るかして渡す。作った分は save で書く
class SkinnedLodSidecar
{
public:
	/// @param modelRadius モデル全体の外接球の半径 (モデルの単位)。段の形の誤差の上限を決める
	SkinnedLodSidecar(const std::string& modelPath, float modelRadius, const SkinnedLodBuild& build = {})
		: m_radius(modelRadius), m_build(build), m_key(skinnedLodBuildKey(build, modelRadius))
	{
		m_file.buildKey = m_key;
		m_logical = modelPath + ".skinlod";
		if (!vfs::hasGlobalMount())
		{
			m_modelDisk = detail::diskPathOfLogical(modelPath);
			m_disk = m_modelDisk.string() + ".skinlod";
		}
		load();
	}

	/// @brief i 番目のスキン prim の段。sidecar の中身が元の prim と合わなければ作り直す
	[[nodiscard]] const std::vector<SkinnedLodLevel>& levelsFor(std::size_t i, std::span<const Vertex3D> vertices,
	                                                            std::span<const std::uint32_t> indices,
	                                                            std::span<const SkinVertexBinding> skin)
	{
		if (m_file.prims.size() <= i) { m_file.prims.resize(i + 1); }
		auto& p = m_file.prims[i];
		const bool fresh = p.vertexCount == vertices.size() && p.indexCount == indices.size() && p.vertexCount > 0;
		if (!fresh)
		{
			p.vertexCount = static_cast<std::uint32_t>(vertices.size());
			p.indexCount = static_cast<std::uint32_t>(indices.size());
			p.levels = buildSkinnedLods(vertices, indices, skin, m_radius, m_build);
			m_dirty = true;
		}
		return p.levels;
	}

	/// @brief 作った段を書く (書けない場所なら何もしない。pack を mount 中は書かない)
	void save() const
	{
		if (!m_dirty || m_disk.empty()) { return; }
		const std::filesystem::path tmp(m_disk.string() + ".tmp");
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f) { return; }
			const auto bytes = encodeSkinnedLodFile(m_file);
			f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!f) { return; }
		}
		std::error_code ec;
		std::filesystem::rename(tmp, m_disk, ec);
		if (ec) { std::filesystem::remove(tmp, ec); }
	}

	[[nodiscard]] bool loadedFromSidecar() const noexcept { return m_loaded; }

private:
	void load()
	{
		std::optional<std::vector<std::uint8_t>> bytes;
		if (vfs::hasGlobalMount()) { bytes = vfs::readGlobal(m_logical); }
		else
		{
			std::error_code ec;
			if (m_disk.empty() || !std::filesystem::exists(m_disk, ec) || detail::isOlderThan(m_disk, m_modelDisk)) { return; }
			bytes = vfs::detail::readDiskFile(m_disk);
		}
		if (!bytes) { return; }
		auto file = decodeSkinnedLodFile(*bytes);
		if (file.buildKey != m_key || file.prims.empty()) { return; }
		m_file = std::move(file);
		m_loaded = true;
	}

	float m_radius = 0.0f;
	SkinnedLodBuild m_build;
	std::uint32_t m_key = 0;
	std::string m_logical;
	std::filesystem::path m_disk;
	std::filesystem::path m_modelDisk;
	SkinnedLodFile m_file;
	bool m_dirty = false;
	bool m_loaded = false;
};

} // namespace mitiru::render
