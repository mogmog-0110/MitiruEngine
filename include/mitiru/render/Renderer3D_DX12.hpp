#pragma once

/// @file Renderer3D_DX12.hpp
/// @brief DirectX 12ベース3Dレンダラー
/// @details Pipeline State Object (PSO) ベースの3D描画を提供する。
///          トゥーンシェーディング + アウトラインの2パスレンダリングを行い、
///          PSO切り替えによる安全でアトミックなステート管理を実現する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12Shader.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/ToonShaders3D.hpp>
#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/LightArrayCB.hpp>
#include <mitiru/render/SkyboxShaders.hpp>

#include <mitiru/render/Shadow.hpp>

#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/dx12/DX12FXAAShaders.hpp>
#include <mitiru/render/dx12/DX12MultiLightShaders.hpp>
#include <mitiru/render/dx12/DX12Tonemap.hpp>
#include <mitiru/render/dx12/DX12ShaderModePS.hpp>
#include <mitiru/render/dx12/DX12ShaderModeVS.hpp>
#include <mitiru/render/dx12/DX12Shaders.hpp>
#include <mitiru/render/dx12/Dx12ShadowMap.hpp>
#include <mitiru/render/dx12/Dx12TextureUpload.hpp>
#include <mitiru/render/dx12/Dx12UploadRing.hpp>

namespace mitiru::render
{

// OutlineMode enum と OUTLINE_MODE_COUNT は IRenderer3D.hpp で定義済み

// ─────────────────────────────────────────────────────────────
//  Renderer3D_DX12 本体
// ─────────────────────────────────────────────────────────────

/// @brief DirectX 12ベース3Dレンダラー
/// @details PSO（Pipeline State Object）によるステート管理で、
///          トゥーンシェーディング + アウトラインの2パスレンダリングを行う。
///
/// @code
/// Renderer3D_DX12 renderer;
/// renderer.initialize(&dx12Device);
///
/// renderer.beginFrame(sgc::Colorf{0.2f, 0.2f, 0.3f, 1.0f});
/// renderer.setCamera(camera);
/// renderer.setLight(sunLight);
/// renderer.drawMesh(cubeMesh, worldMatrix, material);
/// renderer.endFrame();
/// @endcode
class Renderer3D_DX12 : public IRenderer3D
{
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// @brief デフォルトコンストラクタ
	Renderer3D_DX12() {}

	/// @brief デストラクタ
	~Renderer3D_DX12()
	{
		destroy();
	}

	/// コピー禁止
	Renderer3D_DX12(const Renderer3D_DX12&) = delete;
	Renderer3D_DX12& operator=(const Renderer3D_DX12&) = delete;

	/// ムーブ禁止（内部リソースがthisを参照する可能性）
	Renderer3D_DX12(Renderer3D_DX12&&) noexcept = default;
	Renderer3D_DX12& operator=(Renderer3D_DX12&&) noexcept = default;

	/// @brief レンダラー設定
	struct Config
	{
		float viewportWidth = 1280.0f;                        ///< ビューポート幅
		float viewportHeight = 720.0f;                        ///< ビューポート高さ
		sgc::Colorf defaultAmbient{0.5f, 0.5f, 0.5f, 1.0f};  ///< デフォルトアンビエント色
		bool enableOutline = true;                             ///< アウトライン描画の有効化
		float outlineThickness = 0.03f;                        ///< アウトラインの太さ
	};

	/// @brief レンダラーを初期化する
	/// @param device Dx12Deviceへのポインタ（外部で管理・ライフタイム保証）
	/// @param cfg レンダラー設定
	void initialize(gfx::Dx12Device* device, const Config& cfg = {});

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept override
	{
		return m_initialized;
	}

	/// @brief ビューポートサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(float width, float height);

	/// @brief フレーム開始処理
	/// @param clearColor バックバッファのクリア色
	void beginFrame(const sgc::Colorf& clearColor = {0.2f, 0.2f, 0.3f, 1.0f}) override;

	/// @brief カメラを設定する
	/// @param camera 3Dカメラ
	void setCamera(const Camera3D& camera) override;

	/// @brief ライトを設定する
	/// @param light ライト情報
	void setLight(const Light& light) override
	{
		m_light = light;
	}

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

