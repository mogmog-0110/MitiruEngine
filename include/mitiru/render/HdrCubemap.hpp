#pragma once

/// @file HdrCubemap.hpp
/// @brief 線形の浮動小数 RGB の 6 面 cubemap と、HDR の環境光 (拡散と鏡面) の畳み込み
/// @details 8 bit の Cubemap は 1 を超える明るさを持てないので、HDR の環境はこちらで持つ。ホストの中だけの型で、
///          ゲームの DLL とは渡さない (境界を渡るのは Cubemap のまま)。面の番号と向きは Cubemap と同じ約束を使う。
///          畳み込みの結果の正規化は Cubemap::irradiance / prefilterSpecular と同じ (重みの総和で割る)ので、
///          一様な環境 L を渡すとどの粗さでも L になる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>

namespace mitiru::render
{

namespace hdr_detail
{

inline constexpr float kPi = 3.14159265358979f;
/// half で表せる最大。これを超える値 (太陽の円など) は無限大にせず頭打ちにする
inline constexpr float kHalfMax = 65504.0f;

[[nodiscard]] inline float radicalInverse(std::uint32_t bits) noexcept
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// 面を 1 つずつ別のスレッドで計算する。面どうしは書く先が別なので、ロックは要らない
template <typename Fn>
inline void forEachFaceParallel(Fn&& fn)
{
	std::array<std::thread, kCubemapFaceCount> workers;
	for (int f = 0; f < kCubemapFaceCount; ++f) { workers[static_cast<std::size_t>(f)] = std::thread(fn, f); }
	for (auto& w : workers) { w.join(); }
}

}  // namespace hdr_detail

/// @brief 線形 RGB の浮動小数 cubemap。6 面は同じ大きさの正方
class HdrCubemap
{
public:
	HdrCubemap() = default;

	/// @brief 各テクセルの中心の向きから色 (線形 RGB、1 を超えてよい) を決めて作る
	template <typename Fn>
	[[nodiscard]] static HdrCubemap fromDirections(int size, Fn&& colorOf)
	{
		HdrCubemap cm;
		if (size <= 0) { return cm; }
		cm.m_size = size;
		for (int f = 0; f < kCubemapFaceCount; ++f)
		{
			auto& px = cm.m_faces[static_cast<std::size_t>(f)];
			px.resize(static_cast<std::size_t>(size) * size * 3u);
			for (int y = 0; y < size; ++y)
			{
				for (int x = 0; x < size; ++x)
				{
					const sgc::Vec3f c = colorOf(texelDirection(f, x, y, size));
					float* p = px.data() + (static_cast<std::size_t>(y) * size + x) * 3u;
					p[0] = c.x; p[1] = c.y; p[2] = c.z;
				}
			}
		}
		return cm;
	}

	[[nodiscard]] bool valid() const noexcept { return m_size > 0; }
	[[nodiscard]] int faceSize() const noexcept { return m_size; }

	[[nodiscard]] sgc::Vec3f texel(int face, int x, int y) const noexcept
	{
		const float* p = m_faces[static_cast<std::size_t>(face)].data() + (static_cast<std::size_t>(y) * m_size + x) * 3u;
		return {p[0], p[1], p[2]};
	}

	/// @brief dir を含むテクセルの色 (最近傍)。dir は正規化されていなくてよい
	[[nodiscard]] sgc::Vec3f sampleNearest(const sgc::Vec3f& dir) const noexcept
	{
		CubeFace face = CubeFace::PosX;
		float u = 0.0f, v = 0.0f;
		Cubemap::faceUvOfDirection(dir, face, u, v);
		const int x = std::clamp(static_cast<int>(u * static_cast<float>(m_size)), 0, m_size - 1);
		const int y = std::clamp(static_cast<int>(v * static_cast<float>(m_size)), 0, m_size - 1);
		return texel(static_cast<int>(face), x, y);
	}

	/// @brief 2 x 2 の平均で半分にする。大きさ 1 以下はそのまま返す
	[[nodiscard]] HdrCubemap halved() const
	{
		if (m_size <= 1) { return *this; }
		HdrCubemap out;
		out.m_size = m_size / 2;
		for (int f = 0; f < kCubemapFaceCount; ++f)
		{
			auto& px = out.m_faces[static_cast<std::size_t>(f)];
			px.resize(static_cast<std::size_t>(out.m_size) * out.m_size * 3u);
			for (int y = 0; y < out.m_size; ++y)
			{
				for (int x = 0; x < out.m_size; ++x)
				{
					const sgc::Vec3f sum = texel(f, 2 * x, 2 * y) + texel(f, 2 * x + 1, 2 * y) +
					                       texel(f, 2 * x, 2 * y + 1) + texel(f, 2 * x + 1, 2 * y + 1);
					float* p = px.data() + (static_cast<std::size_t>(y) * out.m_size + x) * 3u;
					p[0] = sum.x * 0.25f; p[1] = sum.y * 0.25f; p[2] = sum.z * 0.25f;
				}
			}
		}
		return out;
	}

