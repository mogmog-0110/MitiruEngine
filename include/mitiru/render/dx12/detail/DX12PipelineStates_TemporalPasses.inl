// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 射影のずらし、不透明の描画の記録、動きベクトルのパス、TAA のパス。

public:

/// @brief 直前のフレームの動きベクトル (UV 単位、前 − 今) を読む。診断とテスト用で GPU の完了を待つ。
///        フレームの外 (finalizeFrame の後) で呼ぶ。動きベクトルを作っていなければ空
[[nodiscard]] std::vector<float> readVelocityForDiagnostics()
{
	if (!m_velocityTex || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	const auto raw = gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), m_velocityTex.Get(),
	                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 4);
	std::vector<float> out(raw.size() / 2);
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		std::uint16_t h = 0;
		std::memcpy(&h, raw.data() + i * 2, sizeof(h));
		out[i] = gfx::halfToFloat(h);
	}
	return out;
}

/// @brief 以後の描画の動きベクトルの鍵。同じメッシュを何個も描く物の対応を、描く順ではなく鍵で取る
void setMotionKey(std::uint32_t key) override { m_motionKey = key; }

/// @brief false の間の描画は、カメラが動いても画面上で止まって見える物として動きを書く
void setMotionVectorCaster(bool enabled) override { m_motionCaster = enabled; }

private:

std::uint32_t m_motionKey = 0;
bool          m_motionCaster = true;

/// @brief 鍵と描画の物 (メッシュや prim) から、履歴を引く鍵を作る。最上位 bit を立てて本物のアドレスと重ねない
[[nodiscard]] const void* motionHistoryKey(const void* object) const noexcept
{
	if (m_motionKey == 0) { return object; }
	const auto mixed = (static_cast<std::uint64_t>(m_motionKey) * 0x9E3779B97F4A7C15ull) ^
	                   static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
	return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(mixed | (1ull << 63)));
}

/// @brief beginFrame から呼ぶ。前フレームの行列と描画を「前」へ回し、このフレームのずらしを決める
void beginTemporalFrame(uint32_t frameIndex)
{
	if (m_postTimingEnabled) { m_postTimer.beginFrame(frameIndex); }
	m_prevViewProjNoJitter = m_viewProjNoJitter;
	m_prevViewProjValid = m_cameraEverSet;
	m_motionHistory.beginFrame();
	m_motionKey = 0;
	m_motionCaster = true;
	const bool upscale = upscaleActive();
	const bool taa = (m_aaMode == AntiAliasing3D::Taa) || upscale;
	const std::uint32_t phases = upscale ? upscaleJitterPhases(m_renderScale) : kTemporalJitterPhases;
	m_jitterNdc = taa ? jitterToNdc(temporalJitter(m_taaFrameIndex, phases), m_config.viewportWidth, m_config.viewportHeight)
	                  : JitterOffset{};
	m_projMatrix = jitteredProjection(m_projNoJitter);
}

/// @brief setPostGpuTimingEnabled(true) の間だけ、fn が積むコマンドの GPU 時間を測る
template <typename Fn>
void timePostPass(PostGpuPass pass, Fn&& fn)
{
	const auto id = static_cast<uint32_t>(pass);
	if (m_postTimingEnabled) { m_postTimer.begin(m_graphicsCmdList.Get(), id); }
	fn();
	if (m_postTimingEnabled) { m_postTimer.end(m_graphicsCmdList.Get(), id); }
}

/// @brief NDC で平行移動してから射影する。ずらしは画素の中なので、視錐台カリングは元の射影のまま
[[nodiscard]] glm::mat4 jitteredProjection(const glm::mat4& proj) const
{
	if (m_jitterNdc.x == 0.0f && m_jitterNdc.y == 0.0f) { return proj; }
	glm::mat4 shift(1.0f);
	shift[3][0] = m_jitterNdc.x;
	shift[3][1] = m_jitterNdc.y;
	return shift * proj;
}

/// @brief setCamera から呼ぶ。射影はずらし込みと抜きの両方を持つ
void setTemporalCamera(const glm::mat4& proj)
{
	m_projNoJitter = proj;
	m_projMatrix = jitteredProjection(proj);
	m_viewProjNoJitter = proj * m_viewMatrix;
	if (!m_cameraEverSet)
	{
		m_prevViewProjNoJitter = m_viewProjNoJitter;
		m_cameraEverSet = true;
	}
}