	/// @brief メッシュを描画する
	/// @param mesh 描画対象メッシュ
	/// @param worldTransform ワールド変換行列
	/// @param material マテリアル
	void drawMesh(const Mesh& mesh,
	              const sgc::Mat4f& worldTransform,
	              const Material& material) override;

	/// @brief フレーム終了処理（アウトラインパス + バリア + コマンド実行）
	void endFrame() override;

	/// @brief コマンドリストを閉じてGPU実行する（Engine::endFrame前に呼ぶ）
	/// @details endFrame()で3D描画完了後、ImGui描画を追記してからこれを呼ぶ。
	void finalizeFrame();

	/// @brief 現在のフレームの描画コール数を返す
	[[nodiscard]] int drawCallCount() const noexcept override
	{
		return m_drawCallCount;
	}

	/// @brief アウトライン描画の有効/無効を設定する
	void setOutlineEnabled(bool enabled) noexcept override
	{
		m_config.enableOutline = enabled;
	}

	/// @brief アウトライン描画が有効かどうかを返す
	[[nodiscard]] bool isOutlineEnabled() const noexcept override
	{
		return m_config.enableOutline;
	}

	/// @brief アウトラインモードを設定する
	/// @param mode 使用するアウトラインモード
	void setOutlineMode(OutlineMode mode) noexcept override
	{
		m_outlineMode = mode;
	}

	/// @brief 現在のアウトラインモードを返す
	[[nodiscard]] OutlineMode outlineMode() const noexcept override
	{
		return m_outlineMode;
	}

	/// @brief tonemap exposure を設定する (ENG-106)
	void setTonemapExposure(float exposure) override
	{
		m_tonemapExposure = (exposure > 0.0f) ? exposure : 1.0f;
	}

	/// @brief 現在の tonemap exposure を返す
	[[nodiscard]] float tonemapExposure() const noexcept override
	{
		return m_tonemapExposure;
	}

	/// @brief tonemap gamma を設定する (ENG-106)
	void setTonemapGamma(float gamma) override
	{
		m_tonemapGamma = (gamma > 0.0f) ? gamma : 2.2f;
	}

	/// @brief 現在の tonemap gamma を返す
	[[nodiscard]] float tonemapGamma() const noexcept override
	{
		return m_tonemapGamma;
	}

	/// @brief FXAA ポストプロセス AA の有効/無効を切り替える (ENG-104)
	void setFXAAEnabled(bool enabled) noexcept
	{
		m_fxaaEnabled = enabled;
	}

	/// @brief FXAA ポストプロセス AA が有効かどうか
	[[nodiscard]] bool isFXAAEnabled() const noexcept
	{
		return m_fxaaEnabled;
	}

	/// @brief FXAA の品質パラメータを設定する
	/// @param subpixQuality      サブピクセル AA 強度 (0.0-1.0、default 0.75)
	/// @param edgeThreshold      エッジ検出閾値 (default 0.166)
	/// @param edgeThresholdMin   最小エッジ閾値 (default 0.0833)
	/// @details Low プリセット: 0.50 / 0.250 / 0.0833
	///          Medium プリセット (default): 0.75 / 0.166 / 0.0833
	///          High プリセット: 1.00 / 0.063 / 0.0312
	void setFXAAQuality(float subpixQuality,
	                    float edgeThreshold,
	                    float edgeThresholdMin) noexcept
	{
		m_fxaaSubpixQuality    = subpixQuality;
		m_fxaaEdgeThreshold    = edgeThreshold;
		m_fxaaEdgeThresholdMin = edgeThresholdMin;
	}

	// ─────────────────────────────────────────────────────────
	//  外部アクセス用API（カスタムアウトラインパス等で使用）
	// ─────────────────────────────────────────────────────────

	/// @brief グラフィクスコマンドリストを取得する
	[[nodiscard]] ID3D12GraphicsCommandList* getCommandList() noexcept
	{
		return m_graphicsCmdList.Get();
	}

	/// @brief ネイティブD3D12デバイスを取得する
	[[nodiscard]] ID3D12Device* getNativeDevice() noexcept
	{
		return m_d3dDevice;
	}

	/// @brief Dx12Deviceを取得する
	[[nodiscard]] gfx::Dx12Device* getDx12Device() noexcept
	{
		return m_device;
	}

