// Class-body chunk for Renderer3D_DX12 - included via DX12PipelineStates.hpp


/// @brief シャドウマップ用 PSO (depth-only, no PS) を作る
/// @details VS はメインの TOON_VS_3D を流用。CbTransform の view/projection を
///          light view / light projection に差し替えて drawMesh と同じ経路で
///          描画する。PSO が PS を持たないため、RTV を 0 にして DSV だけバインドする。
void createShadowPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();

	if (!m_toonVS) return;
	psoDesc.VS = m_toonVS->shaderBytecode();
	psoDesc.PS = D3D12_SHADER_BYTECODE{nullptr, 0};

	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayout(inputLayout, inputCount);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements        = inputCount;

	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	// shadow acne 抑制
	psoDesc.RasterizerState.DepthBias            = 1000;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	psoDesc.RasterizerState.DepthClipEnable      = TRUE;

	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;

	psoDesc.DepthStencilState.DepthEnable    = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable  = FALSE;

	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 0;  // depth-only
	psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count      = 1;

	HRESULT hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_shadowPSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error("CreateGraphicsPipelineState (shadow) failed");
	}
}

/// @brief シャドウパスを描画する（前フレームの shadow casters を用いる）
/// @details beginFrame の早い段階で呼ぶ。command list が recording 中である必要あり。
///          メインパスのターゲット復元は呼び出し側で行うこと。
///
///          重要: shadow が無効 / casters 不在のフレームでも必ず depth クリア
///          (= 1.0) は実行する。clear しないと texture が 0 のままになり、PS の
///          SampleCmpLevelZero が「ライト視錐台内の全 pixel = 影」を返して
///          シーン中央付近の geometry が ambient (~0.20) しか効かず真っ黒
///          になる (ENG-103)。一部 GPU では R8G8B8A8 を白として bind しても
///          comparison sampler 経由では 1.0 を返さないため、R32_FLOAT 深度
///          そのものを 1.0 にクリアしておく方が確実。
void renderShadowPass()
{
	if (!m_shadowMap.isInitialized() || !m_shadowPSO) return;

	// 1.0 クリアは常に行う (state 遷移 → ClearDSV)。
	// caster 不在 / shadow 無効のフレームはここで return して PS には
	// depth=1.0 の shadow map を見せる → SampleCmp が必ず 1.0 (= no shadow)。
	m_shadowMap.beginShadowPass(m_graphicsCmdList.Get());

	const bool drawCasters
		= m_shadowEnabled && !m_shadowCommandsPrev.empty();
	if (!drawCasters)
	{
		m_shadowMap.endShadowPass(m_graphicsCmdList.Get());
		m_shadowDrawnThisFrame = true;
		return;
	}

	// シーンフォーカスを「前フレーム casters の重心」で簡易計算
	sgc::Vec3f focus{0, 0, 0};
	for (const auto& c : m_shadowCommandsPrev)
	{
		focus.x += c.world.m[0][3];
		focus.y += c.world.m[1][3];
		focus.z += c.world.m[2][3];
	}
	const float invN = 1.0f / static_cast<float>(m_shadowCommandsPrev.size());
	focus = {focus.x * invN, focus.y * invN, focus.z * invN};

	const auto lightView = m_directionalShadow.lightViewMatrix(focus);
	const auto lightProj = m_directionalShadow.lightProjectionMatrix();

	// shadow pass は上で既に begin 済み (DSV + viewport セット + クリア完了)。
	// ここでは PSO / root sig だけ設定して caster を発射する。
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(m_shadowPSO.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b3 (CbShadow): VS が LightSpacePos 算出で読むため depth-only パスでも
	// bind 必須。未 bind の root CBV アクセスは UB で、WARP では
	// device removed (0x887A0005) になる。
	const auto shadowPassCb = uploadShadowCB();
	if (shadowPassCb != 0)
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(3, shadowPassCb);
	}

	// shadow ライト視点で CbTransform を組み直す
	auto uploadShadowTransform = [&](const sgc::Mat4f& world)
		-> D3D12_GPU_VIRTUAL_ADDRESS
	{
		DX12CbTransform cb;
		toColumnMajor(cb.world,      toGlm(world));
		toColumnMajor(cb.view,       toGlm(lightView));
		toColumnMajor(cb.projection, toGlm(lightProj));
		auto a = m_uploadRing.upload(&cb, sizeof(DX12CbTransform), 256);
		return a.valid() ? a.gpuAddr : 0;
	};

	for (const auto& caster : m_shadowCommandsPrev)
	{
		if (!caster.mesh || caster.mesh->vertexCount() == 0) continue;
		const auto cbAddr = uploadShadowTransform(caster.world);
		if (cbAddr == 0) continue;

		// VB / IB は既存キャッシュを使う
		const void* key = static_cast<const void*>(caster.mesh);
		auto vbIt = m_meshVBCache.find(key);
		auto ibIt = m_meshIBCache.find(key);
		if (vbIt == m_meshVBCache.end() || !vbIt->second.resource) continue;

		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = vbIt->second.resource->GetGPUVirtualAddress();
		vbv.SizeInBytes    = vbIt->second.size;
		vbv.StrideInBytes  = sizeof(Vertex3D);
		m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

		m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

		const auto& indices = caster.mesh->indices();
		if (!indices.empty() && ibIt != m_meshIBCache.end() && ibIt->second.resource)
		{
			D3D12_INDEX_BUFFER_VIEW ibv = {};
			ibv.BufferLocation = ibIt->second.resource->GetGPUVirtualAddress();
			ibv.SizeInBytes    = ibIt->second.size;
			ibv.Format         = DXGI_FORMAT_R32_UINT;
			m_graphicsCmdList->IASetIndexBuffer(&ibv);
			m_graphicsCmdList->DrawIndexedInstanced(
				static_cast<UINT>(indices.size()), 1, 0, 0, 0);
		}
		else
		{
			m_graphicsCmdList->DrawInstanced(
				static_cast<UINT>(caster.mesh->vertexCount()), 1, 0, 0);
		}
	}

	m_shadowMap.endShadowPass(m_graphicsCmdList.Get());

	// メイン viewport / RTV はこの後 beginFrame 側で復元される必要があるため、
	// 呼び出し側で適切に設定し直すこと（このメソッドは shadow pass のみ責任）。
	m_shadowDrawnThisFrame = true;
}

