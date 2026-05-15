#pragma once

/// @file Renderer3DBridge.hpp
/// @brief SoftwareRenderer3D出力とGPUバックエンドの接続ブリッジ
/// @details SoftwareRenderer3DがScreen（2Dピクセルバッファ）に描画した結果を
///          IDeviceバックエンド経由でGPUに転送する。
///          このブリッジはクロスプラットフォームな3D表示の手段を提供する:
///          CPUで3DをScreenに描画し、そのピクセルをGPUテクスチャとしてブリットする。
///
/// @code
/// mitiru::bridge::Renderer3DBridge bridge(screen, device);
///
/// // SoftwareRenderer3Dで3Dを描画する
/// renderer3d.drawBox(screen, {0, 0, 0}, {2, 2, 2}, color);
///
/// // ScreenのピクセルをGPUテクスチャに変換する
/// auto tex = bridge.toTexture();
///
/// // 取得したテクスチャをスプライトとして全画面表示する
/// screen.drawSprite(tex, {0, 0, screenW, screenH});
/// @endcode

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/core/Screen.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::bridge
{

/// @brief フルスクリーンクワッドの頂点データ
/// @details NDC座標系でスクリーン全体を覆う4頂点（位置 + テクスチャ座標）
struct FullScreenVertex
{
	float x;   ///< NDC X座標 (-1.0〜+1.0)
	float y;   ///< NDC Y座標 (-1.0〜+1.0)
	float u;   ///< テクスチャU座標 (0.0〜1.0)
	float v;   ///< テクスチャV座標 (0.0〜1.0)
};

/// @brief SoftwareRenderer3D出力とGPUバックエンドの接続ブリッジ
/// @details ScreenのソフトウェアフレームバッファをIDeviceのGPUリソースに転送する。
///          クロスプラットフォーム（DX11/DX12/Vulkan/OpenGL/WebGL）で動作する。
class Renderer3DBridge
{
public:
	/// @brief コンストラクタ
	/// @param screen ピクセルバッファの取得元となるサーフェス（非所有参照）
	/// @param device 転送先のGPUデバイス（nullptrの場合は無効ブリッジとして動作）
	Renderer3DBridge(mitiru::Screen& screen, gfx::IDevice* device) noexcept
		: m_screen(screen)
		, m_device(device)
	{
	}

	// ── テクスチャ変換 ────────────────────────────────────────

	/// @brief Screenのピクセルデータをrender::Textureに変換する
	/// @details Screen::pixels()が提供するRGBA8バッファをそのまま
	///          render::Textureにラップして返す。
	///          IDeviceには依存しないため、ヘッドレス環境でも使用可能。
	/// @return Screen全体のスナップショットを持つテクスチャ（無効なPixelバッファ時は空テクスチャ）
	[[nodiscard]] render::Texture toTexture() const
	{
		if (!m_screen.hasSoftwareFramebuffer())
		{
			return render::Texture{};
		}

		const auto& px = m_screen.pixels();
		if (px.empty())
		{
			return render::Texture{};
		}

		return render::Texture{m_screen.width(), m_screen.height(), px};
	}

	// ── GPU頂点バッファ生成 ────────────────────────────────────

	/// @brief フルスクリーンクワッド頂点バッファをGPUに生成する
	/// @details IDevice::createBuffer()でNDCフルスクリーンクワッドの
	///          頂点バッファを生成して返す。
	///          フルスクリーンブリット用のパイプラインと組み合わせて使用する。
	/// @return 生成された頂点バッファ（デバイスが無効な場合はnullptr）
	[[nodiscard]] std::unique_ptr<gfx::IBuffer> createQuadVertexBuffer() const
	{
		if (!m_device)
		{
			return nullptr;
		}

		/// NDC座標系でスクリーン全体を覆う2三角形（6頂点）を構成する
		/// 頂点順: 位置(x, y) + テクスチャ座標(u, v)
		/// Y軸はNDCとテクスチャで反転していることに注意する（NDC上がY+、テクスチャ上がV=0）
		static constexpr FullScreenVertex kQuadVerts[6] = {
			// 第1三角形（左上・左下・右上）
			{-1.0f,  1.0f,  0.0f, 0.0f},
			{-1.0f, -1.0f,  0.0f, 1.0f},
			{ 1.0f,  1.0f,  1.0f, 0.0f},
			// 第2三角形（左下・右下・右上）
			{-1.0f, -1.0f,  0.0f, 1.0f},
			{ 1.0f, -1.0f,  1.0f, 1.0f},
			{ 1.0f,  1.0f,  1.0f, 0.0f},
		};

		return m_device->createBuffer(
			gfx::BufferType::Vertex,
			static_cast<std::uint32_t>(sizeof(kQuadVerts)),
			/*dynamic=*/false,
			kQuadVerts);
	}

	/// @brief Screenのピクセルデータを動的頂点バッファとしてGPUにアップロードする
	/// @details createBuffer()でデータバッファを生成し、毎フレームのブリットに使用できる。
	///          ピクセルデータをそのまま頂点バッファとして渡すことで、
	///          テクスチャAPIを持たないシンプルなバックエンドでも動作させる。
	/// @return 生成されたピクセルデータバッファ（デバイスまたはピクセルバッファが無効な場合はnullptr）
	[[nodiscard]] std::unique_ptr<gfx::IBuffer> uploadPixelsAsBuffer() const
	{
		if (!m_device || !m_screen.hasSoftwareFramebuffer())
		{
			return nullptr;
		}

		const auto& px = m_screen.pixels();
		if (px.empty())
		{
			return nullptr;
		}

		return m_device->createBuffer(
			gfx::BufferType::Vertex,
			static_cast<std::uint32_t>(px.size()),
			/*dynamic=*/true,
			px.data());
	}

	/// @brief 既存のピクセルバッファをGPUバッファに再アップロードする
	/// @details 毎フレーム呼び出してScreenの最新ピクセルをバッファに反映する。
	/// @param buffer 更新対象のGPUバッファ（uploadPixelsAsBuffer()で生成したもの）
	void updatePixelBuffer(gfx::IBuffer& buffer) const
	{
		if (!m_screen.hasSoftwareFramebuffer())
		{
			return;
		}

		const auto& px = m_screen.pixels();
		if (px.empty())
		{
			return;
		}

		buffer.update(px.data(), static_cast<std::uint32_t>(px.size()));
	}

	// ── アクセサ ─────────────────────────────────────────────

	/// @brief 接続されているScreenへの参照を取得する
	[[nodiscard]] mitiru::Screen& screen() noexcept
	{
		return m_screen;
	}

	/// @brief 接続されているScreenへのconst参照を取得する
	[[nodiscard]] const mitiru::Screen& screen() const noexcept
	{
		return m_screen;
	}

	/// @brief 接続されているGPUデバイスを取得する
	/// @return デバイスへのポインタ（未接続時はnullptr）
	[[nodiscard]] gfx::IDevice* device() const noexcept
	{
		return m_device;
	}

	/// @brief GPUデバイスを差し替える
	/// @param device 新しいデバイス（nullptrで接続解除）
	void setDevice(gfx::IDevice* device) noexcept
	{
		m_device = device;
	}

	/// @brief ブリッジが有効か（ScreenとDeviceが揃っているか）を取得する
	[[nodiscard]] bool isReady() const noexcept
	{
		return m_device != nullptr && m_screen.hasSoftwareFramebuffer();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief ブリッジ状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"ready\":" + std::string(isReady() ? "true" : "false") + ",";
		json += "\"screenWidth\":" + std::to_string(m_screen.width()) + ",";
		json += "\"screenHeight\":" + std::to_string(m_screen.height()) + ",";
		json += "\"hasSoftwareFramebuffer\":" +
			std::string(m_screen.hasSoftwareFramebuffer() ? "true" : "false") + ",";
		json += "\"pixelBufferBytes\":" +
			std::to_string(m_screen.pixels().size()) + ",";
		json += "\"hasDevice\":" + std::string(m_device != nullptr ? "true" : "false");
		json += "}";
		return json;
	}

private:
	mitiru::Screen& m_screen;   ///< ピクセルバッファの取得元サーフェス（非所有参照）
	gfx::IDevice* m_device;     ///< 転送先GPUデバイス（非所有ポインタ）
};

} // namespace mitiru::bridge
