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
#include <stack>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/render/DefaultShaders.hpp>
#include <mitiru/render/StyledRectBatch.hpp>
#include <mitiru/render/Vertex2D.hpp>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <mitiru/gfx/webgl/WebGlShader.hpp>
#include <mitiru/gfx/webgl/WebGlBuffer.hpp>
#endif

#ifdef _WIN32
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11CommandList.hpp>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx11/Dx11Pipeline.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace mitiru::render
{

/// @brief pixel-grid 描画でテクスチャサンプリングに使うフィルタ種別
/// @details Linear はバイリニアで滑らかな拡縮、Point は最近傍で
///          ピクセルアートのシャープなエッジを保つ。
enum class PixelArtFilter
{
	Linear, ///< D3D12_FILTER_MIN_MAG_MIP_LINEAR (default — 後方互換)
	Point   ///< D3D12_FILTER_MIN_MAG_MIP_POINT  (シャープな pixel art)
};

/// @brief 正射影行列（列優先 column-major、float4x4 — OpenGL標準配置）
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
		/// column 0: X スケール
		mat.m[0][0] = 2.0f / width;
		/// column 1: Y スケール（上下反転）
		mat.m[1][1] = -2.0f / height;
		/// column 2: Z スケール
		mat.m[2][2] = 1.0f;
		/// column 3: 平行移動 (tx, ty, tz, 1)
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
		if (m_useDx12Path)
		{
			submitBatchDx12(vertices, indices);
			return;
		}
#endif

		if (m_useGenericPath)
		{
			submitBatchGeneric(vertices, indices);
			return;
		}

#ifdef _WIN32
		submitBatchDx11(vertices, indices);
#endif
	}

	/// @brief SDF矩形バッチをGPUに送信して描画する
	/// @param vertices StyledVertex2D頂点配列
	/// @param indices インデックス配列
	/// @param style スタイル定数（cbuffer b1）
	void submitStyledRectBatch(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style)
	{
		if (!m_valid || vertices.empty() || indices.empty())
		{
			return;
		}

#ifdef _WIN32
		if (m_useDx12Path)
		{
			submitStyledBatchDx12(
				vertices, indices, style,
				SDF_RECT_VS, SDF_RECT_PS,
				m_dx12SdfRectPso);
			return;
		}
#endif

#ifdef _WIN32
		if (!m_useGenericPath && !m_useDx12Path)
		{
			submitStyledBatchDx11(
				vertices, indices, style,
				SDF_RECT_VS, SDF_RECT_PS,
				m_sdfRectVS, m_sdfRectPS, m_sdfRectPipeline);
		}
#endif
	}

	/// @brief SDF円/楕円バッチをGPUに送信して描画する
	/// @param vertices StyledVertex2D頂点配列
	/// @param indices インデックス配列
	/// @param style スタイル定数（cbuffer b1）
	void submitStyledCircleBatch(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style)
	{
		if (!m_valid || vertices.empty() || indices.empty())
		{
			return;
		}

#ifdef _WIN32
		if (m_useDx12Path)
		{
			submitStyledBatchDx12(
				vertices, indices, style,
				SDF_CIRCLE_VS, SDF_CIRCLE_PS,
				m_dx12SdfCirclePso);
			return;
		}
#endif

#ifdef _WIN32
		if (!m_useGenericPath && !m_useDx12Path)
		{
			submitStyledBatchDx11(
				vertices, indices, style,
				SDF_CIRCLE_VS, SDF_CIRCLE_PS,
				m_sdfCircleVS, m_sdfCirclePS, m_sdfCirclePipeline);
		}
#endif
	}

	/// @brief テクスチャ付きスプライトバッチをサポートするか（ADR 0009）
	/// @details 現状 DX12 path のみ。false の backend では Screen が per-pixel
	///          fallback を使う（software / DX11 / WebGL / Null）。
	[[nodiscard]] bool supportsTexturedBatch() const noexcept
	{
		return m_valid && m_useDx12Path;
	}

	/// @brief render::Texture の RGBA8 を GPU にアップロード／キャッシュしハンドルを返す
	/// @details key+(w,h) が一致する限り再アップロードしない（永続キャッシュ）。
	///          初回のみ同期アップロード（CopyTextureRegion + barrier→PSR）。
	/// @param key  テクスチャ識別キー（通常 &Texture）
	/// @param w,h  テクスチャ寸法（ピクセル）
	/// @param rgba RGBA8 ピクセル（w*h*4 bytes、行優先）
	/// @return 1 以上のハンドル。未対応 backend / 失敗時は 0
	std::uint32_t ensureSpriteTexture(const void* key, int w, int h,
	                                  const std::uint8_t* rgba);

	/// @brief texHandle のテクスチャをバインドして頂点バッチを描画する（uUseTexture=1）
	/// @param vertices Vertex2D 頂点（UV 付き）
	/// @param indices インデックス
	/// @param texHandle ensureSpriteTexture が返したハンドル
	void submitTexturedBatch(const std::vector<Vertex2D>& vertices,
	                         const std::vector<std::uint32_t>& indices,
	                         std::uint32_t texHandle);

	/// @brief スクリーンサイズ変更時に正射影行列を更新する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(float width, float height)
	{
		m_screenWidth = width;
		m_screenHeight = height;

		if (m_useGenericPath && m_genConstBuffer)
		{
			const auto ortho = OrthoMatrix::create(width, height);
			m_genConstBuffer->update(ortho.m, sizeof(ortho.m));
		}
#ifdef _WIN32
		if (!m_useGenericPath && !m_useDx12Path && m_dx11Context && m_constantBuffer)
		{
			const auto ortho = OrthoMatrix::create(width, height);
			m_constantBuffer->update(
				m_dx11Context, ortho.m, sizeof(ortho.m));
		}

		if (m_useDx12Path && m_dx12VsCb)
		{
			// CRITICAL: 実 runtime の VS constant buffer は m_dx12VsCb。
			// m_dx12ConstantBuffer は "エイリアス用" コメントの dead pointer
			// (init で populate されない) — そっちを update してた古い resize
			// は ortho 更新が runtime に届かず、resize 後に anisotropic
			// stretch が発生する。
			const auto ortho = OrthoMatrix::create(width, height);
			updateCbDx12(m_dx12VsCb.Get(), ortho.m, sizeof(ortho.m));
		}
#endif
	}

	/// @brief 抽象IDeviceから2Dパイプラインを構築する
	/// @details D3D12やVulkan等、DX11以外のバックエンドで使用する。
	/// @param device GPUデバイス
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return 構築されたパイプライン
	[[nodiscard]] static RenderPipeline2D createFromDevice(
		gfx::IDevice* device,
		float screenWidth,
		float screenHeight)
	{
		if (!device)
		{
			return {};
		}

		RenderPipeline2D pipeline;
		pipeline.m_screenWidth = screenWidth;
		pipeline.m_screenHeight = screenHeight;
		pipeline.m_useGenericPath = true;
		pipeline.m_genDevice = device;

		/// 定数バッファ（正射影行列）を生成する
		const auto ortho = OrthoMatrix::create(screenWidth, screenHeight);
		pipeline.m_genConstBuffer = device->createBuffer(
			gfx::BufferType::Constant,
			sizeof(ortho.m),
			true,
			ortho.m);

		/// 動的頂点バッファを生成する（初期サイズ64KB）
		constexpr std::uint32_t INITIAL_VB_SIZE = 65536;
		pipeline.m_genVertexBuffer = device->createBuffer(
			gfx::BufferType::Vertex,
			INITIAL_VB_SIZE,
			true);
		pipeline.m_genVbCapacity = INITIAL_VB_SIZE;

		/// 動的インデックスバッファを生成する（初期サイズ32KB）
		constexpr std::uint32_t INITIAL_IB_SIZE = 32768;
		pipeline.m_genIndexBuffer = device->createBuffer(
			gfx::BufferType::Index,
			INITIAL_IB_SIZE,
			true);
		pipeline.m_genIbCapacity = INITIAL_IB_SIZE;

		/// コマンドリストを生成する
		pipeline.m_genCommandList = device->createCommandList();

#ifdef __EMSCRIPTEN__
		/// WebGL: 2Dシェーダープログラムを作成する
		pipeline.m_glShader = std::make_unique<gfx::WebGLShader>(
			gfx::WebGLShader::createProgram(
				gfx::WEBGL_VERTEX_SHADER_2D,
				gfx::WEBGL_FRAGMENT_SHADER_2D));
		pipeline.m_glProgram = pipeline.m_glShader->program();
		pipeline.m_glProjLoc = glGetUniformLocation(pipeline.m_glProgram, "uProjection");
		pipeline.m_glUseTexLoc = glGetUniformLocation(pipeline.m_glProgram, "uUseTexture");

		/// VAOを作成し頂点アトリビュートを設定する
		glGenVertexArrays(1, &pipeline.m_glVAO);
		glBindVertexArray(pipeline.m_glVAO);

		auto* vb = dynamic_cast<gfx::WebGLBuffer*>(pipeline.m_genVertexBuffer.get());
		auto* ib = dynamic_cast<gfx::WebGLBuffer*>(pipeline.m_genIndexBuffer.get());
		if (vb) glBindBuffer(GL_ARRAY_BUFFER, vb->handle());
		if (ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->handle());

		/// Vertex2D: pos(vec2) + texCoord(vec2) + color(vec4) = 32 bytes
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, texCoord)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, color)));

		glBindVertexArray(0);

		/// アルファブレンドを有効化する
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif

		pipeline.m_valid = true;
		return pipeline;
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
		float screenHeight);

	/// @brief DX12デバイスから2Dパイプラインを構築する
	/// @details MitiruCefTexture と同等のスタイルで PSO / root signature /
	///          persistent command allocator + command list + fence を自前で持つ。
	///          generic createFromDevice / Dx12CommandList 抽象は PSO/root sig が
	///          bind されないため DX12 で silent no-op になる。本パスで解消する。
	/// @param dx12Device DX12デバイス
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return 構築されたパイプライン
	[[nodiscard]] static RenderPipeline2D createFromDx12(
		gfx::Dx12Device* dx12Device,
		float screenWidth,
		float screenHeight);
