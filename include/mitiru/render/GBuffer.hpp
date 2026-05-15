#pragma once

/// @file GBuffer.hpp
/// @brief ディファードレンダリング用Gバッファ（ソフトウェア実装）
/// @details 位置・法線・アルベド・深度をCPU側のピクセルバッファに格納する。
///          DeferredPipelineのジオメトリパスで書き込み、ライティングパスで読み出す。

#include <cstddef>
#include <stdexcept>
#include <vector>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief Gバッファの1ピクセル分のデータ
/// @details ワールド空間の位置・法線・アルベド色・深度を保持する。
struct GBufferPixel
{
	sgc::Vec3f position{};            ///< ワールド空間位置
	sgc::Vec3f normal{0.0f, 1.0f, 0.0f}; ///< 法線ベクトル（正規化済み）
	sgc::Colorf albedo{0.0f, 0.0f, 0.0f, 1.0f}; ///< アルベド色
	float depth = 1.0f;               ///< 深度値 [0, 1]（1.0 = 最大深度）
};

/// @brief ディファードレンダリング用Gバッファ（ソフトウェア実装）
/// @details initialize() → clear() → writePixel() → readPixel() の順で使用する。
///
/// @code
/// mitiru::render::GBuffer gbuf;
/// gbuf.initialize(1280, 720);
/// gbuf.clear();
///
/// mitiru::render::GBufferPixel px;
/// px.position = {1.0f, 0.0f, 0.0f};
/// px.normal = {0.0f, 1.0f, 0.0f};
/// px.albedo = sgc::Colorf::white();
/// px.depth = 0.5f;
/// gbuf.writePixel(640, 360, px);
///
/// const auto& read = gbuf.readPixel(640, 360);
/// @endcode
class GBuffer
{
public:
	/// @brief デフォルトコンストラクタ
	GBuffer() noexcept = default;

	/// @brief Gバッファを初期化する
	/// @param width 幅（ピクセル）
	/// @param height 高さ（ピクセル）
	/// @throws std::invalid_argument 幅または高さが1未満の場合
	void initialize(int width, int height)
	{
		if (width < 1 || height < 1)
		{
			throw std::invalid_argument("GBuffer: width and height must be >= 1");
		}

		m_width = width;
		m_height = height;
		m_pixels.resize(static_cast<std::size_t>(width) * height);
		clear();
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_width > 0 && m_height > 0;
	}

	/// @brief バッファをクリアする（全ピクセルをデフォルト値にリセット）
	void clear()
	{
		const GBufferPixel defaultPixel{};
		std::fill(m_pixels.begin(), m_pixels.end(), defaultPixel);
	}

	/// @brief ピクセルを書き込む
	/// @param x X座標
	/// @param y Y座標
	/// @param pixel 書き込むピクセルデータ
	/// @note 範囲外の座標は無視される
	void writePixel(int x, int y, const GBufferPixel& pixel)
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height)
		{
			return;
		}

		m_pixels[static_cast<std::size_t>(y * m_width + x)] = pixel;
	}

	/// @brief ピクセルを読み出す
	/// @param x X座標
	/// @param y Y座標
	/// @return ピクセルデータの定数参照（範囲外はデフォルトピクセル）
	/// @note 範囲外アクセス時はm_outOfRangePixelを返す（静的ローカルは使用しない）
	[[nodiscard]] const GBufferPixel& readPixel(int x, int y) const noexcept
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height)
		{
			return m_outOfRangePixel;
		}

		return m_pixels[static_cast<std::size_t>(y * m_width + x)];
	}

	/// @brief 幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief 高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief 全ピクセルデータを取得する（テスト・GPU転送用）
	[[nodiscard]] const std::vector<GBufferPixel>& pixels() const noexcept
	{
		return m_pixels;
	}

private:
	int m_width = 0;   ///< 幅
	int m_height = 0;  ///< 高さ
	std::vector<GBufferPixel> m_pixels;  ///< ピクセルバッファ
	GBufferPixel m_outOfRangePixel{};    ///< 範囲外アクセス時に返すデフォルトピクセル
};

} // namespace mitiru::render