	/// @brief 深度バッファリソースを取得する
	[[nodiscard]] ID3D12Resource* getDepthBuffer() noexcept
	{
		return m_depthBuffer.Get();
	}

	/// @brief 法線バッファリソースを取得する
	[[nodiscard]] ID3D12Resource* getNormalBuffer() noexcept
	{
		return m_normalBuffer.Get();
	}

	/// @brief メインルートシグネチャを取得する
	[[nodiscard]] ID3D12RootSignature* getMainRootSignature() noexcept
	{
		return m_rootSignature.Get();
	}

	/// @brief ポストプロセスアウトライン用ルートシグネチャを取得する
	[[nodiscard]] ID3D12RootSignature* getOutlinePostRootSig() noexcept
	{
		return m_outlinePostRootSig.Get();
	}

	/// @brief 深度SRVヒープを取得する
	[[nodiscard]] ID3D12DescriptorHeap* getDepthSRVHeap() noexcept
	{
		return m_depthSRVHeap.Get();
	}

	/// @brief アップロードバッファを生成する（外部パス用）
	[[nodiscard]] ComPtr<ID3D12Resource> createUploadBufferPublic(UINT64 sizeBytes) const
	{
		return createUploadBuffer(sizeBytes);
	}

	/// @brief 一時リソースを現在のフレームに追加する（フレーム終了まで保持）
	void keepTempResource(ComPtr<ID3D12Resource> resource)
	{
		m_frameTempResources.push_back(std::move(resource));
	}

	/// @brief メインPSOとルートシグネチャに戻す
	void restoreMainState();

	/// @brief Vertex3D用の入力レイアウトを取得する（外部PSO作成用）
	/// @param desc 出力先の配列（4要素）
	/// @param count 出力先の要素数
	static void getInputLayout(D3D12_INPUT_ELEMENT_DESC* desc, UINT& count)
	{
		getInputLayoutInternal(desc, count);
	}

	/// @brief リソースを破棄する
	void destroy();

private:
	/// @brief トリプルバッファリングのフレーム数
	static constexpr uint32_t FRAME_COUNT = 3;

	// ─────────────────────────────────────────────────────────
	//  PSO生成・リソース生成・描画ヘルパー（別ファイルに分離）
	//  NOTE: これは class body 内への意図的な .inl include である。
	//  DX12PipelineStates.inl は Renderer3D_DX12 の private member function を
	//  宣言しており、class scope にアクセスするためここで include する必要が
	//  ある。この include を class 宣言の外に移動してはいけない。
	// ─────────────────────────────────────────────────────────

	// NOLINTNEXTLINE(google-build-namespaces) — intentional in-class .inl include
	#include <mitiru/render/dx12/DX12PipelineStates.hpp> // NOLINT(build/include)

	// skybox 実装も同じパターンで分離（DX11 と機能パリティ）
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Skybox.hpp> // NOLINT(build/include)

	// ─────────────────────────────────────────────────────────
	//  メンバ変数
	// ─────────────────────────────────────────────────────────

	/// 初期化フラグ
	bool m_initialized = false;

	/// 設定
	Config m_config;

	/// デバイス参照（外部所有）
	gfx::Dx12Device* m_device = nullptr;
	ID3D12Device* m_d3dDevice = nullptr;

	/// コマンドリソース（レンダラー専用）
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList> m_graphicsCmdList;

	/// ルートシグネチャ
	ComPtr<ID3D12RootSignature> m_rootSignature;

	/// PSO（Pipeline State Objects）
	ComPtr<ID3D12PipelineState> m_mainPSO;           ///< メイン（トゥーン）PSO
	ComPtr<ID3D12PipelineState> m_outlinePSO;         ///< アウトラインPSO（背面膨張・未使用）
	ComPtr<ID3D12PipelineState> m_outlinePostPSO;     ///< ポストプロセスアウトラインPSO（モード0）
	ComPtr<ID3D12PipelineState> m_outlinePostPSOs[OUTLINE_MODE_COUNT]; ///< モード別PSOスロット(1-4)
	ComPtr<ID3D12PipelineState> m_fresnelMainPSO;    ///< Fresnel付きメインPSO（モード5）
	ComPtr<ID3D12RootSignature> m_outlinePostRootSig; ///< ポストプロセス用ルートシグネチャ

