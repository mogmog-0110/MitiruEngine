// Renderer3D_DX12 の接触影の宣言。DX12PipelineStates.hpp から include される
// 接触影は AO の直後に 1 パス追加し、AO テクスチャへ乗算する（ADR 0109）。
// AO が切のフレームは、AO テクスチャを 1 で塗ってから接触影を重ねる。
// SceneLook::contactShadow が 0 のフレームは何も記録しない。
// AO テクスチャと深度の表を共用するため、ambient_occlusion を引き込む。

public:

/// @brief SceneLook::contactShadow と contactShadowDistance を適用する（ADR 0109）。strength が 0 なら切、distance が 0 なら既定値は 0.5
void applyContactShadow(float strength, float distance)
{
	m_contactStrength = (strength > 0.0f) ? std::min(strength, 1.0f) : 0.0f;
	m_contactDistance = (distance > 0.0f) ? distance : 0.5f;
	if (m_contactStrength <= 0.0f) { return; }
	features::use(features::Feature::ContactShadow);
	if (!m_needs.wants(features::Feature::ContactShadow))
	{
		features::note(features::Feature::ContactShadow);
		m_contactStrength = 0.0f;
		return;
	}
	if (!m_contactPSO && !m_contactCreateTried)
	{
		m_contactCreateTried = true;
		createContactShadowPipeline();
	}
}

private:

float                          m_contactStrength = 0.0f;
float                          m_contactDistance = 0.5f;
std::optional<gfx::Dx12Shader> m_contactPS;
ComPtr<ID3D12PipelineState>    m_contactPSO;
bool                           m_contactCreateTried = false;

[[nodiscard]] bool contactShadowWanted() const noexcept
{
	return m_contactStrength > 0.0f && m_contactPSO && m_ssaoTex[0] && m_ssaoSrvHeap && m_ssaoRtvHeap && m_depthBuffer &&
	       m_normalBuffer && m_qualityCaps.ambientOcclusion;
}

void createContactShadowPipeline()
{
	if (m_contactPSO || !m_outlinePostVS || !m_outlinePostRootSig) { return; }
	try
	{
		m_contactPS = gfx::Dx12Shader::createPixelShader(DX12_CONTACT_SHADOW_PS, "PSMain");
	}
	catch (const std::exception& e)
	{
		console::verbosef("接触影のシェーダーのコンパイルに失敗しました (%s)", e.what());
		m_contactPS.reset();
		return;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_outlinePostRootSig.Get(), *m_contactPS);
	pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	// 既存の AO × 接触影
	auto& blend = pd.BlendState.RenderTarget[0];
	blend.BlendEnable = TRUE;
	blend.SrcBlend = D3D12_BLEND_ZERO;
	blend.DestBlend = D3D12_BLEND_SRC_COLOR;
	blend.BlendOp = D3D12_BLEND_OP_ADD;
	blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
	blend.DestBlendAlpha = D3D12_BLEND_ONE;
	blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_contactPSO.ReleaseAndGetAddressOf()))))
	{
		m_contactPSO.Reset();
	}
}

/// @brief AO の直後に呼ぶ。この時点では深度が DEPTH_WRITE、法線 RT が RENDER_TARGET
void drawContactShadowPass()
{
	if (!contactShadowWanted()) { return; }
	// 定数を置けなかったフレームは、前の値や未設定の引数を読まないよう、パスごと飛ばす
	const D3D12_GPU_VIRTUAL_ADDRESS cb = uploadContactShadowCB();
	if (cb == 0) { return; }

	D3D12_RESOURCE_BARRIER barriers[3] = {};
	const auto transition = [](D3D12_RESOURCE_BARRIER& b, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = r;
		b.Transition.StateBefore = from;
		b.Transition.StateAfter = to;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	};
	transition(barriers[0], m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	transition(barriers[1], m_normalBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	transition(barriers[2], m_ssaoTex[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_graphicsCmdList->ResourceBarrier(3, barriers);

	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_ssaoRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	if (!m_aoAppliedThisFrame)
	{
		const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		m_graphicsCmdList->ClearRenderTargetView(rtv, one, 0, nullptr);
	}

	const D3D12_VIEWPORT vp{0.0f, 0.0f, m_config.viewportWidth, m_config.viewportHeight, 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
	m_graphicsCmdList->SetGraphicsRootSignature(m_outlinePostRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = {m_ssaoSrvHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);
	m_graphicsCmdList->SetPipelineState(m_contactPSO.Get());
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(0, m_ssaoSrvHeap->GetGPUDescriptorHandleForHeapStart());
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cb);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
	std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
	std::swap(barriers[2].Transition.StateBefore, barriers[2].Transition.StateAfter);
	m_graphicsCmdList->ResourceBarrier(3, barriers);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_aoAppliedThisFrame = true;
}

/// @brief 光の向きを視空間へ直し、CbContactShadow（b0）を ring buffer に置く。行列は SSAO と同じ column-major の memcpy
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadContactShadowCB()
{
	struct alignas(256) CbContact256
	{
		float proj[4][4];
		float invProj[4][4];
		float lightView[3];
		float strength;
		float screenSize[2];
		float rayLength;
		float bias;
	};
	CbContact256 cb{};
	toColumnMajor(cb.proj, m_projMatrix);
	toColumnMajor(cb.invProj, glm::inverse(m_projMatrix));
	const sgc::Vec3f travel = m_directionalShadow.lightDirection();
	const glm::vec3 toLight = -glm::normalize(glm::vec3(travel.x, travel.y, travel.z));
	const glm::vec3 lv = glm::normalize(glm::vec3(m_viewMatrix * glm::vec4(toLight, 0.0f)));
	cb.lightView[0] = lv.x;
	cb.lightView[1] = lv.y;
	cb.lightView[2] = lv.z;
	cb.strength = m_contactStrength;
	cb.screenSize[0] = m_config.viewportWidth;
	cb.screenSize[1] = m_config.viewportHeight;
	cb.rayLength = m_contactDistance;
	cb.bias = 0.01f;
	const auto a = m_uploadRing.upload(&cb, sizeof(CbContact256), 256);
	return a.valid() ? a.gpuAddr : 0;
}