#endif

private:
	/// @brief 抽象インターフェース経由でバッチ描画を実行する
	/// @param vertices 頂点配列
	/// @param indices インデックス配列
	void submitBatchGeneric(
		const std::vector<Vertex2D>& vertices,
		const std::vector<std::uint32_t>& indices)
	{
		if (!m_genDevice || !m_genCommandList)
		{
			return;
		}

		const auto vbSize = static_cast<std::uint32_t>(
			vertices.size() * sizeof(Vertex2D));
		const auto ibSize = static_cast<std::uint32_t>(
			indices.size() * sizeof(std::uint32_t));

		/// バッファサイズが不足していたら再生成する
		if (vbSize > m_genVbCapacity)
		{
			const auto newCapacity = std::max(
				vbSize, m_genVbCapacity * 2);
			m_genVertexBuffer = m_genDevice->createBuffer(
				gfx::BufferType::Vertex, newCapacity, true);
			m_genVbCapacity = newCapacity;
		}

		if (ibSize > m_genIbCapacity)
		{
			const auto newCapacity = std::max(
				ibSize, m_genIbCapacity * 2);
			m_genIndexBuffer = m_genDevice->createBuffer(
				gfx::BufferType::Index, newCapacity, true);
			m_genIbCapacity = newCapacity;
		}

		/// バッファを更新する
		m_genVertexBuffer->update(vertices.data(), vbSize);
		m_genIndexBuffer->update(indices.data(), ibSize);

		/// 描画コマンドを発行する
#ifdef __EMSCRIPTEN__
		/// WebGL: 2D描画では深度テストを無効化し、ビューポートを設定する
		glDisable(GL_DEPTH_TEST);
		glViewport(0, 0,
			static_cast<GLsizei>(m_screenWidth),
			static_cast<GLsizei>(m_screenHeight));
		glUseProgram(m_glProgram);
		if (m_glProjLoc >= 0 && m_genConstBuffer)
		{
			const auto ortho = OrthoMatrix::create(m_screenWidth, m_screenHeight);
			/// OrthoMatrix は column-major で構築済み → GL_FALSE（転置不要）
			glUniformMatrix4fv(m_glProjLoc, 1, GL_FALSE, &ortho.m[0][0]);
		}
		if (m_glUseTexLoc >= 0)
		{
			glUniform1i(m_glUseTexLoc, 0); // テクスチャ未使用
		}

		glBindVertexArray(m_glVAO);

		/// VB/IBを再バインド（動的再生成されている可能性があるため）
		auto* vb = dynamic_cast<gfx::WebGLBuffer*>(m_genVertexBuffer.get());
		auto* ib = dynamic_cast<gfx::WebGLBuffer*>(m_genIndexBuffer.get());
		if (vb) glBindBuffer(GL_ARRAY_BUFFER, vb->handle());
		if (ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->handle());

		/// 頂点アトリビュートを再設定（バッファ再生成時に必要）
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, position)));
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, texCoord)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
			reinterpret_cast<void*>(offsetof(Vertex2D, color)));

		glDrawElements(GL_TRIANGLES,
			static_cast<GLsizei>(indices.size()),
			GL_UNSIGNED_INT, nullptr);

		glBindVertexArray(0);
		glUseProgram(0);
		/// 深度テストを復元する（3D描画に必要）
		glEnable(GL_DEPTH_TEST);
