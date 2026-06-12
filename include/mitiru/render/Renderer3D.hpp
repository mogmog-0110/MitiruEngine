#pragma once

/// @file Renderer3D.hpp
/// @brief DX11 3Dレンダラー
/// @details Phongシェーディングによる3Dメッシュ描画を行うレンダラー。
///          シェーダーコンパイル・定数バッファ管理・深度バッファ・ラスタライザ状態を
///          統合的に管理し、drawMesh()一発でメッシュを描画できる。
///          実装本体は detail/Renderer3D_Setup_impl.hpp / detail/Renderer3D_Draw_impl.hpp。

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/DefaultShaders3D.hpp>
#include <mitiru/render/ToonShaders3D.hpp>
#include <mitiru/render/NPRShaders3D.hpp>
#include <mitiru/render/MultiLightShaders3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/LightArrayCB.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/Skybox.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/RenderState3D.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/IRenderer3D.hpp>

#include <mitiru/debug/TracyZones.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>

#endif // _WIN32

namespace mitiru::render
{

// ShaderMode3D は IRenderer3D.hpp で定義済み

/// @brief トランスフォーム用定数バッファ（CbTransform: register(b0)）
/// @details ワールド・ビュー・射影行列をGPUに転送する。
struct alignas(16) CbTransform
{
	float world[4][4]{};       ///< ワールド行列
	float view[4][4]{};        ///< ビュー行列
	float projection[4][4]{};  ///< 射影行列
};

/// @brief ライティング用定数バッファ（CbLighting: register(b1)）
/// @details ライト・マテリアル・カメラ位置の情報をGPUに転送する。
struct alignas(16) CbLighting
{
	float lightDir[4]{};          ///< ライト方向 (xyz) + パディング
	float lightColor[4]{};        ///< ライト色 (xyz) + パディング
	float ambientColor[4]{};      ///< アンビエント色 (xyz) + パディング
	float cameraPos[4]{};         ///< カメラ位置 (xyz) + パディング
	float materialDiffuse[4]{};   ///< マテリアル拡散色 (rgba)
	float materialSpecular[4]{};  ///< マテリアル鏡面色 (rgba)
	float materialShininess = 32.0f;  ///< マテリアル光沢度
	float _pad[3]{};              ///< パディング
};

/// @brief Renderer3D設定
/// @details 初期化時に渡す設定パラメータ。
struct Renderer3DConfig
{
	float viewportWidth = 1280.0f;   ///< ビューポート幅
	float viewportHeight = 720.0f;   ///< ビューポート高さ
	bool enableDepthBuffer = true;   ///< 深度バッファ有効
	sgc::Colorf defaultAmbient{0.15f, 0.15f, 0.15f, 1.0f};  ///< デフォルトアンビエント色
};

/// @brief DX11 3Dレンダラー
/// @details Camera3Dとリアルタイムライトを使い、メッシュをPhong照明で描画する。
///          内部でシェーダーコンパイル・入力レイアウト・定数バッファ・深度バッファ・
///          ラスタライザステートを管理する。
///
/// @code
/// // 初期化
/// mitiru::render::Renderer3D renderer;
/// renderer.initialize(dx11Device, {1280, 720});
///
/// // 毎フレーム
/// renderer.beginFrame({0.2f, 0.3f, 0.4f, 1.0f});
/// renderer.setCamera(camera);
/// renderer.setLight(Light::directional({0, -1, 0.5f}));
/// renderer.drawMesh(cubeMesh, worldMat, material);
/// renderer.endFrame();
/// @endcode
class Renderer3D : public IRenderer3D
{
public:
	/// @brief デフォルトコンストラクタ
	Renderer3D() noexcept = default;

	/// @brief 描画統計情報を取得する
	/// @return 直前フレームのドローコール数
	[[nodiscard]] int drawCallCount() const noexcept override
	{
		return m_drawCallCount;
	}

	/// @brief 初期化済みかどうかを取得する
	/// @return GPUリソースが構築済みならtrue
	[[nodiscard]] bool isInitialized() const noexcept override
	{
		return m_initialized;
	}