	/// @brief 面 1 枚を RGBA16F (half、A は 1) の bytes にする。half の上限を超える値は頭打ちにする
	[[nodiscard]] std::vector<std::uint8_t> faceAsHalfRgba(int face) const
	{
		const auto& src = m_faces[static_cast<std::size_t>(face)];
		const std::size_t texels = static_cast<std::size_t>(m_size) * m_size;
		std::vector<std::uint8_t> out(texels * 8u);
		const auto put = [&](std::size_t i, float v) {
			const float clamped = v > 0.0f ? std::min(v, hdr_detail::kHalfMax) : 0.0f;   // 負と NaN は 0
			const std::uint16_t h = gi::floatToHalf(clamped);
			out[i * 2] = static_cast<std::uint8_t>(h & 0xFFu);
			out[i * 2 + 1] = static_cast<std::uint8_t>(h >> 8);
		};
		for (std::size_t t = 0; t < texels; ++t)
		{
			put(t * 4 + 0, src[t * 3 + 0]);
			put(t * 4 + 1, src[t * 3 + 1]);
			put(t * 4 + 2, src[t * 3 + 2]);
			put(t * 4 + 3, 1.0f);
		}
		return out;
	}

	/// @brief 自分から半分ずつ小さくした連鎖 (先頭は自分、末尾は一辺 minSize 以上の最小)
	[[nodiscard]] std::vector<HdrCubemap> mipChain(int minSize = 4) const
	{
		std::vector<HdrCubemap> chain;
		if (!valid()) { return chain; }
		chain.push_back(*this);
		while (chain.back().m_size / 2 >= std::max(minSize, 1)) { chain.push_back(chain.back().halved()); }
		return chain;
	}

	/// @brief 拡散の irradiance。法線 n ごとに、半球を cosine で重みづけた平均 (総和で割る)
	/// @details 小さく畳んだ元を全テクセル総当たりで積分する (立体角を掛ける)。標本の揺れが無いので、
	///          太陽のような強い光があってもまだらにならない。mips は元の連鎖で、一辺 16 以下の最大の段を使う
	[[nodiscard]] static HdrCubemap irradiance(const std::vector<HdrCubemap>& mips, int outSize)
	{
		if (outSize <= 0 || mips.empty()) { return {}; }
		const HdrCubemap& src = sourceNear(mips, 16);
		struct Texel { sgc::Vec3f dir; sgc::Vec3f color; float solidAngle; };
		std::vector<Texel> texels;
		texels.reserve(static_cast<std::size_t>(src.m_size) * src.m_size * kCubemapFaceCount);
		for (int f = 0; f < kCubemapFaceCount; ++f)
		{
			for (int y = 0; y < src.m_size; ++y)
			{
				for (int x = 0; x < src.m_size; ++x)
				{
					const float uc = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(src.m_size) - 1.0f;
					const float vc = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(src.m_size) - 1.0f;
					const float cell = 2.0f / static_cast<float>(src.m_size);
					const float r2 = 1.0f + uc * uc + vc * vc;
					texels.push_back({texelDirection(f, x, y, src.m_size), src.texel(f, x, y),
					                  cell * cell / (r2 * std::sqrt(r2))});
				}
			}
		}
		HdrCubemap out;
		out.allocate(outSize);
		hdr_detail::forEachFaceParallel([&](int f) {
			for (int y = 0; y < outSize; ++y)
			{
				for (int x = 0; x < outSize; ++x)
				{
					const sgc::Vec3f n = texelDirection(f, x, y, outSize);
					sgc::Vec3f sum{};
					float weight = 0.0f;
					for (const Texel& t : texels)
					{
						const float c = n.dot(t.dir);
						if (c <= 0.0f) { continue; }
						sum += t.color * (c * t.solidAngle);
						weight += c * t.solidAngle;
					}
					out.set(f, x, y, weight > 0.0f ? sum / weight : sum);
				}
			}
		});
		return out;
	}

