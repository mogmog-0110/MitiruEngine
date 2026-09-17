#pragma once

/// @file Cubemap.hpp
/// @brief 6 面 RGBA8 キューブマップ値型
/// @details 6 つの正方 Texture を保持する CPU 専用データ型。
///          Skybox / 環境マップ / IBL irradiance などの素材として使う。
///          GPU リソース作成は Skybox 等の利用側で行う。
///
///          面インデックスは D3D11 / DX12 規約に合わせる:
///            0 = +X (right),  1 = -X (left)
///            2 = +Y (up),     3 = -Y (down)
///            4 = +Z (front),  5 = -Z (back)
///
///          6 面はすべて同じ正方サイズ（width == height、6 面で同一）でなければ
///          valid() は false を返す。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief キューブマップ面インデックス
enum class CubeFace : std::uint8_t
{
	PosX = 0,  ///< +X (right)
	NegX = 1,  ///< -X (left)
	PosY = 2,  ///< +Y (up / top)
	NegY = 3,  ///< -Y (down / bottom)
	PosZ = 4,  ///< +Z (front)
	NegZ = 5,  ///< -Z (back)
};

/// @brief 面の総数
constexpr int kCubemapFaceCount = 6;

/// @brief 6 面 RGBA8 キューブマップ
/// @details 6 つの正方 Texture を保持する不変値型。
///          factory メソッドで構築する想定で、ユーザーが個別に
///          face() で書き換えることは想定しない（イミュータブル運用）。
class Cubemap
{
public:
	/// @brief デフォルトコンストラクタ（空キューブマップ）
	Cubemap() = default;

	/// @brief 6 面 Texture を直接受け取って構築する
	/// @param faces 6 面の Texture 配列（+X, -X, +Y, -Y, +Z, -Z 順）
	/// @details 1 面でも空 / 非正方 / サイズ不一致なら valid() は false。
	explicit Cubemap(const std::array<Texture, kCubemapFaceCount>& faces) noexcept
		: m_faces(faces)
	{
	}

	/// @brief 単色キューブマップを生成する
	/// @param size 1 面の辺長（ピクセル）。1 以上必須
	/// @param color RGB 色（A は 255 固定）
	/// @return 全 6 面が同一単色のキューブマップ
	[[nodiscard]] static Cubemap solid(int size, const sgc::Colorf& color) noexcept
	{
		if (size <= 0)
		{
			return {};
		}

		const auto r = toByte(color.r);
		const auto g = toByte(color.g);
		const auto b = toByte(color.b);

		Cubemap cm;
		const auto face = Texture::solid(size, size, r, g, b, 255u);
		for (auto& f : cm.m_faces)
		{
			f = face;
		}
		return cm;
	}

	/// @brief 縦方向グラデーション（空 → 地面）の手抜き sky
	/// @details +Y (top) は zenith 単色、-Y (bottom) は nadir 単色、
	///          側面 4 面は上端 zenith、下端 nadir、線形補間。
	///          IBL ライティングの最小プレースホルダや HDR 不要の
	///          スタイライズドゲーム用途を想定。
	/// @param size 1 面の辺長
	/// @param zenith 天頂（+Y）色
	/// @param nadir 地表（-Y）色
	[[nodiscard]] static Cubemap verticalGradient(
		int size, const sgc::Colorf& zenith, const sgc::Colorf& nadir) noexcept
	{
		if (size <= 0)
		{
			return {};
		}

		Cubemap cm;
		cm.m_faces[static_cast<int>(CubeFace::PosY)] = solidFace(size, zenith);
		cm.m_faces[static_cast<int>(CubeFace::NegY)] = solidFace(size, nadir);
		const auto vGradient = makeVerticalGradient(size, zenith, nadir);
		cm.m_faces[static_cast<int>(CubeFace::PosX)] = vGradient;
		cm.m_faces[static_cast<int>(CubeFace::NegX)] = vGradient;
		cm.m_faces[static_cast<int>(CubeFace::PosZ)] = vGradient;
		cm.m_faces[static_cast<int>(CubeFace::NegZ)] = vGradient;
		return cm;
	}

	/// @brief 1 面アクセサ
	[[nodiscard]] const Texture& face(CubeFace f) const noexcept
	{
		return m_faces[static_cast<int>(f)];
	}

	/// @brief 1 面アクセサ（int 版）
	[[nodiscard]] const Texture& face(int index) const noexcept
	{
		return m_faces.at(static_cast<std::size_t>(index));
	}