	/// @brief 現在の描画状態を取得する
	/// @return 描画状態の定数参照
	[[nodiscard]] const RenderState3D& renderState() const noexcept
	{
		return m_renderState;
	}

	/// @brief 描画状態を設定する
	/// @param state 新しい描画状態
	void setRenderState(const RenderState3D& state) noexcept
	{
		m_renderState = state;
		m_rasterizerDirty = true;
	}

	/// @brief 設定を取得する
	/// @return 設定の定数参照
	[[nodiscard]] const Renderer3DConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief シェーダーモードを設定する
	/// @param mode 新しいシェーダーモード
	/// @details 初期化済みの場合はシェーダーを再コンパイルする。
	void setShaderMode(ShaderMode3D mode) override
	{
		if (m_shaderMode == mode)
		{
			return;
		}
		m_shaderMode = mode;
#ifdef _WIN32
		if (m_initialized)
		{
			recompileShaders();
		}
#endif
	}

	/// @brief 現在のシェーダーモードを取得する
	/// @return 現在のシェーダーモード
	[[nodiscard]] ShaderMode3D shaderMode() const noexcept
	{
		return m_shaderMode;
	}

#ifdef _WIN32

	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief レンダラーを初期化する
	/// @param device DX11デバイスへのポインタ
	/// @param cfg 設定パラメータ
	/// @param mode シェーダーモード（初期化時に直接設定 — setShaderModeの再コンパイルを回避）
	void initialize(gfx::Dx11Device* device,
		const Renderer3DConfig& cfg = {},
		ShaderMode3D mode = ShaderMode3D::Phong);

	/// @brief フレームアクティブフラグをリセットする（Engine::run()から毎フレーム呼ばれる）
	void resetFrameActive() noexcept override { m_frameActive = false; }

	/// @brief 今フレームで3D描画が行われたか
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }

	/// @brief フレーム描画を開始する
	/// @param clearColor 画面クリア色
	void beginFrame(const sgc::Colorf& clearColor) override;

	/// @brief カメラを設定する
	/// @param camera 3Dカメラ
	/// @details DX11 でも projection を **DX 規約 Z[0,1]** にする。
	///          sgc::Mat4f::perspective は OpenGL 規約 (Z[-1,1]) のため、
	///          DX で使うと near 側半分の depth が clip され precision が半減する。
	///          camera の fov / aspect / near / far を使い直して DX 友好的な
	///          RH perspective を組む。view は sgc RH のまま。
	void setCamera(const Camera3D& camera) override
	{
		m_viewMatrix = camera.viewMatrix();
		m_projMatrix = makePerspectiveRH_ZO_DX(
			camera.fov(), camera.aspectRatio(),
			camera.nearClip(), camera.farClip());
		m_cameraPosition = camera.position();
	}

	/// @brief DX 用 RH perspective (Z[0,1])
	/// @details column-major で GPU に流すと前提で sgc 行レイアウトに合わせて作る。
	[[nodiscard]] static sgc::Mat4f makePerspectiveRH_ZO_DX(
		float fovY, float aspect, float nearZ, float farZ) noexcept
	{
		const float tanHalf = std::tan(fovY * 0.5f);
		const float range   = nearZ - farZ;   // < 0
		// 標準 RH-ZO 透視行列（OpenGL gluPerspective を DX Z[0,1] に書き換えたもの）
		return {
			1.0f / (aspect * tanHalf), 0.0f,            0.0f,                0.0f,
			0.0f,                      1.0f / tanHalf,  0.0f,                0.0f,
			0.0f,                      0.0f,            farZ / range,        (farZ * nearZ) / range,
			0.0f,                      0.0f,           -1.0f,                0.0f,
		};
	}

	/// @brief ディレクショナルライトを設定する
	/// @param light ライトデータ
	void setLight(const Light& light) override
	{
		m_light = light;
	}

