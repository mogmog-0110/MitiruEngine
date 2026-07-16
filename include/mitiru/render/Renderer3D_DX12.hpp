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
#include <mitiru/render/dx12/clod/ClodRenderer.hpp>
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

// 3D Gaussian Splatting (M1)。**ファイルスコープで**先に include する必要がある
// (DX12Splat.hpp は class body 内 .inl のため、これらの namespace 宣言を class
//  内へ入れないよう、ここで先に取り込んでおく = skybox と同じ作法)。
#include <mitiru/render/SplatScene.hpp>
#include <mitiru/render/dx12/DX12SplatShaders.hpp>
#include <mitiru/render/dx12/DX12SplatSort.hpp>
#include <mitiru/render/NeuralStyle.hpp>
#ifdef MITIRU_HAS_DIRECTML
#include <cstdio>
#include <DirectML.h>   // raw DirectML (in-pipeline neural post-process, DX12DirectML.hpp)
#include <mitiru/render/dx12/DX12NeuralFx.hpp>   // DirectML in-pipeline ニューラル後処理 (zero readback)
#include <mitiru/render/dx12/DX12NeuralRelight.hpp>   // ニューラル・リライティング (平面→法線推定→動的光源)
#include <mitiru/render/NeuralDepth.hpp>              // ORT+DML 単眼深度 (キャラ立体形状の推論)
#endif
#ifdef MITIRU_HAS_CUBISM_CORE
#include <mitiru/render/dx12/DX12Live2D.hpp>   // 自前 D3D12 Live2D レンダラ (namespace-scope class)
#endif
#ifdef MITIRU_HAS_CUBISM_FRAMEWORK
#include <mitiru/render/live2d/Live2DModel.hpp>   // Framework 駆動の Live2D モデル (motion/physics/effects)
#endif

#include <mitiru/render/Shadow.hpp>