/// @brief アルベド SRV 用 shader-visible heap を作る
/// @details kAlbedoSrvPerFrame × FRAME_COUNT を確保し frame index で partition
///          する。GPU が in-flight の前フレーム分 descriptor を読んでいる間に
///          CreateShaderResourceView で上書きしないため。beginFrame で cursor を
///          自 frame partition の先頭にリセットする。
void createAlbedoSrvHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kAlbedoSrvPerFrame * FRAME_COUNT;
	hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(
			&hd, IID_PPV_ARGS(m_albedoSrvHeap.GetAddressOf()))))
	{
		throw std::runtime_error("CreateDescriptorHeap albedo SRV failed");
	}
	m_albedoSrvCapacity  = kAlbedoSrvPerFrame;
	m_albedoSrvIncrement = m_d3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_albedoSrvBase      = 0;
	m_albedoSrvCursor    = 0;
}

/// @brief 1x1 白テクスチャを遅延アップロードする (recording 中の command list が必要)
/// @details material.albedoTexture が null の draw 用デフォルト。
void ensureDefaultWhiteTexture()
{
	if (m_defaultWhiteReady) return;
	if (!m_graphicsCmdList) return;

	const auto white = Texture::solid(1, 1, 255, 255, 255, 255);
	if (m_defaultWhiteTexture.uploadFrom(
			m_d3dDevice, m_graphicsCmdList.Get(), white,
			m_frameTempResources))
	{
		m_defaultWhiteReady = true;
	}
}

/// @brief キャッシュ済み or 新規アップロードして Dx12Texture2D を返す
/// @param tex 元 Texture（null で nullptr 返却）
/// @return Dx12Texture2D ポインタ（cache が所有）。失敗時 nullptr。
[[nodiscard]] dx12::Dx12Texture2D* getOrUploadAlbedo(const Texture* tex)
{
	if (!tex) return nullptr;
	auto it = m_textureCache.find(tex);
	if (it != m_textureCache.end())
	{
		return it->second.get();
	}
	auto t = std::make_unique<dx12::Dx12Texture2D>();
	if (!t->uploadFrom(m_d3dDevice, m_graphicsCmdList.Get(), *tex,
	                   m_frameTempResources))
	{
		return nullptr;
	}
	auto* raw = t.get();
	m_textureCache.emplace(tex, std::move(t));
	return raw;
}