#else
		m_genCommandList->begin();
		m_genCommandList->setViewport(viewportWidth(), viewportHeight());
		m_genCommandList->setVertexBuffer(m_genVertexBuffer.get());
		m_genCommandList->setIndexBuffer(m_genIndexBuffer.get());
		m_genCommandList->drawIndexed(
			static_cast<std::uint32_t>(indices.size()), 0, 0);
		m_genCommandList->end();
#endif
	}

	/// ── 汎用バックエンド用メンバ ──────────────────────────
	gfx::IDevice* m_genDevice = nullptr;                    ///< GPUデバイス（非所有）
	std::unique_ptr<gfx::IBuffer> m_genConstBuffer;         ///< 定数バッファ
	std::unique_ptr<gfx::IBuffer> m_genVertexBuffer;        ///< 動的頂点バッファ
	std::unique_ptr<gfx::IBuffer> m_genIndexBuffer;         ///< 動的インデックスバッファ
	std::unique_ptr<gfx::ICommandList> m_genCommandList;    ///< コマンドリスト
	std::uint32_t m_genVbCapacity = 0;                      ///< VB容量（バイト）
	std::uint32_t m_genIbCapacity = 0;                      ///< IB容量（バイト）
	bool m_useGenericPath = false;                           ///< 汎用パス使用フラグ
	bool m_useDx12Path = false;                              ///< DX12専用パス使用フラグ