#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/dx12/DX12FXAAShaders.hpp>
#include <mitiru/render/dx12/DX12OitTransparentPS.hpp>
#include <mitiru/render/dx12/WeightedBlendedOIT.hpp>
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
	Renderer3D_DX12(Renderer3D_DX12&&) = delete;
	Renderer3D_DX12& operator=(Renderer3D_DX12&&) = delete;

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

	/// @brief IRenderer3D 経由の resize (物理 px)
	void resize(int width, int height) override
	{
		resize(static_cast<float>(width), static_cast<float>(height));
	}

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

	/// @brief .clod モデルのインスタンスを積む (clod 世界ジオメトリパス、ADR 0027)
	/// @param path .clod への vfs パス
	void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
	               float scale) override
	{
		/// drawMesh を一度も呼ばないフレームでも skybox が出るように
		/// drawMesh 側と同じ遅延描画をここでも行う (フラグ共有で二重描画なし)
		if (m_skyboxEnabled && !m_skyboxDrawnThisFrame && m_skyboxCubemap.valid())
		{
			ensureSkyboxPipelineDx12();
			ensureSkyboxTextureDx12();
			drawSkyboxIfNeededDx12();
		}
		m_clod.queueInstance(path, &position.x, rotYDeg, scale);
	}

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

	/// メッシュバッファキャッシュ entry（毎フレーム再生成を防止）
	/// NOTE: .inl 内 helper (acquireMeshBuffer) の引数型のため include より前に定義する
	struct CachedBuffer
	{
		ComPtr<ID3D12Resource> resource;
		UINT size = 0;
		uint64_t revision      = 0;  ///< Mesh::revision() — 内容改変/アドレス再利用の失効検知
		uint64_t lastUsedFrame = 0;  ///< 最終参照フレーム（eviction 用）
	};

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

	// 3D Gaussian Splatting 描画 (M1) も同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Splat.hpp> // NOLINT(build/include)

	// ニューラル現像 (M3: ORT+DirectML で 3D フレームを 2D 絵画へ) も .inl で分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Neural.hpp> // NOLINT(build/include)

	// raw DirectML (in-pipeline ニューラル後処理: RT→tensor→DML→tensor→RT, CPU 往復なし)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12DirectML.hpp> // NOLINT(build/include)

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

	/// メッシュバッファキャッシュ（struct CachedBuffer は class 冒頭で定義）
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

	/// ── clod 世界ジオメトリパス (ADR 0027) ──────────────────
	/// 大規模静的モデル (.clod) を endFrame 先頭で offscreen に描き、
	/// depth-tested な inject で MSAA HDR + depth へ合成する
	clod::ClodRenderer m_clod;
	Camera3D m_clodCamera{ {0, 0, 5}, {0, 0, 0}, {0, 1, 0},
	                       0.7853982f, 16.0f / 9.0f, 0.1f, 500.0f };
	uint32_t m_frameCursor = 0;   ///< beginFrame で確定するフレーム index (upload ring 用)
	ComPtr<ID3D12RootSignature> m_clodInjectRS;
	ComPtr<ID3D12PipelineState> m_clodInjectPSO;
	ComPtr<ID3D12DescriptorHeap> m_clodInjectHeap;   ///< [0]=clod color SRV [1]=visbuffer SRV
	ID3D12Resource* m_clodInjectKey = nullptr;       ///< heap が指す color tex (作り直し検知)
	void createClodInjectPso();     ///< inject の root sig + PSO (initialize から)
	void renderClodPass();          ///< endFrame 先頭: clod 記録 + inject 合成

	/// ── 半透明 OIT (Weighted-Blended) ──────────────────────
	/// material.diffuse.a < 1 のメッシュを溜め、不透明の後にまとめて accum/reveal へ
	/// 蓄積→composite する。深度は不透明と共有 (読み取り専用テスト)。順序非依存。
	struct TransparentDraw { const Mesh* mesh; sgc::Mat4f world; Material material; };
	std::vector<TransparentDraw>  m_transparentCommands;
	dx12::WeightedBlendedOIT      m_oit;
	ComPtr<ID3D12PipelineState>   m_oitTransparentPSO;
	void createOitResources();   ///< OIT の accum/reveal + 透明 PSO を生成 (initialize から)
	void recordTransparentMesh(const Mesh& mesh, const sgc::Mat4f& world, const Material& material);
	void renderTransparentPass(D3D12_CPU_DESCRIPTOR_HANDLE msaaColorRtv,
	                           D3D12_CPU_DESCRIPTOR_HANDLE dsv);  ///< endFrame から呼ぶ OIT パス

	/// 2Dオーバーレイ
	ComPtr<ID3D12PipelineState> m_overlay2DPSO;       ///< 2Dオーバーレイ用PSO
	ComPtr<ID3D12RootSignature> m_overlay2DRootSig;   ///< 2Dオーバーレイ用ルートシグネチャ
	std::optional<gfx::Dx12Shader> m_overlay2DVS;     ///< 2Dオーバーレイ用頂点シェーダー
	std::optional<gfx::Dx12Shader> m_overlay2DPS;     ///< 2Dオーバーレイ用ピクセルシェーダー
	const Screen* m_overlayScreen = nullptr;           ///< 2Dオーバーレイ用Screen（非所有）
	std::vector<Overlay2DVertex> m_overlay2DVerts;    ///< 頂点 scratch（clear+reserve で毎フレーム再利用、hot path 確保なし）
	std::vector<std::uint32_t> m_overlay2DIndices;    ///< インデックス scratch（同上）

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
	/// shader-visible SRV ヒープ。frame index で partition し、GPU が in-flight の
	/// 前フレーム分 descriptor を読んでいる間に上書きしない。beginFrame で
	/// cursor を自 frame partition の先頭にリセット。
	static constexpr UINT kAlbedoSrvPerFrame = 256;  ///< 1 frame 分（2 SRV/draw → 128 draw）
	ComPtr<ID3D12DescriptorHeap>                 m_albedoSrvHeap;
	UINT                                         m_albedoSrvCapacity  = 0;  ///< 1 frame 分の実効 capacity
	UINT                                         m_albedoSrvBase      = 0;  ///< 現 frame partition の先頭 slot
	UINT                                         m_albedoSrvCursor    = 0;
	UINT                                         m_albedoSrvIncrement = 0;
	dx12::Dx12Texture2D                          m_defaultWhiteTexture;
	bool                                         m_defaultWhiteReady  = false;
	std::unordered_map<const Texture*, std::unique_ptr<dx12::Dx12Texture2D>> m_textureCache;

	/// ── Skybox（DX11 と機能パリティ）─────────────────────────
	Cubemap                     m_skyboxCubemap;
	bool                        m_skyboxEnabled         = false;
	bool                        m_skyboxPipelineReady   = false; ///< PSO/RootSig/VB/IB
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
	// CbSkyTransform は m_uploadRing から per-frame 切り出し (専用 CB 無し)

	/// ── 3D Gaussian Splatting (M1、DX12Splat.hpp が使う) ───────────────
	ComPtr<ID3D12Resource>       m_splatBuffer;        ///< UPLOAD: StructuredBuffer<SplatGPU>
	UINT                         m_splatCount = 0;     ///< スプラット数
	ComPtr<ID3D12DescriptorHeap> m_splatSrvHeap;       ///< shader-visible: t0=splat, t1=order
	ComPtr<ID3D12Resource>       m_splatCb;            ///< カメラ CB (view/proj/params)
	ComPtr<ID3D12RootSignature>  m_splatRootSig;
	ComPtr<ID3D12PipelineState>  m_splatPSO;
	std::vector<float>           m_splatPos;           ///< CPU 位置 (3*N、neural 現像で使用)
	sgc::Vec3f                   m_splatSortCam{};     ///< 前回ソート時のカメラ位置 (静止フレーム検出)
	bool                         m_splatSorted = false;///< GPU 深度ソートを一度でも実行したか (シーン読込でリセット)
	SplatDepthSortGpu            m_splatSort;          ///< GPU 深度ソート (compute)。order を生成
	float                        m_splatCenter[3] = {0.0f, 0.0f, 0.0f};  ///< シーン重心 (自動フレーミング)
	float                        m_splatRadius = 1.0f;                    ///< シーン境界球半径
	bool                         m_splatReady = false;         ///< シーン読込済み
	bool                         m_splatPipelineReady = false; ///< PSO/rootsig/CB 構築済み

	/// ── ニューラル現像 (M3、DX12Neural.hpp が使う) ───────────────────
	NeuralStyle                  m_neuralStyle;        ///< ORT + DirectML EP セッション
