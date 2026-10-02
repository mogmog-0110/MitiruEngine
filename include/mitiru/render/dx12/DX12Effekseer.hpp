// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// Effekseer のエフェクト (.efkefc /.efk)。ゲームからは drawModel(path, 位置, 回転, 倍率, key, 経過秒)
// で渡される (アニメ付きモデルと同じ「時刻を渡して描く」入口)。ModuleApi にも Screen にも何も足さない。
// 描くのは不透明と半透明 (OIT) の後、MSAA の resolve の前: 深度で壁に隠れ、HDR のまま tonemap される。

/// @brief drawModel に渡されたのが Effekseer のエフェクトか (拡張子で見る)
[[nodiscard]] static bool isEffekseerPath(const char* path) noexcept
{
	if (path == nullptr) { return false; }
	const std::string_view p(path);
	const auto endsWith = [&p](std::string_view ext) {
		return p.size() >= ext.size() && p.substr(p.size() - ext.size()) == ext;
	};
	return endsWith(".efkefc") || endsWith(".efk");
}

/// @return エフェクトとして受け取ったら true (Effekseer の無いビルドでは受け取って捨て、1 度だけ知らせる)
bool queueEffekseer(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale,
                    const char* key, float ageSec)
{
	if (!isEffekseerPath(path)) { return false; }
#if defined(MITIRU_HAS_EFFEKSEER)
	if (m_effekseer) { m_effekseer->draw(path, position, rotYDeg, scale, key, ageSec); }
#else
	(void)position; (void)rotYDeg; (void)scale; (void)key; (void)ageSec;
	debug::warnOnceFix("render.effekseer.missing", std::string("Effekseer のエフェクトを描けない: ") + path,
		"このビルドには Effekseer が入っていない", "-DMITIRU_WITH_EFFEKSEER=ON で configure し直す");
#endif
	return true;
}

#if defined(MITIRU_HAS_EFFEKSEER)
std::unique_ptr<fx::EffekseerRuntime> m_effekseer;

/// @brief initialize から。作れなくても 3D 全体は止めず、エフェクトだけ描かない
void createEffekseerRuntime()
{
	fx::EffekseerTarget target;
	target.colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	target.depthFormat = DXGI_FORMAT_D32_FLOAT;
	target.sampleCount = static_cast<int>(MSAA_SAMPLE_COUNT);
	target.framesInFlight = static_cast<int>(FRAME_COUNT);
	std::string error;
	m_effekseer = fx::EffekseerRuntime::create(m_d3dDevice, m_device->commandQueue(), target, error);
	if (!m_effekseer) { std::fprintf(stderr, "[mitiru][effekseer] 使えない: %s\n", error.c_str()); }
}

/// @brief endFrame から。MSAA HDR + depth に束縛し直して記録する (Effekseer は自分の PSO とヒープを積む)
void renderEffekseerPass()
{
	if (!m_effekseer || !m_graphicsCmdList || !m_msaaColorRtvHeap || !m_dsvHeap) { return; }
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	D3D12_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &vp);
	const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight)};
	m_graphicsCmdList->RSSetScissorRects(1, &scissor);

	fx::EffectCamera cam;
	cam.eye = m_clodCamera.position();
	cam.target = m_clodCamera.target();
	cam.up = m_clodCamera.up();
	cam.fovYRad = m_clodCamera.fov();
	cam.aspect = m_clodCamera.aspectRatio();
	cam.nearZ = m_clodCamera.nearClip();
	cam.farZ = m_clodCamera.farClip();
	m_effekseer->render(m_graphicsCmdList.Get(), cam);
}
#endif
