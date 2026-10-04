#pragma once

/// @file LightingBakeFile.hpp
/// @brief 焼いた光 (放射照度のプローブの格子と、箱で映す反射のプローブ) と、その 1 本のファイル (*.lighting.bin)
/// @details ファイルはリトルエンディアンの並び。頭は "MLGB" と版 2。描く側は readLightingBake で読む
///          (vfs::readAsset を通すので、pack 配布でも同じパスで読める)。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/gi/ProbeVolume.hpp>

namespace mitiru::render::gi
{

/// 反射のプローブの mip の段数。段 i は粗さ i / (段数 - 1) で畳み込む (IBL の prefilter と同じ割り当て)
inline constexpr std::uint32_t kReflectionMips = 5;

/// @brief 箱で映す反射のプローブ 1 個。中身は RGBA の 16 bit 浮動小数で、面 0..5 ごとに mip 0..mips-1 を並べる
struct ReflectionProbeData
{
	Vec3 boxMin{};
	Vec3 boxMax{};
	Vec3 position{};       ///< 撮った位置
	float blend = 0.5f;    ///< 箱の外へこの距離で重みが 1 から 0 になる (箱の中は 1)
	std::uint32_t size = 0;
	std::uint32_t mips = 0;
	std::vector<std::uint16_t> texels;