	/// @brief 複数ライトを設定する
	/// @param lights ライト配列（kMaxLights を超える分は切り詰め）
	/// @details `setUseMultiLight(true)` 時は GPU 側 b2 にこのライト配列が
	///          毎フレーム drawMesh で送られる。`setUseMultiLight(false)` 時
	///          （デフォルト）は `m_light = lights[0]` の後方互換動作のみ。
	void setLights(std::span<const Light> lights) override
	{
		m_lights.assign(lights.begin(), lights.end());
		if (m_lights.size() > static_cast<std::size_t>(IRenderer3D::kMaxLights))
		{
			m_lights.resize(IRenderer3D::kMaxLights);
		}
		if (!m_lights.empty())
		{
			m_light = m_lights.front();
		}
	}

	/// @brief 直近に設定されたライト配列を取得する
	[[nodiscard]] std::span<const Light> lights() const noexcept
	{
		return { m_lights.data(), m_lights.size() };
	}

	/// @brief マルチライト経路を有効/無効にする
	/// @details `true` にすると Phong シェーダーが MULTI_LIGHT 変種に切り替わり、
	///          b2 = `CbLightArray` を消費するようになる。Toon / Unlit / 他の
	///          NPR モードは現状マルチライト未対応で、本フラグは無視される。
	///          初期化済み状態で値が変わった場合のみ recompileShaders を呼ぶ。
	void setUseMultiLight(bool useMulti) override
	{
		if (m_useMultiLight == useMulti) return;
		m_useMultiLight = useMulti;
		if (m_initialized)
		{
			recompileShaders();
		}
	}

	/// @brief 現在マルチライト経路かを返す
	[[nodiscard]] bool useMultiLight() const noexcept override { return m_useMultiLight; }

	/// @brief キューブマップ skybox をセットする (DX11)
	void setSkybox(const Cubemap& cubemap) override
	{
		m_skyboxImpl.setCubemap(cubemap);
		m_skyboxNeedsInit = cubemap.valid();
		if (cubemap.valid())
		{
			m_skyboxEnabled = true;
		}
	}

	/// @brief skybox 描画の有効/無効
	void setSkyboxEnabled(bool enabled) override
	{
		m_skyboxEnabled = enabled;
	}

	/// @brief skybox 描画が有効か
	[[nodiscard]] bool isSkyboxEnabled() const noexcept override
	{
		return m_skyboxEnabled && m_skyboxImpl.hasValidCubemap();
	}

	/// @brief 内部の DX11 デバイスを返す（拡張機能用 — Skybox など）
	/// @details Skybox / IBL / experimental エフェクトのように Renderer3D の
	///          外で DX11 リソースを直接構築する consumer 向け。
	///          `initialize()` 前は nullptr。
	[[nodiscard]] gfx::Dx11Device* getDx11Device() const noexcept { return m_device; }

	/// @brief 内部の ID3D11DeviceContext を返す（拡張機能用）
	/// @details ID3D11DeviceContext を直接触る必要があるとき（例: Skybox の
	///          drawDx11 にコンテキストを渡す）に使う。`beginFrame()` で
	///          RTV/DSV を設定済みの context を返すため、追加の draw を
	///          そのままインジェクトできる。
	[[nodiscard]] ID3D11DeviceContext* getD3DContext() const noexcept { return m_d3dContext; }

	/// @brief シーンのアンビエント色を設定する
	/// @param color RGB アンビエント色
	void setAmbientColor(const sgc::Colorf& color) override
	{
		m_sceneAmbient = color;
	}

	/// @brief 現在のシーンアンビエント色を返す
	[[nodiscard]] sgc::Colorf ambientColor() const noexcept override
	{
		return m_sceneAmbient;
	}

	/// @brief テクスチャを設定する
	/// @param tex 適用するテクスチャ
	void setTexture(const Texture& tex)
	{
		if (!tex.valid())
		{
			return;
		}
		uploadTexture(tex);
	}

	/// @brief テクスチャをクリアしてデフォルト（白）に戻す
	void clearTexture()
	{
		m_currentSRV = m_defaultWhiteSRV;
	}

