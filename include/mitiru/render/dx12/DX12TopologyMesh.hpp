// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12PhysicsFxDraw.hpp の後に include)。
// 頂点の数と並びが毎フレーム変わるゲームのメッシュ (ADR 0076)
//
// 粒から張り直す面は、今の i 番の頂点が前のフレームの i 番と同じ点ではないので、VB の回転 slot に残った前の中身は前の形に
// 使えない。ゲームが頂点ごとに渡す前の位置を float3 だけの VB に写し、動きベクトルのパスの slot 1 に結ぶ。
// Mesh は登録の数を容量として持ち続けて先頭だけを書くので、VB・IB・前の位置の VB は同じ大きさのまま回り、容量の中では確保しない。

public:

bool updateGameMeshTopology(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
                            int indexCount, const sgc::Vec3f* prevPositions) override
{
	if (!validTopologyFrame(vertices, vertexCount, indices, indexCount))
	{
		debug::verboseOnce("dx12.gamemesh.topology." + std::to_string(id),
		                "updateMesh3D (添字つき) の添字が頂点の数を超えているか、添字の数が 3 の倍数でないので、形を変えません。");
		return false;
	}
	const auto n = static_cast<std::size_t>(vertexCount);
	const auto m = static_cast<std::size_t>(indexCount);
	Mesh& mesh = topologyMeshWithRoom(id, n, m);
	auto [it, added] = m_topologyMeshes.try_emplace(&mesh);
	TopologyMeshState& st = it->second;
	// 登録や registerMesh3D の差し替えで書かれた添字は、どこまで埋まっているか分からないので全部埋め直す
	if (added || st.revision != mesh.revision()) { st.writtenIndices = mesh.indexCount(); }
	if (!mesh.overwriteActive(vertices, n, indices, m, std::max(m, st.writtenIndices))) { return false; }
	st.writtenIndices = m;
	st.hasPrev = prevPositions != nullptr && n > 0;
	if (st.hasPrev)
	{
		if (st.prev.size() != mesh.vertexCount()) { st.prev.assign(mesh.vertexCount(), sgc::Vec3f{}); }
		std::copy(prevPositions, prevPositions + n, st.prev.begin());
	}
	st.revision = mesh.revision();
	st.frame = m_frameCounter;
	return true;
}

private:

static_assert(sizeof(sgc::Vec3f) == 12, "前の位置の VB は float3 を詰めて並べる");

struct TopologyMeshState
{
	std::vector<sgc::Vec3f> prev;      ///< 前の位置。容量ぶん持ち、先頭の頂点の数だけが今の値
	std::size_t writtenIndices = 0;    ///< 前に書いた添字の数。減った分だけ潰れた三角形で埋める
	std::uint64_t revision = 0;        ///< 書いた時の Mesh::revision
	std::uint64_t frame = 0;           ///< 書いた時の m_frameCounter
	bool hasPrev = false;
};

std::unordered_map<const Mesh*, TopologyMeshState> m_topologyMeshes;
std::unordered_map<const void*, CachedBuffer>      m_prevPositionBuffers;   ///< 前の位置の VB (Mesh ごとに回す)

[[nodiscard]] static bool validTopologyFrame(const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
                                             int indexCount)
{
	if (vertexCount < 0 || indexCount < 0 || indexCount % 3 != 0) { return false; }
	if ((vertexCount > 0 && vertices == nullptr) || (indexCount > 0 && indices == nullptr)) { return false; }
	const auto limit = static_cast<std::uint32_t>(vertexCount);
	return std::all_of(indices, indices + indexCount, [limit](std::uint32_t i) { return i < limit; });
}

/// @brief 足りる容量か。足りなければ 2 の冪へ広げる (登録より多い数が来た時の 1 回だけ確保する)
[[nodiscard]] static std::size_t topologyRoom(std::size_t need, std::size_t have) noexcept
{
	return need <= have ? have : std::bit_ceil(std::max<std::size_t>(need, 64));
}

/// @brief id のメッシュを n 頂点・m 添字が入る大きさで返す。無ければ作る。広げても同じ Mesh のまま (動きの対が切れない)
Mesh& topologyMeshWithRoom(std::uint32_t id, std::size_t n, std::size_t m)
{
	auto it = m_gameMeshes.find(id);
	if (it == m_gameMeshes.end()) { it = m_gameMeshes.emplace(id, std::make_unique<Mesh>()).first; }
	Mesh& mesh = *it->second;
	const std::size_t haveV = mesh.vertices().size();
	const std::size_t haveI = mesh.indices().size();
	if (n <= haveV && m <= haveI && haveV > 0 && haveI > 0) { return mesh; }
	if (haveV > 0)
	{
		debug::verboseOnce("dx12.gamemesh.topology.grow." + std::to_string(id),
		                "updateMesh3D (添字つき) の頂点か添字が容量を超えたので広げました。最初に registerMesh3D へ最大の数を渡すと、"
		                "途中で GPU のバッファを作り直しません。");
	}
	mesh.setVertices(std::vector<Vertex3D>(topologyRoom(std::max<std::size_t>(n, 1), haveV)));
	mesh.setIndices(std::vector<std::uint32_t>(topologyRoom(std::max<std::size_t>(m, 3), haveI), 0u));
	return mesh;
}

/// @brief このフレームに前の位置を受けたメッシュなら、その VB を返す。無ければ BufferLocation が 0
[[nodiscard]] D3D12_VERTEX_BUFFER_VIEW topologyPrevPositions(const Mesh& mesh)
{
	const auto it = m_topologyMeshes.find(&mesh);
	if (it == m_topologyMeshes.end()) { return {}; }
	TopologyMeshState& st = it->second;
	if (!st.hasPrev || st.revision != mesh.revision() || st.frame != m_frameCounter) { return {}; }
	const auto bytes = static_cast<UINT>(st.prev.size() * sizeof(sgc::Vec3f));
	ID3D12Resource* buffer = acquireMeshBuffer(m_prevPositionBuffers, mesh, st.prev.data(), bytes);
	if (buffer == nullptr) { return {}; }
	return {buffer->GetGPUVirtualAddress(), bytes, static_cast<UINT>(sizeof(sgc::Vec3f))};
}

/// @brief 手放したメッシュの印と前の位置の VB を消す (VB は描いたフレームが終わるまで解放を待つ)
void forgetTopologyMesh(const Mesh* mesh)
{
	m_topologyMeshes.erase(mesh);
	m_prevPositionBuffers.erase(static_cast<const void*>(mesh));
}

/// @brief 頂点の並びが変わるメッシュか (BLAS を更新で済ませず積み直すため)
[[nodiscard]] bool isTopologyMesh(const Mesh& mesh) const { return m_topologyMeshes.contains(&mesh); }