/// @brief 現在の draw 用に { albedo SRV, shadow SRV } を heap に書いて gpu handle を返す
/// @details 2 スロット分の連続範囲を自 frame partition 内に書く。cursor は +2。
///          失敗時は gpuHandle.ptr = 0。
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE writeMainSrvTable(const Texture* tex)
{
	D3D12_GPU_DESCRIPTOR_HANDLE invalid = {0};
	if (m_albedoSrvCursor + 1 >= m_albedoSrvCapacity)
	{
		// 超過 draw は table 未更新のまま = 直前 draw のテクスチャ流用になる
		mitiru::debug::warnOnce("dx12.mainSrvTable.full",
			"3D SRV heap 超過: 1 フレーム "
			+ std::to_string(m_albedoSrvCapacity / 2)
			+ " draw まで。以降の draw は直前のアルベド/シャドウ SRV を流用する");
		return invalid;
	}

	// --- t0: albedo ---
	const dx12::Dx12Texture2D* t = nullptr;
	if (tex)
	{
		t = getOrUploadAlbedo(tex);
	}
	if (!t)
	{
		ensureDefaultWhiteTexture();
		if (!m_defaultWhiteReady) return invalid;
		t = &m_defaultWhiteTexture;
	}

	const UINT slot = m_albedoSrvBase + m_albedoSrvCursor;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu0 =
		m_albedoSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu0.ptr += static_cast<SIZE_T>(slot)
		* static_cast<SIZE_T>(m_albedoSrvIncrement);
	t->createSRV(m_d3dDevice, cpu0);

	// --- t1: shadow ---
	// renderShadowPass() が毎フレーム depth=1.0 にクリアする保証があるため
	// (ENG-103)、shadow map が初期化済みなら必ず実テクスチャを bind する。
	// 無効フレームでも texture には 1.0 が入ってるので SampleCmp は 1.0 を返す。
	D3D12_CPU_DESCRIPTOR_HANDLE cpu1 = cpu0;
	cpu1.ptr += static_cast<SIZE_T>(m_albedoSrvIncrement);
	if (m_shadowMap.isInitialized() && m_shadowMap.nativeResource())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                    = DXGI_FORMAT_R32_FLOAT;
		srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels       = 1;
		srv.Texture2D.MostDetailedMip = 0;
		m_d3dDevice->CreateShaderResourceView(
			m_shadowMap.nativeResource(), &srv, cpu1);
	}
	else
	{
		// shadow map init 失敗時のセーフティ — 白テクスチャを代用
		ensureDefaultWhiteTexture();
		if (!m_defaultWhiteReady) return invalid;
		m_defaultWhiteTexture.createSRV(m_d3dDevice, cpu1);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE gpu =
		m_albedoSrvHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot)
		* static_cast<UINT64>(m_albedoSrvIncrement);
	m_albedoSrvCursor += 2;
	return gpu;
}

/// @brief 後方互換: 旧 writeAlbedoSrv（writeMainSrvTable に転送）
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE writeAlbedoSrv(const Texture* tex)
{
	return writeMainSrvTable(tex);
}