	/// アウトラインモード
	OutlineMode m_outlineMode = OutlineMode::DepthSobel;

	/// 色バッファコピー用リソース（モード3,4で使用）
	ComPtr<ID3D12Resource> m_colorCopyBuffer;
	ComPtr<ID3D12DescriptorHeap> m_colorEdgeSRVHeap;   ///< モード3用: [色,法線,dummy]
	ComPtr<ID3D12DescriptorHeap> m_depthColorSRVHeap;  ///< モード4用: [深度,法線,色]

	// ─── MSAA リソース (ENG-105 v2) ────────────────────────────
	// 4x MSAA で MRT (color + normal + depth) を multisample 描画し、
	// outline / FXAA 前に backbuffer に Resolve する。
	// depth/normal の resource format は TYPELESS にして DSV/RTV と SRV の
	// 両方から異なる typed view を作れるようにする (v1 が壊れた原因の 1 つ
	// として疑った format 強指定を回避)。
	static constexpr UINT MSAA_SAMPLE_COUNT = 4;
	ComPtr<ID3D12Resource>       m_msaaColorBuffer;   ///< 4x MSAA color RT (ENG-106: FP16)
	ComPtr<ID3D12DescriptorHeap> m_msaaColorRtvHeap;  ///< 上記の RTV ヒープ

	/// HDR intermediate (ENG-106) — single-sample FP16. MSAA color の Resolve
	/// 先で、tonemap PS が SRV としてサンプリングして backbuffer に焼く。
	ComPtr<ID3D12Resource>       m_hdrIntermediateBuffer;
	ComPtr<ID3D12DescriptorHeap> m_hdrIntermediateRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_hdrIntermediateSrvHeap;

	/// Tonemap pass (ENG-106) — HDR FP16 → backbuffer LDR R8G8B8A8。
	/// ACES filmic curve + exposure + gamma 2.2。
	std::optional<gfx::Dx12Shader> m_tonemapVS;
	std::optional<gfx::Dx12Shader> m_tonemapPS;
	ComPtr<ID3D12RootSignature>    m_tonemapRootSig;
	ComPtr<ID3D12PipelineState>    m_tonemapPSO;
	float                          m_tonemapExposure = 1.0f;
	float                          m_tonemapGamma    = 2.2f;

	/// D3D12 InfoQueue (debug layer 用)。Debug build かつデバッグ層有効時のみ
	/// 検証メッセージを溜める。pollD3D12Validation() で毎フレーム読み出し、
	/// ERROR / CORRUPTION 級だけ mitiru_d3d12_runtime.log に append する。
	ComPtr<ID3D12InfoQueue>      m_infoQueue;
	std::uint64_t                m_frameCounter = 0;  ///< validation log の frame 番号

	/// FXAA ポストプロセス (ENG-104)。outline 描画後・overlay2D 描画前に走らせて
	/// シーン色のジャギーを近似 AA する。intermediate に backbuffer を copy して
	/// 自分自身を read/write する読み書き競合を回避。
	ComPtr<ID3D12PipelineState> m_fxaaPSO;
	ComPtr<ID3D12RootSignature> m_fxaaRootSig;
	ComPtr<ID3D12Resource>      m_fxaaIntermediate;     ///< backbuffer サイズの色コピー
	ComPtr<ID3D12DescriptorHeap> m_fxaaSrvHeap;         ///< shader-visible: t0 = intermediate
	std::optional<gfx::Dx12Shader> m_fxaaPS;
	bool  m_fxaaEnabled         = true;                 ///< default ON; setFXAAEnabled で切り替え可
	float m_fxaaSubpixQuality   = 0.75f;                ///< FXAA 3.11 sub-pixel AA 強度
	float m_fxaaEdgeThreshold   = 0.166f;
	float m_fxaaEdgeThresholdMin = 0.0833f;

