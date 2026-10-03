// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12Instancing.hpp の後に include)。物理の演出の描画 (ADR 0069)
//
// 布のように毎フレーム形が変わるゲームのメッシュ (updateGameMesh) は、VB の回転 slot に前のフレームの頂点が残っているので、
// 動きベクトルのパスでそれを前の形として渡す。registerGameMesh に同じ数をもう一度渡した物は、別の形への差し替えとみなして
// 止まった物のまま扱う。破片 (drawMeshPieces) は meshId と影の旗ごとに並べ替え、1 組を 1 回の instanced draw にする。

public:

bool updateGameMesh(std::uint32_t id, const Vertex3D* vertices, int vertexCount) override
{
	const auto it = m_gameMeshes.find(id);
	if (it == m_gameMeshes.end() || vertices == nullptr || vertexCount <= 0 ||
	    !it->second->overwriteVertices(vertices, static_cast<std::size_t>(vertexCount)))
	{
		debug::warnOnce("dx12.gamemesh.update." + std::to_string(id),
		                "updateMesh3D: registerMesh3D で登録していないか、頂点の数が登録と違う (何もしない)");
		return false;
	}
	m_deformedGameMeshes[it->second.get()] = it->second->revision();
	return true;
}

void drawMeshPieces(const PieceInstancePod* pieces, int count) override
{
	if (pieces == nullptr || count <= 0) { return; }
	m_pieceOrder.clear();
	for (int i = 0; i < count; ++i) { m_pieceOrder.push_back(static_cast<std::uint32_t>(i)); }
	// 添字も比べるので、同じ組の中は渡した順のまま (動きベクトルの対が描く順で取れる)
	std::sort(m_pieceOrder.begin(), m_pieceOrder.end(), [pieces](std::uint32_t a, std::uint32_t b) {
		const PieceInstancePod& pa = pieces[a];
		const PieceInstancePod& pb = pieces[b];
		if (pa.meshId != pb.meshId) { return pa.meshId < pb.meshId; }
		if (pa.flags != pb.flags) { return pa.flags < pb.flags; }
		return a < b;
	});
	std::size_t begin = 0;
	while (begin < m_pieceOrder.size())
	{
		std::size_t end = begin + 1;
		const PieceInstancePod& head = pieces[m_pieceOrder[begin]];
		while (end < m_pieceOrder.size() && pieces[m_pieceOrder[end]].meshId == head.meshId &&
		       pieces[m_pieceOrder[end]].flags == head.flags)
		{
			++end;
		}
		drawPieceRun(pieces, begin, end);
		begin = end;
	}
}

/// @brief 破片を描いた instanced draw の延べ数 (診断とテスト用)
[[nodiscard]] std::uint64_t pieceBatchCount() const noexcept { return m_pieceBatches; }

private:

std::unordered_map<const Mesh*, std::uint64_t> m_deformedGameMeshes;   ///< updateGameMesh で書いた時の Mesh::revision
std::vector<std::uint32_t>                     m_pieceOrder;
std::vector<MeshInstance>                      m_pieceInstances;
std::vector<std::uint32_t>                     m_pieceKeys;
std::uint64_t                                  m_pieceBatches = 0;

void drawPieceRun(const PieceInstancePod* pieces, std::size_t begin, std::size_t end)
{
	const PieceInstancePod& head = pieces[m_pieceOrder[begin]];
	const Mesh* mesh = findGameMesh(head.meshId);
	if (mesh == nullptr)
	{
		debug::warnOnce("dx12.pieces.unknown." + std::to_string(head.meshId),
		                "drawMeshPieces: meshId が registerMesh3D の返り値でない (その破片は描かない)");
		return;
	}
	m_pieceInstances.clear();
	m_pieceKeys.clear();
	for (std::size_t k = begin; k < end; ++k)
	{
		const PieceInstancePod& p = pieces[m_pieceOrder[k]];
		m_pieceInstances.push_back(pieceInstance(p));
		m_pieceKeys.push_back(p.motionKey);
	}
	Material material;
	material.diffuse = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};
	const bool caster = m_shadowCasterEnabled;
	if ((head.flags & kPieceNoShadow) != 0) { m_shadowCasterEnabled = false; }
	drawMeshInstancesDx12(*mesh, m_pieceInstances.data(), m_pieceInstances.size(), material, nullptr, m_pieceKeys.data());
	m_shadowCasterEnabled = caster;
	++m_pieceBatches;
}

/// @brief updateGameMesh で書いた中身を描いていて、前のフレームの形が VB の回転 slot に残っていれば、その VB を返す
[[nodiscard]] ID3D12Resource* previousDeformedVertices(const Mesh& mesh) const
{
	const auto def = m_deformedGameMeshes.find(&mesh);
	if (def == m_deformedGameMeshes.end() || def->second != mesh.revision()) { return nullptr; }
	const auto vb = m_meshVBCache.find(static_cast<const void*>(&mesh));
	if (vb == m_meshVBCache.end() || vb->second.revision != mesh.revision()) { return nullptr; }
	const CachedBuffer& entry = vb->second;
	const std::uint32_t prev = (entry.activeSlot + kMeshSlotCount - 1) % kMeshSlotCount;
	const bool lastFrame = entry.slots[prev] && entry.slotFrame[prev] + 1 == m_frameCounter &&
	                       entry.slotFrame[entry.activeSlot] == m_frameCounter;
	return lastFrame ? entry.slots[prev].Get() : nullptr;
}

/// @brief 手放したゲームのメッシュの印を消す (同じアドレスに来た別の Mesh を形の変わる物と取り違えない)
void forgetDeformedGameMesh(const Mesh* mesh) { m_deformedGameMeshes.erase(mesh); }