	/// @brief 1 面のサイズ（valid のときのみ意味を持つ）
	[[nodiscard]] int faceSize() const noexcept
	{
		return m_faces[0].width();
	}

	/// @brief 拡散 (diffuse) IBL 用 irradiance キューブマップを畳み込みで生成する
	/// @details 出力の各テクセルの法線方向まわりの半球を cosine 項付きで加重平均する。
	///          正規化は総重みで割る加重平均なので、単色 (solid) 環境を渡すと
	///          出力もほぼ同じ単色になる (呼び出し側で π 等の追加係数は不要)。
	/// @param outSize 出力キューブ 1 面の辺長
	/// @param phiSteps,thetaSteps 半球積分の分割数（大きいほど滑らかだが遅い）
	[[nodiscard]] Cubemap irradiance(int outSize, int phiSteps = 32, int thetaSteps = 8) const noexcept
	{
		if (outSize <= 0 || !valid() || phiSteps <= 0 || thetaSteps <= 0)
		{
			return {};
		}

		constexpr float kTwoPi = 6.28318530717958f;
		constexpr float kHalfPi = 1.57079632679490f;
		const float deltaPhi = kTwoPi / static_cast<float>(phiSteps);
		const float deltaTheta = kHalfPi / static_cast<float>(thetaSteps);
		const Cubemap& src = *this;

		Cubemap out;
		for (int f = 0; f < kCubemapFaceCount; ++f)
		{
			auto convolve = [&](const sgc::Vec3f& n, const sgc::Vec3f& right, const sgc::Vec3f& up)
			{
				sgc::Vec3f sum{};
				float weight = 0.0f;
				for (int ip = 0; ip < phiSteps; ++ip)
				{
					const float phi = static_cast<float>(ip) * deltaPhi;
					const float cosPhi = std::cos(phi);
					const float sinPhi = std::sin(phi);
					for (int it = 0; it < thetaSteps; ++it)
					{
						const float theta = (static_cast<float>(it) + 0.5f) * deltaTheta;
						const float sinTheta = std::sin(theta);
						const float cosTheta = std::cos(theta);
						const sgc::Vec3f sampleDir =
							right * (sinTheta * cosPhi) + up * (sinTheta * sinPhi) + n * cosTheta;
						const auto c = sampleDirectionNearest(src, sampleDir);
						const float w = cosTheta * sinTheta;
						sum += sgc::Vec3f{c.r, c.g, c.b} * w;
						weight += w;
					}
				}
				return weight > 0.0f ? sum / weight : sum;
			};
			out.m_faces[static_cast<std::size_t>(f)] =
				convolveFace(static_cast<CubeFace>(f), outSize, convolve);
		}
		return out;
	}

	/// @brief GGX 重点サンプリングで鏡面 (specular) 環境マップを roughness 別に畳み込む
	/// @details split-sum 近似の view=roughness=normal 仮定 (V=R=N) で
	///          `ISceneFx` の prefiltered environment map 用ミップを roughness ごとに 1 枚ずつ作る。
	/// @param outSize 出力キューブ 1 面の辺長
	/// @param roughness [0,1]。0 に近いほど入力に近く、1 に近いほど irradiance に近い広がり方をする
	/// @param sampleCount 重点サンプル数（大きいほど滑らかだが遅い）
	[[nodiscard]] Cubemap prefilterSpecular(int outSize, float roughness, int sampleCount = 64) const noexcept
	{
		if (outSize <= 0 || !valid() || sampleCount <= 0)
		{
			return {};
		}
		roughness = std::clamp(roughness, 0.0f, 1.0f);
		const float a = roughness * roughness;
		const Cubemap& src = *this;

		Cubemap out;
		for (int f = 0; f < kCubemapFaceCount; ++f)
		{
			auto convolve = [&](const sgc::Vec3f& n, const sgc::Vec3f& tangentX, const sgc::Vec3f& tangentY)
			{
				sgc::Vec3f sum{};
				float totalWeight = 0.0f;
				for (int i = 0; i < sampleCount; ++i)
				{
					float xi1 = 0.0f, xi2 = 0.0f;
					hammersley(i, sampleCount, xi1, xi2);
					const sgc::Vec3f hTangent = importanceSampleGGX(xi1, xi2, a);
					const sgc::Vec3f h =
						tangentX * hTangent.x + tangentY * hTangent.y + n * hTangent.z;
					const sgc::Vec3f l = (h * (2.0f * n.dot(h)) - n).normalized();

					const float nDotL = n.dot(l);
					if (nDotL > 0.0f)
					{
						const auto c = sampleDirectionNearest(src, l);
						sum += sgc::Vec3f{c.r, c.g, c.b} * nDotL;
						totalWeight += nDotL;
					}
				}
				if (totalWeight > 0.0f) { return sum / totalWeight; }
				// n·l>0 になるサンプルが 1 つもない場合の縮退先: 法線方向を直接サンプルする。
				const auto fallback = sampleDirectionNearest(src, n);
				return sgc::Vec3f{fallback.r, fallback.g, fallback.b};
			};
			out.m_faces[static_cast<std::size_t>(f)] =
				convolveFace(static_cast<CubeFace>(f), outSize, convolve);
		}
		return out;
	}