	/// コンパイル済みシェーダー
	std::optional<gfx::Dx12Shader> m_toonVS;
	std::optional<gfx::Dx12Shader> m_toonPS;
	std::optional<gfx::Dx12Shader> m_outlineVS;
	std::optional<gfx::Dx12Shader> m_outlinePS;
	std::optional<gfx::Dx12Shader> m_outlinePostVS;
	std::optional<gfx::Dx12Shader> m_outlinePostPS;
	std::optional<gfx::Dx12Shader> m_outlinePostPS_Laplacian;   ///< モード1
	std::optional<gfx::Dx12Shader> m_outlinePostPS_DepthNdotV;  ///< モード2
	std::optional<gfx::Dx12Shader> m_outlinePostPS_ColorEdge;   ///< モード3
	std::optional<gfx::Dx12Shader> m_outlinePostPS_DepthColor;  ///< モード4
	std::optional<gfx::Dx12Shader> m_fresnelToonPS;             ///< モード5

	/// 深度バッファ
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_depthSRVHeap;  ///< 深度バッファSRV用ヒープ

	/// 法線バッファ（MRT RT1）
	ComPtr<ID3D12Resource> m_normalBuffer;
	ComPtr<ID3D12DescriptorHeap> m_normalRTVHeap;  ///< 法線RT用RTVヒープ

	/// メッシュバッファキャッシュ（毎フレーム再生成を防止）
	struct CachedBuffer
	{
		ComPtr<ID3D12Resource> resource;
		UINT size = 0;
	};
	std::unordered_map<const void*, CachedBuffer> m_meshVBCache; ///< 頂点バッファキャッシュ
	std::unordered_map<const void*, CachedBuffer> m_meshIBCache; ///< インデックスバッファキャッシュ

	/// Per-frame UPLOAD ヒープリング — drawMesh の transient CB/VB/IB を集約
	dx12::Dx12UploadRing m_uploadRing;

	/// フレーム内の一時アップロードバッファ（定数バッファ含む）
	std::vector<ComPtr<ID3D12Resource>> m_frameTempResources;
	std::vector<ComPtr<ID3D12Resource>> m_perFrameTempResources[FRAME_COUNT]; ///< フレーム毎の一時リソース保持

	/// カメラ状態（glm形式、toHLSL変換用）
	glm::mat4 m_viewMatrix{1.0f};
	glm::mat4 m_projMatrix{1.0f};
	sgc::Vec3f m_cameraPosition{};

	/// ライト状態
	Light m_light;

	/// シーンアンビエント色（initialize 時に config.defaultAmbient で初期化）
	sgc::Colorf m_sceneAmbient{0.5f, 0.5f, 0.5f, 1.0f};

	/// アウトラインパス用の遅延描画コマンド
	std::vector<OutlineDrawCommand> m_outlineCommands;

	/// 2Dオーバーレイ
	ComPtr<ID3D12PipelineState> m_overlay2DPSO;       ///< 2Dオーバーレイ用PSO
	ComPtr<ID3D12RootSignature> m_overlay2DRootSig;   ///< 2Dオーバーレイ用ルートシグネチャ
	std::optional<gfx::Dx12Shader> m_overlay2DVS;     ///< 2Dオーバーレイ用頂点シェーダー
	std::optional<gfx::Dx12Shader> m_overlay2DPS;     ///< 2Dオーバーレイ用ピクセルシェーダー
	const Screen* m_overlayScreen = nullptr;           ///< 2Dオーバーレイ用Screen（非所有）

	/// 描画統計
	int m_drawCallCount = 0;
	bool m_frameActive = false;  ///< このフレームでbeginFrame()が呼ばれたか
	bool m_needsFinalize = false; ///< endFrame後、finalizeFrame待ち

	/// ── マルチライト（DX11 と機能パリティ）──────────────────
	std::vector<Light>                      m_lights;          ///< setLights で蓄積
	bool                                    m_useMultiLight = false;
	std::optional<gfx::Dx12Shader>          m_multiLightPS;     ///< b2 を読む Phong PS
	ComPtr<ID3D12PipelineState>             m_multiLightPSO;    ///< 同 PSO（メインと同 root sig）

	/// ── ShaderMode (DX11 と機能パリティ) ───────────────────
	/// setShaderMode で切替。未実装モードは Toon フォールバック。
	ShaderMode3D m_shaderMode = ShaderMode3D::Toon;
	std::optional<gfx::Dx12Shader> m_phongPS;
	std::optional<gfx::Dx12Shader> m_unlitPS;
	std::optional<gfx::Dx12Shader> m_flatPS;
	ComPtr<ID3D12PipelineState>    m_phongPSO;
	ComPtr<ID3D12PipelineState>    m_unlitPSO;
	ComPtr<ID3D12PipelineState>    m_flatPSO;

