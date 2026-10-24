// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12Instancing.hpp の後に include)。
// 連続して同じ見た目の不透明な drawMesh を、自動で 1 回の instanced draw にまとめる (ADR 0093)
//
// 同じメッシュ・同じ材質 (拡散色は除く)・同じ色の調整で、間に別の描画も別の呼び出しも挟まない drawMesh の並びを「ひと続き」と呼ぶ。
// ひと続きの世界行列と拡散色を溜めておき、次の違う呼び出しが来た時に instanced の draw 1 回で描く。描く順は変わらない。
// 1 個で終わったひと続きは、今までと同じ手順 (PSO、root 引数、VB/IB、draw 1 回) でそのまま描くので、まとめない描画は絵も費用も変わらない。
//
// flush は Mesh を読まない。drawMesh は返った後に Mesh を捨ててよい約束なので (影の caster になる描画だけは、影のパスが描画の終わりまで
// Mesh を読むので生きている約束)、描くのに要る GPU のバッファ・頂点数・箱は join のうちに値で写す。キーには Mesh の世代番号 (revision) も入れ、
// 捨てた Mesh と同じアドレスに作った別の Mesh を、前のひと続きに混ぜない。
//
// 「間に別の呼び出しを挟まない」は、IRenderer3D / ISceneFx / IExperimentalRenderer3D の override の最初で flushDrawRun を呼んで守る
// (tools/check_draw_run_flush.py が、override が呼んでいることを調べる)。その呼び出しが状態を変えるか、コマンドを積むかを問わず、
// 一度でも別の呼び出しが来たらそこで描き終える。溜めている間に状態が変わって、前の描画が新しい状態で描かれる事は起きない。
//
// 絵を変えないための約束:
//  - 世界行列と拡散色以外が 1 つでも違えば、ひと続きを区切る。半透明 (OIT)、抜き (Mask)、テクスチャのマップ (glTF)、スキン、
//    GPU にしか中身の無いメッシュ、Fresnel の輪郭は、まとめずに今までの経路で描く。
//  - 拡散色は CPU が定数バッファと同じ式で線形にして、インスタンスの InstTint (a を負にして印を付ける) に入れる。
//    VS は頂点色に何も掛けず、PS が定数バッファの代わりにその色を読む (DX12LitShaders.hpp の chooseDrawColors)。
//    頂点の位置は instanced VS が同じ式で作る。
//  - 動きベクトル・DDGI・透かしの的・視錐台の判定は、今までどおり描画 1 回ごとに記録する。影の caster だけは、ひと続きを 1 個の
//    caster (インスタンスの範囲) にして、深度のパスも 1 回の draw にする。centroid (手動のカスケードの中心) は、各インスタンスの位置を
//    今までと同じ順に足して同じ値になる。
//
// MITIRU_DRAW_MERGE=0 でまとめを切る (まとめた絵と、まとめない絵を同じビルドで比べる用)。

/// @brief ひと続きの条件。世界行列と拡散色以外の、描画を決める物
struct DrawRunKey
{
	const Mesh* mesh = nullptr;
	std::uint64_t revision = 0;   ///< 一時の Mesh が前の Mesh と同じアドレスに作られても、別のひと続きにするため
	const Texture* albedoTexture = nullptr;
	/// specular[4]、shininess、metallic、roughness、alphaCutoff、tint.mul[4]、tint.add[3]
	float f[15] = {};
	bool doubleSided = false;
	bool nearestFilter = false;
};

[[nodiscard]] static bool sameDrawRunKey(const DrawRunKey& a, const DrawRunKey& b) noexcept
{
	return a.mesh == b.mesh && a.revision == b.revision && a.albedoTexture == b.albedoTexture && a.doubleSided == b.doubleSided &&
	       a.nearestFilter == b.nearestFilter && std::memcmp(a.f, b.f, sizeof(a.f)) == 0;
}

[[nodiscard]] static DrawRunKey makeDrawRunKey(const Mesh& mesh, const Material& m, const DrawTint& tint) noexcept
{
	DrawRunKey k;
	k.mesh = &mesh;
	k.revision = mesh.revision();
	k.albedoTexture = m.albedoTexture;
	k.doubleSided = m.doubleSided;
	k.nearestFilter = m.nearestFilter;
	k.f[0] = m.specular.r; k.f[1] = m.specular.g; k.f[2] = m.specular.b; k.f[3] = m.specular.a;
	k.f[4] = m.shininess; k.f[5] = m.metallic; k.f[6] = m.roughness; k.f[7] = m.alphaCutoff;
	for (int i = 0; i < 4; ++i) { k.f[8 + i] = tint.mul[i]; }
	for (int i = 0; i < 3; ++i) { k.f[12 + i] = tint.add[i]; }
	return k;
}

