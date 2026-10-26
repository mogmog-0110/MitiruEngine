// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 色の仕上げ (LUT・ビネット・フィルムグレイン、ColorLook.hpp)。3D の後処理の最後 (AA とアップスケールの後、当たりの演出と
// HUD の前) に、出力の大きさのバックバッファをコピーへ取って全画面で書き戻す。宣言 (assets/color_looks.json) が無いゲームと、
// SceneLook::colorLut が 0 のフレームは、PSO もコピーも作らず何もしないので、既定の絵は今までと同じ画素になる。
// グレインを AA の後に置くのは、TAA の履歴と FXAA が細かい粒をならして消すから。

public:

/// @brief 色の仕上げの宣言を読む。組があれば PSO まで作る (renderer の初期化の直後に、host が 1 回呼ぶ)。LUT の GPU への転送は、
///        その番号が最初に選ばれたフレームで開いているリストに積む。組が無ければ false で何も確保しない
bool loadColorLooks(const char* manifestPath)
{
	m_colorLooks = ColorLookSet{};
	m_colorLutTextures.clear();
	m_colorFinishPSO.Reset();
	if (manifestPath == nullptr || manifestPath[0] == '\0') { return false; }
	m_colorLooks = render::loadColorLooks(manifestPath);
	if (m_colorLooks.empty()) { return false; }
	m_colorLutTextures = std::vector<dx12::Dx12Texture2D>(m_colorLooks.looks.size());
	createColorFinishPipeline();
	return static_cast<bool>(m_colorFinishPSO);
}

/// @brief 粒のシードにする決定論のフレーム番号。host が毎フレーム描く前に渡す (描いた回数は使わない。リプレイで同じ粒にするため)
void setColorFinishSeed(std::uint32_t frame) noexcept { m_colorFinishSeed = frame; }

/// @brief SceneLook::colorLut (ADR 0099)。0 で仕上げなし。番号は color_looks.json の looks の並びの 1 始まり
void applyColorLut(std::uint8_t number)
{
	m_colorLookNumber = 0;
	if (number == 0) { return; }
	features::use(features::Feature::ColorLut);
	const ColorLookSpec* spec = m_colorLooks.find(number);
	if (spec == nullptr)
	{
		::mitiru::render::detail::warnColorLutMissing(m_colorLooks.empty());
		return;
	}
	if (!m_colorFinishPSO) { return; }
	uploadColorLutIfNeeded(static_cast<std::size_t>(number - 1));
	m_colorLookNumber = number;
}

private:

ColorLookSet                          m_colorLooks;
std::vector<dx12::Dx12Texture2D>      m_colorLutTextures;   ///< looks と同じ添字。使われた番号の分だけ転送する
ComPtr<ID3D12RootSignature>           m_colorFinishRS;
ComPtr<ID3D12PipelineState>           m_colorFinishPSO;
ComPtr<ID3D12DescriptorHeap>          m_colorFinishHeap;   ///< frame slot ごとに {コピー, LUT} の 2 枚
int                                   m_colorLookNumber = 0;
std::uint32_t                         m_colorFinishSeed = 0;

static constexpr UINT kColorFinishDescriptors = 2;

void createColorFinishPipeline()
{
	if (!m_outlinePostVS) { return; }
	D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kColorFinishDescriptors, 0, 0, 0};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[1].DescriptorTable = {1, &range};
	p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = p;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = FRAME_COUNT * kColorFinishDescriptors;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!serializeRootSig(rd, m_colorFinishRS) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_colorFinishHeap.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	try
	{
		const auto ps = gfx::Dx12Shader::createPixelShader(DX12_COLOR_FINISH_PS, "PSMain");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_colorFinishRS.Get(), ps);
		pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_colorFinishPSO.ReleaseAndGetAddressOf()))))
		{
			m_colorFinishPSO.Reset();
		}
	}
	catch (const std::exception& e)
	{
		console::verbosef("色の仕上げのシェーダーのコンパイルに失敗しました (%s)", e.what());
		m_colorFinishPSO.Reset();
	}
}

