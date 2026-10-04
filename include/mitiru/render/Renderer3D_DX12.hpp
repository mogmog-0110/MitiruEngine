#pragma once

/// @file Renderer3D_DX12.hpp
/// @brief DirectX 12 ベースの 3D レンダラー
/// @details Pipeline State Object (PSO) ベースの 3D 描画を提供する。
///          トゥーンシェーディング + アウトラインの 2 パスレンダリングを行い、
///          PSO の切り替えでステートを安全かつアトミックに管理する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <unordered_map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <cassert>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/gfx/dx12/Dx12ShaderCompiler.hpp>
#include <mitiru/render/dx12/Dx12PassMarkers.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12Shader.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/render/Camera3D.hpp>
#ifdef MITIRU_HAS_MAKINA
#include <mitiru/render/csg/CsgBake.hpp>
#include <mitiru/render/csg/CsgRenderPass.hpp>
#include <mitiru/render/csg/CsgSolid.hpp>
#endif
#include <mitiru/render/FrustumCulling.hpp>
#include <mitiru/render/OcclusionCuller.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/QualityCaps.hpp>
#include <mitiru/render/dx12/clod/ClodRenderer.hpp>
#if defined(MITIRU_HAS_EFFEKSEER)
#include <mitiru/render/dx12/EffekseerRuntime.hpp>
#endif
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/ToonShaders3D.hpp>
#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/ISceneFx.hpp>
#include <mitiru/render/experimental/IExperimentalRenderer3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/IblBrdfLut.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/LocalLights.hpp>
#include <mitiru/render/SkyboxShaders.hpp>
#include <mitiru/render/dx12/DX12ClusterShaders.hpp>
#include <mitiru/render/dx12/DX12LitShaders.hpp>
#include <mitiru/render/dx12/DX12PBRShaders.hpp>
#include <mitiru/render/dx12/DX12SceneTableLayout.hpp>
#include <mitiru/render/dx12/DX12SsrShaders.hpp>
#include <mitiru/render/IndirectLighting.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>

// 3D Gaussian Splatting (M1)。**ファイルスコープで**先に include する必要がある
// (DX12Splat.hpp は class body 内の .inl なので、これらの namespace 宣言を class
//  内へ入れないよう、ここで先に取り込んでおく = skybox と同じ作法)。
#include <mitiru/render/SplatScene.hpp>
#include <mitiru/render/dx12/DX12SplatShaders.hpp>
#include <mitiru/render/dx12/DX12SplatSort.hpp>

// スキンアニメ付き glTF モデル。DX12SkinnedModel.hpp (class body 内 .inl)
// が使う namespace 宣言をここで先に取り込む (splat と同じ作法)。
#include <mitiru/asset/AssetReload.hpp>
#include <mitiru/asset/FbxImport.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/render/GltfLoader.hpp>
#include <mitiru/render/GltfMaterialConverter.hpp>
#include <mitiru/render/MaterialTextures.hpp>
#include <mitiru/render/Skinning.hpp>
#include <mitiru/render/Texture.hpp>
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
#include <mitiru/render/dx12/DX12GtaoShaders.hpp>
#include <mitiru/render/dx12/DX12MotionBlurShaders.hpp>
#include <mitiru/render/dx12/DX12TemporalShaders.hpp>
#include <mitiru/render/dx12/DX12TrailShaders.hpp>
#include <mitiru/render/dx12/DX12AtmosphereShaders.hpp>
#include <mitiru/render/dx12/DX12FogShaders.hpp>
#include <mitiru/render/dx12/DX12UpscaleShaders.hpp>
#include <mitiru/render/dx12/DX12Fsr3Shaders.hpp>
#include <mitiru/render/dx12/Fsr3Upscaler.hpp>
#include <mitiru/render/Atmosphere.hpp>
#include <mitiru/render/RenderScale.hpp>
#include <mitiru/render/VolumetricFog.hpp>
#include <mitiru/render/dx12/Dx12GpuTimer.hpp>
#include <mitiru/render/dx12/Dx12PassTimeline.hpp>
#include <mitiru/util/TransparentStringHash.hpp>
#include <mitiru/gfx/dx12/Dx12TextureReadback.hpp>
#include <mitiru/render/FrameMotionHistory.hpp>
#include <mitiru/render/TemporalJitter.hpp>
#include <mitiru/render/PostEffectSettings.hpp>
#include <mitiru/render/TrailRibbon.hpp>
#include <mitiru/render/Decals.hpp>
#include <mitiru/render/GpuParticles.hpp>
#include <mitiru/render/HitFeel.hpp>
#include <mitiru/render/VfxTextures.hpp>
#include <mitiru/render/dx12/DX12HitFeelShaders.hpp>
#include <mitiru/render/dx12/DX12ParticleShaders.hpp>

// XeGTAO の定数の作り方 (GTAOUpdateConstants) と、HLSL と共有する GTAOConstants の並び。上流のまま読む
#pragma warning(push, 0)
#include <XeGTAO/XeGTAO.h>
#pragma warning(pop)
#include <mitiru/render/dx12/WeightedBlendedOIT.hpp>
#include <mitiru/render/dx12/DX12OcclusionResolveShaders.hpp>
#include <mitiru/render/dx12/DX12Tonemap.hpp>
#include <mitiru/render/dx12/DX12SsaoShaders.hpp>
#include <mitiru/render/dx12/DX12BloomShaders.hpp>
#include <mitiru/render/dx12/DX12DofShaders.hpp>
#include <mitiru/render/dx12/DX12ShaderModePS.hpp>
#include <mitiru/render/dx12/DX12ShaderModeVS.hpp>
#include <mitiru/render/dx12/DX12Shaders.hpp>
#include <mitiru/render/dx12/Dx12ShadowMap.hpp>
#include <mitiru/render/dx12/Dx12SkinningCompute.hpp>
#include <mitiru/render/SkinnedLod.hpp>
#include <mitiru/render/dx12/Dx12TextureUpload.hpp>
#include <mitiru/render/dx12/Dx12UploadRing.hpp>
#include <mitiru/render/dx12/Dx12CopyQueue.hpp>
#include <mitiru/render/dx12/DX12ModelPrepare.hpp>
#include <mitiru/render/dx12/DX12WorldPrepare.hpp>
#include <mitiru/resource/AssetStreamer.hpp>
// 屋外の 1 枚 (world.json)。DX12World.hpp (class body 内 .inl) が使う宣言を先に取り込む
#include <mitiru/render/dx12/DX12WorldShaders.hpp>
#include <mitiru/terrain/OutdoorWorldLoad.hpp>
#include <mitiru/terrain/OutdoorRegion.hpp>

namespace mitiru::render
{

// OutlineMode enum と OUTLINE_MODE_COUNT は IRenderer3D.hpp で定義済み

// ─────────────────────────────────────────────────────────────
//  Renderer3D_DX12 本体
// ─────────────────────────────────────────────────────────────

/// @brief DirectX 12 ベースの 3D レンダラー
/// @details PSO（Pipeline State Object）によるステート管理で、
///          トゥーンシェーディング + アウトラインの 2 パスレンダリングを行う。
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
class Renderer3D_DX12 : public IRenderer3D, public ISceneFx, public IExperimentalRenderer3D
{
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// @brief queryInterface 相当: 自分自身を ISceneFx / IExperimentalRenderer3D として返す
	[[nodiscard]] ISceneFx* sceneFx() noexcept override { return this; }
	[[nodiscard]] const ISceneFx* sceneFx() const noexcept override { return this; }
	[[nodiscard]] IExperimentalRenderer3D* experimental() noexcept override { return this; }
	[[nodiscard]] const IExperimentalRenderer3D* experimental() const noexcept override { return this; }

#ifndef MITIRU_HAS_MAKINA
	// MITIRU_HAS_MAKINA 未定義ビルドでは drawSolid を override しないため、IRenderer3D の
	// 後方互換転送関数と IExperimentalRenderer3D の既定 no-op が同名で並び曖昧になる。
	using IExperimentalRenderer3D::drawSolid;
#endif

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

	/// ムーブ禁止（内部リソースが this を参照する可能性がある）
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

		/// @brief skinned glTF 1 体あたりの joint 数の上限（B8）。超えると剛体 fallback で
		///        描画する（warnOnce で通知）。既定の 256 は `kMaxSkinJoints` のハード上限と同じ。
		uint32_t maxSkinJoints = 256;
		/// @brief 1 フレームに描けるスキン prim 数の上限（B8）。`kMaxSkinnedDrawsPerFrame`
		///        (プールの物理サイズ) を超える値を渡してもそこでクランプされる。
		uint32_t maxSkinnedDrawsPerFrame = 2048;
	};

