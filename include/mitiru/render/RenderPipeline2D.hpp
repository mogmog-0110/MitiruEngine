#pragma once

/// @file RenderPipeline2D.hpp
/// @brief 2Dレンダリングパイプラインオーケストレーター
/// @details Screen/SpriteBatch/ShapeRenderer → GPU描画を接続する。
///          DX11環境ではシェーダー・バッファ・パイプラインを構築し、
///          Null環境では何もしない。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/render/DefaultShaders.hpp>
#include <mitiru/render/Vertex2D.hpp>

#ifdef _WIN32
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11CommandList.hpp>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx11/Dx11Pipeline.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>
#endif

namespace mitiru::render
{

/// @brief 正射影行列（行優先、float4x4）
/// @details left=0, right=w, top=0, bottom=h, near=0, far=1
struct OrthoMatrix
{
	float m[4][4] = {};

	/// @brief 正射影行列を計算する
	/// @param width スクリーン幅
	/// @param height スクリーン高さ
	static OrthoMatrix create(float width, float height) noexcept
	{
		OrthoMatrix mat = {};
		/// 列0
		mat.m[0][0] = 2.0f / width;
		/// 列1
		mat.m[1][1] = -2.0f / height;
		/// 列2
		mat.m[2][2] = 1.0f;
		/// 列3（平行移動）
		mat.m[3][0] = -1.0f;
		mat.m[3][1] = 1.0f;
		mat.m[3][2] = 0.0f;
		mat.m[3][3] = 1.0f;
		return mat;
	}
};

/// @brief 2Dレンダリングパイプラインオーケストレーター
/// @details GPU描画に必要なリソース（シェーダー・バッファ・パイプライン）を保持し、
///          SpriteBatch/ShapeRendererの頂点データをGPUに送信する。
///          ヘッドレス（NullDevice）時は何もしない。
///
/// @code
/// auto pipeline = RenderPipeline2D::create(device, 1280, 720);
/// // フレームごと:
/// pipeline->submitBatch(vertices, indices);
/// @endcode
class RenderPipeline2D
{
public:
	/// @brief デフォルトコンストラクタ（ヘッドレス用、何もしない）
	RenderPipeline2D() noexcept = default;

	/// @brief パイプラインが有効かどうかを判定する
	/// @return GPU描画が可能ならtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return m_valid;
	}

	/// @brief 頂点・インデックスデータをGPUに送信して描画する
	/// @param vertices 頂点配列
	/// @param indices インデックス配列
	void submitBatch(const std::vector<Vertex2D>& vertices,
	                 const std::vector<std::uint32_t>& indices)
	{
		if (!m_valid || vertices.empty() || indices.empty())
		{
			return;
		}

#ifdef _WIN32
		submitBatchDx11(vertices, indices);
#endif
	}