	/// @brief 鏡面の prefilter。粗さ roughness の GGX で重点的に標本を取り、pdf から元の mip を選ぶ
	/// @details V = R = N の split-sum 近似。mips は元の連鎖 (先頭が最大)。標本の立体角に合う mip から読むので、
	///          太陽のような小さく強い光でも点々にならない。粗さ 0 は元の連鎖のうち outSize に近い段をそのまま写す
	[[nodiscard]] static HdrCubemap prefilterSpecular(const std::vector<HdrCubemap>& mips, int outSize, float roughness,
	                                                   int sampleCount = 64)
	{
		if (outSize <= 0 || mips.empty() || sampleCount <= 0) { return {}; }
		roughness = std::clamp(roughness, 0.0f, 1.0f);
		HdrCubemap out;
		out.allocate(outSize);
		const int lastLevel = static_cast<int>(mips.size()) - 1;
		if (roughness < 1e-3f)
		{
			const int level = std::clamp(static_cast<int>(std::lround(std::log2(
				static_cast<float>(mips[0].m_size) / static_cast<float>(outSize)))), 0, lastLevel);
			hdr_detail::forEachFaceParallel([&](int f) {
				for (int y = 0; y < outSize; ++y)
				{
					for (int x = 0; x < outSize; ++x) { out.set(f, x, y, mips[static_cast<std::size_t>(level)].sampleNearest(texelDirection(f, x, y, outSize))); }
				}
			});
			return out;
		}
		const float a = roughness * roughness;
		const float a2 = a * a;
		const float texelSolidAngle = 4.0f * hdr_detail::kPi / (6.0f * static_cast<float>(mips[0].m_size) * static_cast<float>(mips[0].m_size));
		hdr_detail::forEachFaceParallel([&](int f) {
			for (int y = 0; y < outSize; ++y)
			{
				for (int x = 0; x < outSize; ++x)
				{
					out.set(f, x, y, filterTexel(mips, texelDirection(f, x, y, outSize), a, a2, texelSolidAngle, sampleCount, lastLevel));
				}
			}
		});
		return out;
	}

	/// @brief 面 face の x, y テクセルの中心の向き (正規化済み)。Cubemap と同じ面の約束
	[[nodiscard]] static sgc::Vec3f texelDirection(int face, int x, int y, int size) noexcept
	{
		const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
		const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		return Cubemap::directionOfFaceUv(static_cast<CubeFace>(face), u, v).normalized();
	}

private:
	int m_size = 0;
	std::array<std::vector<float>, kCubemapFaceCount> m_faces;

	void allocate(int size)
	{
		m_size = size;
		for (auto& f : m_faces) { f.assign(static_cast<std::size_t>(size) * size * 3u, 0.0f); }
	}

	void set(int face, int x, int y, const sgc::Vec3f& c) noexcept
	{
		float* p = m_faces[static_cast<std::size_t>(face)].data() + (static_cast<std::size_t>(y) * m_size + x) * 3u;
		p[0] = c.x; p[1] = c.y; p[2] = c.z;
	}

	/// 一辺が target 以下で最大の段 (無ければ最小の段)
	[[nodiscard]] static const HdrCubemap& sourceNear(const std::vector<HdrCubemap>& mips, int target) noexcept
	{
		for (const auto& m : mips) { if (m.m_size <= target) { return m; } }
		return mips.back();
	}

	[[nodiscard]] static sgc::Vec3f filterTexel(const std::vector<HdrCubemap>& mips, const sgc::Vec3f& n, float a, float a2,
	                                            float texelSolidAngle, int sampleCount, int lastLevel) noexcept
	{
		constexpr float kTwoPi = 6.28318530717958f;
		const sgc::Vec3f up = std::fabs(n.y) < 0.999f ? sgc::Vec3f::up() : sgc::Vec3f::right();
		const sgc::Vec3f tx = up.cross(n).normalized();
		const sgc::Vec3f ty = n.cross(tx);
		sgc::Vec3f sum{};
		float total = 0.0f;
		for (int i = 0; i < sampleCount; ++i)
		{
			const float xi1 = static_cast<float>(i) / static_cast<float>(sampleCount);
			const float xi2 = hdr_detail::radicalInverse(static_cast<std::uint32_t>(i));
			const float phi = kTwoPi * xi1;
			const float cosT = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
			const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
			const sgc::Vec3f h = tx * (sinT * std::cos(phi)) + ty * (sinT * std::sin(phi)) + n * cosT;
			const sgc::Vec3f l = (h * (2.0f * n.dot(h)) - n).normalized();
			const float nl = n.dot(l);
			if (nl <= 0.0f) { continue; }
			// V = N なので VdotH = NdotH で、pdf = D / 4
			const float d = cosT * cosT * (a2 - 1.0f) + 1.0f;
			const float pdf = a2 / (hdr_detail::kPi * d * d) * 0.25f;
			const float sampleSolidAngle = 1.0f / (static_cast<float>(sampleCount) * std::max(pdf, 1e-6f));
			const float lod = std::clamp(0.5f * std::log2(sampleSolidAngle / texelSolidAngle) + 1.0f, 0.0f, static_cast<float>(lastLevel));
			const int lo = static_cast<int>(lod);
			const int hi = std::min(lo + 1, lastLevel);
			const float t = lod - static_cast<float>(lo);
			const sgc::Vec3f c = mips[static_cast<std::size_t>(lo)].sampleNearest(l) * (1.0f - t) +
			                     mips[static_cast<std::size_t>(hi)].sampleNearest(l) * t;
			sum += c * nl;
			total += nl;
		}
		return total > 0.0f ? sum / total : mips[0].sampleNearest(n);
	}
};

}  // namespace mitiru::render