	/// @brief メッシュを描画する
	/// @param mesh 描画するメッシュ
	/// @param worldTransform ワールド変換行列
	/// @param material マテリアル
	void drawMesh(const Mesh& mesh,
	              const sgc::Mat4f& worldTransform,
	              const Material& material) override;

	/// @brief フレーム描画を終了する
	/// @note アウトライン描画には ToonPipeline を使う。
	void endFrame() override
	{
		m_outlineQueue.clear();
	}

private:
	// ===== 実装本体は detail/Renderer3D_Setup_impl.hpp / detail/Renderer3D_Draw_impl.hpp =====

	/// @brief HLSLシェーダーをコンパイルする
	void compileShaders();

	/// @brief シェーダーを再コンパイルする（シェーダーモード変更時に呼び出される）
	void recompileShaders();

	/// @brief HLSL文字列をコンパイルする
	[[nodiscard]] ComPtr<ID3DBlob> compileHLSL(
		const char* source,
		const char* entryPoint,
		const char* target);

	/// @brief Vertex3D用の入力レイアウトを作成する
	void createInputLayout();

	/// @brief 定数バッファを作成する
	void createConstantBuffers();

	/// @brief マルチライト CB を更新して b2 にバインドする
	void updateLightArrayCB();

	/// @brief 深度バッファを作成する
	void createDepthBuffer();

	/// @brief ラスタライザステートを作成する
	void createRasterizerState();

	/// @brief 深度ステンシルステートを作成する
	void createDepthStencilState();

	/// @brief トランスフォーム定数バッファを更新する
	void updateTransformCB(const sgc::Mat4f& worldTransform);

	/// @brief ライティング定数バッファを更新する
	void updateLightingCB(const Material& material);

	/// @brief 動的頂点バッファを作成する
	[[nodiscard]] ComPtr<ID3D11Buffer> createDynamicVertexBuffer(
		const void* data, UINT sizeBytes);

	/// @brief 動的インデックスバッファを作成する
	[[nodiscard]] ComPtr<ID3D11Buffer> createDynamicIndexBuffer(
		const void* data, UINT sizeBytes);

	/// @brief テクスチャデータをGPUにアップロードする
	void uploadTexture(const Texture& tex);

	/// @brief Material.albedoTexture 用の SRV を取得（必要なら upload + cache）
	[[nodiscard]] ID3D11ShaderResourceView* getOrUploadAlbedoSrv(const Texture* tex);

	/// @brief デフォルトの1x1白テクスチャを作成する
	void createDefaultWhiteTexture();

	/// @brief テクスチャサンプラーステートを作成する
	void createSamplerState();

	/// @brief アウトラインパス（drawMesh内から呼ばれる、メイン描画の前に実行）
	void drawOutlinePass(const Mesh& mesh, const sgc::Mat4f& worldTransform);

	/// @brief アウトライン描画（旧API、互換用）
	void drawMeshOutline(const Mesh& mesh, const sgc::Mat4f& worldTransform);

	/// @brief DX11デバイス（非所有）
	gfx::Dx11Device* m_device = nullptr;
	/// @brief D3D11デバイス（非所有）
	ID3D11Device* m_d3dDevice = nullptr;
	/// @brief D3D11即時コンテキスト（非所有）
	ID3D11DeviceContext* m_d3dContext = nullptr;

	/// @brief 頂点シェーダー
	ComPtr<ID3D11VertexShader> m_vertexShader;
	/// @brief ピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_pixelShader;
	/// @brief VSバイトコード（InputLayout用）
	std::vector<uint8_t> m_vsBytecode;
	/// @brief 入力レイアウト
	ComPtr<ID3D11InputLayout> m_inputLayout;

	/// @brief トランスフォーム定数バッファ
	ComPtr<ID3D11Buffer> m_cbTransform;
	/// @brief ライティング定数バッファ
	ComPtr<ID3D11Buffer> m_cbLighting;
	/// @brief マルチライト定数バッファ (b2)
	ComPtr<ID3D11Buffer> m_cbLightArray;

