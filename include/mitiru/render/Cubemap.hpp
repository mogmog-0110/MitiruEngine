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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