static constexpr std::size_t kRunCasterChunk = 64;   ///< 影の caster の箱を持つ、インスタンスの数
bool m_drawMerge = true;              ///< MITIRU_DRAW_MERGE=0 で切る (initialize で読む)
int m_runMerged = 0;
bool m_runMotion = false;             ///< ひと続きの描画ごとに動きを記録するか (ひと続きの間は変わらないので、始める時に決める)
bool m_runDdgi = false;
bool m_runCaster = false;             ///< ひと続きの描画が影の caster か                  ///< このフレームで、2 個以上のひと続きに入って instanced draw で描いた drawMesh の数
bool m_runActive = false;             ///< ひと続きを溜めている
DrawRunKey m_runKey{};
sgc::Mat4f m_runVisWorld{};           ///< 1 個で終わったひと続きを今までの経路で描き直すための、視錐台を通った最初の 1 個
sgc::Colorf m_runVisDiffuse{};
// flush は Mesh を読まない。呼び出し側は drawMesh が返った後に Mesh を捨ててよいので (影の caster の分を除く)、
// 描くのに要る物は join のうちに値で写しておく
ID3D12Resource* m_runVb = nullptr;
ID3D12Resource* m_runIb = nullptr;
UINT m_runVertexCount = 0;
UINT m_runIndexCount = 0;
Mesh::AABB m_runAabb{};
std::vector<InstanceDataDx12> m_runInstances;   ///< 描くインスタンス (視錐台を通った物)。tint は線形の拡散色で、a は符号を反転した不透明度
std::vector<InstanceDataDx12> m_runCasters;     ///< 影の caster (視錐台で落とす前の全部)。行列の 4 行だけを使う

public:
/// @brief まとめ描きを切り替える (MITIRU_DRAW_MERGE と同じ。まとめた絵とまとめない絵を比べる試験用)
void setDrawMerge(bool enabled)
{
	flushDrawRun();
	m_drawMerge = enabled;
}

/// @brief Phong の instanced PSO を作れない状態にする (試験用。作れない陰影の run がまとまらず、従来の経路で描かれることを確かめる)
void simulateNoInstancedPhongForTest()
{
	flushDrawRun();
	m_phongPS.reset();
	m_instancedPso[InstPhong][0].Reset();
	m_instancedPso[InstPhong][1].Reset();
}

/// @brief このフレームで、まとめて 1 回の instanced draw になった drawMesh の数 (試験・診断用)
[[nodiscard]] int drawMergedCount() const noexcept
{
	const_cast<Renderer3D_DX12*>(this)->flushDrawRun();
	return m_runMerged;
}

private:
/// @brief 溜めているひと続きがあれば、描き終える。IRenderer3D の drawMesh 以外の入口の最初で呼ぶ
void flushDrawRun()
{
	if (m_runActive) { flushDrawRunSlow(); }
}

/// @brief 溜めたひと続きを描かずに捨てる。コマンドリストが閉じている所 (フレームの外、前のフレームが途中で終わった後) で使う
void discardDrawRun() noexcept
{
	m_runActive = false;
	m_runInstances.clear();
	m_runCasters.clear();
}

/// @brief この drawMesh をひと続きに入れてよいか
[[nodiscard]] bool canJoinDrawRun(const Mesh& mesh, const Material& material, const MaterialMaps* maps, const DrawTint& tint) const noexcept
{
	// テクスチャは、描いた後も呼び出し側が持つ約束がない (定数の組み立てが描く時に読む) ので、溜めずにすぐ描く
	return m_drawMerge && maps == nullptr && material.albedoTexture == nullptr && material.alphaMode == Material::AlphaMode::Opaque &&
	       material.diffuse.a * tint.mul[3] == 1.0f && !mesh.isGpuOnly() && !isSkinnedPoolMesh(mesh) &&
	       m_outlineMode != OutlineMode::Fresnel && !m_instancedPipelineFailedDx12;
}