	/// @brief レンダラーを初期化する
	/// @param device Dx12Device へのポインタ（外部で管理し、寿命を保証する）
	/// @param cfg レンダラー設定
	void initialize(gfx::Dx12Device* device, const Config& cfg = {});

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept override
	{
		return m_initialized;
	}

	/// @brief 出力 (バックバッファ) の大きさを変える。3D の内部解像度は setRenderScale の倍率を掛けた大きさ
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
	/// @param camera 3D カメラ
	void setCamera(const Camera3D& camera) override;

	void setCameraShake(float fracX, float fracY) override
	{
		m_cameraShakeX = fracX;
		m_cameraShakeY = fracY;
	}

	/// @brief ライトを設定する
	/// @param light ライト情報
	void setLight(const Light& light) override
	{
		m_light = light;
		m_lightBaseColor = light.color;
		applySkyToLight();
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

	/// @brief 同一メッシュを複数のワールド行列で GPU instancing 描画する
	/// @details per-instance vertex buffer 方式 (DX11 と同じ設計)。PSO/シェーダー生成に
	///          失敗した環境では drawMesh のループ呼び出しへフォールバックする。
	void drawMeshInstanced(const Mesh& mesh,
	                       std::span<const sgc::Mat4f> worlds,
	                       const Material* material) override
	{
		drawMeshInstancedDx12(mesh, worlds, material);
	}

#ifdef MITIRU_HAS_MAKINA
	/// @brief Makina の CSG ソリッドを積む。実描画は endFrame の renderCsgPass
	/// @details 積むだけなのは drawModel と同じ理由。呼ばれた時点ではまだ不透明パスの
	///          途中で、MSAA ターゲットへ直接割り込むと以降の OIT / resolve の前提が崩れる。
	void drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position, float rotYDeg,
	               float scale, float timeSec) override
	{
		if (bakeManifestPath == nullptr || !(scale > 0.0f) || rejectInView("drawSolid"))
		{
			return;
		}
		m_csgQueue.push_back(QueuedSolid{ bakeManifestPath, position, rotYDeg, scale, timeSec });
	}
#endif