	[[nodiscard]] static std::size_t texelCount(std::uint32_t size, std::uint32_t mips) noexcept
	{
		std::size_t n = 0;
		for (std::uint32_t m = 0; m < mips; ++m)
		{
			const std::size_t s = std::max<std::size_t>(1, size >> m);
			n += s * s;
		}
		return n * 6;
	}
};

struct LightingBake
{
	bool hasVolume = false;
	ProbeVolume volume;
	std::vector<ReflectionProbeData> reflections;
};

/// @brief float を 16 bit 浮動小数へ (最近接偶数へ丸める)
[[nodiscard]] inline std::uint16_t floatToHalf(float f) noexcept
{
	std::uint32_t x = 0;
	std::memcpy(&x, &f, sizeof(x));
	const std::uint32_t sign = (x >> 16) & 0x8000u;
	const std::uint32_t absx = x & 0x7FFFFFFFu;
	if (absx >= 0x7F800000u) { return static_cast<std::uint16_t>(sign | (absx > 0x7F800000u ? 0x7E00u : 0x7C00u)); }
	if (absx >= 0x477FF000u) { return static_cast<std::uint16_t>(sign | 0x7C00u); }
	if (absx < 0x38800000u)
	{
		// 非正規化数: 仮数を右へずらして丸める
		const std::uint32_t shift = 113u - (absx >> 23);
		if (shift > 24u) { return static_cast<std::uint16_t>(sign); }
		const std::uint32_t mant = (absx & 0x7FFFFFu) | 0x800000u;
		const std::uint32_t half = mant >> (shift + 13u);
		const std::uint32_t rem = mant & ((1u << (shift + 13u)) - 1u);
		const std::uint32_t mid = 1u << (shift + 12u);
		const std::uint32_t up = (rem > mid || (rem == mid && (half & 1u))) ? 1u : 0u;
		return static_cast<std::uint16_t>(sign | (half + up));
	}
	const std::uint32_t biased = absx - 0x38000000u;
	const std::uint32_t half = biased >> 13;
	const std::uint32_t rem = biased & 0x1FFFu;
	const std::uint32_t up = (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ? 1u : 0u;
	return static_cast<std::uint16_t>(sign | (half + up));
}

namespace detail
{

inline constexpr char kBakeMagic[4] = {'M', 'L', 'G', 'B'};
inline constexpr std::uint32_t kBakeVersion = 2;   ///< 2 = 距離の地図が 1 枚 16x16 (ADR 0074)

class ByteWriter
{
public:
	template <class T>
	void put(const T& v)
	{
		const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
		bytes.insert(bytes.end(), p, p + sizeof(T));
	}
	template <class T>
	void putArray(const T* data, std::size_t n)
	{
		const auto* p = reinterpret_cast<const std::uint8_t*>(data);
		bytes.insert(bytes.end(), p, p + sizeof(T) * n);
	}
	void putVec3(Vec3 v) { put(v.x); put(v.y); put(v.z); }
	std::vector<std::uint8_t> bytes;
};

class ByteReader
{
public:
	explicit ByteReader(const std::vector<std::uint8_t>& b) : m_b(b) {}
	template <class T>
	bool get(T& v)
	{
		return getArray(&v, 1);
	}
	template <class T>
	bool getArray(T* out, std::size_t n)
	{
		const std::size_t bytes = sizeof(T) * n;
		if (n > (m_b.size() - m_pos) / sizeof(T) || m_pos + bytes > m_b.size()) { return false; }
		std::memcpy(out, m_b.data() + m_pos, bytes);
		m_pos += bytes;
		return true;
	}
	bool getVec3(Vec3& v) { return get(v.x) && get(v.y) && get(v.z); }
	[[nodiscard]] bool atEnd() const noexcept { return m_pos == m_b.size(); }

private:
	const std::vector<std::uint8_t>& m_b;
	std::size_t m_pos = 0;
};

inline void writeVolume(ByteWriter& w, const ProbeVolume& v)
{
	w.putVec3(v.grid.origin);
	w.putVec3(v.grid.spacing);
	w.putArray(v.grid.dims, 3);
	w.put(v.tilesPerRow);
	for (const ShL2Rgb& sh : v.irradiance)
	{
		for (const Vec3& c : sh.c) { w.putVec3(c); }
	}
	w.putArray(v.valid.data(), v.valid.size());
	w.putArray(v.visibility.data(), v.visibility.size());
}

[[nodiscard]] inline bool readVolume(ByteReader& r, ProbeVolume& v)
{
	ProbeGridDesc g;
	std::uint32_t tiles = 0;
	if (!r.getVec3(g.origin) || !r.getVec3(g.spacing) || !r.getArray(g.dims, 3) || !r.get(tiles)) { return false; }
	const std::uint64_t count = static_cast<std::uint64_t>(g.dims[0]) * g.dims[1] * g.dims[2];
	if (count == 0 || count > (1u << 22) || !(g.spacing.x > 0.0f && g.spacing.y > 0.0f && g.spacing.z > 0.0f)) { return false; }
	v.allocate(g);
	if (tiles != v.tilesPerRow) { return false; }
	for (ShL2Rgb& sh : v.irradiance)
	{
		for (Vec3& c : sh.c)
		{
			if (!r.getVec3(c)) { return false; }
		}
	}
	return r.getArray(v.valid.data(), v.valid.size()) && r.getArray(v.visibility.data(), v.visibility.size());
}

inline void writeReflection(ByteWriter& w, const ReflectionProbeData& p)
{
	w.putVec3(p.boxMin);
	w.putVec3(p.boxMax);
	w.putVec3(p.position);
	w.put(p.blend);
	w.put(p.size);
	w.put(p.mips);
	w.putArray(p.texels.data(), p.texels.size());
}

[[nodiscard]] inline bool readReflection(ByteReader& r, ReflectionProbeData& p)
{
	if (!r.getVec3(p.boxMin) || !r.getVec3(p.boxMax) || !r.getVec3(p.position) || !r.get(p.blend) || !r.get(p.size) ||
	    !r.get(p.mips))
	{
		return false;
	}
	if (p.size == 0 || p.size > 1024 || p.mips == 0 || p.mips > 11 || (p.size >> (p.mips - 1)) == 0) { return false; }
	p.texels.resize(ReflectionProbeData::texelCount(p.size, p.mips) * 4);
	return r.getArray(p.texels.data(), p.texels.size());
}

} // namespace detail

[[nodiscard]] inline std::vector<std::uint8_t> encodeLightingBake(const LightingBake& bake)
{
	detail::ByteWriter w;
	w.putArray(detail::kBakeMagic, 4);
	w.put(detail::kBakeVersion);
	w.put(static_cast<std::uint32_t>(bake.hasVolume ? 1u : 0u));
	if (bake.hasVolume) { detail::writeVolume(w, bake.volume); }
	w.put(static_cast<std::uint32_t>(bake.reflections.size()));
	for (const ReflectionProbeData& p : bake.reflections) { detail::writeReflection(w, p); }
	return std::move(w.bytes);
}

/// @brief バイト列から読む。形が崩れていれば nullopt と理由
[[nodiscard]] inline std::optional<LightingBake> decodeLightingBake(const std::vector<std::uint8_t>& bytes, std::string& error)
{
	detail::ByteReader r(bytes);
	char magic[4] = {};
	std::uint32_t version = 0, flags = 0, reflections = 0;
	if (!r.getArray(magic, 4) || std::memcmp(magic, detail::kBakeMagic, 4) != 0 || !r.get(version))
	{
		error = "焼いた光のファイルではない (頭が MLGB でない)";
		return std::nullopt;
	}
	if (version != detail::kBakeVersion)
	{
		error = "焼いた光のファイルの版が違う: " + std::to_string(version);
		return std::nullopt;
	}
	LightingBake bake;
	if (!r.get(flags) || (flags & ~1u) != 0 || ((flags & 1u) != 0 && !detail::readVolume(r, bake.volume)) || !r.get(reflections) ||
	    reflections > 64)
	{
		error = "焼いた光のファイルが途中で切れているか、知らない印があるか、プローブの格子の大きさがおかしい";
		return std::nullopt;
	}
	bake.hasVolume = (flags & 1u) != 0;
	bake.reflections.resize(reflections);
	for (ReflectionProbeData& p : bake.reflections)
	{
		if (!detail::readReflection(r, p))
		{
			error = "反射のプローブの中身が途中で切れているか、大きさがおかしい";
			return std::nullopt;
		}
	}
	if (!r.atEnd())
	{
		error = "焼いた光のファイルの終わりに読まないバイトが残っている";
		return std::nullopt;
	}
	return bake;
}

[[nodiscard]] inline bool writeLightingBake(const std::filesystem::path& path, const LightingBake& bake, std::string& error)
{
	const auto bytes = encodeLightingBake(bake);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!out)
	{
		error = "書き出せない: " + path.string();
		return false;
	}
	return true;
}

/// @brief アセットのパスから読む (pack 配布でも同じパス)
[[nodiscard]] inline std::optional<LightingBake> readLightingBake(std::string_view path, std::string& error)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes)
	{
		error = "焼いた光のファイルを開けない: " + std::string(path);
		return std::nullopt;
	}
	return decodeLightingBake(*bytes, error);
}

} // namespace mitiru::render::gi