/// @brief 世界行列の 4 行をインスタンスの行へ写す
static void copyWorldRows(const sgc::Mat4f& world, InstanceDataDx12& out) noexcept
{
	static_assert(sizeof(world.m) == sizeof(float) * 16);
	std::memcpy(out.row0, world.m, sizeof(float) * 16);
}

/// @brief drawMesh 1 回ぶんをひと続きへ足す (canJoinDrawRun が true の描画だけ)。視錐台の判定と描画ごとの記録もここでする
void joinDrawRun(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const DrawTint& tint)
{
	MITIRU_ZONE_NAMED("Draw::join");
	const DrawRunKey key = makeDrawRunKey(mesh, material, tint);
	// 視錐台の外の描画も、違うひと続きなら区切る。影の caster を今までと同じ順に積むため
	if (m_runActive && !sameDrawRunKey(m_runKey, key)) { flushDrawRunSlow(); }
	if (!m_runActive)
	{
		m_runActive = true;
		m_runKey = key;
		m_runVb = nullptr;
		m_runIb = nullptr;
		m_runVertexCount = static_cast<UINT>(mesh.vertexCount());
		m_runIndexCount = static_cast<UINT>(mesh.indexCount());
		m_runAabb = mesh.localAABB();
		m_runMotion = motionVectorsActive() && recordsMotion();
		m_runDdgi = m_ddgiRecording;
		m_runCaster = wantsShadowCasters();
		if (m_runCaster) { prepareShadowMeshBuffers(mesh); }
	}
	const bool culled = cullMesh(mesh, world);
	if (m_runMotion) { recordMotionDraw(mesh, world, !culled); }
	if (m_runDdgi) { recordDdgiCaster(mesh, world, drawBaseColor(material, nullptr, tint), material.doubleSided); }
	InstanceDataDx12 d;
	copyWorldRows(world, d);
	d.tint[0] = d.tint[1] = d.tint[2] = 0.0f;
	d.tint[3] = -1.0f;
	if (m_runCaster) { m_runCasters.push_back(d); }
	if (culled) { return; }
	recordFadeTargetDraw(mesh, world);
	drawSkyboxBeforeFirstDraw();
	if (m_runInstances.empty())
	{
		m_runVisWorld = world;
		m_runVisDiffuse = material.diffuse;
		acquireRunBuffers(mesh);
	}
	const sgc::Colorf diffuse = linearTinted(m_linDiffuse(material.diffuse), tint);
	d.tint[0] = diffuse.r; d.tint[1] = diffuse.g; d.tint[2] = diffuse.b; d.tint[3] = -diffuse.a;
	m_runInstances.push_back(d);
}

/// @brief ひと続きが描く VB / IB を GPU へ上げて覚える (ひと続きの最初の、視錐台を通った描画で 1 回)
void acquireRunBuffers(const Mesh& mesh)
{
	const auto& v = mesh.vertices();
	m_runVb = acquireMeshBuffer(m_meshVBCache, mesh, v.data(), m_runVertexCount * static_cast<UINT>(sizeof(Vertex3D)));
	const auto& ix = mesh.indices();
	m_runIb = (m_runVb == nullptr || ix.empty()) ? nullptr
		: acquireMeshBuffer(m_meshIBCache, mesh, ix.data(), m_runIndexCount * static_cast<UINT>(sizeof(std::uint32_t)));
}

/// @brief 影のパスが使うメッシュの VB / IB を GPU へ上げておく (recordMeshShadowCaster と同じ)
void prepareShadowMeshBuffers(const Mesh& mesh)
{
	const auto& v = mesh.vertices();
	const auto& ix = mesh.indices();
	if (acquireMeshBuffer(m_meshVBCache, mesh, v.data(), static_cast<UINT>(v.size() * sizeof(Vertex3D))) == nullptr) { return; }
	if (!ix.empty()) { (void)acquireMeshBuffer(m_meshIBCache, mesh, ix.data(), static_cast<UINT>(ix.size() * sizeof(std::uint32_t))); }
}

/// @brief ひと続きの材質を作り直す。拡散色はインスタンスごとに持つので白にしておく
[[nodiscard]] Material drawRunMaterial() const
{
	Material m;
	m.diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
	m.specular = {m_runKey.f[0], m_runKey.f[1], m_runKey.f[2], m_runKey.f[3]};
	m.shininess = m_runKey.f[4];
	m.metallic = m_runKey.f[5];
	m.roughness = m_runKey.f[6];
	m.alphaCutoff = m_runKey.f[7];
	m.alphaMode = Material::AlphaMode::Opaque;
	m.doubleSided = m_runKey.doubleSided;
	m.nearestFilter = m_runKey.nearestFilter;
	m.albedoTexture = m_runKey.albedoTexture;
	return m;
}

