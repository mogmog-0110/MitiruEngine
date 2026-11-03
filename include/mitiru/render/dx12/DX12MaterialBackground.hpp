// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12UserMaterial.hpp の後に include)。背景を読む自作の材質 (ADR 0123)
// readsBackground の材質の描画は、呼ばれた時点の root 引数と VB/IB を積むだけで描かない。endFrame の水面の後・半透明の前 (副ビューは
// finishView の同じ所) で、場面の色を 1 枚写し、積んだ描画を写しの後に描く。だから読む材質は自分も、他の読む材質も見ない。
// 状態 (材質の値、陰影、トゥーンの段、光のレイヤー) は積む時に root 引数の中身 (定数バッファの GPU アドレス) へ焼いてあるので、
// 後からゲームが状態を替えても、積んだ描画は変わらない。

public:

/// @brief このフレームで場面の色を背景として写した回数 (試験・診断用)。背景を読む材質を描かないフレームでは 0
[[nodiscard]] int backgroundCopyCount() const noexcept { return m_backgroundCopies; }

private:

struct BackgroundDraw
{
	ComPtr<ID3D12PipelineState> pso;   ///< ComPtr で持つ。作り直しで手放された PSO を、積んだ描画が参照しない
	D3D12_GPU_VIRTUAL_ADDRESS cbTransform = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbLighting = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbDrawEx = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbShadow = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE materialTable{};
	D3D12_GPU_DESCRIPTOR_HANDLE sceneTable{};
	D3D12_VERTEX_BUFFER_VIEW vertexViews[2]{};
	D3D12_INDEX_BUFFER_VIEW indexView{};
	UINT vertexCount = 0;
	UINT indexCount = 0;
	int pass = 0;   ///< 積んだビュー (0 = 主ビュー)
};

std::vector<BackgroundDraw> m_backgroundQueue;
ComPtr<ID3D12RootSignature> m_userBackgroundRootSig;   ///< 0〜8 は主の root signature と同じ。9 が背景の表 { t45 }
gfx::GpuResource            m_backgroundCopy;
UINT                        m_backgroundCopyWidth = 0;
UINT                        m_backgroundCopyHeight = 0;
int                         m_backgroundCopies = 0;

[[nodiscard]] bool activeUserMaterialReadsBackground() const noexcept
{
	return m_activeUserMaterial > 0 && m_activeUserMaterial <= static_cast<int>(m_userMaterials.size()) &&
	       m_userMaterials[static_cast<std::size_t>(m_activeUserMaterial) - 1].src.desc.readsBackground;
}

/// @brief mesh の VB/IB と接線の view を取る。drawMeshBuffers と同じ取り方で、積む時に確かめて描く時は張るだけにする
[[nodiscard]] bool captureMeshViews(const Mesh& mesh, BackgroundDraw& d)
{
	const bool gpuOnly = mesh.isGpuOnly();
	const UINT vbSize = static_cast<UINT>(mesh.vertexCount() * sizeof(Vertex3D));
	auto* vb = gpuOnly ? boundGpuBuffer(m_meshVBCache, mesh) : acquireMeshBuffer(m_meshVBCache, mesh, mesh.vertices().data(), vbSize);
	if (!vb) { return false; }
	d.vertexCount = static_cast<UINT>(mesh.vertexCount());
	d.indexCount = static_cast<UINT>(mesh.indexCount());
	ID3D12Resource* ib = nullptr;
	if (d.indexCount != 0)
	{
		const UINT ibSize = d.indexCount * static_cast<UINT>(sizeof(uint32_t));
		ib = gpuOnly ? boundGpuBuffer(m_meshIBCache, mesh) : acquireMeshBuffer(m_meshIBCache, mesh, mesh.indices().data(), ibSize);
		if (!ib) { return false; }
		d.indexView = {ib->GetGPUVirtualAddress(), ibSize, DXGI_FORMAT_R32_UINT};
	}
	ID3D12Resource* tangent = tangentBufferOrNull(mesh);
	d.vertexViews[0] = {vb->GetGPUVirtualAddress(), vbSize, sizeof(Vertex3D)};
	d.vertexViews[1] = {tangent ? tangent->GetGPUVirtualAddress() : 0, tangent ? d.vertexCount * static_cast<UINT>(sizeof(float) * 4) : 0,
	                    static_cast<UINT>(sizeof(float) * 4)};
	return true;
}