	/// ── 指向性シャドウマップ ──────────────────────────────
	DirectionalShadow         m_directionalShadow;
	dx12::Dx12ShadowMap       m_shadowMap;
	bool                      m_shadowEnabled = false;
	bool                      m_shadowDrawnThisFrame = false;
	ComPtr<ID3D12PipelineState> m_shadowPSO;  ///< depth-only PSO (PS なし)
	std::optional<gfx::Dx12Shader> m_shadowVS; ///< shadow パス用 VS（メインと同じ）

	struct ShadowCaster {
		const Mesh* mesh = nullptr;
		sgc::Mat4f  world;
	};
	std::vector<ShadowCaster> m_shadowCommands;       ///< 当フレーム描画分
	std::vector<ShadowCaster> m_shadowCommandsPrev;   ///< 前フレーム — shadow pass で使う

	/// ── アルベドテクスチャ（material.albedoTexture）─────────
	/// shader-visible SRV ヒープ。1 フレームの drawMesh 数だけ SRV を append し、
	/// beginFrame で cursor = 0 にリセット。
	ComPtr<ID3D12DescriptorHeap>                 m_albedoSrvHeap;
	UINT                                         m_albedoSrvCapacity  = 0;
	UINT                                         m_albedoSrvCursor    = 0;
	UINT                                         m_albedoSrvIncrement = 0;
	dx12::Dx12Texture2D                          m_defaultWhiteTexture;
	bool                                         m_defaultWhiteReady  = false;
	std::unordered_map<const Texture*, std::unique_ptr<dx12::Dx12Texture2D>> m_textureCache;

	/// ── Skybox（DX11 と機能パリティ）─────────────────────────
	Cubemap                     m_skyboxCubemap;
	bool                        m_skyboxEnabled         = false;
	bool                        m_skyboxPipelineReady   = false; ///< PSO/RootSig/VB/IB/CB
	bool                        m_skyboxTextureReady    = false; ///< TextureCube/Upload/SRV
	bool                        m_skyboxNeedsUpload     = false;
	bool                        m_skyboxTextureInPSR    = false; ///< テクスチャが PIXEL_SHADER_RESOURCE 状態か
	bool                        m_skyboxDrawnThisFrame  = false;
	UINT                        m_skyboxFaceStride      = 0;
	UINT                        m_skyboxAlignedRow      = 0;
	int                         m_skyboxFaceSize        = 0;
	ComPtr<ID3D12Resource>      m_skyboxTexture;       ///< default-heap TextureCube
	ComPtr<ID3D12Resource>      m_skyboxUpload;        ///< upload-heap (6 face)
	ComPtr<ID3D12DescriptorHeap> m_skyboxSrvHeap;      ///< 1 SRV (shader-visible)
	ComPtr<ID3D12RootSignature> m_skyboxRootSig;       ///< skybox 専用 root sig
	ComPtr<ID3D12PipelineState> m_skyboxPSO;           ///< skybox 専用 PSO
	ComPtr<ID3D12Resource>      m_skyboxVB;            ///< cube vertex buffer
	ComPtr<ID3D12Resource>      m_skyboxIB;            ///< cube index buffer
	ComPtr<ID3D12Resource>      m_skyboxCb;            ///< CbSkyTransform
public:
	/// @brief このフレームで3D描画が行われたかを返す
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }
	/// @brief フレームアクティブフラグをリセットする（Engine側で毎フレーム呼ぶ）
	void resetFrameActive() noexcept override { m_frameActive = false; }

	/// @brief 2Dオーバーレイ用のScreen参照を設定する
	/// @param screen Screenへのポインタ（nullptrで解除）
	/// @details endFrame()でバックバッファ上に2D HUD/UIを描画するために使用する。
	void setOverlayScreen(const Screen* screen) noexcept override { m_overlayScreen = screen; }

	/// @brief 複数ライトを設定する（DX12）
	/// @details kMaxLights を超える分は捨てる。useMultiLight=true の時のみ
	///          drawMesh で b2 にアップロードされる。
	void setLights(std::span<const Light> lights) override;