#ifdef __EMSCRIPTEN__
	std::unique_ptr<gfx::WebGLShader> m_glShader; ///< WebGL シェーダーオブジェクト（RAII）
	GLuint m_glProgram = 0;   ///< WebGL 2Dシェーダープログラム
	GLuint m_glVAO = 0;       ///< WebGL 頂点配列オブジェクト
	GLint m_glProjLoc = -1;   ///< uProjection uniform location
	GLint m_glUseTexLoc = -1; ///< uUseTexture uniform location
#endif

#ifdef _WIN32
	/// @brief DX11でバッチ描画を実行する
	void submitBatchDx11(
		const std::vector<Vertex2D>& vertices,
		const std::vector<std::uint32_t>& indices);

	/// @brief SDF StyledVertex2D用の入力レイアウトを持つパイプラインを生成する
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D11InputLayout>
	createSdfInputLayout(ID3D11Device* device, const gfx::Dx11Shader& vs);

	/// @brief SDF Alpha用ブレンドステートを生成する
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D11BlendState>
	createSdfBlendState(ID3D11Device* device);

	/// @brief SDF用ラスタライザステートを生成する
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D11RasterizerState>
	createSdfRasterizerState(ID3D11Device* device);

	/// @brief SDFパイプラインを遅延初期化してバッチ描画する（Rect/Circle共通）
	void submitStyledBatchDx11(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style,
		std::string_view vsSource,
		std::string_view psSource,
		std::unique_ptr<gfx::Dx11Shader>& cachedVS,
		std::unique_ptr<gfx::Dx11Shader>& cachedPS,
		Microsoft::WRL::ComPtr<ID3D11InputLayout>& cachedLayout);

	ID3D11Device* m_dx11Device = nullptr;               ///< D3D11デバイス（非所有）
	ID3D11DeviceContext* m_dx11Context = nullptr;        ///< D3D11コンテキスト（非所有）
	std::unique_ptr<gfx::Dx11Shader> m_vertexShader;    ///< 頂点シェーダー
	std::unique_ptr<gfx::Dx11Shader> m_pixelShader;     ///< ピクセルシェーダー
	std::unique_ptr<gfx::Dx11Pipeline> m_pipeline;      ///< パイプラインステート
	std::unique_ptr<gfx::Dx11Buffer> m_constantBuffer;  ///< VS定数バッファ
	std::unique_ptr<gfx::Dx11Buffer> m_psConstantBuffer; ///< PS定数バッファ（uUseTexture）
	std::unique_ptr<gfx::Dx11Buffer> m_vertexBuffer;    ///< 動的頂点バッファ
	std::unique_ptr<gfx::Dx11Buffer> m_indexBuffer;     ///< 動的インデックスバッファ
	std::unique_ptr<gfx::Dx11CommandList> m_commandList; ///< コマンドリスト
	std::uint32_t m_vbCapacity = 0;                      ///< VB容量（バイト）
	std::uint32_t m_ibCapacity = 0;                      ///< IB容量（バイト）

	/// ── PixelGrid キャッシュテクスチャ用メンバ (DX11) ────────
	std::unique_ptr<gfx::Dx11Texture> m_pgTexture;      ///< キャッシュGPUテクスチャ（単一スロット）
	int m_pgTexW = 0;                                    ///< キャッシュテクスチャ幅
	int m_pgTexH = 0;                                    ///< キャッシュテクスチャ高さ
	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pgSampler; ///< ポイントフィルタサンプラー

	/// ── PixelGrid キャッシュテクスチャ用メンバ (DX12) ────────
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12PgTexture;  ///< default heap texture
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12PgUpload;   ///< upload heap intermediate
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dx12PgSrvHeap;  ///< shader-visible SRV heap
	int  m_dx12PgTexW     = 0;     ///< キャッシュテクスチャ幅
	int  m_dx12PgTexH     = 0;     ///< キャッシュテクスチャ高さ
	bool m_dx12PgTexReady = false; ///< 初回 CopyTextureRegion 後に true (state = PSR)

	/// ── textured sprite batch 用 永続テクスチャキャッシュ (DX12, ADR 0009) ──
	/// 各 entry は default-heap texture (PSR 状態) + 専用 1-slot shader-visible
	/// SRV heap を持つ。key+(w,h) でキャッシュし、初回のみ同期アップロードする。
	struct Dx12SpriteTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource>       tex;     ///< default heap (PSR)
		Microsoft::WRL::ComPtr<ID3D12Resource>       upload;  ///< upload heap 中間
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap; ///< 1-slot SRV heap
		int         w   = 0;
		int         h   = 0;
		const void* key = nullptr;
	};
	std::vector<Dx12SpriteTexture> m_dx12SpriteTextures;              ///< index+1 = handle
	std::unordered_map<const void*, std::uint32_t> m_dx12SpriteTexLookup; ///< key → index