	/// @brief 全 6 面が同サイズの正方 RGBA8 として有効か
	[[nodiscard]] bool valid() const noexcept
	{
		const int w = m_faces[0].width();
		const int h = m_faces[0].height();
		if (w <= 0 || h <= 0 || w != h)
		{
			return false;
		}
		for (int i = 1; i < kCubemapFaceCount; ++i)
		{
			if (m_faces[i].width() != w || m_faces[i].height() != h)
			{
				return false;
			}
			if (!m_faces[i].valid())
			{
				return false;
			}
		}
		return m_faces[0].valid();
	}

private:
	std::array<Texture, kCubemapFaceCount> m_faces{};

	/// @brief 面 index + UV [0,1] からワールド方向ベクトル（非正規化）を得る
	/// @details `directionToFaceUv` の逆写像。面ごとの符号は互いに一致するよう選んである。
	[[nodiscard]] static sgc::Vec3f faceUvToDirection(CubeFace face, float u, float v) noexcept
	{
		const float uc = 2.0f * u - 1.0f;
		const float vc = 2.0f * v - 1.0f;
		switch (face)
		{
		case CubeFace::PosX: return {1.0f, -vc, -uc};
		case CubeFace::NegX: return {-1.0f, -vc, uc};
		case CubeFace::PosY: return {uc, 1.0f, vc};
		case CubeFace::NegY: return {uc, -1.0f, -vc};
		case CubeFace::PosZ: return {uc, -vc, 1.0f};
		default:             return {-uc, -vc, -1.0f}; // NegZ
		}
	}

	/// @brief 方向ベクトルから面 + UV [0,1] を求める（`faceUvToDirection` の逆写像）
	static void directionToFaceUv(const sgc::Vec3f& dir, CubeFace& face, float& u, float& v) noexcept
	{
		const float ax = std::fabs(dir.x);
		const float ay = std::fabs(dir.y);
		const float az = std::fabs(dir.z);
		float sc = 0.0f, tc = 0.0f, ma = 1.0f;
		if (ax >= ay && ax >= az)
		{
			ma = ax;
			if (dir.x > 0.0f) { face = CubeFace::PosX; sc = -dir.z; tc = -dir.y; }
			else              { face = CubeFace::NegX; sc =  dir.z; tc = -dir.y; }
		}
		else if (ay >= ax && ay >= az)
		{
			ma = ay;
			if (dir.y > 0.0f) { face = CubeFace::PosY; sc = dir.x; tc =  dir.z; }
			else              { face = CubeFace::NegY; sc = dir.x; tc = -dir.z; }
		}
		else
		{
			ma = az;
			if (dir.z > 0.0f) { face = CubeFace::PosZ; sc =  dir.x; tc = -dir.y; }
			else              { face = CubeFace::NegZ; sc = -dir.x; tc = -dir.y; }
		}
		if (ma <= 0.0f) { ma = 1.0f; }
		u = 0.5f * (sc / ma + 1.0f);
		v = 0.5f * (tc / ma + 1.0f);
	}

	/// @brief 方向ベクトルでキューブマップを最近傍サンプルする（RGB, [0,1]）
	[[nodiscard]] static sgc::Colorf sampleDirectionNearest(const Cubemap& cm, const sgc::Vec3f& dir) noexcept
	{
		CubeFace face = CubeFace::PosX;
		float u = 0.0f, v = 0.0f;
		directionToFaceUv(dir, face, u, v);
		const Texture& tex = cm.face(face);
		const int size = tex.width();
		if (size <= 0) { return {}; }
		int px = static_cast<int>(u * static_cast<float>(size));
		int py = static_cast<int>(v * static_cast<float>(size));
		px = std::clamp(px, 0, size - 1);
		py = std::clamp(py, 0, size - 1);
		const std::uint32_t packed = tex.pixelAt(px, py);
		return sgc::Colorf{
			static_cast<float>((packed >> 24) & 0xFFu) / 255.0f,
			static_cast<float>((packed >> 16) & 0xFFu) / 255.0f,
			static_cast<float>((packed >> 8)  & 0xFFu) / 255.0f,
			1.0f};
	}