	/// @brief .clod モデルのインスタンスを積む (clod 世界ジオメトリパス)
	/// @param path .clod への vfs パス
	void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
	               float scale) override
	{
		/// drawMesh を一度も呼ばないフレームでも skybox が出るように、
		/// drawMesh 側と同じ遅延描画をここでも行う (フラグを共有するので二重には描かない)
		if (rejectInView("drawModel (clod)") || rejectOutdoorPath(path)) { return; }
		drawSkyboxBeforeFirstDraw();
		if (!streamClodModel(path)) { return; }
		m_clod.queueInstance(path, &position.x, rotYDeg, scale);
		recordClodShadowCaster(path, position, rotYDeg, scale);
	}

	/// @brief スキンアニメ付き glTF モデルを forward パスで描く
	void drawSkinnedModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
	                      float scale, const char* clipA, float timeA,
	                      const char* clipB, float timeB, float blend01) override
	{
		if (queueEffekseer(path, position, rotYDeg, scale, clipA, timeA)) { return; }
		if (rejectOutdoorPath(path)) { return; }
		drawSkinnedModelImpl(path, position, rotYDeg, scale, clipA, timeA, clipB, timeB,
		                     blend01);
	}

	/// @brief 3 軸回転の rigid glb を forward パスで描く (ABI v26)
	void drawModelRot(const char* path, const sgc::Vec3f& position,
	                  const sgc::Vec3f& rotDeg, float scale) override
	{
		drawModelRotImpl(path, position, rotDeg, scale);
	}

	/// @brief glTF モデルを world 行列 (非一様スケール・3 軸回転) と色の調整つきで描く
	void drawModelPosed(const char* path, const sgc::Mat4f& world, const ModelPose& pose,
	                    const DrawTint& tint) override
	{
		if (isEffekseerPath(path))
		{
			IRenderer3D::drawModelPosed(path, world, pose, tint);
			return;
		}
		drawSkinnedModelWorldImpl(path, world, pose.clipA, pose.timeA, pose.clipB, pose.timeB, pose.blend, tint);
	}

	/// @brief ゲーム DLL の Screen::drawModelPose (ABI v48)。姿勢の評価と IK は DLL と同じ関数で行う
	void drawModelAnimPose(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
	                       const animation::AnimIkRequest* ik, int ikCount, const DrawTint& tint) override
	{
		if (isEffekseerPath(path))
		{
			IRenderer3D::drawModelAnimPose(path, world, pose, ik, ikCount, tint);
			return;
		}
		drawSkinnedModelParams(path, world, (pose != nullptr) ? *pose : animation::AnimPoseParams{}, tint, ik, ikCount);
	}

	void drawModelNodeMatrices(const char* path, const sgc::Mat4f& world, const sgc::Mat4f* nodeModel, int count,
	                           const DrawTint& tint) override
	{
		if (nodeModel == nullptr || count <= 0) { return; }
		drawSkinnedModelPose(path, world, std::span<const sgc::Mat4f>(nodeModel, static_cast<std::size_t>(count)), tint);
	}

	bool registerGameMesh(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
	                      int indexCount) override
	{
		if (!validGameMesh(vertices, vertexCount, indices, indexCount))
		{
			debug::warnOnce("dx12.gamemesh.invalid." + std::to_string(id),
			                "registerMesh3D: 頂点が無いか、添字が頂点の数を超えている (登録しない)");
			return false;
		}
		// 同じ数の差し替え (布のように毎フレーム形が変わる物) は同じ Mesh に写し、GPU のバッファは回して使う
		if (const auto it = m_gameMeshes.find(id); it != m_gameMeshes.end()
		    && it->second->overwrite(vertices, static_cast<std::size_t>(vertexCount), indices,
		                             indices != nullptr ? static_cast<std::size_t>(std::max(indexCount, 0)) : 0))
		{
			return true;
		}
		releaseGameMesh(id);
		auto mesh = std::make_unique<Mesh>();
		mesh->setVertices(std::vector<Vertex3D>(vertices, vertices + vertexCount));
		if (indices != nullptr && indexCount > 0) { mesh->setIndices(std::vector<std::uint32_t>(indices, indices + indexCount)); }
		m_gameMeshes[id] = std::move(mesh);
		return true;
	}

	void releaseGameMesh(std::uint32_t id) override
	{
		const auto it = m_gameMeshes.find(id);
		if (it == m_gameMeshes.end()) { return; }
		forgetDeformedGameMesh(it->second.get());
		m_gameMeshGraveyard.push_back({std::move(it->second), m_frameCounter});
		m_gameMeshes.erase(it);
	}

	[[nodiscard]] const Mesh* findGameMesh(std::uint32_t id) const override
	{
		const auto it = m_gameMeshes.find(id);
		return (it != m_gameMeshes.end()) ? it->second.get() : nullptr;
	}

	/// @brief 剛体の glTF モデルをインスタンスごとの行列と色で描く (prim ごとに 1 回の instanced draw)
	void drawModelInstances(const char* path, const MeshInstance* instances, int count) override
	{
		if (count > 0) { drawModelInstancesImpl(path, instances, static_cast<std::size_t>(count)); }
	}

	int reloadModel(const char* changedPathUtf8) override
	{
		const std::string_view s(changedPathUtf8 != nullptr ? changedPathUtf8 : "");
		const std::filesystem::path changed(std::u8string(s.begin(), s.end()));
		return forgetSkinnedModel(changed) + m_clod.forgetModel(changed);
	}

	/// @brief フレーム終了処理（アウトラインパス + バリア + コマンド実行）
	void endFrame() override;

	/// @brief コマンドリストを閉じて GPU で実行する（Engine::endFrame の前に呼ぶ）
	void finalizeFrame();

	/// @brief 現在のフレームの描画コール数を返す
	[[nodiscard]] int drawCallCount() const noexcept override
	{
		return m_drawCallCount;
	}

	/// @brief 視錐台カリングの有効/無効を切り替える（既定 ON、DX11 と同じ意味論）
	void setFrustumCullingEnabled(bool enabled) noexcept override
	{
		m_frustumCullingEnabled = enabled;
	}

	/// @brief 視錐台カリングが有効かを返す
	[[nodiscard]] bool isFrustumCullingEnabled() const noexcept override
	{
		return m_frustumCullingEnabled;
	}

	/// @brief 直前フレームでカリングされたメッシュ（インスタンス含む）数を返す
	[[nodiscard]] int culledCount() const noexcept override
	{
		return m_culledCount;
	}

	/// @brief オクルージョンカリングの有効/無効を切り替える（既定 OFF、DX11 と同じ意味論）
	/// @details DX12 の深度は常に 4x MSAA だが、min-depth resolve パス
	///          (`recordOcclusionResolvePass`) が毎回読み戻すため DX11 のような
	///          MSAA punt は無い。ON 自体は常に受理する。
	void setOcclusionCullingEnabled(bool enabled) noexcept override
	{
		m_occlusionCullingEnabled = enabled;
	}

	/// @brief オクルージョンカリングが有効かを返す
	[[nodiscard]] bool isOcclusionCullingEnabled() const noexcept override
	{
		return m_occlusionCullingEnabled;
	}

	/// @brief 直前フレームでオクルージョン判定によりスキップされたメッシュ数
	[[nodiscard]] int occludedCount() const noexcept override
	{
		return m_occludedCount;
	}

	/// @brief mesh VB/IB の committed resource 生成回数 (累計、デバッグ計測用)
	/// @details 毎フレーム頂点を更新しても、スロットの warm-up 後は増えないことを
	///          golden test が検証する。
	[[nodiscard]] uint64_t meshBufferCreates() const noexcept
	{
		return m_meshBufferCreates;
	}

	/// @brief 1 フレームぶんの GPU 時間 (ミリ秒)。測っていないものは 0
	struct FrameGpuTimes
	{
		double mainMs = 0.0;   ///< メインのコマンドリスト (影・不透明・後処理)
		double auxMs  = 0.0;   ///< メインより前に流す補助リスト (局所光の割り当てとスキニング)
		double totalMs = 0.0;  ///< 両方の和
	};

	/// @brief 読めた中で最も新しいフレームの GPU 時間。FRAME_COUNT フレーム遅れで、計測できない環境では 0
	[[nodiscard]] FrameGpuTimes gpuFrameTimes() const noexcept
	{
		FrameGpuTimes t;
		t.mainMs = m_frameTimer.milliseconds(kFrameTimerMain);
		t.auxMs = m_frameTimer.milliseconds(kFrameTimerLights) + m_frameTimer.milliseconds(kFrameTimerSkin);
		t.totalMs = t.mainMs + t.auxMs;
		return t;
	}

	/// @brief 直前のフレームの影のパスで、投影の外だったので描かなかった caster の数
	[[nodiscard]] int shadowCastersSkipped() const noexcept { return m_shadowCastersSkipped; }

	/// @brief パスのしるし (dx12::Pass3D) ごとの GPU 時間と後処理の内訳を測るか。測ると数フレーム遅れて埋まる
	void setPassGpuTimingEnabled(bool enabled) noexcept
	{
		m_passTimeline.setEnabled(enabled);
		setPostGpuTimingEnabled(enabled);
	}

	/// @brief 最後に読めたパス pass の GPU 時間 (ms)。測っていなければ 0
	[[nodiscard]] double passGpuMilliseconds(dx12::Pass3D pass) const noexcept
	{
		return m_passTimeline.milliseconds(static_cast<std::uint32_t>(pass));
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

	/// @brief アウトラインの線幅 (px) と検出しきい値を設定する
	void setOutlineParams(float widthPx, float threshold) noexcept override
	{
		m_outlineWidthPx = (widthPx < 1.0f) ? 1.0f : widthPx;
		m_outlineThresh  = (threshold < 0.001f) ? 0.001f : threshold;
	}

	/// @brief トゥーン時の影色 (乗算係数) を設定する
	void setToonShadowTint(const sgc::Colorf& tint) noexcept override
	{
		m_toonShadowTint = tint;
	}

	/// @brief 画質の上限 (設定画面)。下げた効果はすぐ切る。上げた効果は、ゲームが次に頼んだ時から効く
	///        (sceneLook3D を使うゲームは毎フレーム頼み直すので、次のフレームから)
	void setQualityCaps(const QualityCaps& caps) noexcept
	{
		const QualityCaps previous = m_qualityCaps;
		m_qualityCaps = caps;
		if (!caps.ambientOcclusion) { m_aoEnabled = false; }
		if (!caps.bloom) { m_bloomEnabled = false; }
		if (!caps.depthOfField) { m_dofStrength = 0.0f; }
		applyShadowCascadeMode();
		// 画面の反射は host の既定とゲームの頼みを持っているので、上げた時もすぐ戻せる
		refreshIndirectLighting();
		// 設定が変わった時だけ当てる。毎回当てると、エンジンの中で setRenderScale した倍率を上書きする
		if (caps.upscaler != previous.upscaler) { setUpscaler(caps.upscaler); }
		if (caps.upscale != previous.upscale) { setUpscaleQuality(caps.upscale); }
	}

	/// @brief SSAO (v40)。radius は 0 以下なら既定 0.25、strength は 0..4 に丸める
	void setAmbientOcclusion(bool enabled, float radius, float strength) noexcept override
	{
		m_aoEnabled  = enabled && m_qualityCaps.ambientOcclusion;
		m_aoRadius   = (radius > 0.0f) ? radius : 0.25f;
		m_aoStrength = (strength < 0.0f) ? 0.0f : ((strength > 4.0f) ? 4.0f : strength);
	}

	/// @brief トゥーンの段数 (0..4) と境の幅 (v40、v43 で 0 = 段なし)
	void setToonRamp(int bands, float softness, const sgc::Colorf& midTint) noexcept override
	{
		m_toonBands    = (bands < 0) ? 0 : ((bands > 4) ? 4 : bands);
		m_toonSoftness = (softness < 0.0f) ? 0.0f : ((softness > 1.0f) ? 1.0f : softness);
		m_toonMidTint  = midTint;
	}

	/// @brief 段付きハイライトの強さと指数 (v40)
	void setToonSpecular(float strength, float power) noexcept override
	{
		m_toonSpecular      = (strength < 0.0f) ? 0.0f : ((strength > 1.0f) ? 1.0f : strength);
		m_toonSpecularPower = (power >= 1.0f) ? power : 1.0f;
	}

	/// @brief bloom (v41)。threshold は 0 以上、strength は 0..4 に丸める
	void setBloom(bool enabled, float threshold, float strength) noexcept override
	{
		m_bloomEnabled   = enabled && m_qualityCaps.bloom;
		m_bloomThreshold = (threshold < 0.0f) ? 0.0f : threshold;
		m_bloomStrength  = (strength < 0.0f) ? 0.0f : ((strength > 4.0f) ? 4.0f : strength);
	}

	/// @brief 影の PCF のタップ間隔 (v41)。0..8 texel
	void setShadowSoftness(float texels) noexcept override
	{
		m_shadowSoftness = (texels < 0.0f) ? 0.0f : ((texels > 8.0f) ? 8.0f : texels);
	}

	/// @brief 彩度 0..4 とコントラスト 0.1..4 (v41)
	void setColorGrade(float saturation, float contrast) noexcept override
	{
		m_gradeSaturation = (saturation < 0.0f) ? 0.0f : ((saturation > 4.0f) ? 4.0f : saturation);
		m_gradeContrast   = (contrast < 0.1f) ? 0.1f : ((contrast > 4.0f) ? 4.0f : contrast);
	}

	/// @brief 輪郭線の距離減衰 (v42)。far <= near は「減衰しない」の指定なのでそのまま通す
	void setOutlineFade(float nearDist, float farDist, float minStrength) noexcept override
	{
		m_outlineFadeNear = (nearDist < 0.0f) ? 0.0f : nearDist;
		m_outlineFadeFar  = (farDist < 0.0f) ? 0.0f : farDist;
		m_outlineFadeMin  = (minStrength < 0.0f) ? 0.0f : ((minStrength > 1.0f) ? 1.0f : minStrength);
	}

	/// @brief 半球アンビエント (v43)。sky/ground が両方真っ黒なら平坦な m_sceneAmbient にフォールバックする
	void setHemisphereAmbient(const sgc::Colorf& sky, const sgc::Colorf& ground) noexcept override
	{
		m_ambientSky    = sky;
		m_ambientGround = ground;
	}

	/// @brief 縁光 (v43)。strength 0..4、power は 0.1 以上
	void setRimLight(float strength, float power, const sgc::Colorf& color) noexcept override
	{
		m_rimStrength = (strength < 0.0f) ? 0.0f : ((strength > 4.0f) ? 4.0f : strength);
		m_rimPower    = (power >= 0.1f) ? power : 0.1f;
		m_rimColor    = color;
	}

	/// @brief 輪郭線を下の色へ寄せる度合い (v43)。0..1
	void setOutlineDarken(float darken) noexcept override
	{
		m_outlineDarken = (darken < 0.0f) ? 0.0f : ((darken > 1.0f) ? 1.0f : darken);
	}

	/// @brief 遠景のぼけ (v44)。strength は 720p の画素で 0..16 (タップは 16 本なので、それ以上は穴が見える)
	void setDepthOfField(float start, float end, float strength) noexcept override
	{
		m_dofStart    = (start < 0.0f) ? 0.0f : start;
		m_dofEnd      = (end < 0.0f) ? 0.0f : end;
		m_dofStrength = (strength < 0.0f || !m_qualityCaps.depthOfField) ? 0.0f : ((strength > 16.0f) ? 16.0f : strength);
	}

	/// @brief 影の比較の余白 (v44)。0 以下は従来の余白
	void setShadowBias(float worldUnits) noexcept override
	{
		m_shadowBiasWorld = (worldUnits > 0.0f) ? worldUnits : 0.0f;
	}

	/// @brief 直前の draw がアップロードしたライティング CB。材質の metallic/roughness が
	///        シェーダーへ届いているかを、絵ではなく数値で確かめるために見る
	[[nodiscard]] const DX12CbLighting& lastLightingCB() const noexcept { return m_lastLightingCB; }

	/// @brief 距離フォグを設定する
	void setFog(bool enabled, const sgc::Colorf& color, float nearDist,
	            float farDist) noexcept override
	{
		m_fogOn = enabled;
		m_fogColor = color;
		m_fogNear = nearDist;
		m_fogFar = (farDist > nearDist + 0.01f) ? farDist : (nearDist + 0.01f);
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

	/// @brief FXAA の有効/無効 (ENG-104)。setAntiAliasing の MsaaFxaa / Msaa の切り替えと同じ
	void setFXAAEnabled(bool enabled) noexcept
	{
		setAntiAliasing(enabled ? AntiAliasing3D::MsaaFxaa : AntiAliasing3D::Msaa);
	}

	[[nodiscard]] bool isFXAAEnabled() const noexcept
	{
		return m_aaMode == AntiAliasing3D::MsaaFxaa;
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
	//  外部アクセス用 API（カスタムアウトラインパス等で使用）
	// ─────────────────────────────────────────────────────────

	/// @brief グラフィクスコマンドリストを取得する
	[[nodiscard]] ID3D12GraphicsCommandList* getCommandList() noexcept
	{
		return m_graphicsCmdList.Get();
	}

	/// @brief ネイティブの D3D12 デバイスを取得する
	[[nodiscard]] ID3D12Device* getNativeDevice() noexcept
	{
		return m_d3dDevice;
	}

	/// @brief Dx12Device を取得する
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

	/// @brief 深度 SRV ヒープを取得する
	[[nodiscard]] ID3D12DescriptorHeap* getDepthSRVHeap() noexcept
	{
		return m_depthSRVHeap.Get();
	}

	/// @brief メイン PSO とルートシグネチャに戻す
	void restoreMainState();

	/// @brief Vertex3D 用の入力レイアウトを取得する（外部での PSO 作成用）
	/// @param desc 出力先の配列（4 要素）
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

	/// メッシュバッファキャッシュの entry（毎フレームの再生成を防ぐ）
	/// NOTE: .inl 内の helper (acquireMeshBuffer) の引数型に使うので、include より前に定義する
	// shadow pass は前フレームの caster をこのキャッシュ経由で描くので、frame N に書いた slot は
	// frame N+1 のリストからも読まれる。in-flight 数と同じ 3 だと N+3 の CPU 書き込みが実行中の N+1 と重なる。
	static constexpr uint32_t kMeshSlotCount = FRAME_COUNT + 1;

	struct CachedBuffer
	{
		gfx::GpuResource resource;             ///< 現行バッファ (bind と shadow の find() 経路が読む)
		gfx::GpuResource slots[kMeshSlotCount]; ///< 同サイズ動的 mesh 用の回転 slot (遅延生成)
		uint64_t slotFrame[kMeshSlotCount] = {};      ///< 各 slot を最後に CPU が書いた m_frameCounter
		uint32_t activeSlot    = 0;                  ///< slots の現在位置
		UINT size = 0;
		uint64_t revision      = 0;  ///< Mesh::revision() — 内容改変/アドレス再利用の失効検知
		uint64_t lastUsedFrame = 0;  ///< 最終参照フレーム（eviction 用）
	};

	/// 影の caster。.inl 内の helper (addShadowCaster) の引数型に使うので、include より前に定義する。
	/// instanceCount > 0 はインスタンス描画で、行列は m_shadowInstances[instanceFirst..] にある (world は使わない)。
	/// bounds はワールドでの外接箱 (インスタンスなら全部を包む箱)。影の各カスケードで、写らない caster を描かずに済ませる
	struct ShadowCaster {
		const Mesh* mesh = nullptr;
		sgc::Mat4f  world;
		uint32_t    instanceFirst = 0;
		uint32_t    instanceCount = 0;
		CullAABB    bounds{};
	};

	// ─────────────────────────────────────────────────────────
	//  PSO 生成・リソース生成・描画ヘルパー（別ファイルに分離）
	//  NOTE: これは class body 内への意図的な .inl include である。
	//  DX12PipelineStates.inl は Renderer3D_DX12 の private member function を
	//  宣言しており、class scope にアクセスするためにここで include する必要が
	//  ある。この include を class 宣言の外に移動してはいけない。
	// ─────────────────────────────────────────────────────────

	// NOLINTNEXTLINE(google-build-namespaces) — intentional in-class .inl include
	#include <mitiru/render/dx12/DX12PipelineStates.hpp> // NOLINT(build/include)

	// 材質の SRV 表と描画ごとの定数、局所光の割り当ても同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Materials.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12ClusteredLights.hpp> // NOLINT(build/include)
	// 焼いた光 (放射照度と反射のプローブ) と画面の反射の履歴
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12LightingProbes.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Ssr.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12SpotShadows.hpp> // NOLINT(build/include)

	// skybox 実装も同じパターンで分離（DX11 と機能パリティ）
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Skybox.hpp> // NOLINT(build/include)

	// PBR / IBL 実装 (B17) も同じ .inl パターンで分離。skybox とは独立した
	// root signature / PSO を持つため、共有 root signature 側 (DX12PipelineStates_Setup.inl)
	// の変更を必要としない
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/Renderer3D_DX12_PBR.hpp> // NOLINT(build/include)

	// 3D Gaussian Splatting 描画 (M1) も同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Splat.hpp> // NOLINT(build/include)

	// スキンアニメ付き glTF モデルも同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12SkinnedModel.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12SkinnedDraw.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12ModelRelease.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12ModelStreaming.hpp> // NOLINT(build/include)

	// ニューラル現像 (ORT+DirectML で 3D フレームを 2D 絵画へ) も .inl で分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Neural.hpp> // NOLINT(build/include)

	// raw DirectML (in-pipeline ニューラル後処理: RT→tensor→DML→tensor→RT, CPU 往復なし)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12DirectML.hpp> // NOLINT(build/include)

	// GPU instancing (drawMeshInstanced) も同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Instancing.hpp> // NOLINT(build/include)
	// 形の変わるゲームのメッシュの動きと、破片のまとめ描き (ADR 0069)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12PhysicsFxDraw.hpp> // NOLINT(build/include)

	// 副ビュー (分割画面・小窓・描いた絵を材質に使う)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Views.hpp> // NOLINT(build/include)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12ViewsSetup.hpp> // NOLINT(build/include)
	#include <mitiru/render/dx12/DX12ViewSlots.hpp> // NOLINT(build/include)
	#include <mitiru/render/dx12/DX12ViewShadows.hpp> // NOLINT(build/include)
	#include <mitiru/render/dx12/DX12ViewTemporal.hpp> // NOLINT(build/include)
	#include <mitiru/render/dx12/DX12ViewAtmosphere.hpp> // NOLINT(build/include)

	// 屋外の 1 枚 (地形・草・撒いた物・水面) も同じ .inl パターンで分離
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12World.hpp> // NOLINT(build/include)

	// Effekseer のエフェクト (drawModel の時刻つき版で .efkefc を受ける)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12Effekseer.hpp> // NOLINT(build/include)
	// 副ビューの仕上げ (主ビューのパスを副ビューの資源で回す)
	// NOLINTNEXTLINE(google-build-namespaces)
	#include <mitiru/render/dx12/DX12ViewsFinish.hpp> // NOLINT(build/include)

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

	/// アウトラインの線幅 (px) と検出しきい値
	float m_outlineWidthPx = 1.0f;
	float m_outlineThresh  = 0.30f;
	bool  m_outlineCasterEnabled = true;  ///< 以後の描画が輪郭線の検出に入るか。フレーム頭で true

	/// 輪郭線の距離減衰 (v42)。far <= near なら減衰なし = 従来の絵
	float m_outlineFadeNear = 0.0f;
	float m_outlineFadeFar  = 0.0f;
	float m_outlineFadeMin  = 0.0f;

	/// 輪郭線を下の色へ寄せる度合い (v43)。1 = 従来のインク色
	float m_outlineDarken = 1.0f;

	/// 半球アンビエントと縁光 (v43)。どちらも既定は無効 (半球は両方真っ黒、縁光は強さ 0)
	sgc::Colorf m_ambientSky{0.0f, 0.0f, 0.0f, 1.0f};
	sgc::Colorf m_ambientGround{0.0f, 0.0f, 0.0f, 1.0f};
	float       m_rimStrength = 0.0f;
	float       m_rimPower    = 3.0f;
	sgc::Colorf m_rimColor{1.0f, 1.0f, 1.0f, 1.0f};

	/// トゥーン時の影色 (乗算係数)
	sgc::Colorf m_toonShadowTint{0.60f, 0.64f, 0.76f, 1.0f};

	/// トゥーンの段と段付きハイライト (v40)。1 段 = 従来の 2 トーン
	int         m_toonBands         = 1;
	float       m_toonSoftness      = 0.12f;
	sgc::Colorf m_toonMidTint{0.78f, 0.80f, 0.88f, 1.0f};
	float       m_toonSpecular      = 0.0f;
	float       m_toonSpecularPower = 32.0f;

	/// SSAO (v40)。テクスチャ 2 枚は ping-pong (0 が最終、tonemap が t1 で読む)。
	/// m_aoAppliedThisFrame は drawSsaoPasses が立て、uploadTonemapCB が読む
	QualityCaps m_qualityCaps{};
	bool  m_aoEnabled  = false;
	float m_aoRadius   = 0.25f;
	float m_aoStrength = 1.0f;
	bool  m_aoAppliedThisFrame = false;
	std::optional<gfx::Dx12Shader> m_ssaoPS;
	std::optional<gfx::Dx12Shader> m_ssaoBlurPS;
	ComPtr<ID3D12PipelineState>    m_ssaoPSO;
	ComPtr<ID3D12PipelineState>    m_ssaoBlurPSO;
	gfx::GpuResource               m_ssaoTex[2];
	ComPtr<ID3D12DescriptorHeap>   m_ssaoRtvHeap;   ///< 2 slot
	ComPtr<ID3D12DescriptorHeap>   m_ssaoSrvHeap;   ///< shader-visible 6 slot (3 × 2 組)

	/// bloom (v41)。tex[0] = 1/2 解像 (しきい値 + 縮小)、tex[1] = 1/4 解像、tex[2] = 1/2 解像 (戻し + 加算、tonemap が t2 で読む)。
	/// m_bloomAppliedThisFrame は drawBloomPasses が立て、uploadTonemapCB が読む
	bool  m_bloomEnabled   = false;
	float m_bloomThreshold = 1.0f;
	float m_bloomStrength  = 0.3f;
	bool  m_bloomAppliedThisFrame = false;
	std::optional<gfx::Dx12Shader> m_bloomDownPS;
	std::optional<gfx::Dx12Shader> m_bloomUpPS;
	ComPtr<ID3D12PipelineState>    m_bloomDownPSO;
	ComPtr<ID3D12PipelineState>    m_bloomUpPSO;
	BloomChain                     m_bloom;

	/// 影の PCF タップ間隔 (v41、CbShadow 経由で全 PS が読む) と、tonemap 後の色調補正
	float m_shadowSoftness  = 1.0f;
	float m_gradeSaturation = 1.0f;
	float m_gradeContrast   = 1.0f;

	/// 影の比較の余白 (v44)。0 = 従来の 0.001 × max(softness, 1)。CbShadow へは影マップ深度に直して渡す
	float m_shadowBiasWorld = 0.0f;

	/// 遠景のぼけ (v44)。SRV heap は {FXAA intermediate, 深度, null}
	float m_dofStart    = 0.0f;
	float m_dofEnd      = 0.0f;
	float m_dofStrength = 0.0f;
	std::optional<gfx::Dx12Shader> m_dofPS;
	ComPtr<ID3D12PipelineState>    m_dofPSO;
	ComPtr<ID3D12DescriptorHeap>   m_dofSrvHeap;

	/// 距離フォグ
	sgc::Colorf m_fogColor{0.7f, 0.78f, 0.86f, 1.0f};
	float m_fogNear = 30.0f;
	float m_fogFar  = 90.0f;
	bool  m_fogOn   = false;

	/// 色バッファのコピー用リソース（モード 3,4 で使用）
	gfx::GpuResource m_colorCopyBuffer;
	ComPtr<ID3D12DescriptorHeap> m_colorEdgeSRVHeap;   ///< モード3用: [色,法線,dummy]
	ComPtr<ID3D12DescriptorHeap> m_depthColorSRVHeap;  ///< モード4用: [深度,法線,色]

	// ─── MSAA リソース (ENG-105 v2) ────────────────────────────
	// 4x MSAA で MRT (color + normal + depth) を multisample 描画し、
	// outline / FXAA の前に backbuffer に Resolve する。
	// depth/normal の resource format は TYPELESS にして、DSV/RTV と SRV の
	// 両方から異なる typed view を作れるようにする (v1 がおかしくなった原因の 1 つ
	// として疑った format の強指定を避ける)。
	static constexpr UINT MSAA_SAMPLE_COUNT = 4;

#ifdef MITIRU_HAS_MAKINA
	/// 1 立体 = 1 PSO なので、シーンごとに solid + bake + pass を丸ごと持つ。
	/// failed を覚えておくのは、壊れたマニフェストを毎フレーム開き直して
	/// 毎フレーム同じ警告を出さないため。
	struct CsgEntry
	{
		csg::CsgSolid solid;
		csg::CsgBake bake;
		csg::CsgRenderPass pass;
		bool failed = false;
	};
	struct QueuedSolid
	{
		std::string manifest;
		sgc::Vec3f position;
		float rotYDeg;
		float scale;
		float timeSec;   ///< モーションの時刻 (D-15)。live でない bake は読まない
	};
	std::unordered_map<std::string, CsgEntry> m_csgCache;
	std::vector<QueuedSolid> m_csgQueue;
	void renderCsgPass();
#endif
	gfx::GpuResource             m_msaaColorBuffer;   ///< 4x MSAA color RT (ENG-106: FP16)
	ComPtr<ID3D12DescriptorHeap> m_msaaColorRtvHeap;  ///< 上記の RTV ヒープ

	/// HDR intermediate (ENG-106)。single-sample の FP16。MSAA color の Resolve
	/// 先で、tonemap PS が SRV としてサンプリングして backbuffer に書き込む。
	gfx::GpuResource             m_hdrIntermediateBuffer;
	ComPtr<ID3D12DescriptorHeap> m_hdrIntermediateRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_hdrIntermediateSrvHeap;

	/// Tonemap pass (ENG-106)。HDR FP16 → backbuffer LDR R8G8B8A8。
	/// 露出 → 中立の肩 → sRGB (DX12Tonemap.hpp)。
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

	/// FXAA ポストプロセス (ENG-104)。outline 描画の後、overlay2D 描画の前に実行して
	/// シーン色のジャギーを近似的に AA する。intermediate に backbuffer を copy して、
	/// 自分自身を read/write する読み書き競合を避ける。
	ComPtr<ID3D12PipelineState> m_fxaaPSO;
	ComPtr<ID3D12RootSignature> m_fxaaRootSig;
	gfx::GpuResource            m_fxaaIntermediate;     ///< backbuffer サイズの色コピー
	ComPtr<ID3D12DescriptorHeap> m_fxaaSrvHeap;         ///< shader-visible: t0 = intermediate
	std::optional<gfx::Dx12Shader> m_fxaaPS;
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
	gfx::GpuResource m_depthBuffer;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_depthSRVHeap;  ///< 深度バッファSRV用ヒープ

	/// 法線バッファ（MRT RT1）
	gfx::GpuResource m_normalBuffer;
	ComPtr<ID3D12DescriptorHeap> m_normalRTVHeap;  ///< 法線RT用RTVヒープ

	/// メッシュバッファキャッシュ（struct CachedBuffer は class 冒頭で定義）
	std::unordered_map<const void*, CachedBuffer> m_meshVBCache; ///< 頂点バッファキャッシュ
	std::unordered_map<const void*, CachedBuffer> m_meshIBCache; ///< インデックスバッファキャッシュ

	/// ゲームが registerMesh3D で登録したメッシュ (ABI v48)。消したものは影が前フレームの描画を読み終えるまで残す
	struct RetiredGameMesh
	{
		std::unique_ptr<Mesh> mesh;
		std::uint64_t         frame = 0;
	};
	std::unordered_map<std::uint32_t, std::unique_ptr<Mesh>> m_gameMeshes;
	std::vector<RetiredGameMesh>                             m_gameMeshGraveyard;

	[[nodiscard]] static bool validGameMesh(const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
	                                        int indexCount) noexcept
	{
		if (vertices == nullptr || vertexCount <= 0) { return false; }
		if (indices == nullptr || indexCount <= 0) { return true; }
		for (int i = 0; i < indexCount; ++i)
		{
			if (indices[i] >= static_cast<std::uint32_t>(vertexCount)) { return false; }
		}
		return true;
	}

	/// beginFrame から呼ぶ。消してから 3 フレーム経ったメッシュの GPU バッファを一時資源へ回して解放する
	void collectRetiredGameMeshes()
	{
		std::erase_if(m_gameMeshGraveyard, [this](RetiredGameMesh& r)
		{
			if (m_frameCounter < r.frame + 3) { return false; }
			evictMeshBuffers(r.mesh.get());
			return true;
		});
	}

	/// Per-frame UPLOAD ヒープリング。drawMesh の transient CB/VB/IB をまとめる
	dx12::Dx12UploadRing m_uploadRing;

	/// フレーム内の一時アップロードバッファ（定数バッファ含む）
	std::vector<gfx::GpuResource> m_frameTempResources;
	std::vector<gfx::GpuResource> m_perFrameTempResources[FRAME_COUNT]; ///< フレーム毎の一時リソース保持

	/// カメラ状態（glm 形式、toHLSL 変換用）
	glm::mat4 m_viewMatrix{1.0f};
	glm::mat4 m_projMatrix{1.0f};
	sgc::Vec3f m_cameraPosition{};
	float      m_cameraShakeX = 0.0f;   ///< setCameraShake の画面幅に対する割合
	float      m_cameraShakeY = 0.0f;

	/// ライト状態
	Light m_light;

	/// シーンアンビエント色（initialize 時に config.defaultAmbient で初期化）
	sgc::Colorf m_sceneAmbient{0.5f, 0.5f, 0.5f, 1.0f};

	/// ── clod 世界ジオメトリパス ──────────────────
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
	void transitionShadowMapsForCompute(bool enabled, bool toCompute);

	/// ── 半透明 OIT (Weighted-Blended) ──────────────────────
	/// material.diffuse.a < 1 のメッシュを溜め、不透明の後にまとめて accum/reveal へ
	/// 蓄積→composite する。深度は不透明と共有 (読み取り専用テスト)。順序非依存。
	struct TransparentDraw
	{
		const Mesh* mesh;
		sgc::Mat4f world;
		Material material;
		const MaterialMaps* maps;
		DrawTint tint;
		int pass = 0;   ///< 積んだビュー (0 = 主ビュー、DX12Views.hpp の pass)
	};
	std::vector<TransparentDraw>  m_transparentCommands;
	dx12::WeightedBlendedOIT      m_oit;
	ComPtr<ID3D12PipelineState>   m_oitTransparentPSO;
	void createOitResources();   ///< OIT の accum/reveal + 透明 PSO を生成 (initialize から)
	void recordTransparentMesh(const TransparentDraw& draw);
	void renderTransparentPass(D3D12_CPU_DESCRIPTOR_HANDLE msaaColorRtv,
	                           D3D12_CPU_DESCRIPTOR_HANDLE dsv);  ///< endFrame から呼ぶ OIT パス (今のビューのぶん)
	[[nodiscard]] bool hasTransparentFor(int pass) const noexcept;

	/// 描画統計
	int m_drawCallCount = 0;
	/// フレームの GPU 時間 (パス 0 = メインのリスト、1 = 局所光の割り当て、2 = スキニングの補助リスト)
	dx12::Dx12GpuTimer m_frameTimer;
	static constexpr std::uint32_t kFrameTimerMain = 0;
	static constexpr std::uint32_t kFrameTimerLights = 1;
	static constexpr std::uint32_t kFrameTimerSkin = 2;
	/// メインのリストのパスごとの GPU 時間 (setPassGpuTimingEnabled の間だけ打つ)
	dx12::Dx12PassTimeline m_passTimeline;
	static_assert(static_cast<std::uint32_t>(dx12::Pass3D::Count) <= dx12::Dx12PassTimeline::kMaxMarks);
	/// DRED のしるしを置き、計測中ならタイムスタンプも打つ
	void markPass3D(dx12::Pass3D pass)
	{
		dx12::markPass(m_graphicsCmdList.Get(), pass);
		m_passTimeline.mark(m_graphicsCmdList.Get(), static_cast<std::uint32_t>(pass));
	}
	bool m_frameActive = false;  ///< このフレームでbeginFrame()が呼ばれたか
	bool m_needsFinalize = false; ///< endFrame後、finalizeFrame待ち

	/// ── 視錐台カリング（DX11 Renderer3D と同じ意味論）─────────────
	Frustum m_frustum;                        ///< setCamera で毎回更新
	bool    m_frustumCullingEnabled = true;   ///< 既定 ON
	int     m_culledCount = 0;                ///< 直前フレームでカリングされた数

	/// ── オクルージョンカリング（CPU Hi-Z、DX11 Renderer3D と同じ意味論）───
	/// 深度は常に 4x MSAA。`m_depthSRVHeap` のスロット 0 が既に t0=深度
	/// (R32_FLOAT, TEXTURE2DMS) を指しているため、resolve パスはそれを
	/// そのまま読む（新規 SRV ヒープは不要）。実体は隣接する PSO 生成ファイル群と
	/// 同じ流儀の class-body chunk に分離してある。
	OcclusionCuller m_occlusionCuller;
	bool m_occlusionCullingEnabled = false;
	int  m_occludedCount = 0;                 ///< 直前フレームでオクルージョン判定によりスキップされた数
	unsigned m_occlusionFrameCounter = 0;     ///< kOcclusionUpdateInterval ごとに resolve を記録
	static constexpr unsigned kOcclusionUpdateInterval = 4;
	static constexpr int kOcclusionDownsampleStride = 8;
	std::vector<float> m_occlusionDepthScratch;   ///< 間引き後の深度スクラッチ（毎回 alloc しない）

	/// resolve 先（単一サンプル R32_FLOAT RT）と、FRAME_COUNT 個の readback バッファ
	/// （buffer リソース、CopyTextureRegion で行ピッチ揃えして書く）。
	/// スロット frameIndex の読み戻しは、そのスロットを次に再利用する beginFrame
	/// （デバイス側が既にフェンス待機済み）の先頭で行う。
	gfx::GpuResource m_occlusionResolveTex;
	ComPtr<ID3D12DescriptorHeap> m_occlusionResolveRtvHeap;
	gfx::GpuResource m_occlusionReadback[FRAME_COUNT];
	bool m_occlusionReadbackPending[FRAME_COUNT]{};
	UINT m_occlusionReadbackRowPitch = 0;
	std::optional<gfx::Dx12Shader> m_occlusionResolvePS;
	ComPtr<ID3D12RootSignature> m_occlusionResolveRootSig;
	ComPtr<ID3D12PipelineState> m_occlusionResolvePSO;

	/// 資源生成・resolve 描画・読み戻し関数の実体は、他の PSO 生成メソッドと同じ流儀で
	/// クラス本体の分割ファイルに置くので、ここでは宣言しない。

	/// @brief ローカル AABB をワールド変換し、外接する `CullAABB` を作る
	/// @details DX11 `Renderer3D::worldOcclusionAABB` と同じ近似（8 頂点変換 + min/max）。
	[[nodiscard]] static CullAABB worldOcclusionAABB(const Mesh::AABB& local,
	                                                 const sgc::Mat4f& world) noexcept
	{
		const sgc::Vec3f corners[8] = {
			{local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
			{local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
			{local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
			{local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
		};
		CullAABB box;
		box.minX = box.minY = box.minZ = std::numeric_limits<float>::max();
		box.maxX = box.maxY = box.maxZ = -std::numeric_limits<float>::max();
		for (const auto& c : corners)
		{
			const auto w = world.transformPoint(c);
			box.minX = std::min(box.minX, w.x); box.maxX = std::max(box.maxX, w.x);
			box.minY = std::min(box.minY, w.y); box.maxY = std::max(box.maxY, w.y);
			box.minZ = std::min(box.minZ, w.z); box.maxZ = std::max(box.maxZ, w.z);
		}
		return box;
	}

	/// @brief 現在の view*proj を `OcclusionCuller::isOccluded` が期待する
	///        column-major float[16] へ変換する
	/// @details `m_viewMatrix`/`m_projMatrix` は既に glm（内部 column-major）で
	///          保持しているため、DX11 のような行/列入れ替えは不要でそのまま
	///          memcpy できる。描画に使うのと同じ行列（DX の Z[0,1] 規約）を使う
	///          ことで、深度読み戻し値との規約を一致させる。
	[[nodiscard]] std::array<float, 16> occlusionViewProj() const noexcept
	{
		const glm::mat4 vp = m_projMatrix * m_viewMatrix;
		std::array<float, 16> m{};
		std::memcpy(m.data(), &vp, sizeof(m));
		return m;
	}

	/// ── マルチライト（DX11 と機能パリティ）──────────────────
	/// setLights の先頭は主光源、点光源・スポットは useMultiLight の間だけ局所光として使う
	std::vector<Light>                      m_lights;          ///< setLights で蓄積
	bool                                    m_useMultiLight = false;

	/// ── ShaderMode (DX11 と機能パリティ) ───────────────────
	/// setShaderMode で切替。未実装モードは Toon フォールバック。
	ShaderMode3D m_shaderMode = ShaderMode3D::Toon;
	std::optional<gfx::Dx12Shader> m_phongPS;
	std::optional<gfx::Dx12Shader> m_unlitPS;
	std::optional<gfx::Dx12Shader> m_flatPS;
	std::optional<gfx::Dx12Shader> m_pbrPS;
	ComPtr<ID3D12PipelineState>    m_phongPSO;
	ComPtr<ID3D12PipelineState>    m_unlitPSO;
	ComPtr<ID3D12PipelineState>    m_flatPSO;
	ComPtr<ID3D12PipelineState>    m_pbrPSO;

	/// 両面描画 (glTF doubleSided) 用。上と同じ PS でカリングだけ切った双子。
	ComPtr<ID3D12PipelineState>    m_mainPSONoCull;
	ComPtr<ID3D12PipelineState>    m_phongPSONoCull;
	ComPtr<ID3D12PipelineState>    m_unlitPSONoCull;
	ComPtr<ID3D12PipelineState>    m_flatPSONoCull;
	ComPtr<ID3D12PipelineState>    m_pbrPSONoCull;

	/// ── 指向性シャドウマップ ──────────────────────────────
	DirectionalShadow         m_directionalShadow;
	dx12::Dx12ShadowMap       m_shadowMap;      ///< カスケード0 (近距離、単一カスケード時は唯一のマップ)
	dx12::Dx12ShadowMap       m_shadowMapFar;   ///< カスケード1 (遠距離、B13。cascadedShadow 無効時は未使用)
	bool                      m_cascadedShadowEnabled = false;
	int                       m_shadowCascadesRequested = 1;   ///< ゲームが頼んだカスケードの数 (1 = エンジンの既定)
	bool                      m_shadowAutoFitRequested = false;
	bool                      m_shadowEnabled = false;
	bool                      m_shadowCasterEnabled = true;  ///< 以後の描画が影を落とすか
	bool                      m_shadowDrawnThisFrame = false;
	ComPtr<ID3D12PipelineState> m_shadowPSO;  ///< depth-only PSO (PS なし)
	ComPtr<ID3D12PipelineState> m_shadowPSOTwoSided;  ///< 同じ PSO の両面版 (setShadowBias > 0 の間だけ使う)
	ComPtr<ID3D12PipelineState> m_spotShadowPSO;      ///< スポットの影 (透視) 用。ラスタライザの余白なし
	std::optional<gfx::Dx12Shader> m_shadowVS; ///< shadow パス用 VS（メインと同じ）

	std::vector<ShadowCaster> m_shadowCommands;       ///< 当フレーム描画分
	std::vector<ShadowCaster> m_shadowCommandsPrev;   ///< 前フレーム — shadow pass で使う
	int m_shadowCastersSkipped = 0;   ///< このフレームの影のパスで、投影の外だったので描かなかった caster の数 (カスケードごとに数える)

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
	gfx::GpuResource            m_skyboxTexture;       ///< default-heap TextureCube
	gfx::GpuResource            m_skyboxUpload;        ///< upload-heap (6 face)
	ComPtr<ID3D12DescriptorHeap> m_skyboxSrvHeap;      ///< 1 SRV (shader-visible)
	ComPtr<ID3D12RootSignature> m_skyboxRootSig;       ///< skybox 専用 root sig
	ComPtr<ID3D12PipelineState> m_skyboxPSO;           ///< skybox 専用 PSO
	gfx::GpuResource            m_skyboxVB;            ///< cube vertex buffer
	gfx::GpuResource            m_skyboxIB;            ///< cube index buffer
	// CbSkyTransform は m_uploadRing から per-frame 切り出し (専用 CB 無し)

	/// ── PBR / IBL 環境キューブマップ（B17。skybox と同じ upload パターン）───
	/// diffuse/specular 用の畳み込み結果は CPU の Cubemap::irradiance() /
	/// prefilterSpecular() で 1 回だけ作り、場面の表 (t8〜t10) から PBR の PS が読む。
	Cubemap                     m_pbrEnvironmentCubemap;      ///< setEnvironment() で受けた原本
	Cubemap                     m_pbrIrradianceCubemap;       ///< diffuse IBL 畳み込み結果
	std::vector<Cubemap>        m_pbrPrefilteredChain;        ///< specular IBL 畳み込み結果 (mip i = roughness i/(N-1)、kPbrPrefilterMipCount 枚)
	std::vector<UINT>           m_pbrPrefilterMipOffsets;     ///< upload buffer 内の mip ごとの先頭 offset (face 6 枚分が続く)
	std::vector<UINT>           m_pbrPrefilterFaceStrides;    ///< mip ごとの face 1 枚分の byte 数 (placement alignment 済み)
	std::vector<UINT>           m_pbrPrefilterAlignedRows;    ///< mip ごとの行 pitch
	std::vector<int>            m_pbrPrefilterSizes;          ///< mip ごとの一辺
	gfx::GpuResource            m_pbrBrdfLutTexture;          ///< 環境 BRDF 表 (t10、R32G32_FLOAT、kPbrBrdfLutSize^2)
	gfx::GpuResource            m_pbrBrdfLutUpload;
	bool                        m_pbrBrdfLutInPSR = false;
	bool                        m_pbrEnvironmentTextureReady = false;
	bool                        m_pbrEnvironmentNeedsUpload  = false;
	bool                        m_pbrEnvironmentTextureInPSR = false;
	int                         m_pbrEnvironmentFaceSize  = 0;
	UINT                        m_pbrEnvironmentFaceStride = 0;
	UINT                        m_pbrEnvironmentAlignedRow = 0;
	gfx::GpuResource            m_pbrIrradianceTexture;       ///< default-heap TextureCube (t8)
	gfx::GpuResource            m_pbrIrradianceUpload;
	gfx::GpuResource            m_pbrPrefilteredTexture;      ///< default-heap TextureCube (t9)
	gfx::GpuResource            m_pbrPrefilteredUpload;

	/// ── 3D Gaussian Splatting (M1、DX12Splat.hpp が使う) ───────────────
	gfx::GpuResource             m_splatBuffer;        ///< UPLOAD: StructuredBuffer<SplatGPU>
	UINT                         m_splatCount = 0;     ///< スプラット数
	ComPtr<ID3D12DescriptorHeap> m_splatSrvHeap;       ///< shader-visible: t0=splat, t1=order
	gfx::GpuResource             m_splatCb;            ///< カメラ CB (view/proj/params)
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
	gfx::GpuResource             m_styleTex;                 ///< DEFAULT: 現像 2D テクスチャ
	ComPtr<ID3D12DescriptorHeap> m_styleSrvHeap;             ///< shader-visible: FRAME_COUNT 個。フレーム k は k 番だけを書く
	ComPtr<ID3D12RootSignature>  m_styleBlitRootSig;
	ComPtr<ID3D12PipelineState>  m_styleBlitPSO;

	/// ── 現像焼き込み (2D 絵画を 3D スプラットへ焼く) ─────────────────────
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
	void drawSplats() override
	{
		if (!rejectInView("drawSplats")) { drawSplatsDx12(); }
	}
	void splatBounds(float& cx, float& cy, float& cz, float& r) const override
	{ cx = m_splatCenter[0]; cy = m_splatCenter[1]; cz = m_splatCenter[2]; r = m_splatRadius; }

	/// ── ニューラル現像 (M3, IRenderer3D) ──
	void requestDevelop(const char* modelPath) override { requestDevelopDx12(modelPath); }
	void tickDevelop() override { tickDevelopDx12(); relightDepthTickDx12(); }
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
	///          レンダラの GPU リソースを構築する。毎フレーム Framework が更新し、レンダラが描画する。
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

		auto* bb  = m_device->currentBackBuffer();
		if (!bb) { return; }
		auto  rtv = bb->rtvHandle();
		const auto bbDesc = bb->nativeResource()->GetDesc();
		m_live2d.render(m_graphicsCmdList.Get(), rtv,
		                static_cast<int>(bbDesc.Width), static_cast<int>(bbDesc.Height),
		                static_cast<int>(m_device->currentFrameIndex()));
#endif
	}

	// ── DirectML in-pipeline ニューラル後処理 (zero readback) ──
	void enableNeuralFx(bool e, float strength) override
	{
#ifdef MITIRU_HAS_DIRECTML
		// 描く途中で作らないよう、有効にした時点で DirectML の device を用意する
		m_neuralFx.setEnabled(e && ensureDirectMLDx12());
		m_neuralFx.setStrength(strength);
#else
		(void)e; (void)strength;
#endif
	}
	/// @brief endFrame (Live2D 後) に backbuffer へ DirectML 後処理を適用する。
	void neuralFxTickDx12()
	{
#ifdef MITIRU_HAS_DIRECTML
		if (!m_neuralFx.enabled() || !m_graphicsCmdList || !m_dmlDevice) { return; }
		auto* bb = m_device->currentBackBuffer();
		if (!bb) { return; }
		const auto d = bb->nativeResource()->GetDesc();
		const int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
		if (m_neuralFx.needsRebuild(w, h)) { m_device->waitForGpu(); }
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
		auto* bb = m_device->currentBackBuffer();
		if (!bb) { return; }
		const auto d = bb->nativeResource()->GetDesc();
		const int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
		if (!m_relight.ensure(m_d3dDevice, FRAME_COUNT, w, h)) { return; }
		m_relight.apply(m_graphicsCmdList.Get(), m_uploadRing, m_frameCursor, bb->nativeResource(), bb->rtvHandle(), w, h);
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
	///        → relight へ深度を渡す。間引き (数フレームに 1 回) で stall を抑える。深度未取得時は relight が
	///        輝度プロキシにフォールバックする。
	void relightDepthTickDx12()
	{
#ifdef MITIRU_HAS_ONNX
		if (!m_relight.enabled() || m_relightModel.empty() || m_device == nullptr) { return; }
		const unsigned f = m_relightFrame++;
		if (f < 180u) { return; }                  // 起動直後はスキップ (資産の読み込みを優先)
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
		const glm::vec4 clip = m_viewProjNoJitter * glm::vec4(wx, wy, wz, 1.0f);
		if (clip.w <= 0.0001f) { u = v = -1.0f; return false; }
		const float nx = clip.x / clip.w, ny = clip.y / clip.w;
		u = nx * 0.5f + 0.5f;
		v = 0.5f - 0.5f * ny;   // NDC y↑ → 画面 v↓
		return (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);
	}

	/// @brief このフレームで 3D 描画が行われたかを返す
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }
	/// @brief フレームアクティブフラグをリセットする（Engine 側で毎フレーム呼ぶ）
	void resetFrameActive() noexcept override { m_frameActive = false; }

	/// @brief 複数ライトを設定する（DX12）
	/// @details kMaxLights を超える分は捨てる。先頭は主光源、点光源・スポットは
	///          useMultiLight=true の間だけ毎フレームの局所光に加わる。
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
	/// @details Toon / Phong / Unlit / Flat / PBR を実装。他モードは Toon フォールバック。
	///          PBR は setEnvironment の環境マップがあれば IBL、無ければ半球アンビエントで照らす。
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

	/// @brief 以後の描画が影を落とすかを切り替える。フレーム頭で true に戻る。
	void setShadowCaster(bool enabled) noexcept override { m_shadowCasterEnabled = enabled; }

	/// @brief 以後の描画を輪郭線の検出から外す。フレーム頭で true に戻る。
	void setOutlineCaster(bool enabled) noexcept override { m_outlineCasterEnabled = enabled; }

	/// @brief 当フレームに影キャスタとして記録された描画の数
	[[nodiscard]] std::size_t shadowCasterCount() const noexcept
	{
		return m_shadowCommands.size();
	}

	/// @brief シャドウマップが有効か
	[[nodiscard]] bool isShadowEnabled() const noexcept { return m_shadowEnabled; }

	/// @brief カスケードシャドウ (B13) を頼む。2 カスケード (近距離 = cascadeNearHalfExtent / 遠距離 = orthoHalfExtent) を
	///        m_directionalShadow.config().cascadeSplitDistance で切り替える。頼まない (false) 時はエンジンの既定
	///        (applyShadowCascadeMode) になる
	void setCascadedShadowEnabled(bool enabled) noexcept override
	{
		m_shadowCascadesRequested = enabled ? std::max(m_shadowCascadesRequested, 2) : 1;   // 3 を指定済みなら下げない
		applyShadowCascadeMode();
	}

	void setShadowCascadeCount(int count) override
	{
		m_shadowCascadesRequested = std::clamp(count, 1, 3);
		applyShadowCascadeMode();
	}

	/// @brief カスケードシャドウが有効か
	[[nodiscard]] bool isCascadedShadowEnabled() const noexcept override
	{
		return m_cascadedShadowEnabled;
	}

	void setCascadedShadowAutoFit(bool enabled, float maxDistance) override
	{
		m_shadowAutoFitRequested = enabled;
		if (maxDistance > 0.0f) { m_directionalShadow.config().cascadeMaxDistance = maxDistance; }
		applyShadowCascadeMode();
	}

	/// @brief ゲームの頼みと画質の上限から、使うカスケードの数と視錐台への合わせ方を決める。
	///        カスケードを頼まない (1 段) 時は、画質の上限まで段を使い、カメラの視錐台に合わせる (エンジンの既定)。
	///        20 m 四方を 1 枚で覆う従来の単一マップは、カメラの近くで 1 texel が画面の 9 画素にもなり、影の縁が段になってぼける
	void applyShadowCascadeMode() noexcept
	{
		DirectionalShadowConfig& cfg = m_directionalShadow.config();
		const int cap = std::clamp(m_qualityCaps.maxShadowCascades, 1, 3);
		const bool engineDefault = m_shadowCascadesRequested <= 1;
		cfg.cascadeCount = engineDefault ? cap : std::min(m_shadowCascadesRequested, cap);
		cfg.autoFitCascades = engineDefault || m_shadowAutoFitRequested;
		m_cascadedShadowEnabled = cfg.cascadeCount > 1;
	}

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
	/// @brief 現在の (shaderMode, outlineMode) に対する PSO を選ぶ
	/// @param doubleSided 真なら背面カリングを切った双子を返す (glTF doubleSided)
	[[nodiscard]] ID3D12PipelineState* selectMainPSO(bool doubleSided = false) const noexcept;

	void drawMeshEx(const Mesh& mesh, const sgc::Mat4f& worldTransform, const Material& material,
	                const MaterialMaps* maps, const DrawTint& tint);
	[[nodiscard]] bool cullMesh(const Mesh& mesh, const sgc::Mat4f& world);
	void drawSkyboxBeforeFirstDraw();
	void rebuildViewportResources();
	[[nodiscard]] bool drawMeshBuffers(const Mesh& mesh);

public:

	/// @brief 現在の蓄積済みライト配列（テスト・診断用）
	[[nodiscard]] std::span<const Light> lights() const noexcept
	{
		return std::span<const Light>(m_lights.data(), m_lights.size());
	}

	/// @brief キューブマップ skybox をセットする（DX12）
	/// @details テクスチャ部分（TextureCube + upload + SRV）だけをリセットし、
	///          PSO / root signature / VB / IB / CB は再利用する。
	///          これで 1/2/3 のような頻繁な variant 切替のたびに
	///          shader compile + PSO 作成が走ることはない（「もっさり」防止）。
	void setSkybox(const Cubemap& cubemap) override;

	void setSkyboxEnabled(bool enabled) override
	{
		m_skyboxEnabled = enabled;
	}

	[[nodiscard]] bool isSkyboxEnabled() const noexcept override
	{
		return m_skyboxEnabled && m_skyboxCubemap.valid();
	}

	/// @brief IBL 用環境キューブマップをセットする（DX12）
	/// @details CPU 側の畳み込みと GPU アップロードは次の描画まで遅延する
	void setEnvironment(const Cubemap& cubemap) override
	{
		m_pbrEnvironmentCubemap = cubemap;
		m_pbrEnvironmentTextureReady = false;
	}

	/// @brief endFrame() 内で 2D オーバーレイを自動描画する
	[[nodiscard]] bool hasOverlaySupport() const noexcept override { return true; }

	/// @brief コマンドリストを取得する
	/// @details beginFrame()後〜endFrame()前に呼び出すこと。
	[[nodiscard]] ID3D12GraphicsCommandList* getCommandList() const noexcept
	{
		return m_graphicsCmdList.Get();
	}

	/// @brief Dx12Device を返す（IRenderer3D）
	[[nodiscard]] void* nativeDevice() const noexcept override
	{
		return static_cast<void*>(m_device);
	}

	/// @brief Dx12SwapChain を返す（IRenderer3D）
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