public:
	/// @brief RGBA8ピクセルバッファをGPUにアップロードし、クワッドとして描画する
	/// @details (pw, ph) が前回と異なる場合のみテクスチャを再確保する。
	///          サンプラーは初回使用時に遅延生成する。
	///          Screen::drawPixelGrid から呼ばれるため public。
	/// @param dest 描画先矩形（論理スクリーン座標）
	/// @param pixels RGBA8ピクセルバッファ（pw * ph 要素）
	/// @param pw バッファ幅
	/// @param ph バッファ高さ
	/// @param screenW 論理スクリーン幅（正射影行列用）
	/// @param screenH 論理スクリーン高さ（正射影行列用）
	/// @param filter サンプラフィルタ。pixel-art は Point、滑らかな拡縮は Linear。
	///               default は Linear で後方互換。DX11 path では現状 point 固定で
	///               この flag は無視される（baked-in sampler は変更しない方針）。
	///               DX12 path では本 flag に応じて point/linear PSO を選択する。
	void submitPixelGrid(const sgc::Rectf& dest,
	                     const std::uint32_t* pixels,
	                     int pw, int ph,
	                     float screenW, float screenH,
	                     PixelArtFilter filter = PixelArtFilter::Linear);

private:

	/// ── SDF パイプライン用メンバ ──────────────────────────
	std::unique_ptr<gfx::Dx11Shader> m_sdfRectVS;       ///< SDF矩形 頂点シェーダー
	std::unique_ptr<gfx::Dx11Shader> m_sdfRectPS;       ///< SDF矩形 ピクセルシェーダー
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_sdfRectPipeline; ///< SDF矩形 入力レイアウト
	std::unique_ptr<gfx::Dx11Shader> m_sdfCircleVS;     ///< SDF円 頂点シェーダー
	std::unique_ptr<gfx::Dx11Shader> m_sdfCirclePS;     ///< SDF円 ピクセルシェーダー
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_sdfCirclePipeline; ///< SDF円 入力レイアウト
	Microsoft::WRL::ComPtr<ID3D11BlendState> m_sdfBlendState;     ///< SDFブレンドステート
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_sdfRasterizerState; ///< SDFラスタライザ
	std::unique_ptr<gfx::Dx11Buffer> m_sdfStyleBuffer;  ///< スタイル定数バッファ（b1）
	std::unique_ptr<gfx::Dx11Buffer> m_sdfVertexBuffer;  ///< SDF動的頂点バッファ
	std::unique_ptr<gfx::Dx11Buffer> m_sdfIndexBuffer;   ///< SDF動的インデックスバッファ
	std::uint32_t m_sdfVbCapacity = 0;                   ///< SDF VB容量（バイト）
	std::uint32_t m_sdfIbCapacity = 0;                   ///< SDF IB容量（バイト）

	/// ── DX12 基本 2D パス用メンバ ─────────────────────────
	gfx::Dx12Device* m_dx12Device = nullptr;                         ///< DX12 デバイス（非所有）
	Microsoft::WRL::ComPtr<ID3D12Device>         m_dx12NativeDevice;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>   m_dx12Queue;
	Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_dx12RootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12Pipeline;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12VsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12PsBlob;
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12VertexBuffer; ///< upload heap
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12IndexBuffer;  ///< upload heap
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12ConstantBuffer; ///< エイリアス用 (resize で参照)
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12VsCb;         ///< projection matrix CB
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12PsCb;         ///< uUseTexture CB
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dx12SrvHeap;      ///< null SRV 用
	std::uint32_t m_dx12VbCapacity = 0;
	std::uint32_t m_dx12IbCapacity = 0;

	/// ── DX12 pixel-grid point-filter パス用メンバ（createFromDx12 で eager 初期化）─
	/// 別 root signature が必要な理由: DX12 の static sampler は root signature
	/// に焼き込まれており PSO 単体では差し替えできない。同じ shader / blend / 入力
	/// layout を保ったまま root sig の static sampler の Filter だけを
	/// D3D12_FILTER_MIN_MAG_MIP_POINT に変えた variant を保持する。
	///
	/// createFromDx12 時点で linear PSO と並べて eager に構築する。構築に失敗した
	/// 場合は nullptr のまま残り、submitPixelGridDx12 は linear へ自動
	/// フォールバックする (draw() 内での遅延初期化は禁止 — エンジン規約)。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_dx12PointRootSig; ///< point-filter root sig
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_dx12PointPipeline; ///< point-filter PSO

	/// @brief pixel-grid 用 point-filter root signature + PSO を eager に構築する
	/// @details createFromDx12 から base PSO 構築直後に呼ばれる。失敗時は
	///          m_dx12PointRootSig / m_dx12PointPipeline を nullptr のまま残し、
	///          submitPixelGridDx12 が linear PSO へフォールバックする。
	///          ここで例外を投げないのは pixel-grid の Point は性能改善で
	///          あって機能要件ではないため (linear でも描画は成立する)。
	void buildDx12PointFilterResources(
		ID3D12Device* device,
		ID3DBlob* vsBlob, ID3DBlob* psBlob,
		const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount);

	/// ── DX12 SDF パス用メンバ（遅延初期化）──────────────
	Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_dx12SdfRootSig;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfRectVsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfRectPsBlob;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12SdfRectPso;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfCircleVsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfCirclePsBlob;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12SdfCirclePso;
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfIndexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfStyleCb;
	std::uint32_t m_dx12SdfVbCapacity = 0;
	std::uint32_t m_dx12SdfIbCapacity = 0;

	/// ── DX12 共通: 永続コマンドリスト + fence ──────────
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_dx12Alloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_dx12Cl;
	Microsoft::WRL::ComPtr<ID3D12Fence>               m_dx12Fence;
	UINT64                                            m_dx12FenceValue = 0;
	HANDLE                                            m_dx12FenceEvent = nullptr;

	/// @brief upload heap の ID3D12Resource を作成する
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D12Resource>
	createUploadBufferDx12(ID3D12Device* device, std::uint32_t sizeBytes);

	/// @brief 定数バッファを upload heap 経由で更新する
	void updateCbDx12(ID3D12Resource* cb, const void* data, size_t bytes);

	/// @brief 動的バッファの容量を確保し、データを書き込む
	void updateDx12Buffer(
		Microsoft::WRL::ComPtr<ID3D12Resource>& buf,
		std::uint32_t& capacity,
		const void* data, std::uint32_t bytes);

	/// @brief 2D 用 PSO を生成する (alpha blend, no depth, triangle list)
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D12PipelineState>
	buildDx12Pso(ID3D12Device* device,
		ID3D12RootSignature* rootSig,
		ID3DBlob* vs, ID3DBlob* ps,
		const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount);

	/// @brief 前回発行した command list の GPU 完了を待機する
	void waitDx12Fence();

	/// @brief SDF ルートシグネチャ + PSO を遅延初期化する
	void ensureDx12SdfResources(
		std::string_view vsSource, std::string_view psSource,
		Microsoft::WRL::ComPtr<ID3DBlob>& cachedVs,
		Microsoft::WRL::ComPtr<ID3DBlob>& cachedPs,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso);

	/// @brief DX12 で基本 2D バッチ描画を実行する
	void submitBatchDx12(
		const std::vector<Vertex2D>& vertices,
		const std::vector<std::uint32_t>& indices);

	/// @brief DX12 で SDF バッチ描画を実行する (Rect/Circle 共通)
	void submitStyledBatchDx12(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style,
		std::string_view vsSource,
		std::string_view psSource,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso);

	/// @brief DX12 で pixel-grid テクスチャクワッドを描画する
	/// @param filter Linear なら既存 PSO (m_dx12Pipeline)、Point なら point-filter
	///               root sig + PSO variant (m_dx12PointPipeline) を選択する。
	void submitPixelGridDx12(
		const sgc::Rectf& dest,
		const std::uint32_t* pixels,
		int pw, int ph,
		PixelArtFilter filter);