/// @brief issueMeshDraw の代わり。bindForwardDraw が張る root 引数を、描かずに積む
void queueBackgroundDraw(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const MaterialMaps* maps, const DrawTint& tint)
{
	flushDrawRun();   // bindForwardDraw と同じ。溜めた描画より後に定数を作る
	features::use(features::Feature::MaterialBackground);
	BackgroundDraw d;
	d.pso = selectMainPSO(material.doubleSided);
	d.cbTransform = uploadTransformCB(world);
	d.cbLighting = uploadLightingCB(material, tint);
	d.cbDrawEx = uploadDrawExCB(material, maps, tint);
	d.cbShadow = uploadShadowCB();
	if (!d.pso || d.cbTransform == 0 || d.cbLighting == 0 || d.cbDrawEx == 0) { return; }
	d.sceneTable = ensureSceneTable();
	d.materialTable = writeMaterialTable(resolveAlbedo(material, maps), maps);
	if (!captureMeshViews(mesh, d)) { return; }
	d.pass = currentPass();
	m_backgroundQueue.push_back(std::move(d));
	++m_drawCallCount;   // 副ビューだけに描いた主ビューを隠す判定 (mainViewHidden) が数えるので、描く時でなく積む時に数える
}

/// @brief 水面の後・半透明の前に呼ぶ。今のビューに積んだ読む材質の描画があれば、場面の色を写してから描く
void renderBackgroundMaterialPass()
{
	const int pass = currentPass();
	const bool any = std::any_of(m_backgroundQueue.begin(), m_backgroundQueue.end(), [pass](const BackgroundDraw& d) { return d.pass == pass; });
	if (!any) { return; }
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	if (!m_userBackgroundRootSig || !m_msaaColorRtvHeap || !m_normalRTVHeap || !m_dsvHeap ||
	    !ensureSceneCopy(m_backgroundCopy, m_backgroundCopyWidth, m_backgroundCopyHeight, L"Renderer3D material background") ||
	    !reserveSrvSlots(1, cpu, gpu))
	{
		debug::verboseOnce("dx12.usermat.background", "背景の写しを用意できなかったので、背景を読む材質の物は描きません。");
		m_drawCallCount -= static_cast<int>(std::count_if(m_backgroundQueue.begin(), m_backgroundQueue.end(), [pass](const BackgroundDraw& d) { return d.pass == pass; }));
		return;
	}
	MITIRU_ZONE_NAMED("Render::Dx12::BackgroundMaterialPass");
	auto* cl = m_graphicsCmdList.Get();
	copySceneColorTo(m_backgroundCopy);
	++m_backgroundCopies;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(m_backgroundCopy.Get(), &srv, cpu);

	bindMainTargets();
	cl->SetGraphicsRootSignature(m_userBackgroundRootSig.Get());
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D12DescriptorHeap* heaps[] = {m_albedoSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	for (const BackgroundDraw& d : m_backgroundQueue)
	{
		if (d.pass != pass) { continue; }
		cl->SetPipelineState(d.pso.Get());
		cl->SetGraphicsRootConstantBufferView(0, d.cbTransform);
		cl->SetGraphicsRootConstantBufferView(1, d.cbLighting);
		cl->SetGraphicsRootConstantBufferView(2, d.cbDrawEx);
		if (d.cbShadow != 0) { cl->SetGraphicsRootConstantBufferView(3, d.cbShadow); }
		if (d.materialTable.ptr != 0) { cl->SetGraphicsRootDescriptorTable(4, d.materialTable); }
		if (d.sceneTable.ptr != 0) { cl->SetGraphicsRootDescriptorTable(5, d.sceneTable); }
		bindFrameLighting();
		cl->SetGraphicsRootDescriptorTable(9, gpu);
		cl->IASetVertexBuffers(0, 2, d.vertexViews);
		if (d.indexCount == 0)
		{
			cl->DrawInstanced(d.vertexCount, 1, 0, 0);
		}
		else
		{
			cl->IASetIndexBuffer(&d.indexView);
			cl->DrawIndexedInstanced(d.indexCount, 1, 0, 0, 0);
		}
	}
	restoreMainState();
}
