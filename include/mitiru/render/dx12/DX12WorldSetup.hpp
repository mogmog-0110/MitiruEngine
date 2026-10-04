// DX12World.hpp から include する Renderer3D_DX12 の class body 内チャンク。

/// @brief 初回の屋外描画で作る。失敗は 1 度だけ知らせ、以後は屋外を描かない
[[nodiscard]] bool ensureWorldPipeline()
{
	if (m_worldPipelineTried) { return m_terrainPso[0] != nullptr; }
	m_worldPipelineTried = true;
	try
	{
		m_terrainVS = gfx::Dx12Shader::createVertexShader(dx12TerrainVertexShader(), "VSMain");
		m_grassVS = gfx::Dx12Shader::createVertexShader(dx12GrassVertexShader(), "VSMain");
		m_waterVS = gfx::Dx12Shader::createVertexShader(dx12WaterVertexShader(), "VSMain");
		m_waterPS = gfx::Dx12Shader::createPixelShader(dx12WaterPixelShader(), "PSMain");
		const WorldShade shades[3] = {WorldShade::Toon, WorldShade::Phong, WorldShade::Pbr};
		for (int i = 0; i < 3; ++i) { m_terrainPS[i] = gfx::Dx12Shader::createPixelShader(dx12TerrainPixelShader(shades[i]), "PSMain"); }
	}
	catch (const std::exception& e)
	{
		debug::verboseOnce("dx12.world.shader", std::string("屋外のシェーダーを作れなかったので、地形・草・水面を描きません (") + e.what() + ")。");
		return false;
	}
	if (!createWorldRootSignature()) { return false; }
	createWorldPsos();
	buildTerrainPatchMesh(m_terrainPatchMesh, 32);
	buildGrassBladeMesh(m_grassBladeMesh);
	if (!m_terrainPso[0])
	{
		debug::verboseOnce("dx12.world.pso", "屋外のパイプラインを作れなかったので、地形・草・水面を描きません。");
		return false;
	}
	return true;
}