/// @brief CbShadow (b3) — light-space view * proj を ring buffer から確保
/// @return GPU virtual address (0 で失敗)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadShadowCB()
{
	struct alignas(256) CbShadow {
		float lightViewProj[4][4]{};
	};
	CbShadow cb;

	if (m_shadowEnabled && !m_shadowCommandsPrev.empty())
	{
		// shadow pass で使った focus と同じロジックで lightVP を組む。
		// 簡易: 前フレーム casters 重心。
		sgc::Vec3f focus{0, 0, 0};
		for (const auto& c : m_shadowCommandsPrev)
		{
			focus.x += c.world.m[0][3];
			focus.y += c.world.m[1][3];
			focus.z += c.world.m[2][3];
		}
		const float invN = 1.0f / static_cast<float>(m_shadowCommandsPrev.size());
		focus = {focus.x * invN, focus.y * invN, focus.z * invN};

		const auto V = toGlm(m_directionalShadow.lightViewMatrix(focus));
		const auto P = toGlm(m_directionalShadow.lightProjectionMatrix());
		const glm::mat4 VP = P * V;
		toColumnMajor(cb.lightViewProj, VP);
	}
	else
	{
		// shadow 無効: 恒等行列。HLSL 側で shadow factor=1.0 になる
		glm::mat4 I(1.0f);
		toColumnMajor(cb.lightViewProj, I);
	}

	auto a = m_uploadRing.upload(&cb, sizeof(CbShadow), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief マルチライト CbLightArray (b2) を ring buffer 経由でアップロードする
/// @return CBV にバインドする GPU アドレス（0 で失敗）
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadLightArrayCB()
{
	LightArrayCB cb = LightArrayCB::fromLights(
		std::span<const Light>(m_lights.data(), m_lights.size()),
		m_sceneAmbient);

	auto a = m_uploadRing.upload(&cb, sizeof(LightArrayCB), 256);
	return a.valid() ? a.gpuAddr : 0;
}

// ─────────────────────────────────────────────────────────────
//  ユーティリティ
// ─────────────────────────────────────────────────────────────

/// @brief mesh の VB/IB cache entry を取得する（失効時は作り直す）
/// @details 失効 = サイズ変化 or Mesh::revision 変化（内容改変・アドレス再利用）。
///          **同サイズの内容改変** (毎フレームの CPU スキニング等、ADR 0028) は
///          FRAME_COUNT 周期の slot 回転 + memcpy のみで済ませ、committed resource を
///          毎フレーム作らない。slot N の次の書き込みは N+FRAME_COUNT フレーム後で、
///          Dx12Device::beginFrame の fence がその間の GPU 読み完了を保証している
///          (upload ring が依存しているのと同じ保証)。
///          サイズ変化時の旧リソースは in-flight が読み終わるまで deferRelease で生存させる。
/// @return 使用可能なリソース（生成失敗時 nullptr）
[[nodiscard]] ID3D12Resource* acquireMeshBuffer(
	std::unordered_map<const void*, CachedBuffer>& cache,
	const Mesh& mesh, const void* data, UINT sizeBytes)
{
	auto& entry = cache[static_cast<const void*>(&mesh)];
	entry.lastUsedFrame = m_frameCounter;

	if (entry.resource && entry.size == sizeBytes && entry.revision == mesh.revision())
	{
		return entry.resource.Get();  // 不変 (静的 mesh の通常経路)
	}

	if (entry.resource && entry.size == sizeBytes)
	{
		// 同サイズで内容だけ変わった動的 mesh → slot 回転 (warm-up 後は生成ゼロ)
		entry.activeSlot = (entry.activeSlot + 1) % FRAME_COUNT;
		auto& slot = entry.slots[entry.activeSlot];
		if (!slot)
		{
			slot = createUploadBuffer(sizeBytes);
			++m_meshBufferCreates;
		}
		if (slot)
		{
			uploadToBuffer(slot.Get(), data, sizeBytes);
			entry.resource = slot;
			entry.revision = mesh.revision();
			return entry.resource.Get();
		}
		// slot 生成失敗 → 従来 slow path へ落とす
	}

	// 初回 or サイズ変化: 全 slot を退役して作り直す
	if (m_device)
	{
		if (entry.resource) { m_device->deferRelease(entry.resource); }
		for (auto& s : entry.slots)
		{
			if (s) { m_device->deferRelease(s); s.Reset(); }
		}
	}
	entry.resource   = createUploadBuffer(sizeBytes);
	++m_meshBufferCreates;
	entry.slots[0]   = entry.resource;  // 次の同サイズ改変からここを起点に回転する
	entry.activeSlot = 0;
	entry.size       = sizeBytes;
	entry.revision   = mesh.revision();
	if (entry.resource)
	{
		uploadToBuffer(entry.resource.Get(), data, sizeBytes);
	}
	return entry.resource.Get();
}

/// @brief 長期間参照の無い mesh VB/IB を deferRelease で退役させる
/// @details beginFrame で毎フレーム呼ぶ。破棄済み Mesh の entry を掃除して
///          GPU メモリ漏れとアドレス再利用時の stale ヒットを防ぐ。
void evictStaleMeshBuffers()
{
	constexpr uint64_t kKeepFrames = FRAME_COUNT + 2;
	auto sweep = [&](std::unordered_map<const void*, CachedBuffer>& cache)
	{
		for (auto it = cache.begin(); it != cache.end();)
		{
			if (m_frameCounter - it->second.lastUsedFrame > kKeepFrames)
			{
				if (m_device)
				{
					if (it->second.resource) { m_device->deferRelease(it->second.resource); }
					for (auto& s : it->second.slots)
					{
						if (s) { m_device->deferRelease(s); }
					}
				}
				it = cache.erase(it);
			}
			else
			{
				++it;
			}
		}
	};
	sweep(m_meshVBCache);
	sweep(m_meshIBCache);
}

/// @brief アップロードヒープにバッファリソースを生成する
/// @param sizeBytes バッファサイズ（バイト）
/// @return 生成されたリソース（失敗時はnullptr）
[[nodiscard]] ComPtr<ID3D12Resource> createUploadBuffer(UINT64 sizeBytes) const
{
	/// 256バイトアラインメントを保証する（CBV用）
	const UINT64 alignedSize = (sizeBytes + 255) & ~255ULL;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = alignedSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> resource;
	HRESULT hr = m_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(resource.GetAddressOf()));

	if (FAILED(hr))
	{
		return nullptr;
	}

	return resource;
}

/// @brief バッファリソースにデータをアップロードする
/// @param resource 転送先リソース
/// @param data 転送元データ
/// @param sizeBytes データサイズ（バイト）
static void uploadToBuffer(ID3D12Resource* resource,
                           const void* data,
                           UINT sizeBytes)
{
	void* mapped = nullptr;
	D3D12_RANGE readRange = {0, 0};
	HRESULT hr = resource->Map(0, &readRange, &mapped);
	if (FAILED(hr))
	{
		return;
	}

	std::memcpy(mapped, data, sizeBytes);

	resource->Unmap(0, nullptr);
}