#ifdef MITIRU_HAS_DIRECTML
	ComPtr<IDMLDevice>           m_dmlDevice;          ///< raw DirectML device (m_d3dDevice を共有)
	bool                         m_dmlInitTried = false;   ///< device 生成を試したか (一度だけ)
	Dx12NeuralPostFx             m_neuralFx;           ///< in-pipeline ニューラル後処理 (zero readback)
	Dx12NeuralRelight            m_relight;            ///< ニューラル・リライティング (平面→法線→動的光源)
	NeuralDepth                  m_depthNet;           ///< ORT+DML 単眼深度 (キャラ立体形状)
	std::string                  m_relightModel;       ///< depth.onnx パス (demo が設定)
	unsigned                     m_relightFrame = 0;   ///< 深度更新の間引きカウンタ
#endif
#ifdef MITIRU_HAS_CUBISM_CORE
	Dx12Live2D                   m_live2d;             ///< 自前 D3D12 Live2D レンダラ (描画のみ)
	bool                         m_live2dReq = false;  ///< 描画要求 (game.draw が毎フレーム立てる)
	bool                         m_live2dReload = false;   ///< モデル切替で再 load する
	std::string                  m_live2dPath;         ///< model3.json パス
	std::string                  m_live2dStageBg, m_live2dStageGear, m_live2dStageClose;  ///< 公式ステージ画像
	float                        m_live2dDragX = 0.0f, m_live2dDragY = 0.0f;  ///< 注視先 (マウス追従)
	bool                         m_live2dTap = false;  ///< タップ要求 (次フレームで TapBody 再生)
#endif
#ifdef MITIRU_HAS_CUBISM_FRAMEWORK
	live2d::Live2DModel          m_live2dModel;        ///< Framework: model3.json/motion/physics/effects