[[nodiscard]] bool isSkinnedPoolMesh(const Mesh& mesh) const noexcept
{
	const Mesh* first = m_skinnedPool.data();
	return &mesh >= first && &mesh < first + m_skinnedPool.size();
}

/// @brief 描画を記録する (drawMesh とインスタンスの 1 個ずつから)。スキン prim は drawSkinnedPrim が prim を鍵に記録する。
/// @details 前フレームとの対は「同じ鍵の中でゲームが描いた順」で取るので、視錐台やオクルージョンで落とした描画と
///          半透明の描画も順番には数える (drawn = false)。数えないと、落ちた物の後ろの物が隣の物の姿勢と対になり、
///          並んだ箱の間の距離ぶんの偽の動きが TAA と動きのぼけに筋を引く
void recordMotionDraw(const Mesh& mesh, const sgc::Mat4f& world, bool drawn)
{
	// 副ビューの描画は主ビューの TAA と動きのぼけの対に入れない
	if (!motionVectorsActive() || isSkinnedPoolMesh(mesh) || m_activeView != nullptr) { return; }
	MotionDraw d{&mesh, world, nullptr, 0, m_frameCounter, !m_motionCaster};
	d.drawn = drawn;
	m_motionHistory.add(motionHistoryKey(&mesh), d);
}

void recordSkinnedMotionDraw(const void* prim, uint32_t slot, const gfx::GpuResource& vertices,
                             const sgc::Mat4f& instanceWorld)
{
	if (!motionVectorsActive() || m_activeView != nullptr) { return; }
	m_motionHistory.add(motionHistoryKey(prim),
	                    MotionDraw{&m_skinnedPool[slot], instanceWorld, vertices.Get(), slot, m_frameCounter, !m_motionCaster});
}

/// @brief 前フレームの変形済み頂点がまだ同じバッファに残っていれば、それを返す
[[nodiscard]] ID3D12Resource* previousSkinnedVertices(const MotionDraw& prev) const noexcept
{
	if (prev.skinnedVertices == nullptr || prev.frame + 1 != m_frameCounter) { return nullptr; }
	const auto& target = m_skinnedTargets[prev.skinSlot];
	ID3D12Resource* stillThere = target.buffers[prev.frame & 1].Get();
	return (stillThere == prev.skinnedVertices) ? stillThere : nullptr;
}

/// @brief 1 個の物を前フレームの姿勢と今の姿勢で描き、動きを書く。動いていなければ何もしない
void drawVelocityObject(const MotionDraw& cur, const MotionDraw& prev)
{
	if (!cur.drawn) { return; }
	ID3D12Resource* prevSkin = previousSkinnedVertices(prev);
	const bool moved = std::memcmp(&cur.world, &prev.world, sizeof(sgc::Mat4f)) != 0;
	if (!moved && cur.skinnedVertices == nullptr && !cur.cameraLocked) { return; }

	const auto vbIt = m_meshVBCache.find(static_cast<const void*>(cur.mesh));
	if (vbIt == m_meshVBCache.end() || !vbIt->second.resource) { return; }
	ID3D12Resource* curVerts = (cur.skinnedVertices != nullptr) ? cur.skinnedVertices : vbIt->second.resource.Get();
	ID3D12Resource* prevVerts = (prevSkin != nullptr) ? prevSkin : curVerts;
	const UINT vbBytes = static_cast<UINT>(cur.mesh->vertexCount() * sizeof(Vertex3D));
	const D3D12_VERTEX_BUFFER_VIEW views[2] = {
		{curVerts->GetGPUVirtualAddress(), vbBytes, sizeof(Vertex3D)},
		{prevVerts->GetGPUVirtualAddress(), vbBytes, sizeof(Vertex3D)},
	};
	m_graphicsCmdList->IASetVertexBuffers(0, 2, views);

	struct alignas(256) CbVelocityDraw
	{
		float world[4][4];
		float prevWorld[4][4];
	};
	CbVelocityDraw cb{};
	toColumnMajor(cb.world, toGlm(cur.world));
	// カメラに付いた物は、前フレームのカメラで見ても今と同じ画素に来る配置を前の姿勢にする (動きが 0 になる)
	const glm::mat4 prevWorld = (cur.cameraLocked && m_prevViewProjValid)
		? glm::inverse(m_prevViewProjNoJitter) * m_viewProjNoJitter * toGlm(cur.world)
		: toGlm(prev.world);
	toColumnMajor(cb.prevWorld, prevWorld);
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!a.valid()) { return; }
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, a.gpuAddr);
	drawMeshIndexedOrNot(*cur.mesh);
}