/// @brief メインの 0〜8 に、屋外の表、区画の並び、定数 4 個、CbWorld を足す
[[nodiscard]] bool createWorldRootSignature()
{
	D3D12_ROOT_PARAMETER p[kWorldRootCount] = {};
	const auto cbv = [&](UINT i, UINT reg, D3D12_SHADER_VISIBILITY vis) {
		p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		p[i].Descriptor.ShaderRegister = reg;
		p[i].ShaderVisibility = vis;
	};
	cbv(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	cbv(1, 1, D3D12_SHADER_VISIBILITY_ALL);
	cbv(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
	cbv(3, 3, D3D12_SHADER_VISIBILITY_ALL);
	cbv(6, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	static const D3D12_DESCRIPTOR_RANGE materialRanges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
	                                                         {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 3, 0, 1}};
	// 場面の表はメインと同じものを張る (並びは DX12SceneTableLayout.hpp)
	static const D3D12_DESCRIPTOR_RANGE worldRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kWorldTableSize, 12, 0, 0};
	p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[4].DescriptorTable = {2, materialRanges};
	p[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[5].DescriptorTable = {dx12::kSceneTableRangeCount, dx12::kSceneTableRanges};
	p[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	for (UINT i = 7; i <= 8; ++i)
	{
		p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		p[i].Descriptor.ShaderRegister = i - 1;
		p[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}
	p[kWorldTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[kWorldTable].DescriptorTable = {1, &worldRange};
	p[kWorldTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	p[kWorldPatches].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	p[kWorldPatches].Descriptor.ShaderRegister = 34;
	p[kWorldPatches].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	p[kWorldDraw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	p[kWorldDraw].Constants = {5, 0, 4};
	p[kWorldDraw].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	cbv(kWorldCb, 6, D3D12_SHADER_VISIBILITY_ALL);

	// 静的サンプラはメインと同じ 4 つ。地形の PS は s0 で層を、s3 で splat と水面の色を引く
	D3D12_STATIC_SAMPLER_DESC s[4] = {};
	const auto sampler = [&](UINT i, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address) {
		s[i].Filter = filter;
		s[i].AddressU = s[i].AddressV = s[i].AddressW = address;
		s[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		s[i].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		s[i].MaxLOD = D3D12_FLOAT32_MAX;
		s[i].ShaderRegister = i;
		s[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	};
	sampler(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	s[0].MaxAnisotropy = 4;
	sampler(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	s[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	s[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	sampler(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	sampler(3, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = kWorldRootCount;
	desc.pParameters = p;
	desc.NumStaticSamplers = 4;
	desc.pStaticSamplers = s;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), error.GetAddressOf())) ||
	    FAILED(m_d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
	                                            IID_PPV_ARGS(m_worldRootSig.ReleaseAndGetAddressOf()))))
	{
		debug::verboseOnce("dx12.world.rootsig", std::string("屋外のルートシグネチャを作れなかったので、地形・草・水面を描きません (") +
		                   (error ? static_cast<const char*>(error->GetBufferPointer()) : "CreateRootSignature failed") + ")。");
		return false;
	}
	return true;
}

void createWorldPsos()
{
	D3D12_INPUT_ELEMENT_DESC layout[4] = {};
	UINT count = 0;
	getInputLayoutInternal(layout, count);
	const std::optional<gfx::Dx12Shader>* litPS[3] = {&m_toonPS, &m_phongPS, &m_pbrPS};
	for (int i = 0; i < 3; ++i)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		fillForwardPsoState(pd);
		pd.pRootSignature = m_worldRootSig.Get();
		pd.InputLayout = {layout, count};
		pd.VS = m_terrainVS->shaderBytecode();
		pd.PS = m_terrainPS[i]->shaderBytecode();
		(void)m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_terrainPso[i].ReleaseAndGetAddressOf()));
		if (!*litPS[i]) { continue; }
		pd.VS = m_grassVS->shaderBytecode();
		pd.PS = (*litPS[i])->shaderBytecode();
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		(void)m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_grassPso[i].ReleaseAndGetAddressOf()));
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wd = {};
	fillForwardPsoState(wd);
	wd.pRootSignature = m_worldRootSig.Get();
	wd.InputLayout = {nullptr, 0};
	wd.VS = m_waterVS->shaderBytecode();
	wd.PS = m_waterPS->shaderBytecode();
	wd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	wd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;   // 自分の下の深度を読むので、色の間は書かない
	(void)m_d3dDevice->CreateGraphicsPipelineState(&wd, IID_PPV_ARGS(m_waterPso.ReleaseAndGetAddressOf()));
	wd.PS = {};
	wd.NumRenderTargets = 0;
	wd.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	wd.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
	wd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	(void)m_d3dDevice->CreateGraphicsPipelineState(&wd, IID_PPV_ARGS(m_waterDepthPso.ReleaseAndGetAddressOf()));
}

/// @brief cells × cells マスの格子。頂点位置の xz は格子の番号で、VS が区画へ置く。割り方は Heightfield と同じ
static void buildTerrainPatchMesh(Mesh& mesh, std::uint32_t cells)
{
	std::vector<Vertex3D> v;
	for (std::uint32_t z = 0; z <= cells; ++z)
	{
		for (std::uint32_t x = 0; x <= cells; ++x)
		{
			v.emplace_back(sgc::Vec3f{static_cast<float>(x), 0.0f, static_cast<float>(z)}, sgc::Vec3f{0, 1, 0});
		}
	}
	std::vector<std::uint32_t> idx;
	for (std::uint32_t z = 0; z < cells; ++z)
	{
		for (std::uint32_t x = 0; x < cells; ++x)
		{
			const std::uint32_t a = z * (cells + 1) + x, b = a + 1, c = a + cells + 1, d = c + 1;
			idx.insert(idx.end(), {a, c, b, b, c, d});
		}
	}
	mesh.setVertices(std::move(v));
	mesh.setIndices(std::move(idx));
}

/// @brief 草の葉 1 枚。3 段の細い帯と先端で構成し、位置の x は横幅 (-1..1)、y は根元からの割合
static void buildGrassBladeMesh(Mesh& mesh)
{
	std::vector<Vertex3D> v;
	for (const float t : {0.0f, 0.35f, 0.7f})
	{
		v.emplace_back(sgc::Vec3f{-1.0f, t, 0.0f}, sgc::Vec3f{0, 1, 0});
		v.emplace_back(sgc::Vec3f{1.0f, t, 0.0f}, sgc::Vec3f{0, 1, 0});
	}
	v.emplace_back(sgc::Vec3f{0.0f, 1.0f, 0.0f}, sgc::Vec3f{0, 1, 0});
	mesh.setVertices(std::move(v));
	mesh.setIndices({0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6});
}

/// @brief world の地形、層、草の定数。水面の欄は呼び出し側が埋める
[[nodiscard]] CbWorldDx12 makeWorldCb(const terrain::OutdoorWorld& w, const OutdoorDrawPod& pod) const
{
	const float timeSec = pod.timeSec;
	CbWorldDx12 cb;
	const auto& h = w.terrain;
	const auto& p = h.placement();
	const float o[4] = {p.originX, p.originY, p.originZ, p.height};
	std::copy(o, o + 4, cb.terrainOrigin);
	const float s[4] = {p.sizeX, p.sizeZ, 1.0f / p.sizeX, 1.0f / p.sizeZ};
	std::copy(s, s + 4, cb.terrainSize);
	const float g[4] = {static_cast<float>(h.width()), static_cast<float>(h.depth()), h.cellX(), h.cellZ()};
	std::copy(g, g + 4, cb.terrainGrid);
	const float tp[4] = {32.0f, static_cast<float>(w.layers.size()), static_cast<float>(w.splatCount()), timeSec};
	std::copy(tp, tp + 4, cb.terrainPatch);
	for (std::size_t k = 0; k < w.layers.size() && k < terrain::kMaxTerrainLayers; ++k)
	{
		const auto& l = w.layers[k];
		cb.layerTile[k] = 1.0f / l.tile;
		const auto lc = linearRgb(l.color[0], l.color[1], l.color[2]);
		const float c[4] = {lc[0], lc[1], lc[2], l.normalStrength};
		std::copy(c, c + 4, cb.layerColor[k]);
		const float pbr[4] = {l.roughness, l.metallic, l.albedo.empty() ? 0.0f : 1.0f, l.normal.empty() ? 0.0f : 1.0f};
		std::copy(pbr, pbr + 4, cb.layerPbr[k]);
	}
	const auto& gl = w.grassLook;
	const auto& gs = w.grass;
	const float shape[4] = {gl.bladeHeight, gl.bladeWidth, gs.fadeStart, gs.fadeEnd};
	std::copy(shape, shape + 4, cb.grassShape);
	const auto gb = linearRgb(gl.baseColor[0], gl.baseColor[1], gl.baseColor[2]);
	const float base[4] = {gb[0], gb[1], gb[2], std::cos(gl.maxSlopeDeg * 0.0174532925f)};
	std::copy(base, base + 4, cb.grassBase);
	const auto gt = linearRgb(gl.tipColor[0], gl.tipColor[1], gl.tipColor[2]);
	const float tip[4] = {gt[0], gt[1], gt[2], gs.patchSize};
	std::copy(tip, tip + 4, cb.grassTip);
	fillGrassWind(cb, gl, pod);
	const float screen[4] = {m_config.viewportWidth, m_config.viewportHeight, 1.0f / m_config.viewportWidth,
	                         1.0f / m_config.viewportHeight};
	std::copy(screen, screen + 4, cb.screenSize);
	toColumnMajor(cb.invViewProj, glm::inverse(m_projMatrix * m_viewMatrix));
	return cb;
}

/// @brief 風は pod の指定を world.json より優先する。草を押し倒す球も載せる
static void fillGrassWind(CbWorldDx12& cb, const terrain::GrassLook& gl, const OutdoorDrawPod& pod)
{
	const bool podDir = pod.windDir[0] != 0.0f || pod.windDir[1] != 0.0f;
	const float dx = podDir ? pod.windDir[0] : gl.windDir[0];
	const float dz = podDir ? pod.windDir[1] : gl.windDir[1];
	const float wl = std::max(std::sqrt(dx * dx + dz * dz), 1e-4f);
	const float strength = pod.windStrength >= 0.0f ? pod.windStrength : gl.windStrength;
	const float wind[4] = {dx / wl, dz / wl, strength, gl.windSpeed};
	std::copy(wind, wind + 4, cb.grassWind);
	const std::uint32_t n = std::min<std::uint32_t>(pod.benderCount, kMaxOutdoorBenders);
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::copy(pod.benders[i], pod.benders[i] + 4, cb.grassBenders[i]);
		cb.grassBenders[i][3] = std::max(cb.grassBenders[i][3], 0.0f);
	}
	cb.grassBenderCount[0] = static_cast<float>(n);
}

static void fillWaterCb(CbWorldDx12& cb, const terrain::WaterBody& b, const OutdoorDrawPod& pod)
{
	const float rect[4] = {b.minX, b.minZ, b.maxX, b.maxZ};
	std::copy(rect, rect + 4, cb.waterRect);
	// 色は書いた sRGB なので線形にする。absorption は 1 m あたりの減衰の係数なのでそのまま
	const auto ws = linearRgb(b.shallowColor[0], b.shallowColor[1], b.shallowColor[2]);
	const float sh[4] = {ws[0], ws[1], ws[2], b.height + pod.waterOffset};
	std::copy(sh, sh + 4, cb.waterShallow);
	const auto wd = linearRgb(b.deepColor[0], b.deepColor[1], b.deepColor[2]);
	const float dp[4] = {wd[0], wd[1], wd[2], b.foamDepth};
	std::copy(dp, dp + 4, cb.waterDeep);
	const float ab[4] = {b.absorption[0], b.absorption[1], b.absorption[2], b.refraction};
	std::copy(ab, ab + 4, cb.waterAbsorb);
	const auto wf = linearRgb(b.foamColor[0], b.foamColor[1], b.foamColor[2]);
	const float fo[4] = {wf[0], wf[1], wf[2], static_cast<float>(b.toonBands)};
	std::copy(fo, fo + 4, cb.waterFoam);
	const float wv[4] = {b.waveAmplitude * std::max(pod.waveScale, 0.0f), b.waveScale, b.waveSpeed, b.reflectivity};
	std::copy(wv, wv + 4, cb.waterWave);
}

/// @brief 屋外の表 t12〜t33 をこのフレームの区画に書く。水面のときだけ画面の色と深度を載せる
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE writeWorldTable(const OutdoorWorldGpu& g, bool water)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	if (!reserveSrvSlots(kWorldTableSize, cpu, gpu)) { return {}; }
	writeTextureOrNull(&g.height, nextDescriptor(cpu, kSlotHeight));
	writeTextureOrNull(&g.splat[0], nextDescriptor(cpu, kSlotSplat0));
	writeTextureOrNull(&g.splat[1], nextDescriptor(cpu, kSlotSplat1));
	for (UINT k = 0; k < terrain::kMaxTerrainLayers; ++k)
	{
		writeTextureOrNull(&g.albedo[k], nextDescriptor(cpu, kSlotAlbedo + k));
		writeTextureOrNull(&g.normal[k], nextDescriptor(cpu, kSlotNormal + k));
	}
	writeTextureOrNull(&g.density, nextDescriptor(cpu, kSlotDensity));
	D3D12_SHADER_RESOURCE_VIEW_DESC color = {};
	color.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	color.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	color.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	color.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(water ? m_waterColorCopy.Get() : nullptr, &color, nextDescriptor(cpu, kSlotSceneColor));
	D3D12_SHADER_RESOURCE_VIEW_DESC depth = {};
	depth.Format = DXGI_FORMAT_R32_FLOAT;
	depth.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	depth.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_d3dDevice->CreateShaderResourceView(water ? m_depthBuffer.Get() : nullptr, &depth, nextDescriptor(cpu, kSlotSceneDepth));
	return gpu;
}

[[nodiscard]] bool bindWorldDraw(const OutdoorWorldGpu& g, const CbWorldDx12& cb, bool water, ID3D12PipelineState* pso)
{
	const auto cbAddr = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!cbAddr.valid() || pso == nullptr) { return false; }
	m_graphicsCmdList->SetGraphicsRootSignature(m_worldRootSig.Get());
	m_graphicsCmdList->SetPipelineState(pso);
	// 色は層と頂点が持つため、材質は白。地面と草には艶がないため、Phong のハイライトはごく弱くする
	Material ground;
	ground.diffuse = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};
	ground.specular = srgbColor(sgc::Colorf{0.04f, 0.04f, 0.04f, 1.0f});
	ground.shininess = 8.0f;
	ground.roughness = 0.9f;
	if (!bindForwardDraw(sgc::Mat4f::identity(), ground, nullptr, DrawTint{})) { return false; }
	const auto table = writeWorldTable(g, water);
	if (table.ptr == 0) { return false; }
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(kWorldTable, table);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(kWorldCb, cbAddr.gpuAddr);
	return true;
}

/// @brief 今の陰影に対応する PSO の番号。Toon は 0、Phong は 1、PBR は 2。Unlit と Flat は Toon と同じ 0
[[nodiscard]] int worldShadeIndex() const noexcept
{
	if (m_shaderMode == ShaderMode3D::PBR) { return 2; }
	return (m_shaderMode == ShaderMode3D::Phong) ? 1 : 0;
}