	/// @brief 深度ステンシルビュー
	ComPtr<ID3D11DepthStencilView> m_depthStencilView;
	/// @brief 深度ステンシルステート
	ComPtr<ID3D11DepthStencilState> m_depthStencilState;
	/// @brief ラスタライザステート
	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	/// @brief ブレンドステート
	ComPtr<ID3D11BlendState> m_blendState;

	/// @brief アウトライン用頂点シェーダー
	ComPtr<ID3D11VertexShader> m_outlineVS;
	/// @brief アウトライン用ピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_outlinePS;
	/// @brief アウトライン用入力レイアウト
	ComPtr<ID3D11InputLayout> m_outlineInputLayout;
	/// @brief アウトライン用フロントフェースカリングラスタライザ
	ComPtr<ID3D11RasterizerState> m_outlineFrontCull;

	/// @brief デフォルト1x1白テクスチャSRV
	ComPtr<ID3D11ShaderResourceView> m_defaultWhiteSRV;

	/// Material.albedoTexture からの per-Texture* SRV キャッシュ（DX11）
	std::unordered_map<const Texture*, ComPtr<ID3D11ShaderResourceView>> m_albedoSrvCache;
	/// @brief 現在バインドされているテクスチャSRV
	ComPtr<ID3D11ShaderResourceView> m_currentSRV;
	/// @brief テクスチャサンプラーステート
	ComPtr<ID3D11SamplerState> m_samplerState;

	/// @brief ビュー行列
	sgc::Mat4f m_viewMatrix;
	/// @brief 射影行列
	sgc::Mat4f m_projMatrix;
	/// @brief カメラ位置
	sgc::Vec3f m_cameraPosition;
	/// @brief アクティブライト（後方互換の単一光源）
	Light m_light;

	/// @brief マルチライト経路用のライト配列（最大 kMaxLights）
	std::vector<Light> m_lights;

	/// @brief シーンアンビエント色（initialize 時に config.defaultAmbient で初期化）
	sgc::Colorf m_sceneAmbient{0.15f, 0.15f, 0.15f, 1.0f};

	/// @brief マルチライト経路フラグ（false = 単一光 Phong、true = b2 を消費）
	bool m_useMultiLight = false;

	/// @brief Skybox 実装（CPU+GPU リソースを持つ）
	Skybox m_skyboxImpl;
	/// @brief Skybox 描画フラグ
	bool m_skyboxEnabled = false;
	/// @brief 次フレーム以降に initializeDx11 を呼ぶ必要があるか
	bool m_skyboxNeedsInit = false;
	/// @brief このフレーム内で skybox を既に描画したか
	bool m_skyboxDrawnThisFrame = false;

	/// @brief skybox を描画する（drawMesh の最初の呼び出しで一度だけ）
	void drawSkyboxIfNeeded();

#endif // _WIN32

	/// @brief 描画状態
	RenderState3D m_renderState;
	/// @brief ラスタライザ再作成フラグ
	bool m_rasterizerDirty = false;
	/// @brief 設定
	Renderer3DConfig m_config;
	/// @brief シェーダーモード
	ShaderMode3D m_shaderMode = ShaderMode3D::Phong;

	/// @brief アウトライン描画キュー（endFrameで一括描画）
	struct OutlineDrawCommand
	{
		const Mesh* mesh;
		sgc::Mat4f worldTransform;
	};
	std::vector<OutlineDrawCommand> m_outlineQueue;
	/// @brief 初期化済みフラグ
	bool m_initialized = false;
	bool m_frameActive = false;  ///< 今フレームでbeginFrame()が呼ばれたか
	/// @brief ドローコール数
	int m_drawCallCount = 0;
};

} // namespace mitiru::render

// 実装本体（core/Screen.hpp と同じ末尾 detail include 流儀）
#include <mitiru/render/detail/Renderer3D_Setup_impl.hpp>
#include <mitiru/render/detail/Renderer3D_Draw_impl.hpp>