/// @brief キャッシュ済みの IB があればそれで、無ければ頂点の並びで描く (頂点は呼び出し側が束ねる)
void drawMeshIndexedOrNot(const Mesh& mesh)
{
	const auto& indices = mesh.indices();
	const auto ibIt = m_meshIBCache.find(static_cast<const void*>(&mesh));
	if (!indices.empty() && ibIt != m_meshIBCache.end() && ibIt->second.resource)
	{
		const D3D12_INDEX_BUFFER_VIEW ibv{ibIt->second.resource->GetGPUVirtualAddress(), ibIt->second.size,
		                                  DXGI_FORMAT_R32_UINT};
		m_graphicsCmdList->IASetIndexBuffer(&ibv);
		m_graphicsCmdList->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
		return;
	}
	if (indices.empty()) { m_graphicsCmdList->DrawInstanced(static_cast<UINT>(mesh.vertexCount()), 1, 0, 0); }
}

void temporalBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

void setFullViewport()
{
	const D3D12_VIEWPORT vp{0.0f, 0.0f, m_config.viewportWidth, m_config.viewportHeight, 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
}

[[nodiscard]] bool velocityReady() const noexcept
{
	return m_velocityTex && m_velocityObjectPSO && m_velocityResolvePSO && m_temporalSrvHeap && m_temporalRootSig;
}

/// @brief 動きベクトルを作る。不透明・clod・半透明の後、SSAO の前 (主パスの深度が DEPTH_WRITE の位置) で呼ぶ
void drawVelocityPasses()
{
	if (!motionVectorsActive() || !velocityReady()) { return; }
	drawVelocityObjects();
	resolveVelocity();
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

void drawVelocityObjects()
{
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_velocityObjectTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_velocityDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const auto rtv = temporalRtv(kTemporalRtvVelocityObject);
	const auto dsv = m_temporalDsvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	cl->ClearRenderTargetView(rtv, zero, 0, nullptr);
	cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	setFullViewport();
	cl->SetGraphicsRootSignature(m_velocityObjectRootSig.Get());
	cl->SetPipelineState(m_velocityObjectPSO.Get());
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	struct alignas(256) CbVelocityFrame
	{
		float viewProjJittered[4][4];
		float viewProj[4][4];
		float prevViewProj[4][4];
	};
	CbVelocityFrame cb{};
	toColumnMajor(cb.viewProjJittered, m_projMatrix * m_viewMatrix);
	toColumnMajor(cb.viewProj, m_viewProjNoJitter);
	toColumnMajor(cb.prevViewProj, m_prevViewProjValid ? m_prevViewProjNoJitter : m_viewProjNoJitter);
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (a.valid())
	{
		cl->SetGraphicsRootConstantBufferView(1, a.gpuAddr);
		m_motionHistory.seal();
		m_motionHistory.match([this](const MotionDraw& cur, const MotionDraw& prev) { drawVelocityObject(cur, prev); });
	}
	temporalBarrier(m_velocityObjectTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_velocityDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void resolveVelocity()
{
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_velocityTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = temporalRtv(kTemporalRtvVelocity);
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();

	struct alignas(256) CbVelocityResolve
	{
		float invViewProj[4][4];
		float prevViewProj[4][4];
		float texelSize[2];
		float nearZ;
		float farZ;
	};
	CbVelocityResolve cb{};
	toColumnMajor(cb.invViewProj, glm::inverse(m_viewProjNoJitter));
	toColumnMajor(cb.prevViewProj, m_prevViewProjValid ? m_prevViewProjNoJitter : m_viewProjNoJitter);
	cb.texelSize[0] = 1.0f / m_config.viewportWidth;
	cb.texelSize[1] = 1.0f / m_config.viewportHeight;
	cb.nearZ = m_clodCamera.nearClip();
	cb.farZ = m_clodCamera.farClip();
	drawTemporalFullscreen(m_velocityResolvePSO.Get(), kTemporalTableVelocity, &cb, sizeof(cb));

	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	temporalBarrier(m_velocityTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/// @brief temporal の root sig で全画面三角形を描く (RT は呼び出し側が束ねる)
void drawTemporalFullscreen(ID3D12PipelineState* pso, UINT table, const void* cb, std::size_t cbBytes)
{
	auto* cl = m_graphicsCmdList.Get();
	cl->SetGraphicsRootSignature(m_temporalRootSig.Get());
	cl->SetPipelineState(pso);
	ID3D12DescriptorHeap* heaps[] = {m_temporalSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, temporalTable(table));
	const auto a = m_uploadRing.upload(cb, cbBytes, 256);
	if (a.valid()) { cl->SetGraphicsRootConstantBufferView(1, a.gpuAddr); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}

/// @brief 出力先が実バックバッファと同じ大きさか (lo-fi の低解像 RT へ出している間は後処理を飛ばす)
[[nodiscard]] bool backBufferMatchesViewport(gfx::Dx12RenderTarget* bb) const
{
	if (bb == nullptr || bb->nativeResource() == nullptr) { return false; }
	const auto d = bb->nativeResource()->GetDesc();
	return static_cast<float>(d.Width) == m_config.viewportWidth &&
	       static_cast<float>(d.Height) == m_config.viewportHeight;
}

/// @brief backbuffer を FXAA の写しへ取り、PS が t0 で読める状態にする。終わったら endBackBufferCopy
void beginBackBufferCopy(ID3D12Resource* bb)
{
	temporalBarrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_graphicsCmdList->CopyResource(m_fxaaIntermediate.Get(), bb);
	temporalBarrier(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_fxaaIntermediate.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void endBackBufferCopy()
{
	temporalBarrier(m_fxaaIntermediate.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}

/// @brief 選んだ AA を掛ける。被写界深度の後、動きのぼけの前に呼ぶ。TAAU の間は drawUpscalePass が受け持つ
void drawAntiAliasingPass()
{
	if (upscaleActive()) { return; }
	if (m_aaMode == AntiAliasing3D::MsaaFxaa) { drawFXAAPass(); }
	if (m_aaMode == AntiAliasing3D::Taa) { drawTaaPass(); }
}

void drawTaaPass()
{
	auto* bb = sceneColorTarget();
	if (!m_taaPSO || !velocityReady() || !m_fxaaIntermediate || !backBufferMatchesViewport(bb))
	{
		m_taaHistoryValid = false;
		return;
	}
	auto* cl = m_graphicsCmdList.Get();
	const int write = m_taaHistoryWrite;
	beginBackBufferCopy(bb->nativeResource());
	temporalBarrier(m_taaHistory[write].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {bb->rtvHandle(), temporalRtv(kTemporalRtvHistory + static_cast<UINT>(write))};
	cl->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
	setFullViewport();

	struct alignas(256) CbTaa
	{
		float texelSize[2];
		float screenSize[2];
		float blendStill;
		float blendMoving;
		float movingPixels;
		float historyValid;
	};
	CbTaa cb{};
	cb.texelSize[0] = 1.0f / m_config.viewportWidth;
	cb.texelSize[1] = 1.0f / m_config.viewportHeight;
	cb.screenSize[0] = m_config.viewportWidth;
	cb.screenSize[1] = m_config.viewportHeight;
	cb.blendStill = 0.1f;
	cb.blendMoving = 0.25f;
	cb.movingPixels = 8.0f;
	cb.historyValid = m_taaHistoryValid ? 1.0f : 0.0f;
	drawTemporalFullscreen(m_taaPSO.Get(), kTemporalTableTaa + static_cast<UINT>(write), &cb, sizeof(cb));

	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	temporalBarrier(m_taaHistory[write].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	endBackBufferCopy();
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
	m_taaHistoryWrite = 1 - write;
	m_taaHistoryValid = true;
	++m_taaFrameIndex;
}