#endif
	std::string                  m_developModel;       ///< 要求された style モデルパス
	bool                         m_developRequest = false;  ///< 次の安全境界で現像する
	bool                         m_styleReady = false;      ///< 現像済み 2D 画像が有効
	std::vector<std::uint8_t>    m_styleImage;         ///< 現像 2D 画像 (RGBA8、tight)
	int                          m_styleW = 0;
	int                          m_styleH = 0;
	std::vector<std::uint8_t>    m_targetImage;        ///< お題 2D 画像 (RGBA8、現像合わせゲーム用)
	bool                         m_targetReady = false;
	bool                         m_showTarget = false;       ///< blit でお題を表示 (現像との比較)
	float                        m_styleStrength = 0.0f;     ///< 現像 2D 合成強度 (0=3D / 1=2D)
	bool                         m_styleTexDirty = false;    ///< blit テクスチャ再アップロード要
	bool                         m_styleTexUploaded = false; ///< テクスチャに一度でも書いたか
	bool                         m_styleBlitReady = false;   ///< blit PSO/rootsig 構築済み
	int                          m_styleTexW = 0;
	int                          m_styleTexH = 0;
	ComPtr<ID3D12Resource>       m_styleTex;                 ///< DEFAULT: 現像 2D テクスチャ
	ComPtr<ID3D12Resource>       m_styleUpload;              ///< UPLOAD: テクスチャ転送元
	ComPtr<ID3D12DescriptorHeap> m_styleSrvHeap;             ///< shader-visible: t0=現像テクスチャ
	ComPtr<ID3D12RootSignature>  m_styleBlitRootSig;
	ComPtr<ID3D12PipelineState>  m_styleBlitPSO;

	/// ── 現像焼き込み (M4: 2D 絵画を 3D スプラットへ焼く) ───────────────
	glm::mat4                    m_developView{1.0f};   ///< 現像時の view (焼き込み射影用)
	glm::mat4                    m_developProj{1.0f};   ///< 現像時の proj
	bool                         m_bakeRequest = false;
	bool                         m_resetRequest = false;
	std::vector<float>           m_splatOrigRgb;        ///< 元の splat 色 (3*N、リセット用)
	std::vector<std::uint8_t>    m_splatBaked;          ///< 焼き込み済みフラグ (N、達成率用)
	int                          m_bakedTotal = 0;      ///< 焼き込み済み splat 数