#endif

	float m_screenWidth = 0.0f;    ///< スクリーン論理幅（投影行列用）
	float m_screenHeight = 0.0f;   ///< スクリーン論理高さ（投影行列用）
	float m_viewportWidth = 0.0f;  ///< バックバッファ実幅（ビューポート用）
	float m_viewportHeight = 0.0f; ///< バックバッファ実高さ（ビューポート用）
	bool m_valid = false;          ///< 有効フラグ

public:
	/// @brief ビューポートサイズを設定する（バックバッファの実サイズ）
	/// @details スクリーン論理サイズとバックバッファサイズが異なる場合に使用。
	///          設定しない場合はスクリーン論理サイズがビューポートに使われる。
	void setViewportSize(float w, float h) noexcept
	{
		m_viewportWidth = w;
		m_viewportHeight = h;
	}

	/// @brief ビューポートに使用する幅を取得する
	/// @details バックバッファ実幅（setViewportSize）が設定されていればそれを、
	///          未設定なら論理スクリーン幅を返す。
	///          投影行列は常に論理サイズ（m_screenWidth/Height）で構築されるため、
	///          論理座標はバックバッファの実サイズに対して1:1で正しくマッピングされる。
	[[nodiscard]] float viewportWidth() const noexcept
	{
		return m_viewportWidth > 0.0f ? m_viewportWidth : m_screenWidth;
	}

	/// @brief ビューポートに使用する高さを取得する
	[[nodiscard]] float viewportHeight() const noexcept
	{
		return m_viewportHeight > 0.0f ? m_viewportHeight : m_screenHeight;
	}

	/// @brief ブレンドモードを変更する
	/// @details 新しいパイプラインステートを構築してバインドする。
	///          呼び出し側はバッチフラッシュ済みであること。
	void setBlendMode([[maybe_unused]] gfx::BlendMode mode)
	{
#ifdef _WIN32
		if (!m_valid || m_useGenericPath || !m_dx11Device) return;

		gfx::Dx11PipelineDesc pipeDesc;
		pipeDesc.vertexShader = m_vertexShader.get();
		pipeDesc.pixelShader = m_pixelShader.get();
		pipeDesc.blendMode = mode;
		pipeDesc.scissorEnable = hasScissor();
		m_pipeline = std::make_unique<gfx::Dx11Pipeline>(m_dx11Device, pipeDesc);
#endif
	}

	/// @brief シザー矩形をプッシュする（クリッピング開始）
	/// @param rect クリップ矩形
	void pushScissorRect(const sgc::Rectf& rect)
	{
		m_scissorStack.push(rect);
		applyScissorRect(rect);
	}

	/// @brief シザー矩形をポップする（前のクリップに戻す）
	void popScissorRect()
	{
		if (m_scissorStack.empty()) return;
		m_scissorStack.pop();
		if (!m_scissorStack.empty())
		{
			applyScissorRect(m_scissorStack.top());
		}
		else
		{
			resetScissorRect();
		}
	}

	/// @brief シザースタックが空かどうか
	[[nodiscard]] bool hasScissor() const noexcept
	{
		return !m_scissorStack.empty();
	}

private:
	std::stack<sgc::Rectf> m_scissorStack; ///< シザー矩形スタック

	/// @brief シザー矩形を適用する
	void applyScissorRect(const sgc::Rectf& rect)
	{
#ifdef _WIN32
		if (m_commandList)
		{
			m_commandList->setScissorRect(
				static_cast<int>(rect.x()),
				static_cast<int>(rect.y()),
				static_cast<int>(rect.width()),
				static_cast<int>(rect.height()));
		}
#endif
	}

	/// @brief シザー矩形をリセットする
	void resetScissorRect()
	{
#ifdef _WIN32
		if (m_commandList)
		{
			m_commandList->resetScissorRect(
				static_cast<int>(m_screenWidth),
				static_cast<int>(m_screenHeight));
		}
#endif
	}
};

} // namespace mitiru::render

#include <mitiru/render/detail/RenderPipeline2D_Dx11.hpp>
#include <mitiru/render/detail/RenderPipeline2D_Dx12.hpp>
#include <mitiru/render/detail/RenderPipeline2D_PixelGrid.hpp>
#include <mitiru/render/detail/RenderPipeline2D_TexturedBatch.hpp>
