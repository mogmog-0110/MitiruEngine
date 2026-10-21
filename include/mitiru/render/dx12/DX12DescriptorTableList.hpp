// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。3D の資源を写して持つ共有の記述子の組の一覧 (ADR 0088)
//
// 組ごとに「何を写すか」と「そこへ書く関数」をここに並べると、beginFrame の頭と finalizeFrame の提出の直前に、写す物の世代
// (gfx::objectEpoch) が変わった組だけが書き替わる。資源を作り替える側は記述子を書かない。新しい効果が深度や影の絵などを
// 記述子に写すときは、ここに 1 組足す。作り替える経路を探して書き込みを足す必要はない。
// 写す物にはヒープも入れる。ヒープだけが作り替わった組は、中身が空になっているので書き直す。
// 毎フレーム自分の区画へ書く表 (場面の表、粒、止め絵など) と、資源と同じ関数で作る自分だけの表 (GTAO、OIT、DDGI) は載せない。
// 副ビューの組は、副ビューの描画の間だけ入れ替わるメンバーを読むので、ここではなく ViewAtmosphere が世代を持つ。

public:

/// @brief 登録した組の名前と、世代の違いで書き替えた回数。試験と診断に使う
[[nodiscard]] const dx12::DescriptorTableRegistry& descriptorTables() const noexcept { return m_tables; }

private:

dx12::DescriptorTableRegistry m_tables;

/// @brief 組を登録して最初の書き込みをする。initialize の最後に 1 回呼ぶ
void registerDescriptorTables()
{
	using dx12::DescriptorSources;
	if (m_tables.tableCount() != 0)
	{
		(void)refreshDescriptorTables(dx12::RefreshPhase::Lazy);
		return;
	}
	m_tables.add("atmosphere",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_atmoHeap.Get());
		             s.add("sky.transmittance", m_atmoTransmittance.Get());
		             s.add("sky.multiScatter", m_atmoMultiScatter.Get());
		             s.add("sky.skyView", m_atmoSkyView.Get());
		             s.add("aerial", m_atmoAerial.Get());
		             s.add("shadow.near", m_shadowMap.nativeResource());
		             s.add("shadow.far", m_shadowMapFar.nativeResource());
		             s.add("fog.inject0", m_fogInject[0].Get());
		             s.add("fog.inject1", m_fogInject[1].Get());
		             s.add("fog.integrated", m_fogIntegrated.Get());
		             s.add("depth", m_depthBuffer.Get());
	             },
	             [this] { writeAtmosphereTable(); });
	m_tables.add("outline.depthNormal",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_depthSRVHeap.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("normal", m_normalBuffer.Get());
	             },
	             [this] { updateOutlinePostSRVs(); });
	m_tables.add("outline.colorCopy",
	             [this](DescriptorSources& s) {
		             s.add("heap.colorEdge", m_colorEdgeSRVHeap.Get());
		             s.add("heap.depthColor", m_depthColorSRVHeap.Get());
		             s.add("colorCopy", m_colorCopyBuffer.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("normal", m_normalBuffer.Get());
	             },
	             [this] { writeColorCopyViews(); });
	m_tables.add("tonemap",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_hdrIntermediateSrvHeap.Get());
		             s.add("hdr", m_hdrIntermediateBuffer.Get());
		             s.add("ao", m_ssaoTex[0].Get());
		             s.add("bloom", m_bloom.tex[2].Get());
	             },
	             [this] { writeTonemapViews(); });
	m_tables.add("ssao",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_ssaoSrvHeap.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("normal", m_normalBuffer.Get());
		             s.add("ao0", m_ssaoTex[0].Get());
		             s.add("ao1", m_ssaoTex[1].Get());
	             },
	             [this] { writeSsaoViews(); });
	m_tables.add("bloom",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_bloom.srvHeap.Get());
		             s.add("hdr", m_hdrIntermediateBuffer.Get());
		             s.add("half0", m_bloom.tex[0].Get());
		             s.add("quarter", m_bloom.tex[1].Get());
	             },
	             [this] { writeBloomChainViews(m_bloom, m_hdrIntermediateBuffer.Get()); });
	m_tables.add("dof",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_dofSrvHeap.Get());
		             s.add("fxaa", m_fxaaIntermediate.Get());
		             s.add("depth", m_depthBuffer.Get());
	             },
	             [this] { writeDofViews(); });
	m_tables.add("temporal",
	             [this](DescriptorSources& s) {
		             s.add("heap.srv", m_temporalSrvHeap.Get());
		             s.add("heap.rtv", m_temporalRtvHeap.Get());
		             s.add("heap.dsv", m_temporalDsvHeap.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("fxaa", m_fxaaIntermediate.Get());
		             s.add("velocityObject", m_velocityObjectTex.Get());
		             s.add("velocityDepth", m_velocityDepth.Get());
		             s.add("velocity", m_velocityTex.Get());
		             s.add("taaHistory0", m_taaHistory[0].Get());
		             s.add("taaHistory1", m_taaHistory[1].Get());
		             s.add("mbTileMax", m_mbTileMax.Get());
		             s.add("mbNeighborMax", m_mbNeighborMax.Get());
	             },
	             [this] { writeTemporalViews(); });
	m_tables.add("upscale",
	             [this](DescriptorSources& s) {
		             s.add("heap.srv", m_upscaleSrvHeap.Get());
		             s.add("heap.rtv", m_upscaleRtvHeap.Get());
		             s.add("sceneLdr", m_sceneLdr.Get());
		             s.add("history0", m_upscaleHistory[0].Get());
		             s.add("history1", m_upscaleHistory[1].Get());
		             s.add("velocity", m_velocityTex.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("fsrOutput", m_fsrOutput.Get());
		             s.add("oit.reveal", m_oit.revealSampledResource());
	             },
	             [this] { writeUpscaleViews(); });
	m_tables.add("ssr",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_ssrHeap.Get());
		             s.add("depth", m_depthBuffer.Get());
		             s.add("hdr", m_hdrIntermediateBuffer.Get());
		             s.add("hzb", m_ssrHzb.Get());
		             s.add("color", m_ssrColor.Get());
	             },
	             [this] { writeSsrViews(); });
	m_tables.add("clod.inject",
	             [this](DescriptorSources& s) {
		             s.add("heap", m_clodInjectHeap.Get());
		             s.add("color", m_clod.colorTexture());
		             s.add("visBuffer", m_clod.visBuffer());
	             },
	             [this] { writeClodInjectViews(); });
	(void)refreshDescriptorTables(dx12::RefreshPhase::Lazy);
}

/// @brief 写す物の世代が違う組を書き替える。副ビューの描画の間は主ビューのメンバーが入れ替わっているので何もしない
std::size_t refreshDescriptorTables(dx12::RefreshPhase phase)
{
	if (m_activeView != nullptr || m_d3dDevice == nullptr)
	{
		return 0;
	}
	return m_tables.refresh(phase, [this] {
		if (m_device != nullptr) { m_device->waitForGpu(); }
	});
}

/// @brief 空と空気遠近とフォグの表を全部書く。組を一度空にしてから書くので、無くなった資源の記述子は null になる
void writeAtmosphereTable()
{
	if (!m_atmoHeap)
	{
		return;
	}
	for (UINT set = 0; set < kAtmoSetCount; ++set) { clearAtmosphereSet(set); }
	writeAtmosphereViews();
	writeFogViews();
}