/// @brief LUT を GPU へ上げる。開いているフレームのリストに積むので、sceneLook を適用する(フレームの頭)から呼ぶ
void uploadColorLutIfNeeded(std::size_t index)
{
	if (index >= m_colorLutTextures.size() || m_colorLutTextures[index].isReady()) { return; }
	const ColorLutImage& lut = m_colorLooks.luts[index];
	if (!lut.valid() || !m_graphicsCmdList) { return; }
	MipImage image;
	image.format = TextureFormat::Rgba8;   // 線形の値として読む。sRGB の符号化のまま入れる
	image.width = static_cast<std::uint32_t>(lut.size * lut.size);
	image.height = static_cast<std::uint32_t>(lut.size);
	image.mips.push_back(lut.rgba);
	if (!m_colorLutTextures[index].uploadMip(m_d3dDevice, m_graphicsCmdList.Get(), image, m_frameTempResources))
	{
		debug::warnOnce("color_lut.upload:" + m_colorLooks.looks[index].lutPath,
		                "LUT " + m_colorLooks.looks[index].lutPath + " を GPU へ上げられなかったので、この組の LUT は使いません");
	}
}

void writeColorFinishViews(UINT slot, const dx12::Dx12Texture2D* lut, ID3D12Resource* copy)
{
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = m_colorFinishHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * kColorFinishDescriptors * inc;
	m_d3dDevice->CreateShaderResourceView(copy, nullptr, cpu);
	cpu.ptr += inc;
	if (lut != nullptr && lut->isReady())
	{
		lut->createSRV(m_d3dDevice, cpu);
		return;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC none = {};
	none.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	none.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	none.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	none.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(nullptr, &none, cpu);
}

/// @brief このフレームに仕上げを掛けるか。applyColorLut が番号を覚え、PSO があるときだけ true
[[nodiscard]] bool colorFinishWanted() const noexcept { return m_colorLookNumber != 0 && m_colorFinishPSO; }

/// @brief アップスケールの後に呼ぶ。バックバッファ (RENDER_TARGET) をコピーて仕上げを掛け、書き戻す。コピーは当たりの演出と共有する
///        (順に使うだけで、どちらも休んでいるときは PIXEL_SHADER_RESOURCE に戻してある)
void drawColorFinishPass()
{
	const int number = m_colorLookNumber;
	m_colorLookNumber = 0;
	const ColorLookSpec* spec = m_colorLooks.find(number);
	auto* bb = (m_device != nullptr) ? m_device->currentBackBuffer() : nullptr;
	if (spec == nullptr || !m_colorFinishPSO || bb == nullptr || bb->nativeResource() == nullptr) { return; }
	const D3D12_RESOURCE_DESC bbDesc = bb->nativeResource()->GetDesc();
	if (bbDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		debug::verboseOnce("dx12.colorFinish.format", "バックバッファが RGBA8 ではないので、色の仕上げを掛けません。");
		return;
	}
	const std::size_t index = static_cast<std::size_t>(number - 1);
	const dx12::Dx12Texture2D* lutTex = (index < m_colorLutTextures.size() && m_colorLutTextures[index].isReady())
		? &m_colorLutTextures[index] : nullptr;
	const ColorLutImage* lutImage = (lutTex != nullptr) ? &m_colorLooks.luts[index] : nullptr;
	struct alignas(256) PaddedCb { ColorFinishCb cb; };
	PaddedCb padded{makeColorFinishCb(*spec, lutImage, static_cast<int>(bbDesc.Width), static_cast<int>(bbDesc.Height), m_colorFinishSeed)};
	if (!colorFinishActive(padded.cb) || !ensureHitFeelCopy(bbDesc)) { return; }
	const auto c = m_uploadRing.upload(&padded, sizeof(padded), 256);
	if (!c.valid()) { return; }
	copyBackBufferForHitFeel(bb->nativeResource());

	const UINT slot = m_frameCursor % FRAME_COUNT;
	writeColorFinishViews(slot, lutTex, m_hitFeelCopy.Get());
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto gpu = m_colorFinishHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot) * kColorFinishDescriptors * inc;

	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = bb->rtvHandle();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(bbDesc.Width), static_cast<float>(bbDesc.Height), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(bbDesc.Width), static_cast<LONG>(bbDesc.Height)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_colorFinishRS.Get());
	cl->SetPipelineState(m_colorFinishPSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_colorFinishHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootConstantBufferView(0, c.gpuAddr);
	cl->SetGraphicsRootDescriptorTable(1, gpu);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}
