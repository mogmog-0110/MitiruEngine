#pragma once

/// @file ClodRenderer.hpp
/// @brief clod 仮想ジオメトリパス (クラスタ LOD + GPU 駆動カリング + visbuffer)
/// @details Renderer3D_DX12 が所有する世界ジオメトリパス。ゲームの drawModel
///          intent を集め、endFrame 先頭で offscreen (linear HDR + visbuffer) に
///          描画する。合成は Renderer3D_DX12 側の inject パスが行う。ADR 0027。
///          SM 6.6 (mesh shader / int64 atomics / dynamic resources) 必須 —
///          未対応環境では supported()==false となり全 API が no-op。

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/dx12/Dx12UploadRing.hpp>
#include <mitiru/render/dx12/clod/ClodFormat.hpp>
#include <mitiru/render/dx12/clod/ClodScene.hpp>
#include <mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mitiru::render::clod
{

/// @brief 1 フレーム上限インスタンス数 (InstVis ビット幅と ring 容量を規定)
inline constexpr uint32_t kClodMaxInstances = 4096;

/// @brief drawModel intent (フレーム毎に溜めて record で消費)
struct PendingInstance
{
	int model = -1;
	float pos[3] = {};
	float rotYDeg = 0.0f;
	float scale = 1.0f;
};

/// @brief clod 世界ジオメトリパスの GPU 実装
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

	[[nodiscard]] ComPtr<ID3D12Resource> makeBuffer(uint64_t bytes, D3D12_HEAP_TYPE heap,
	                                                D3D12_RESOURCE_STATES state,
	                                                D3D12_RESOURCE_FLAGS flags) const;
	[[nodiscard]] ComPtr<ID3D12PipelineState> makeComputePso(const uint8_t* dxil, size_t size) const;

	// ── Frame (ClodRenderer_Frame_impl.hpp) ──
	void fillDrawCB(ClodDrawCB& cb, const Camera3D& camera, const float lightDir[3],
	                const float lightColor[3], float ambient) const;
	void buildFrameTables(D3D12_GPU_VIRTUAL_ADDRESS& instances, D3D12_GPU_VIRTUAL_ADDRESS& meshTable);
	void bindCompute(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb) const;
	void bindGraphics(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS cb) const;
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
	ComPtr<ID3D12Resource> m_bGroups, m_bClusters, m_bPos, m_bNorm, m_bUv;
	ComPtr<ID3D12Resource> m_bVerts, m_bTris, m_bMats, m_bMeshRanges, m_bBvh;
	std::vector<ComPtr<ID3D12Resource>> m_textures;
	std::vector<ComPtr<ID3D12Resource>> m_pendingUploads;   ///< 今フレームの copy 元 (実行完了まで保持)
	bool m_staticCopyQueued = false;

	// 画面サイズ依存 (resize で作り直し)
	ComPtr<ID3D12Resource> m_visBuf, m_overdraw, m_colorTex, m_hzb;
	uint32_t m_width = 0, m_height = 0;
	uint32_t m_hzbW = 0, m_hzbH = 0, m_hzbMips = 0;

	// 永続 (固定サイズ)
	ComPtr<ID3D12Resource> m_bInstVis, m_bVisListHw, m_bVisListSw, m_bMarked;
	ComPtr<ID3D12Resource> m_bCounters, m_bIndArgs, m_bStats, m_bQueueA, m_bQueueB;
	dx12::Dx12UploadRing m_ring;

	ComPtr<ID3D12DescriptorHeap> m_heap;   ///< [0]=offscreen UAV, [1..mips]=HZB, [1+mips+i]=texture SRV

	float m_prevView[12] = {};
	bool m_prevViewValid = false;
	uint32_t m_frameInstanceCount = 0;
};

} // namespace mitiru::render::clod

#include <mitiru/render/dx12/clod/detail/ClodRenderer_Setup_impl.hpp>
#include <mitiru/render/dx12/clod/detail/ClodRenderer_Frame_impl.hpp>