	/// @brief 出力キューブの 1 面ぶんを、テクセルごとに接空間基底を張って畳み込み関数へ渡す
	/// @details `fn(n, tangentX, tangentY)` は正規化済み法線方向とその接空間基底 2 本を受け取り、
	///          畳み込み結果の RGB を返す。irradiance / prefilterSpecular で共有する。
	template <typename Fn>
	[[nodiscard]] static Texture convolveFace(CubeFace face, int outSize, Fn&& fn) noexcept
	{
		std::vector<std::uint8_t> px(static_cast<std::size_t>(outSize) * outSize * 4u);
		for (int y = 0; y < outSize; ++y)
		{
			for (int x = 0; x < outSize; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outSize);
				const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outSize);
				const sgc::Vec3f n = faceUvToDirection(face, u, v).normalized();

				sgc::Vec3f up = std::fabs(n.y) < 0.999f ? sgc::Vec3f::up() : sgc::Vec3f::right();
				const sgc::Vec3f tangentX = up.cross(n).normalized();
				const sgc::Vec3f tangentY = n.cross(tangentX);

				const sgc::Vec3f result = fn(n, tangentX, tangentY);
				const auto i = static_cast<std::size_t>((y * outSize + x) * 4);
				px[i + 0] = toByte(result.x);
				px[i + 1] = toByte(result.y);
				px[i + 2] = toByte(result.z);
				px[i + 3] = 255u;
			}
		}
		return Texture(outSize, outSize, px);
	}

	/// @brief van der Corput 基数反転列（Hammersley 点列の第 2 成分）
	[[nodiscard]] static float radicalInverseVdC(std::uint32_t bits) noexcept
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return static_cast<float>(bits) * 2.3283064365386963e-10f; // / 2^32
	}

	/// @brief Hammersley 点列の i 番目 (低食い違い量列。GGX 重点サンプリングの分布に使う)
	static void hammersley(int i, int n, float& x1, float& x2) noexcept
	{
		x1 = static_cast<float>(i) / static_cast<float>(n);
		x2 = radicalInverseVdC(static_cast<std::uint32_t>(i));
	}

	/// @brief GGX 分布に従う接空間ハーフベクトルを重点サンプリングする (Karis 2013)
	/// @param a roughness^2 (GGX の alpha パラメータ)
	[[nodiscard]] static sgc::Vec3f importanceSampleGGX(float xi1, float xi2, float a) noexcept
	{
		constexpr float kTwoPi = 6.28318530717958f;
		const float phi = kTwoPi * xi1;
		const float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
		const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
	}

	/// @brief 0-1 float を 0-255 byte に変換する（範囲外は飽和）
	[[nodiscard]] static std::uint8_t toByte(float v) noexcept
	{
		if (v <= 0.0f) return 0u;
		if (v >= 1.0f) return 255u;
		return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
	}

	/// @brief 単色 Texture 生成ヘルパ
	[[nodiscard]] static Texture solidFace(int size, const sgc::Colorf& c) noexcept
	{
		return Texture::solid(size, size,
			toByte(c.r), toByte(c.g), toByte(c.b), 255u);
	}

	/// @brief 縦グラデーション Texture 生成ヘルパ
	/// @details v = 0 (top) で top 色、v = 1 (bottom) で bottom 色。
	///          UV の v 軸は D3D 規約と同じく上→下。
	[[nodiscard]] static Texture makeVerticalGradient(
		int size, const sgc::Colorf& top, const sgc::Colorf& bottom) noexcept
	{
		std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4u);
		for (int y = 0; y < size; ++y)
		{
			const float t = static_cast<float>(y) / static_cast<float>(size - 1);
			const float r = top.r * (1.0f - t) + bottom.r * t;
			const float g = top.g * (1.0f - t) + bottom.g * t;
			const float b = top.b * (1.0f - t) + bottom.b * t;
			const auto rB = toByte(r);
			const auto gB = toByte(g);
			const auto bB = toByte(b);
			for (int x = 0; x < size; ++x)
			{
				const auto i = static_cast<std::size_t>((y * size + x) * 4);
				px[i + 0] = rB;
				px[i + 1] = gB;
				px[i + 2] = bB;
				px[i + 3] = 255u;
			}
		}
		return Texture(size, size, px);
	}
};

} // namespace mitiru::render