[[nodiscard]] DrawTint drawRunTint() const noexcept
{
	DrawTint t;
	for (int i = 0; i < 4; ++i) { t.mul[i] = m_runKey.f[8 + i]; }
	for (int i = 0; i < 3; ++i) { t.add[i] = m_runKey.f[12 + i]; }
	return t;
}

/// @brief ひと続きの影の caster を、インスタンスの範囲を持つ caster に積む。kRunCasterChunk 個ずつ区切って箱を持たせ、
///        カスケードごとの「投影の外なら描かない」を、描画 1 回ずつの時の粒度に近いまま (大きな街の遠くの塊を描かない) 保つ
void recordRunShadowCasters()
{
	if (m_runCasters.empty()) { return; }
	const Mesh* mesh = m_runKey.mesh;   // 影のパスが描画の終わりまで読むので、caster になる描画の Mesh は生きている
	for (std::size_t begin = 0; begin < m_runCasters.size(); begin += kRunCasterChunk)
	{
		const std::size_t end = std::min(begin + kRunCasterChunk, m_runCasters.size());
		const auto first = static_cast<uint32_t>(m_shadowInstances.size());
		CullAABB bounds = emptyCullAABB();
		for (std::size_t i = begin; i < end; ++i)
		{
			const InstanceDataDx12& c = m_runCasters[i];
			m_shadowInstances.push_back(c);
			sgc::Mat4f world;
			std::memcpy(world.m, c.row0, sizeof(float) * 16);
			bounds = unionCullAABB(bounds, worldOcclusionAABB(m_runAabb, world));
		}
		ShadowCaster caster{mesh, sgc::Mat4f::identity(), first, static_cast<uint32_t>(end - begin), bounds};
		caster.fromDraws = true;
		if (!addShadowCaster(caster)) { m_shadowInstances.resize(first); }
	}
	m_runCasters.clear();
}

/// @brief ひと続きを描く。1 個なら今までの経路、2 個以上なら instanced の draw (1,024 個ずつ)
void flushDrawRunSlow()
{
	MITIRU_ZONE_NAMED("Draw::flushRun");
	m_runActive = false;
	recordRunShadowCasters();
	const std::size_t count = m_runInstances.size();
	if (count == 0) { return; }
	if (m_runVb != nullptr && (m_runIndexCount == 0 || m_runIb != nullptr))
	{
		if (count == 1)
		{
			Material material = drawRunMaterial();
			material.diffuse = m_runVisDiffuse;
			drawRunSingle(material);
		}
		else
		{
			m_runMerged += static_cast<int>(count);
			drawRunInstanced();
		}
	}
	m_runInstances.clear();
}

/// @brief 1 個で終わったひと続きを、issueMeshDraw と同じ手順で描く
void drawRunSingle(const Material& material)
{
	if (auto* pso = selectMainPSO(material.doubleSided)) { m_graphicsCmdList->SetPipelineState(pso); }
	if (!bindForwardDraw(m_runVisWorld, material, nullptr, drawRunTint())) { return; }
	drawBuffersRaw(m_runVb, m_runIb, m_runVertexCount, m_runIndexCount);
	++m_drawCallCount;
}

/// @brief ひと続きのインスタンスを instanced の draw で描く
void drawRunInstanced()
{
	const Material material = drawRunMaterial();
	auto* pso = instancedMainPsoOrNull(material.doubleSided);
	if (pso == nullptr) { return; }   // join の時に作れると確かめてあるので、ここへは来ない
	m_graphicsCmdList->SetPipelineState(pso);
	if (!bindForwardDraw(sgc::Mat4f::identity(), material, nullptr, drawRunTint())) { return; }
	const UINT vbSize = m_runVertexCount * static_cast<UINT>(sizeof(Vertex3D));
	for (std::size_t offset = 0; offset < m_runInstances.size(); offset += kInstanceBatchMaxDx12)
	{
		const std::size_t batch = std::min(kInstanceBatchMaxDx12, m_runInstances.size() - offset);
		drawInstanceRangeDx12(m_runVb, m_runIb, vbSize, m_runVertexCount, m_runIndexCount,
		                      m_runInstances.data() + offset, static_cast<UINT>(batch));
	}
}
