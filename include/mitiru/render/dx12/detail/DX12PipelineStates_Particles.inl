// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// GPU パーティクル (GpuParticles.hpp)。エミッターはゲームが毎フレーム経過秒つきで渡し、レンダラは key ごとに
// プールの区画と「どの刻みまで進めたか」だけを覚える。そのフレームに渡されなかったエミッターは区画を返す。
// パスは不透明・半透明・剣筋の後、resolve の前に置く: 深度と法線が出そろっていて、粒も HDR のまま bloom と
// tonemap を受ける。アルファの粒は WBOIT をもう一度使って順に依らず重ね、加算の粒はその上に足す。
// 副ビューの間に渡したエミッターはそのビューにだけ出る。同じ key を複数のビューに渡せば同じ粒を共有し、
// 最初に描くビューの深度と法線で 1 回だけ進める (その後のビューは進んだ姿を描く)。

public:

/// @brief このフレームのエミッターを積む。毎フレーム、出している分を渡す (渡すのをやめれば粒は消える)
void submitParticles(const ParticleEmitterDesc* emitters, int count) override
{
	if (emitters == nullptr || count <= 0) { return; }
	const int room = kMaxParticleEmitters - static_cast<int>(m_particleQueue.size());
	if (count > room)
	{
		debug::warnOnce("dx12.particles.cap", "エミッターは 1 フレームに " + std::to_string(kMaxParticleEmitters) +
		                                          " 個までなので、超えた分は描きません。particles3D に渡すエミッターを減らしてください。");
		count = std::max(room, 0);
	}
	m_particleQueue.insert(m_particleQueue.end(), emitters, emitters + count);
	m_particleQueuePass.insert(m_particleQueuePass.end(), static_cast<std::size_t>(count),
	                           static_cast<std::uint8_t>(currentPass()));
}