	/// @brief スクリーンサイズ変更時に正射影行列を更新する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(float width, float height)
	{
		m_screenWidth = width;
		m_screenHeight = height;

#ifdef _WIN32
		if (m_dx11Context && m_constantBuffer)
		{
			const auto ortho = OrthoMatrix::create(width, height);
			m_constantBuffer->update(
				m_dx11Context, ortho.m, sizeof(ortho.m));
		}
#endif
	}

#ifdef _WIN32
	/// @brief DX11デバイスから2Dパイプラインを構築する
	/// @param dx11Device DX11デバイス
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return 構築されたパイプライン
	[[nodiscard]] static RenderPipeline2D createFromDx11(
		gfx::Dx11Device* dx11Device,
		float screenWidth,
		float screenHeight)
	{
		RenderPipeline2D pipeline;
		pipeline.m_screenWidth = screenWidth;
		pipeline.m_screenHeight = screenHeight;

		auto* device = dx11Device->getD3DDevice();
		auto* context = dx11Device->getD3DContext();
		pipeline.m_dx11Context = context;

		/// シェーダーをコンパイルする
		pipeline.m_vertexShader = std::make_unique<gfx::Dx11Shader>(
			gfx::Dx11Shader::createVertexShader(
				device, DEFAULT_VS_2D, "VSMain"));
		pipeline.m_pixelShader = std::make_unique<gfx::Dx11Shader>(
			gfx::Dx11Shader::createPixelShader(
				device, DEFAULT_PS_2D, "PSMain"));

		/// パイプラインステートを構築する
		gfx::Dx11PipelineDesc pipeDesc;
		pipeDesc.vertexShader = pipeline.m_vertexShader.get();
		pipeDesc.pixelShader = pipeline.m_pixelShader.get();
		pipeDesc.blendMode = gfx::BlendMode::Alpha;
		pipeline.m_pipeline = std::make_unique<gfx::Dx11Pipeline>(
			device, pipeDesc);

		/// 定数バッファ（正射影行列）を生成する
		const auto ortho = OrthoMatrix::create(
			screenWidth, screenHeight);
		pipeline.m_constantBuffer = std::make_unique<gfx::Dx11Buffer>(
			device,
			gfx::BufferType::Constant,
			sizeof(ortho.m),
			true,
			ortho.m);

		/// 動的頂点バッファを生成する（初期サイズ64KB）
		constexpr std::uint32_t INITIAL_VB_SIZE = 65536;
		pipeline.m_vertexBuffer = std::make_unique<gfx::Dx11Buffer>(
			device,
			gfx::BufferType::Vertex,
			INITIAL_VB_SIZE,
			true);
		pipeline.m_vbCapacity = INITIAL_VB_SIZE;

		/// 動的インデックスバッファを生成する（初期サイズ32KB）
		constexpr std::uint32_t INITIAL_IB_SIZE = 32768;
		pipeline.m_indexBuffer = std::make_unique<gfx::Dx11Buffer>(
			device,
			gfx::BufferType::Index,
			INITIAL_IB_SIZE,
			true);
		pipeline.m_ibCapacity = INITIAL_IB_SIZE;

		/// コマンドリストを生成する
		pipeline.m_commandList = std::make_unique<gfx::Dx11CommandList>(
			context);

		pipeline.m_dx11Device = device;
		pipeline.m_valid = true;
		return pipeline;
	}
#endif

private:
#ifdef _WIN32
	/// @brief DX11でバッチ描画を実行する
	/// @param vertices 頂点配列
	/// @param indices インデックス配列
	void submitBatchDx11(
		const std::vector<Vertex2D>& vertices,
		const std::vector<std::uint32_t>& indices)
	{
		const auto vbSize = static_cast<std::uint32_t>(
			vertices.size() * sizeof(Vertex2D));
		const auto ibSize = static_cast<std::uint32_t>(
			indices.size() * sizeof(std::uint32_t));

		/// バッファサイズが不足していたら再生成する
		if (vbSize > m_vbCapacity)
		{
			const auto newCapacity = std::max(
				vbSize, m_vbCapacity * 2);
			m_vertexBuffer = std::make_unique<gfx::Dx11Buffer>(
				m_dx11Device,
				gfx::BufferType::Vertex,
				newCapacity,
				true);
			m_vbCapacity = newCapacity;
		}

		if (ibSize > m_ibCapacity)
		{
			const auto newCapacity = std::max(
				ibSize, m_ibCapacity * 2);
			m_indexBuffer = std::make_unique<gfx::Dx11Buffer>(
				m_dx11Device,
				gfx::BufferType::Index,
				newCapacity,
				true);
			m_ibCapacity = newCapacity;
		}

		/// バッファを更新する
		m_vertexBuffer->update(
			m_dx11Context, vertices.data(), vbSize);
		m_indexBuffer->update(
			m_dx11Context, indices.data(), ibSize);

		/// 描画コマンドを発行する
		m_commandList->begin();
		m_commandList->setViewport(m_screenWidth, m_screenHeight);
		m_commandList->setPipeline(m_pipeline.get());
		m_commandList->setVSConstantBuffer(0, m_constantBuffer.get());
		m_commandList->setVertexBuffer(m_vertexBuffer.get());
		m_commandList->setIndexBuffer(m_indexBuffer.get());
		m_commandList->drawIndexed(
			static_cast<std::uint32_t>(indices.size()), 0, 0);
		m_commandList->end();
	}

	ID3D11Device* m_dx11Device = nullptr;               ///< D3D11デバイス（非所有）
	ID3D11DeviceContext* m_dx11Context = nullptr;        ///< D3D11コンテキスト（非所有）
	std::unique_ptr<gfx::Dx11Shader> m_vertexShader;    ///< 頂点シェーダー
	std::unique_ptr<gfx::Dx11Shader> m_pixelShader;     ///< ピクセルシェーダー
	std::unique_ptr<gfx::Dx11Pipeline> m_pipeline;      ///< パイプラインステート
	std::unique_ptr<gfx::Dx11Buffer> m_constantBuffer;  ///< 定数バッファ
	std::unique_ptr<gfx::Dx11Buffer> m_vertexBuffer;    ///< 動的頂点バッファ
	std::unique_ptr<gfx::Dx11Buffer> m_indexBuffer;     ///< 動的インデックスバッファ
	std::unique_ptr<gfx::Dx11CommandList> m_commandList; ///< コマンドリスト
	std::uint32_t m_vbCapacity = 0;                      ///< VB容量（バイト）
	std::uint32_t m_ibCapacity = 0;                      ///< IB容量（バイト）
#endif

	float m_screenWidth = 0.0f;    ///< スクリーン幅
	float m_screenHeight = 0.0f;   ///< スクリーン高さ
	bool m_valid = false;          ///< 有効フラグ
};

} // namespace mitiru::render