	/// @brief マルチライト経路の有効化（DX12）
	void setUseMultiLight(bool useMulti) override
	{
		m_useMultiLight = useMulti;
	}

	/// @brief マルチライト経路が有効か
	[[nodiscard]] bool useMultiLight() const noexcept override
	{
		return m_useMultiLight;
	}

	/// @brief シェーダーモードを設定する（DX12）
	/// @details Toon / Phong / Unlit / Flat を実装。他モードは Toon フォールバック。
	///          useMultiLight=true の時は ShaderMode に関わらず multi-light Phong PSO 優先。
	void setShaderMode(ShaderMode3D mode) override
	{
		m_shaderMode = mode;
	}

	/// @brief 現在のシェーダーモード
	[[nodiscard]] ShaderMode3D shaderMode() const noexcept
	{
		return m_shaderMode;
	}

	/// @brief シャドウマップを有効/無効にする（DX12）
	void setShadowEnabled(bool enabled) noexcept { m_shadowEnabled = enabled; }

	/// @brief シャドウマップが有効か
	[[nodiscard]] bool isShadowEnabled() const noexcept { return m_shadowEnabled; }

	/// @brief シャドウのライト方向を設定する
	void setShadowDirection(const sgc::Vec3f& dir) noexcept
	{
		m_directionalShadow.setLightDirection(dir);
	}

	/// @brief シャドウ設定への参照（mapSize / orthoHalfExtent 等の調整用）
	[[nodiscard]] DirectionalShadowConfig& shadowConfig() noexcept
	{
		return m_directionalShadow.config();
	}

	/// @brief シャドウ設定への const 参照
	[[nodiscard]] const DirectionalShadowConfig& shadowConfig() const noexcept
	{
		return m_directionalShadow.config();
	}

private:
	/// @brief 現在の (shaderMode, useMultiLight, outlineMode) に対する PSO を選ぶ
	[[nodiscard]] ID3D12PipelineState* selectMainPSO() const noexcept;

public:

	/// @brief 現在の蓄積済みライト配列（テスト・診断用）
	[[nodiscard]] std::span<const Light> lights() const noexcept
	{
		return std::span<const Light>(m_lights.data(), m_lights.size());
	}

	/// @brief キューブマップ skybox をセットする（DX12）
	/// @details テクスチャ部分（TextureCube + upload + SRV）のみリセットし、
	///          PSO / root signature / VB / IB / CB は再利用する。
	///          これにより 1/2/3 のような頻繁な variant 切替で
	///          shader compile + PSO 作成が走らない（「もっさり」防止）。
	void setSkybox(const Cubemap& cubemap) override;

	void setSkyboxEnabled(bool enabled) override
	{
		m_skyboxEnabled = enabled;
	}

	[[nodiscard]] bool isSkyboxEnabled() const noexcept override
	{
		return m_skyboxEnabled && m_skyboxCubemap.valid();
	}

	/// @brief endFrame()内で2Dオーバーレイを自動描画する
	[[nodiscard]] bool hasOverlaySupport() const noexcept override { return true; }

	/// @brief コマンドリストを取得する（ImGui描画用）
	/// @details beginFrame()後〜endFrame()前に呼び出すこと。
	[[nodiscard]] ID3D12GraphicsCommandList* getCommandList() const noexcept
	{
		return m_graphicsCmdList.Get();
	}

	/// @brief 現在開いているコマンドリストを返す（IRenderer3D）
	[[nodiscard]] void* nativeCommandList() const noexcept override
	{
		return static_cast<void*>(m_graphicsCmdList.Get());
	}

	/// @brief Dx12Deviceを返す（IRenderer3D）
	[[nodiscard]] void* nativeDevice() const noexcept override
	{
		return static_cast<void*>(m_device);
	}

	/// @brief Dx12SwapChainを返す（IRenderer3D）
	[[nodiscard]] void* nativeSwapChain() const noexcept override
	{
		return m_device ? static_cast<void*>(m_device->getSwapChain()) : nullptr;
	}

};

} // namespace mitiru::render

// 実装本体（Renderer3D.hpp と同じ末尾 detail include 流儀）
#include <mitiru/render/detail/Renderer3D_DX12_Setup_impl.hpp>
#include <mitiru/render/detail/Renderer3D_DX12_Frame_impl.hpp>

#endif // _WIN32