public:
	/// @brief .splat シーンを読み込んで GPU にアップロードする (IRenderer3D)
	bool loadSplatScene(const char* path) override { return loadSplatSceneDx12(path); }
	/// @brief 読み込み済みスプラットを現在のカメラで描画する (IRenderer3D)
	void drawSplats() override { drawSplatsDx12(); }
	void splatBounds(float& cx, float& cy, float& cz, float& r) const override
	{ cx = m_splatCenter[0]; cy = m_splatCenter[1]; cz = m_splatCenter[2]; r = m_splatRadius; }

	/// ── ニューラル現像 (M3, IRenderer3D) ──
	void requestDevelop(const char* modelPath) override { requestDevelopDx12(modelPath); }
	void tickDevelop() override { ensureDirectMLDx12(); tickDevelopDx12(); relightDepthTickDx12(); }
	void clearDevelop() override { m_styleReady = false; }

	// ── Live2D (Framework 駆動 + 自前 D3D12 レンダラ、MITIRU_HAS_CUBISM_CORE) ──
	void drawLive2D(const char* model3jsonPath) override { requestLive2DDx12(model3jsonPath); }
	void live2dLookAt(float nx, float ny) override
	{
#ifdef MITIRU_HAS_CUBISM_CORE
		m_live2dDragX = nx; m_live2dDragY = ny;
#else
		(void)nx; (void)ny;
#endif
	}
	void live2dTap() override
	{
#ifdef MITIRU_HAS_CUBISM_CORE
		m_live2dTap = true;
#endif
	}
	void live2dStage(const char* bg, const char* gear, const char* close) override
	{
#ifdef MITIRU_HAS_CUBISM_CORE
		m_live2dStageBg    = (bg != nullptr) ? bg : "";
		m_live2dStageGear  = (gear != nullptr) ? gear : "";
		m_live2dStageClose = (close != nullptr) ? close : "";
#else
		(void)bg; (void)gear; (void)close;
#endif
	}
	void requestLive2DDx12(const char* model3jsonPath)
	{
#ifdef MITIRU_HAS_CUBISM_CORE
		const std::string p = (model3jsonPath != nullptr) ? model3jsonPath : "";
		if (m_live2d.ready() && p != m_live2dPath) { m_live2dReload = true; }   // モデルが変わった → 再 load
		m_live2dPath = p;
		m_live2dReq = !m_live2dPath.empty();
#else
		(void)model3jsonPath;
#endif
	}
	/// @brief endFrame (tonemap 後) に backbuffer へ Live2D を 2D オーバーレイ描画する。
	/// @details 初回はここで Framework がモデルをロード (moc/tex/motion/physics/effects) し、自前 D3D12
	///          レンダラの GPU リソースを構築する。毎フレーム Framework が更新 → レンダラが描画。
	void drawLive2DDx12()
	{
#ifdef MITIRU_HAS_CUBISM_FRAMEWORK
		if (!m_live2dReq || !m_graphicsCmdList || m_d3dDevice == nullptr) { return; }
		if (m_live2dReload)
		{
			if (m_device != nullptr) { m_device->waitForGpu(); }   // GPU 完了を待ってから旧リソース解放
			m_live2dModel.unload();
			m_live2d = Dx12Live2D{};
			m_live2dReload = false;
		}
		if (!m_live2d.ready())
		{
			if (!m_live2dModel.ready() && !m_live2dModel.load(m_live2dPath.c_str())) { return; }
			auto* core = static_cast<csmModel*>(m_live2dModel.coreModel());
			if (core == nullptr) { return; }
			std::vector<const char*> texs;
			for (int i = 0; i < m_live2dModel.textureCount(); ++i) { texs.push_back(m_live2dModel.texturePath(i)); }
			if (texs.empty()) { return; }
			if (!m_live2dStageBg.empty())   // 公式 LAppView 相当のステージ (背景/歯車/閉じる)
			{
				m_live2d.setStage(m_live2dStageBg.c_str(), m_live2dStageGear.c_str(), m_live2dStageClose.c_str());
			}
			m_live2d.load(m_d3dDevice, m_graphicsCmdList.Get(), core, texs.data(), static_cast<int>(texs.size()));
		}
		if (!m_live2d.ready()) { return; }

		if (m_live2dTap) { m_live2dModel.tap(); m_live2dTap = false; }
		m_live2dModel.update(m_live2dDragX, m_live2dDragY);   // motion/physics/effects/csmUpdateModel

		auto* sc  = m_device->getSwapChain();
		auto* bb  = static_cast<gfx::Dx12RenderTarget*>(sc->backBuffer());
		auto  rtv = bb->rtvHandle();
		const auto bbDesc = bb->nativeResource()->GetDesc();
		m_live2d.render(m_graphicsCmdList.Get(), rtv,
		                static_cast<int>(bbDesc.Width), static_cast<int>(bbDesc.Height),
		                static_cast<int>(sc->currentBackBufferIndex()));
#endif
	}

	// ── DirectML in-pipeline ニューラル後処理 (zero readback) ──
	void enableNeuralFx(bool e, float strength) override
	{
#ifdef MITIRU_HAS_DIRECTML
		m_neuralFx.setEnabled(e);
		m_neuralFx.setStrength(strength);
#else
		(void)e; (void)strength;
#endif
	}
	/// @brief endFrame (Live2D 後) に backbuffer へ DirectML 後処理を適用する。
	void neuralFxTickDx12()
	{
#ifdef MITIRU_HAS_DIRECTML
		if (!m_neuralFx.enabled() || !m_graphicsCmdList || m_d3dDevice == nullptr) { return; }
		if (!ensureDirectMLDx12()) { return; }
		auto* bb = static_cast<gfx::Dx12RenderTarget*>(m_device->getSwapChain()->backBuffer());
		const auto d = bb->nativeResource()->GetDesc();
		const int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
		if (!m_neuralFx.ensure(m_d3dDevice, m_dmlDevice.Get(), m_graphicsCmdList.Get(), w, h)) { return; }
		m_neuralFx.apply(m_graphicsCmdList.Get(), bb->nativeResource(), bb->rtvHandle(), w, h);
#endif
	}
	// ── ニューラル・リライティング (平面 Live2D → 推定法線 → 動的光源) ──
	void enableRelight(bool e, float lightX, float lightY, float strength, float rim) override
	{
#ifdef MITIRU_HAS_DIRECTML
		m_relight.setEnabled(e);
		m_relight.setLight(lightX, lightY);
		m_relight.setParams(strength, rim, 6.0f);
#else
		(void)e; (void)lightX; (void)lightY; (void)strength; (void)rim;
#endif
	}
	void relightTickDx12()
	{
#ifdef MITIRU_HAS_DIRECTML
		if (!m_relight.enabled() || !m_graphicsCmdList || m_d3dDevice == nullptr) { return; }
		auto* bb = static_cast<gfx::Dx12RenderTarget*>(m_device->getSwapChain()->backBuffer());
		const auto d = bb->nativeResource()->GetDesc();
		const int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
		if (!m_relight.ensure(m_d3dDevice, m_graphicsCmdList.Get(), w, h)) { return; }
		m_relight.apply(m_graphicsCmdList.Get(), bb->nativeResource(), bb->rtvHandle(), w, h);
#endif
	}
	void setRelightDepthModel(const char* path) override
	{
#ifdef MITIRU_HAS_DIRECTML
		if (path) m_relightModel = path;
#else
		(void)path;
#endif
	}
	/// @brief フレーム境界 (tickDevelop) で前フレームを readPixels → キャラ領域クロップ → ORT 深度推論
	///        → relight へ深度を渡す。間引き (数フレームに1回) で stall を抑える。深度未取得時は relight が
	///        輝度プロキシにフォールバックする。
	void relightDepthTickDx12()
	{
#ifdef MITIRU_HAS_ONNX
		if (!m_relight.enabled() || m_relightModel.empty() || m_device == nullptr) { return; }
		const unsigned f = m_relightFrame++;
		if (f < 180u) { return; }                  // 起動直後はスキップ (CEF scene ロードを優先)
		if ((f % 30u) != 0u) { return; }           // 30 フレームに 1 回 (stall を抑える、キャラはゆっくり)
		if (!m_depthNet.ensure(m_relightModel)) { return; }
		const int w = static_cast<int>(m_config.viewportWidth), h = static_cast<int>(m_config.viewportHeight);
		if (w <= 0 || h <= 0) { return; }
		m_device->waitForGpu();
		std::vector<std::uint8_t> frame = m_device->readPixels(w, h);
		if (static_cast<int>(frame.size()) < w * h * 4) { return; }
		// キャラ領域クロップ (公式フレーミングで中央列に立つ): x∈[0.40,0.62], y∈[0.04,0.97]
		if (m_depthNet.infer(frame.data(), w, h, 0.40f, 0.04f, 0.62f, 0.97f))
		{
			float x0, y0, x1, y1; m_depthNet.cropRect(x0, y0, x1, y1);
			m_relight.setDepth(m_depthNet.depth().data(), m_depthNet.depthW(), m_depthNet.depthH(), x0, y0, x1, y1);
		}
#endif
	}
	bool styleReady() const override { return m_styleReady; }
	const std::uint8_t* styleImageData() const override { return m_styleReady ? m_styleImage.data() : nullptr; }
	int styleImageW() const override { return m_styleW; }
	int styleImageH() const override { return m_styleH; }
	void setStyleStrength(float s) override { setStyleStrengthDx12(s); }
	void bakeStyleToSplats() override { m_bakeRequest = true; }
	void resetSplatColors() override { m_resetRequest = true; }
	float bakedFraction() const override { return m_splatCount ? static_cast<float>(m_bakedTotal) / static_cast<float>(m_splatCount) : 0.0f; }
	// ── 現像合わせ (お題再現パズル) ──
	void captureTargetFromStyle() override { captureTargetDx12(); }
	void setShowTarget(bool b) override { if (b != m_showTarget) { m_showTarget = b; m_styleTexDirty = true; } }
	bool hasTarget() const override { return m_targetReady; }
	float matchScore() const override { return matchScoreDx12(); }
	bool worldToScreen(float wx, float wy, float wz, float& u, float& v) const override
	{
		const glm::vec4 clip = m_projMatrix * m_viewMatrix * glm::vec4(wx, wy, wz, 1.0f);
		if (clip.w <= 0.0001f) { u = v = -1.0f; return false; }
		const float nx = clip.x / clip.w, ny = clip.y / clip.w;
		u = nx * 0.5f + 0.5f;
		v = 0.5f - 0.5f * ny;   // NDC y↑ → 画面 v↓
		return (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);
	}

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
