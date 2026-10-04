#pragma once

/// @file ClodRenderer.hpp
/// @brief clod 仮想ジオメトリパス (クラスタ LOD + GPU 駆動カリング + visbuffer)
/// @details Renderer3D_DX12 が所有する世界ジオメトリパス。ゲームの drawModel
///          intent を集め、endFrame 先頭で offscreen (linear HDR + visbuffer) に
///          描画する。合成は Renderer3D_DX12 側の inject パスが行う。
///          SM 6.6 (mesh shader / int64 atomics / dynamic resources) 必須。
///          未対応環境では supported()==false となり全 API が no-op。

#include <mitiru/asset/AssetReload.hpp>
#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/dx12/Dx12TextureUpload.hpp>
#include <mitiru/render/dx12/Dx12UploadRing.hpp>
#include <mitiru/render/dx12/clod/ClodFormat.hpp>
#include <mitiru/render/dx12/clod/ClodScene.hpp>
#include <mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp>
#include <mitiru/render/dx12/clod/ClodShadowMesh.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::render::clod
{

/// @brief 1 フレーム上限インスタンス数 (InstVis ビット幅と ring 容量を規定)
inline constexpr uint32_t kClodMaxInstances = 4096;
/// @brief resolve がフレームごとに読む SRV の数。t39..t43 は間接光 (並びは DX12SceneTableLayout.hpp の間接光の 5 枚)、
///        t44 / t45 は太陽の影マップ (カスケード 0 と、カスケード 1・2 のアトラス)
inline constexpr uint32_t kClodFrameSrvs = 7;

/// @brief drawModel intent (フレーム毎に溜めて record で消費)
struct PendingInstance
{
	int model = -1;
	float pos[3] = {};
	float rotYDeg = 0.0f;
	float scale = 1.0f;
};

/// @brief clod 世界ジオメトリパスの GPU 実装
/// @brief drawModel がこの device で動かない理由。動くなら nullptr
/// @details ClodRenderer::checkCaps の中身をここに出してある。テストが同じ判定を
///          自前で書き直すと、caps の条件が増えたときに片方だけ古くなるため。
///          戻り値は静的文字列なので寿命を気にしなくてよい。
[[nodiscard]] const char* clodUnsupportedReason(ID3D12Device* device);

class ClodRenderer
{
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// @brief 初期化。SM6.6 系 caps が無ければ false (以降 no-op)
	bool initialize(ID3D12Device* device, UINT frameCount);

	[[nodiscard]] bool supported() const noexcept { return m_supported; }

	/// @brief モデルロード等で古い GPU 資源を解放する前に呼ぶ待ち (統合側が結線)
	std::function<void()> waitIdle;
	/// @brief フレームの SRV kClodFrameSrvs 枚を、渡した場所から順に書く (統合側が結線)。record がフレームごとの枠へ呼ぶ。
	///        焼いた光 (setLocalLights の CbCluster) か太陽の影 (setSunShadow) を使うなら、これも結線する
	std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE)> writeFrameSrvs;

	/// @brief drawModel intent を積む。未知の名前は vfs から遅延ロード
	/// @param path .clod への vfs パス (テクスチャは同ディレクトリ基準)
	void queueInstance(const char* path, const float pos[3], float rotYDeg, float scale)
	{
		if (!m_supported || m_pending.size() >= kClodMaxInstances) { return; }
		const int model = ensureModel(path);
		if (model < 0) { return; }
		PendingInstance p;
		p.model = model;
		p.pos[0] = pos[0];
		p.pos[1] = pos[1];
		p.pos[2] = pos[2];
		p.rotYDeg = rotYDeg;
		p.scale = scale;
		m_pending.push_back(p);
	}

	[[nodiscard]] bool hasWork() const noexcept { return m_supported && !m_pending.empty(); }

	/// @brief path を登録済みか (読めなかった負キャッシュも含む)。未登録なら queueInstance がその場で読む
	[[nodiscard]] bool knowsModel(std::string_view path) const { return m_registry.find(path) != m_registry.end(); }

	/// @brief path のモデルを太陽の影に落とす Mesh (一番細かい段)。読めていなければ nullptr
	[[nodiscard]] const Mesh* shadowMesh(std::string_view path) const
	{
		const int model = modelIndex(path);
		return (model >= 0 && static_cast<std::size_t>(model) < m_shadowMeshes.size()) ? m_shadowMeshes[static_cast<std::size_t>(model)].get()
		                                                                             : nullptr;
	}

	/// @brief 登録した model index。読めなかったら -1、まだ登録していなければ -2
	[[nodiscard]] int modelIndex(std::string_view path) const
	{
		const auto it = m_registry.find(path);
		return it != m_registry.end() ? it->second : -2;
	}

	/// @brief 読み込みの前半。変換の cache を作り (初回だけ)、.clod の中身を読む。どのスレッドから呼んでもよい
	[[nodiscard]] static std::optional<std::vector<uint8_t>> readModelBlob(const std::string& path, std::string& err);

	/// @brief 読み込みの後半。連結シーンの末尾へ足して登録する。blob が無ければ err を知らせて負キャッシュにする
	/// @return model index。失敗は -1
	int addModel(const std::string& path, const std::optional<std::vector<uint8_t>>& blob, const std::string& err);

	/// @brief 変更されたファイルから読んだモデルを忘れ、次の queueInstance で読み直させる
	/// @details 読み込み済みの幾何は連結シーンに残したまま、新しい版を末尾に足す (開発中のホットリロード専用で、
	///          詰め直しの待ちを入れない)。kClodMaxMeshes に達したらそれ以上は読めない。
	/// @return 忘れた登録の数
	int forgetModel(const std::filesystem::path& changed)
	{
		int forgotten = 0;
		for (auto it = m_registry.begin(); it != m_registry.end();)
		{
			const bool hit = asset::sameAssetFile(it->first, changed);
			it = hit ? m_registry.erase(it) : std::next(it);
			forgotten += hit ? 1 : 0;
		}
		return forgotten;
	}

	/// @brief フレーム記録: cull → raster → HZB → resolve を cmd に積む
	/// @details 呼び手 (Renderer3D_DX12) の open な command list に追記する。
	///          終了時、offscreen color と visbuffer は UAV state のまま
	void record(ID3D12GraphicsCommandList* cmd, const Camera3D& camera,
	            const float lightDir[3], const float lightColor[3], float ambient,
	            uint32_t width, uint32_t height, UINT frameIndex);

	/// @brief inject パス用: offscreen color / visbuffer を PS 読み state へ
	void transitionForInject(ID3D12GraphicsCommandList* cmd);
	/// @brief inject 後: 次フレームに備えて UAV state へ戻す
	void transitionAfterInject(ID3D12GraphicsCommandList* cmd);

	[[nodiscard]] ID3D12Resource* colorTexture() const noexcept { return m_colorTex.Get(); }
	[[nodiscard]] ID3D12Resource* visBuffer() const noexcept { return m_visBuf.Get(); }
	[[nodiscard]] uint32_t width() const noexcept { return m_width; }
	[[nodiscard]] uint32_t height() const noexcept { return m_height; }

	/// @brief 射影の後に足す NDC の平行移動 (TAA の画素内ずらし)。次の record から効く
	void setProjectionJitter(float ndcX, float ndcY) noexcept
	{
		m_jitterNdc[0] = ndcX;
		m_jitterNdc[1] = ndcY;
	}

	/// @brief このフレームの局所光と焼いた光 (Renderer3D_DX12 の光の一覧・froxel のビット集合・CbCluster)。次の record の
	///        resolve が足す。cluster が 0 なら局所光も焼いた光も使わない。光が 0 個のフレームは lights と masks が 0 でよい
	void setLocalLights(D3D12_GPU_VIRTUAL_ADDRESS lights, D3D12_GPU_VIRTUAL_ADDRESS masks,
	                    D3D12_GPU_VIRTUAL_ADDRESS cluster) noexcept
	{
		m_localLightsVA = lights;
		m_clusterMasksVA = masks;
		m_clusterCbVA = cluster;
	}

	/// @brief このフレームの太陽の影の CbShadow (前方の描画と同じ値)。次の record の resolve が影マップを引く。0 なら影を引かない
	void setSunShadow(D3D12_GPU_VIRTUAL_ADDRESS shadowCb) noexcept { m_shadowCbVA = shadowCb; }

	/// @brief 陰影の番号 (0 トゥーン、1 Phong、2 PBR。Renderer3D_DX12::worldShadeIndex)。次の record から効く。
	///        直接光はどれも Lambert のままで、焼いた光の使い方 (トゥーンの段、PBR の鏡面) だけが変わる
	void setShading(uint32_t shadeIndex) noexcept { m_shadeIndex = shadeIndex; }

	/// @brief フレーム終端で intent を破棄する
	void endFrame() noexcept { m_pending.clear(); }

private:
	// ── Setup (ClodRenderer_Setup_impl.hpp) ──
	[[nodiscard]] bool checkCaps(ID3D12Device* device) const;
	[[nodiscard]] bool createRootSignature();
	[[nodiscard]] bool createPipelines();
	[[nodiscard]] bool createCommandSignatures();
	[[nodiscard]] bool createPersistentBuffers();
	void ensureScreenResources(uint32_t width, uint32_t height);
	void ensureSceneResources(ID3D12GraphicsCommandList* cmd);
	void uploadTextures(ID3D12GraphicsCommandList* cmd);
	void rebuildDescriptorHeap();
	int ensureModel(const char* path);

	[[nodiscard]] gfx::GpuResource makeBuffer(uint64_t bytes, D3D12_HEAP_TYPE heap,
	                                                D3D12_RESOURCE_STATES state,
	                                                D3D12_RESOURCE_FLAGS flags) const;
	[[nodiscard]] ComPtr<ID3D12PipelineState> makeComputePso(const uint8_t* dxil, size_t size) const;

	// ── Frame (ClodRenderer_Frame_impl.hpp) ──
	void fillDrawCB(ClodDrawCB& cb, const Camera3D& camera, const float lightDir[3],
	                const float lightColor[3], float ambient) const;
	void buildFrameTables(D3D12_GPU_VIRTUAL_ADDRESS& instances, D3D12_GPU_VIRTUAL_ADDRESS& meshTable);
	void bindCompute(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb) const;
	void bindGraphics(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb) const;
	void bindLocalLights(ID3D12GraphicsCommandList* cmd, bool compute) const;
	void bindFrameSrvs(UINT frameIndex);
	void uavBarrierAll(ID3D12GraphicsCommandList* cmd) const;
	void recordClears(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb0) const;
	void recordBvhCull(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb0) const;
	void recordDrawPass(ID3D12GraphicsCommandList* cmd, uint32_t pass,
	                    D3D12_GPU_VIRTUAL_ADDRESS cb) const;
	void recordHzbBuild(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb0) const;
	void recordResolve(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb0) const;

	// ── 状態 ──
	ID3D12Device* m_device = nullptr;
	ID3D12Device2* m_device2 = nullptr;
	bool m_supported = false;
	UINT m_frameCount = 3;

	ClodScene m_scene;
	std::vector<std::unique_ptr<Mesh>> m_shadowMeshes;   ///< model index → 影の caster (Mesh の番地を VB の cache の鍵に使うので動かさない)
	std::map<std::string, int, std::less<>> m_registry;   ///< vfs パス → model index (-1 = 失敗の負キャッシュ)
	std::vector<PendingInstance> m_pending;
	uint32_t m_gpuSceneRevision = 0xFFFFFFFFu;   ///< GPU 静的バッファが反映済みの revision

	// フレーム毎テーブル (record 内で構築、ring 上の GPU アドレスを bind で使う)
	std::vector<GpuInstance> m_frameInstances;
	std::vector<GpuMeshRec> m_frameMeshTable;
	D3D12_GPU_VIRTUAL_ADDRESS m_frameInstancesVA = 0;
	D3D12_GPU_VIRTUAL_ADDRESS m_frameMeshTableVA = 0;
	uint32_t m_frameItemCount = 0;
	uint32_t m_frameMeshCount = 0;

	ComPtr<ID3D12RootSignature> m_rootSig;
	ComPtr<ID3D12PipelineState> m_meshPso;
	ComPtr<ID3D12PipelineState> m_cullPso, m_prepPso, m_clearPso, m_resolvePso;
	ComPtr<ID3D12PipelineState> m_swPso, m_icullPso, m_seedPso, m_travPso, m_prepQPso, m_hzbPso;
	ComPtr<ID3D12CommandSignature> m_dispatchMeshSig, m_dispatchSig;

	// 静的シーン GPU 資源 (revision 変化で作り直し)
	gfx::GpuResource m_bGroups, m_bClusters, m_bPos, m_bNorm, m_bUv;
	gfx::GpuResource m_bVerts, m_bTris, m_bMats, m_bMeshRanges, m_bBvh;
	std::vector<gfx::GpuResource> m_textures;
	std::vector<gfx::GpuResource> m_pendingUploads;   ///< 今フレームの copy 元 (実行完了まで保持)
	bool m_staticCopyQueued = false;

	// 画面サイズ依存 (resize で作り直し)
	gfx::GpuResource m_visBuf, m_overdraw, m_colorTex, m_hzb;
	uint32_t m_width = 0, m_height = 0;
	uint32_t m_hzbW = 0, m_hzbH = 0, m_hzbMips = 0;

	// 永続 (固定サイズ)
	gfx::GpuResource m_bInstVis, m_bVisListHw, m_bVisListSw, m_bMarked;
	gfx::GpuResource m_bCounters, m_bIndArgs, m_bStats, m_bQueueA, m_bQueueB;
	dx12::Dx12UploadRing m_ring;

	/// [0]=offscreen UAV, [1..mips]=HZB, [1+mips+i]=texture SRV, その後ろにフレームごとの SRV (kClodFrameSrvs 枚ずつ)
	ComPtr<ID3D12DescriptorHeap> m_heap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_frameTable = {};   ///< このフレームの SRV の枠 (root 28)
	D3D12_GPU_VIRTUAL_ADDRESS m_shadowCbVA = 0;      ///< 太陽の影の CbShadow (root 29、b3)。0 は影なし

	// 局所光 (root 25 = t12 光、26 = t13 ビット集合、27 = b2 CbCluster)。無いフレームは光 0 個の CB を指す
	D3D12_GPU_VIRTUAL_ADDRESS m_localLightsVA = 0;
	D3D12_GPU_VIRTUAL_ADDRESS m_clusterMasksVA = 0;
	D3D12_GPU_VIRTUAL_ADDRESS m_clusterCbVA = 0;
	D3D12_GPU_VIRTUAL_ADDRESS m_noLightsCbVA = 0;

	float m_prevView[12] = {};
	float m_jitterNdc[2] = {};
	uint32_t m_shadeIndex = 0;
	bool m_prevViewValid = false;
	uint32_t m_frameInstanceCount = 0;
};

} // namespace mitiru::render::clod

#include <mitiru/render/dx12/clod/detail/ClodRenderer_Setup_impl.hpp>
#include <mitiru/render/dx12/clod/detail/ClodRenderer_Frame_impl.hpp>