/// @brief key のエミッターの粒を GPU から読む (診断とテスト用。GPU の完了を待つ)。無ければ空
[[nodiscard]] std::vector<ParticleGpu> readParticlesForDiagnostics(std::uint32_t key)
{
	const auto it = std::find_if(m_particleInstances.begin(), m_particleInstances.end(),
	                             [key](const ParticleInstance& p) { return p.key == key; });
	if (it == m_particleInstances.end() || !m_particlePool || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	return readbackParticleRange(it->first, it->capacity);
}

/// @brief 直前のフレームに区画を持っていたエミッターの数
[[nodiscard]] int activeParticleEmitterCount() const noexcept { return static_cast<int>(m_particleInstances.size()); }

private:

static_assert(static_cast<std::uint32_t>(PostGpuPass::Count) <= dx12::Dx12GpuTimer::kMaxPasses,
              "後処理の GPU 時間の枠が足りない (Dx12GpuTimer::kMaxPasses を増やす)");

/// 1 フレームに進める刻みの合計の上限。巻き戻しが重なった時に 1 フレームが伸びすぎないように
static constexpr std::uint32_t kMaxParticleStepsPerFrame = 4096;

struct ParticleInstance
{
	std::uint32_t key = 0;
	std::uint32_t shapeHash = 0;
	std::uint32_t first = 0;
	std::uint32_t capacity = 0;
	std::uint32_t stepsDone = 0;
	bool hasState = false;
	bool usedThisFrame = false;
	bool stepped = false;            ///< このフレームの刻みを積んだ
	std::uint32_t passMask = 0;      ///< このフレームに出すビューの集合 (bit = DX12Views.hpp の pass)
	ParticleEmitterDesc desc{};
	ParticleStepPlan plan{};
	D3D12_GPU_VIRTUAL_ADDRESS cb = 0;
};

std::vector<ParticleEmitterDesc> m_particleQueue;
std::vector<std::uint8_t>        m_particleQueuePass;   ///< m_particleQueue と同じ並びで、積んだビュー
std::vector<ParticleInstance>    m_particleInstances;
ParticlePoolAllocator            m_particleAlloc;
D3D12_RESOURCE_STATES            m_particlePoolState = D3D12_RESOURCE_STATE_COMMON;   ///< このフレームの中での状態

/// @brief 渡されたエミッターに区画を合わせる。形が変わったものは作り直し、渡されなかったものは区画を返す
void syncParticleInstances()
{
	for (ParticleInstance& inst : m_particleInstances)
	{
		inst.usedThisFrame = false;
		inst.stepped = false;
		inst.passMask = 0;
	}
	for (std::size_t q = 0; q < m_particleQueue.size(); ++q)
	{
		const ParticleEmitterDesc& d = m_particleQueue[q];
		const std::uint32_t bit = 1u << m_particleQueuePass[q];
		const std::uint32_t hash = particleShapeHash(d);
		const std::uint32_t capacity = particleCapacity(d);
		auto it = std::find_if(m_particleInstances.begin(), m_particleInstances.end(),
		                       [&d](const ParticleInstance& p) { return p.key == d.key; });
		// 同じ key の 2 個目は形を変えず、出すビューだけ足す (ビューごとに世界を描くと同じ粒が何度も来る)
		if (it != m_particleInstances.end() && it->usedThisFrame) { it->passMask |= bit; continue; }
		if (it != m_particleInstances.end() && (it->shapeHash != hash || it->capacity != capacity))
		{
			m_particleAlloc.release(it->first, it->capacity);
			m_particleInstances.erase(it);
			it = m_particleInstances.end();
		}
		if (it == m_particleInstances.end())
		{
			const std::uint32_t first = (capacity > 0) ? m_particleAlloc.allocate(capacity) : kParticleNoIndex;
			if (first == kParticleNoIndex)
			{
				debug::warnOnce("dx12.particles.pool", "粒は全部で " + std::to_string(kParticlePoolSize) +
				                                           " 個までなので、入りきらないエミッターは描きません。エミッターの粒の数を減らしてください。");
				continue;
			}
			m_particleInstances.push_back({d.key, hash, first, capacity, 0, false, false, false, 0, d, {}, 0});
			it = m_particleInstances.end() - 1;
		}
		it->desc = d;
		it->usedThisFrame = true;
		it->passMask = bit;
	}
	std::erase_if(m_particleInstances, [this](const ParticleInstance& p) {
		if (p.usedThisFrame) { return false; }
		m_particleAlloc.release(p.first, p.capacity);
		return true;
	});
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadEmitterCB(const ParticleInstance& inst)
{
	struct alignas(256) CbEmitter
	{
		float originRadius[4];
		float dirCosSpread[4];
		float speedLife[4];
		float gravityDrag[4];
		float emission[4];
		std::uint32_t counts[4];
		float sizeKeys[4];
		float color[3][4];
		float look[4];
		float shape[4];
	};
	const ParticleEmitterDesc& d = inst.desc;
	CbEmitter cb{};
	const sgc::Vec3f dir = sgc::Vec3f{d.direction[0], d.direction[1], d.direction[2]};
	const sgc::Vec3f w = (dir.lengthSquared() > 1e-12f) ? dir.normalized() : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	for (int i = 0; i < 3; ++i) { cb.originRadius[i] = d.position[i]; cb.gravityDrag[i] = d.gravity[i]; cb.sizeKeys[i] = d.size[i]; }
	cb.originRadius[3] = std::max(d.spawnRadius, 0.0f);
	cb.dirCosSpread[0] = w.x; cb.dirCosSpread[1] = w.y; cb.dirCosSpread[2] = w.z;
	cb.dirCosSpread[3] = std::cos(std::clamp(d.spreadDeg, 0.0f, 180.0f) * 0.017453292519943295f);
	cb.speedLife[0] = d.speedMin; cb.speedLife[1] = d.speedMax; cb.speedLife[2] = d.lifeMin; cb.speedLife[3] = d.lifeMax;
	cb.gravityDrag[3] = std::max(d.drag, 0.0f);
	cb.emission[0] = std::max(d.rate, 0.0f);
	cb.emission[1] = d.duration;
	cb.emission[2] = std::clamp(d.bounce, 0.0f, 1.0f);
	cb.emission[3] = static_cast<float>(d.collision);
	cb.counts[0] = d.burst;
	cb.counts[1] = inst.capacity;
	cb.counts[2] = inst.first;
	cb.counts[3] = d.seed;
	cb.sizeKeys[3] = std::clamp(d.midPoint, 0.0f, 1.0f);
	for (int k = 0; k < 3; ++k)
	{
		const auto c = linearRgb(d.color[k][0], d.color[k][1], d.color[k][2]);
		for (int i = 0; i < 3; ++i) { cb.color[k][i] = c[i]; }
		cb.color[k][3] = d.color[k][3];
	}
	cb.look[0] = std::max(d.stretch, 0.0f);
	cb.look[1] = std::max(d.softDistance, 0.0f);
	cb.look[2] = std::clamp(d.lit, 0.0f, 1.0f);
	cb.look[3] = (d.textureLayer >= 0 && d.textureLayer < m_vfxLayerCount) ? static_cast<float>(d.textureLayer) : -1.0f;
	cb.shape[0] = static_cast<float>(d.shape);
	cb.shape[1] = kFsrMaxReactive;
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadParticleFrameCB()
{
	struct alignas(256) CbParticleFrame
	{
		float viewProj[4][4];
		float viewProjNoJitter[4][4];
		float cameraPos[4];
		float cameraRight[4];
		float cameraUp[4];
		float cameraForward[4];
		float screen[4];
		float sunDir[4];
		float sunColor[4];
		float ambient[4];
	};
	CbParticleFrame cb{};
	toColumnMajor(cb.viewProj, m_projMatrix * m_viewMatrix);
	toColumnMajor(cb.viewProjNoJitter, m_viewProjNoJitter);
	const ClusterView v = ClusterView::fromCamera(m_clodCamera);
	const sgc::Vec3f* axes[4] = {&v.eye, &v.right, &v.up, &v.forward};
	float* dst[4] = {cb.cameraPos, cb.cameraRight, cb.cameraUp, cb.cameraForward};
	for (int i = 0; i < 4; ++i) { dst[i][0] = axes[i]->x; dst[i][1] = axes[i]->y; dst[i][2] = axes[i]->z; }
	cb.screen[0] = m_config.viewportWidth;
	cb.screen[1] = m_config.viewportHeight;
	cb.screen[2] = m_clodCamera.nearClip();
	cb.screen[3] = m_clodCamera.farClip();
	cb.sunDir[0] = m_light.direction.x; cb.sunDir[1] = m_light.direction.y; cb.sunDir[2] = m_light.direction.z;
	cb.sunColor[0] = m_light.color.r * m_light.intensity;
	cb.sunColor[1] = m_light.color.g * m_light.intensity;
	cb.sunColor[2] = m_light.color.b * m_light.intensity;
	const sgc::Colorf ambient = linearColor(m_sceneAmbient);
	cb.ambient[0] = ambient.r; cb.ambient[1] = ambient.g; cb.ambient[2] = ambient.b;
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief 刻みの計画を立てる。合計が上限を超えたエミッターは今回は進めない (次のフレームで続きから進める)
void planParticleFrame()
{
	std::uint32_t budget = kMaxParticleStepsPerFrame;
	for (ParticleInstance& inst : m_particleInstances)
	{
		inst.plan = planParticleSteps(inst.desc, inst.hasState, inst.stepsDone, particleTargetStep(inst.desc.age));
		if (inst.plan.count > budget)
		{
			debug::verboseOnce("dx12.particles.steps", "粒を進める刻みが 1 フレームの上限を超えたので、一部のエミッターは次のフレームで進めます。");
			inst.plan.count = 0;
			inst.plan.reset = false;
			continue;
		}
		budget -= inst.plan.count;
	}
}

/// @brief 今のビューに出すがまだ進めていない粒か
[[nodiscard]] static bool particleStepsHere(const ParticleInstance& inst, std::uint32_t bit) noexcept
{
	return (inst.passMask & bit) != 0 && !inst.stepped && inst.plan.count > 0 && inst.cb != 0;
}

/// @brief 計画した刻みを回す。同じ回目の刻みはエミッターをまたいで並べ、回の間にだけ UAV の壁を置く
void recordParticleSteps(D3D12_GPU_VIRTUAL_ADDRESS frameCb, D3D12_GPU_DESCRIPTOR_HANDLE table, std::uint32_t bit)
{
	std::uint32_t rounds = 0;
	for (const ParticleInstance& inst : m_particleInstances)
	{
		if (particleStepsHere(inst, bit)) { rounds = std::max(rounds, inst.plan.count); }
	}
	if (rounds == 0) { return; }
	if (m_particlePoolState != D3D12_RESOURCE_STATE_COMMON && m_particlePoolState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	{
		temporalBarrier(m_particlePool.Get(), m_particlePoolState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	auto* cl = m_graphicsCmdList.Get();
	cl->SetComputeRootSignature(m_particleStepRS.Get());
	cl->SetPipelineState(m_particleStepPSO.Get());
	cl->SetComputeRootConstantBufferView(1, frameCb);
	cl->SetComputeRootUnorderedAccessView(3, m_particlePool->GetGPUVirtualAddress());
	cl->SetComputeRootDescriptorTable(4, table);
	for (std::uint32_t r = 0; r < rounds; ++r)
	{
		for (const ParticleInstance& inst : m_particleInstances)
		{
			if (r >= inst.plan.count || !particleStepsHere(inst, bit)) { continue; }
			const std::uint32_t consts[2] = {inst.plan.first + r, (inst.plan.reset && r == 0) ? 1u : 0u};
			cl->SetComputeRootConstantBufferView(0, inst.cb);
			cl->SetComputeRoot32BitConstants(2, 2, consts, 0);
			cl->Dispatch((inst.capacity + 63) / 64, 1, 1);
		}
		D3D12_RESOURCE_BARRIER uav = {};
		uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uav.UAV.pResource = m_particlePool.Get();
		cl->ResourceBarrier(1, &uav);
	}
	for (ParticleInstance& inst : m_particleInstances)
	{
		if (!particleStepsHere(inst, bit)) { continue; }
		inst.stepsDone = inst.plan.first + inst.plan.count - 1;
		inst.hasState = true;
		inst.stepped = true;
	}
	temporalBarrier(m_particlePool.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_particlePoolState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

/// @brief blend の粒だけ描く。PSO と描き先は呼び出し側
void drawParticleInstances(ParticleBlend blend, D3D12_GPU_VIRTUAL_ADDRESS frameCb, D3D12_GPU_DESCRIPTOR_HANDLE table,
                           bool all = false)
{
	const std::uint32_t bit = 1u << currentPass();
	auto* cl = m_graphicsCmdList.Get();
	cl->SetGraphicsRootSignature(m_particleDrawRS.Get());
	ID3D12DescriptorHeap* heaps[] = {m_particleHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootConstantBufferView(1, frameCb);
	cl->SetGraphicsRootShaderResourceView(2, m_particlePool->GetGPUVirtualAddress());
	cl->SetGraphicsRootDescriptorTable(3, table);
	if (m_clusterFrameAlloc.valid()) { cl->SetGraphicsRootConstantBufferView(4, m_clusterFrameAlloc.gpuAddr); }
	if (m_lightBufferAlloc.valid()) { cl->SetGraphicsRootShaderResourceView(5, m_lightBufferAlloc.gpuAddr); }
	if (m_clusterMasks) { cl->SetGraphicsRootShaderResourceView(6, m_clusterMasks->GetGPUVirtualAddress()); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	for (const ParticleInstance& inst : m_particleInstances)
	{
		if ((!all && inst.desc.blend != blend) || inst.cb == 0 || !inst.hasState || (inst.passMask & bit) == 0) { continue; }
		cl->SetGraphicsRootConstantBufferView(0, inst.cb);
		cl->DrawInstanced(4, inst.capacity, 0, 0);
		++m_drawCallCount;
	}
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

[[nodiscard]] bool hasParticleBlend(ParticleBlend blend) const noexcept
{
	const std::uint32_t bit = 1u << currentPass();
	return std::any_of(m_particleInstances.begin(), m_particleInstances.end(), [blend, bit](const ParticleInstance& p) {
		return p.desc.blend == blend && p.hasState && (p.passMask & bit) != 0;
	});
}

/// @brief 主ビューの剣筋の後・resolve の前に呼ぶ。このフレームのエミッターを区画に合わせ、刻みを計画してから主ビューのぶんを描く
void drawParticlePass()
{
	syncParticleInstances();
	m_particleQueue.clear();
	m_particleQueuePass.clear();
	m_particlePoolState = D3D12_RESOURCE_STATE_COMMON;
	if (m_particleInstances.empty() || !particlesReady()) { return; }
	planParticleFrame();
	for (ParticleInstance& inst : m_particleInstances) { inst.cb = uploadEmitterCB(inst); }
	drawParticlesForCurrentPass();
}

/// @brief 今のビューに出す粒を、まだ進めていなければ進めてから描く (深度は DEPTH_WRITE、法線と MSAA の色は
///        RENDER_TARGET で受けて返す)。副ビューの仕上げからも呼ぶ
void drawParticlesForCurrentPass()
{
	const std::uint32_t bit = 1u << currentPass();
	const bool any = std::any_of(m_particleInstances.begin(), m_particleInstances.end(),
	                             [bit](const ParticleInstance& p) { return (p.passMask & bit) != 0 && p.cb != 0; });
	if (!any || !particlesReady() || !m_msaaColorRtvHeap) { return; }
	const auto frameCb = uploadParticleFrameCB();
	if (frameCb == 0) { return; }
	D3D12_GPU_DESCRIPTOR_HANDLE stepTable{}, drawTable{};
	writeParticleViews(stepTable, drawTable);

	constexpr auto kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, kRead);
	temporalBarrier(m_normalBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, kRead);
	ID3D12DescriptorHeap* heaps[] = {m_particleHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	recordParticleSteps(frameCb, stepTable, bit);
	temporalBarrier(m_normalBuffer.Get(), kRead, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (m_particlePoolState == D3D12_RESOURCE_STATE_COMMON) { m_particlePoolState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; }
	drawParticleColor(frameCb, drawTable);
	drawParticleReactive(frameCb, drawTable);
	temporalBarrier(m_depthBuffer.Get(), kRead, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

/// @brief アルファの粒を WBOIT で重ねてから、加算の粒を足す
void drawParticleColor(D3D12_GPU_VIRTUAL_ADDRESS frameCb, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	dx12::WeightedBlendedOIT* oit = hasParticleBlend(ParticleBlend::Alpha) ? currentOit() : nullptr;
	if (oit != nullptr)
	{
		oit->beginAccumulate(cl, nullptr);
		setFullViewport();
		cl->SetPipelineState(m_particleAlphaPSO.Get());
		drawParticleInstances(ParticleBlend::Alpha, frameCb, table);
		oit->composite(cl, rtv);
	}
	if (hasParticleBlend(ParticleBlend::Additive))
	{
		cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		setFullViewport();
		cl->SetPipelineState(m_particleAdditivePSO.Get());
		drawParticleInstances(ParticleBlend::Additive, frameCb, table);
	}
}

/// @brief FSR の間は、粒の被覆を反応マスクへ MAX で重ねる (剣筋と同じ扱い)
void drawParticleReactive(D3D12_GPU_VIRTUAL_ADDRESS frameCb, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
	if (!fsrActive() || !m_particleReactivePSO) { return; }
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto rtv = m_fsrRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();
	cl->SetPipelineState(m_particleReactivePSO.Get());
	drawParticleInstances(ParticleBlend::Additive, frameCb, table, true);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/// @brief プールの区画を読み戻す (GPU は待った後で呼ぶ)
[[nodiscard]] std::vector<ParticleGpu> readbackParticleRange(std::uint32_t first, std::uint32_t count)
{
	const UINT64 bytes = UINT64{sizeof(ParticleGpu)} * count;
	gfx::GpuResource readback;
	ComPtr<ID3D12CommandAllocator> alloc;
	ComPtr<ID3D12GraphicsCommandList> list;
	if (count == 0 ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST, readback)) ||
	    FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
	    FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))))
	{
		return {};
	}
	list->CopyBufferRegion(readback.Get(), 0, m_particlePool.Get(), UINT64{sizeof(ParticleGpu)} * first, bytes);
	list->Close();
	ID3D12CommandList* lists[] = {list.Get()};
	m_device->commandQueue()->ExecuteCommandLists(1, lists);
	m_device->waitForGpu();
	std::vector<ParticleGpu> out(count);
	void* p = nullptr;
	const D3D12_RANGE range = {0, static_cast<SIZE_T>(bytes)};
	if (FAILED(readback->Map(0, &range, &p))) { return {}; }
	std::memcpy(out.data(), p, static_cast<std::size_t>(bytes));
	const D3D12_RANGE noWrite = {0, 0};
	readback->Unmap(0, &noWrite);
	return out;
}
