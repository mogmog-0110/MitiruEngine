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
#include <mitiru/gfx/dx12/Dx12MsaaTarget.hpp>
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

/// @brief 正射影行列（列優先 column-major、float4x4。OpenGL標準配置）
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
	                 const std::vector<std::uint32_t>& indices);

	/// @brief SDF矩形バッチをGPUに送信して描画する
	/// @param vertices StyledVertex2D頂点配列
	/// @param indices インデックス配列
	/// @param style スタイル定数（cbuffer b1）
	void submitStyledRectBatch(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style);

	/// @brief SDF円/楕円バッチをGPUに送信して描画する
	/// @param vertices StyledVertex2D頂点配列
	/// @param indices インデックス配列
	/// @param style スタイル定数（cbuffer b1）
	void submitStyledCircleBatch(
		const std::vector<StyledVertex2D>& vertices,
		const std::vector<std::uint32_t>& indices,
		const StyleConstants& style);

	/// @brief テクスチャ付きスプライトバッチをサポートするか
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
	/// @param contentMayChange 同 key・同寸でも中身が毎回変わり得る動的テクスチャか。
	///        false (既定): ポインタ key が内容を一意に決める静的テクスチャ (drawSprite の
	///        render::Texture 等)。cache hit は即返し、**毎フレームの全画素ハッシュを行わない**。
	///        true: 同アドレスを使い回す動的バッファ (drawPixelGrid 等)。内容ハッシュ (#19b) で
	///        変化を検出して再アップロードする。静的テクスチャに true を渡すと巨大シートを毎フレーム
	///        ハッシュして CPU を浪費するので注意。
	/// @return 1 以上のハンドル。未対応 backend / 失敗時は 0
	std::uint32_t ensureSpriteTexture(const void* key, int w, int h,
	                                  const std::uint8_t* rgba,
	                                  bool contentMayChange = false);

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
	void resize(float width, float height);

	/// @brief 抽象IDeviceから2Dパイプラインを構築する
	/// @details D3D12やVulkan等、DX11以外のバックエンドで使用する。
	/// @param device GPUデバイス
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return 構築されたパイプライン
	[[nodiscard]] static RenderPipeline2D createFromDevice(
		gfx::IDevice* device,
		float screenWidth,
		float screenHeight);

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
		const std::vector<std::uint32_t>& indices);

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
	gfx::BlendMode m_glBlendMode = gfx::BlendMode::Alpha; ///< 次の描画で使う合成
	void applyGlBlendMode() const noexcept;
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

	/// ── textured sprite batch 用 永続テクスチャキャッシュ (DX12) ──
	/// 各 entry は default-heap texture (PSR 状態) + 専用 1-slot shader-visible
	/// SRV heap を持つ。key+(w,h) でキャッシュし、初回のみ同期アップロードする。
	struct Dx12SpriteTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource>       tex;     ///< default heap (PSR)
		Microsoft::WRL::ComPtr<ID3D12Resource>       upload;  ///< upload heap 中間
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap; ///< 1-slot SRV heap
		int           w   = 0;
		int           h   = 0;
		const void*   key = nullptr;
		const void*   srcPtr = nullptr; ///< アップロード元 pixel データの先頭。差し替え(sprite hot-reload)検出用
		std::uint64_t contentHash = 0;  ///< pixel 内容の FNV-1a。内容変化で再アップロード判定 (#19b)
	};
	std::vector<Dx12SpriteTexture> m_dx12SpriteTextures;              ///< index+1 = handle
	std::unordered_map<const void*, std::uint32_t> m_dx12SpriteTexLookup; ///< key → index

	// ensureSpriteTexture の single-slot キャッシュ（直前と同じ texture なら map find を省く）。
	const void*         m_lastSpriteTexKey    = nullptr;
	int                 m_lastSpriteTexW      = 0;
	int                 m_lastSpriteTexH      = 0;
	const std::uint8_t* m_lastSpriteTexSrc    = nullptr;
	std::uint32_t       m_lastSpriteTexHandle = 0;   ///< 0 = empty

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
	/// ring 深さ。hot path の submit は slot = m_dx12Slot を使い、その slot を
	/// 前回使った GPU 完了 (= kDx12Ring 個前の submit) だけ待つ。直前ではなく
	/// N 個前を待つので CPU は最大 kDx12Ring submit 先行でき、CPU/GPU が重なる。
	static constexpr int kDx12Ring = 3;

	gfx::Dx12Device* m_dx12Device = nullptr;                         ///< DX12 デバイス（非所有）
	Microsoft::WRL::ComPtr<ID3D12Device>         m_dx12NativeDevice;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>   m_dx12Queue;
	Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_dx12RootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12Pipeline;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12PipelineMsaa; ///< 4x MSAA 変種 (中間 RT が 4x のとき使用)
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12VsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12PsBlob;
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12VertexBuffer[kDx12Ring]; ///< upload heap (slot 別)
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12IndexBuffer[kDx12Ring];  ///< upload heap (slot 別)
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12ConstantBuffer; ///< エイリアス用 (未使用)
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12VsCb;         ///< projection CB (共有。書換は resize のみ、waitDx12Fence で全 in-flight drain 後に限る)
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12PsCb[kDx12Ring]; ///< uUseTexture CB (slot 別)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dx12SrvHeap;      ///< null SRV 用
	std::uint32_t m_dx12VbCapacity[kDx12Ring] = {};
	std::uint32_t m_dx12IbCapacity[kDx12Ring] = {};

	/// ── DX12 pixel-grid point-filter パス用メンバ（createFromDx12 で eager 初期化）─
	/// 別 root signature が必要な理由: DX12 の static sampler は root signature
	/// に焼き込まれており PSO 単体では差し替えできない。同じ shader / blend / 入力
	/// layout を保ったまま root sig の static sampler の Filter だけを
	/// D3D12_FILTER_MIN_MAG_MIP_POINT に変えた variant を保持する。
	///
	/// createFromDx12 時点で linear PSO と並べて eager に構築する。構築に失敗した
	/// 場合は nullptr のまま残り、submitPixelGridDx12 は linear へ自動
	/// フォールバックする (draw() 内での遅延初期化は禁止。エンジン規約)。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_dx12PointRootSig; ///< point-filter root sig
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_dx12PointPipeline; ///< point-filter PSO
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_dx12PointPipelineMsaa; ///< point-filter 4x MSAA 変種

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
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12SdfRectPsoMsaa;   ///< SDF 矩形 4x MSAA 変種
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfCircleVsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob>             m_dx12SdfCirclePsBlob;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12SdfCirclePso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_dx12SdfCirclePsoMsaa; ///< SDF 円 4x MSAA 変種
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfVertexBuffer[kDx12Ring];
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfIndexBuffer[kDx12Ring];
	Microsoft::WRL::ComPtr<ID3D12Resource>       m_dx12SdfStyleCb[kDx12Ring];
	std::uint32_t m_dx12SdfVbCapacity[kDx12Ring] = {};
	std::uint32_t m_dx12SdfIbCapacity[kDx12Ring] = {};

	/// ── DX12 共通: ring allocator + 単一コマンドリスト + fence ──────────
	/// allocator は slot 別 (GPU 使用中の allocator を Reset しないため)。command
	/// list は 1 本で十分 (Close 後すぐ別 allocator で Reset 可能。記録済みコマンドは
	/// allocator のメモリに在り GPU が読む)。fence は単調増加 1 本で、slot ごとの
	/// 最後の signal 値を m_dx12SlotSignal に控える。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_dx12Alloc[kDx12Ring];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_dx12Cl;
	Microsoft::WRL::ComPtr<ID3D12Fence>               m_dx12Fence;
	UINT64                                            m_dx12FenceValue = 0;
	UINT64                                            m_dx12SlotSignal[kDx12Ring] = {}; ///< slot を最後に submit した fence 値
	int                                               m_dx12Slot = 0;   ///< 次に使う ring slot
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
	/// @param sampleCount RT の MSAA サンプル数 (1=非MS, 4=4x MSAA)。RTV の
	///        SampleDesc.Count と一致させないと draw で失敗するため、submit 側は
	///        描画先 RT の sampleCount() に合わせて 1x/4x PSO を選ぶ。
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D12PipelineState>
	buildDx12Pso(ID3D12Device* device,
		ID3D12RootSignature* rootSig,
		ID3DBlob* vs, ID3DBlob* ps,
		const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
		UINT sampleCount = 1);

	/// @brief 既存の 1x PSO と同一構成の 4x MSAA 変種を try で構築する (失敗時 null)。
	/// @details MSAA 中間 RT へ 2D を描く際に使う。4x 非対応環境では null のまま残り、
	///          submit 側が該当描画をスキップして 1x フォールバックへ委ねる。
	[[nodiscard]] static Microsoft::WRL::ComPtr<ID3D12PipelineState>
	tryBuildDx12PsoMsaa(ID3D12Device* device,
		ID3D12RootSignature* rootSig,
		ID3DBlob* vs, ID3DBlob* ps,
		const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount);

	/// @brief 発行済み全 command list の GPU 完了を待機する (drain。cold path / resize 用)
	void waitDx12Fence();

	/// @brief 指定 ring slot を最後に使った submit の GPU 完了だけ待機する (hot path 用)
	void waitForDx12Slot(int slot);

	/// @brief hot path 共通: 次の ring slot を確保し、その slot の前回 GPU 完了を待つ
	/// @return 使用する slot index
	[[nodiscard]] int acquireDx12Slot();

	/// @brief SDF ルートシグネチャ + PSO を遅延初期化する
	/// @param cachedPsoMsaa 1x PSO と並べて構築する 4x MSAA 変種の格納先 (4x 非対応時 null)
	void ensureDx12SdfResources(
		std::string_view vsSource, std::string_view psSource,
		Microsoft::WRL::ComPtr<ID3DBlob>& cachedVs,
		Microsoft::WRL::ComPtr<ID3DBlob>& cachedPs,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPsoMsaa);

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
	void setBlendMode([[maybe_unused]] gfx::BlendMode mode);

	/// @brief シザー矩形をプッシュする（クリッピング開始）
	/// @param rect クリップ矩形
	void pushScissorRect(const sgc::Rectf& rect);

	/// @brief シザー矩形をポップする（前のクリップに戻す）
	void popScissorRect();

	/// @brief シザースタックが空かどうか
	[[nodiscard]] bool hasScissor() const noexcept
	{
		return !m_scissorStack.empty();
	}

private:
	std::stack<sgc::Rectf> m_scissorStack; ///< シザー矩形スタック

	/// @brief シザー矩形を適用する
	void applyScissorRect(const sgc::Rectf& rect);

	/// @brief シザー矩形をリセットする
	void resetScissorRect();
};

} // namespace mitiru::render

#include <mitiru/render/detail/RenderPipeline2D_impl.hpp>
#include <mitiru/render/detail/RenderPipeline2D_Dx11.hpp>
#include <mitiru/render/detail/RenderPipeline2D_Dx12.hpp>
#include <mitiru/render/detail/RenderPipeline2D_PixelGrid.hpp>
#include <mitiru/render/detail/RenderPipeline2D_TexturedBatch.hpp>
