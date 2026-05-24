#pragma once
// This header is included by RenderPipeline2D.hpp — do not include directly.

#ifdef _WIN32

namespace mitiru::render
{

inline RenderPipeline2D RenderPipeline2D::createFromDx11(
	gfx::Dx11Device* dx11Device,
	float screenWidth,
	float screenHeight)
{
	RenderPipeline2D pipeline;
	pipeline.m_screenWidth = screenWidth;
	pipeline.m_screenHeight = screenHeight;

	auto* device = dx11Device->getD3DDevice();
	auto* context = dx11Device->getD3DContext();
	pipeline.m_dx11Context = context;

	/// シェーダーをコンパイルする
	pipeline.m_vertexShader = std::make_unique<gfx::Dx11Shader>(
		gfx::Dx11Shader::createVertexShader(
			device, DEFAULT_VS_2D, "VSMain"));
	pipeline.m_pixelShader = std::make_unique<gfx::Dx11Shader>(
		gfx::Dx11Shader::createPixelShader(
			device, DEFAULT_PS_2D, "PSMain"));

	/// パイプラインステートを構築する
	gfx::Dx11PipelineDesc pipeDesc;
	pipeDesc.vertexShader = pipeline.m_vertexShader.get();
	pipeDesc.pixelShader = pipeline.m_pixelShader.get();
	pipeDesc.blendMode = gfx::BlendMode::Alpha;
	pipeline.m_pipeline = std::make_unique<gfx::Dx11Pipeline>(
		device, pipeDesc);

	/// 定数バッファ（正射影行列）を生成する
	const auto ortho = OrthoMatrix::create(
		screenWidth, screenHeight);
	pipeline.m_constantBuffer = std::make_unique<gfx::Dx11Buffer>(
		device,
		gfx::BufferType::Constant,
		static_cast<std::uint32_t>(sizeof(ortho.m)),  // 64B matrix — fits u32; silences C4267
		true,
		ortho.m);

	/// 動的頂点バッファを生成する（初期サイズ64KB）
	constexpr std::uint32_t INITIAL_VB_SIZE = 65536;
	pipeline.m_vertexBuffer = std::make_unique<gfx::Dx11Buffer>(
		device,
		gfx::BufferType::Vertex,
		INITIAL_VB_SIZE,
		true);
	pipeline.m_vbCapacity = INITIAL_VB_SIZE;

	/// 動的インデックスバッファを生成する（初期サイズ32KB）
	constexpr std::uint32_t INITIAL_IB_SIZE = 32768;
	pipeline.m_indexBuffer = std::make_unique<gfx::Dx11Buffer>(
		device,
		gfx::BufferType::Index,
		INITIAL_IB_SIZE,
		true);
	pipeline.m_ibCapacity = INITIAL_IB_SIZE;

	/// コマンドリストを生成する
	pipeline.m_commandList = std::make_unique<gfx::Dx11CommandList>(
		context);

	/// PS定数バッファ（uUseTexture）を生成する
	const float psConst[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	pipeline.m_psConstantBuffer = std::make_unique<gfx::Dx11Buffer>(
		device,
		gfx::BufferType::Constant,
		static_cast<std::uint32_t>(sizeof(psConst)),  // 16B — fits u32; silences C4267
		true,
		psConst);

	pipeline.m_dx11Device = device;
	pipeline.m_valid = true;
	return pipeline;
}

inline void RenderPipeline2D::submitBatchDx11(
	const std::vector<Vertex2D>& vertices,
	const std::vector<std::uint32_t>& indices)
{
	const auto vbSize = static_cast<std::uint32_t>(
		vertices.size() * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(
		indices.size() * sizeof(std::uint32_t));

	/// バッファサイズが不足していたら再生成する
	if (vbSize > m_vbCapacity)
	{
		const auto newCapacity = std::max(
			vbSize, m_vbCapacity * 2);
		m_vertexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device,
			gfx::BufferType::Vertex,
			newCapacity,
			true);
		m_vbCapacity = newCapacity;
	}

	if (ibSize > m_ibCapacity)
	{
		const auto newCapacity = std::max(
			ibSize, m_ibCapacity * 2);
		m_indexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device,
			gfx::BufferType::Index,
			newCapacity,
			true);
		m_ibCapacity = newCapacity;
	}

	/// バッファを更新する
	m_vertexBuffer->update(
		m_dx11Context, vertices.data(), vbSize);
	m_indexBuffer->update(
		m_dx11Context, indices.data(), ibSize);

	/// 描画コマンドを発行する
	m_commandList->begin();
	m_commandList->setViewport(viewportWidth(), viewportHeight());
	m_commandList->setPipeline(m_pipeline.get());
	m_commandList->setVSConstantBuffer(0, m_constantBuffer.get());
	if (m_psConstantBuffer)
	{
		m_commandList->setPSConstantBuffer(0, m_psConstantBuffer.get());
	}
	m_commandList->setVertexBuffer(m_vertexBuffer.get());
	m_commandList->setIndexBuffer(m_indexBuffer.get());
	m_commandList->drawIndexed(
		static_cast<std::uint32_t>(indices.size()), 0, 0);
	m_commandList->end();
}

inline Microsoft::WRL::ComPtr<ID3D11InputLayout>
RenderPipeline2D::createSdfInputLayout(ID3D11Device* device, const gfx::Dx11Shader& vs)
{
	/// StyledVertex2D: position(float2) + localUV(float2) + color(float4) + shapeRect(float4)
	const D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{
			"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
			0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
			0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
		{
			"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
			0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
			0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
	};

	Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
	const auto& bytecode = vs.bytecode();
	HRESULT hr = device->CreateInputLayout(
		layout,
		static_cast<UINT>(std::size(layout)),
		bytecode.data(),
		bytecode.size(),
		inputLayout.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"RenderPipeline2D: SDF CreateInputLayout failed");
	}
	return inputLayout;
}

inline Microsoft::WRL::ComPtr<ID3D11BlendState>
RenderPipeline2D::createSdfBlendState(ID3D11Device* device)
{
	D3D11_BLEND_DESC desc = {};
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;
	auto& rt = desc.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D11_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D11_BLEND_ONE;
	rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
	HRESULT hr = device->CreateBlendState(&desc, blendState.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"RenderPipeline2D: SDF CreateBlendState failed");
	}
	return blendState;
}

inline Microsoft::WRL::ComPtr<ID3D11RasterizerState>
RenderPipeline2D::createSdfRasterizerState(ID3D11Device* device)
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = D3D11_CULL_NONE;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;
	desc.ScissorEnable = FALSE;

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;
	HRESULT hr = device->CreateRasterizerState(&desc, state.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"RenderPipeline2D: SDF CreateRasterizerState failed");
	}
	return state;
}

inline void RenderPipeline2D::submitStyledBatchDx11(
	const std::vector<StyledVertex2D>& vertices,
	const std::vector<std::uint32_t>& indices,
	const StyleConstants& style,
	std::string_view vsSource,
	std::string_view psSource,
	std::unique_ptr<gfx::Dx11Shader>& cachedVS,
	std::unique_ptr<gfx::Dx11Shader>& cachedPS,
	Microsoft::WRL::ComPtr<ID3D11InputLayout>& cachedLayout)
{
	/// パイプラインを遅延初期化する
	if (!cachedVS)
	{
		cachedVS = std::make_unique<gfx::Dx11Shader>(
			gfx::Dx11Shader::createVertexShader(
				m_dx11Device, vsSource, "VSMain"));
		cachedPS = std::make_unique<gfx::Dx11Shader>(
			gfx::Dx11Shader::createPixelShader(
				m_dx11Device, psSource, "PSMain"));
		cachedLayout = createSdfInputLayout(m_dx11Device, *cachedVS);
		m_sdfBlendState = createSdfBlendState(m_dx11Device);
		m_sdfRasterizerState = createSdfRasterizerState(m_dx11Device);
	}

	/// スタイル定数バッファを遅延初期化する
	if (!m_sdfStyleBuffer)
	{
		m_sdfStyleBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device,
			gfx::BufferType::Constant,
			static_cast<std::uint32_t>(sizeof(StyleConstants)),  // fits u32; silences C4267
			true,
			&style);
	}
	else
	{
		m_sdfStyleBuffer->update(
			m_dx11Context, &style, sizeof(StyleConstants));
	}

	/// SDF頂点/インデックスバッファのサイズを計算する
	const auto vbSize = static_cast<std::uint32_t>(
		vertices.size() * sizeof(StyledVertex2D));
	const auto ibSize = static_cast<std::uint32_t>(
		indices.size() * sizeof(std::uint32_t));

	/// SDF頂点バッファを確保/再確保する
	if (vbSize > m_sdfVbCapacity)
	{
		const auto newCapacity = std::max(vbSize, m_sdfVbCapacity * 2);
		m_sdfVertexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device,
			gfx::BufferType::Vertex,
			newCapacity,
			true);
		m_sdfVbCapacity = newCapacity;
	}

	/// SDF インデックスバッファを確保/再確保する
	if (ibSize > m_sdfIbCapacity)
	{
		const auto newCapacity = std::max(ibSize, m_sdfIbCapacity * 2);
		m_sdfIndexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device,
			gfx::BufferType::Index,
			newCapacity,
			true);
		m_sdfIbCapacity = newCapacity;
	}

	/// バッファを更新する
	m_sdfVertexBuffer->update(m_dx11Context, vertices.data(), vbSize);
	m_sdfIndexBuffer->update(m_dx11Context, indices.data(), ibSize);

	/// ビューポートを設定する
	D3D11_VIEWPORT vp = {};
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx11Context->RSSetViewports(1, &vp);

	/// 入力レイアウト・プリミティブトポロジを設定する
	m_dx11Context->IASetInputLayout(cachedLayout.Get());
	m_dx11Context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/// シェーダーを設定する
	m_dx11Context->VSSetShader(cachedVS->getVertexShader(), nullptr, 0);
	m_dx11Context->PSSetShader(cachedPS->getPixelShader(), nullptr, 0);

	/// ブレンド・ラスタライザステートを設定する
	const float blendFactor[4] = {0, 0, 0, 0};
	m_dx11Context->OMSetBlendState(
		m_sdfBlendState.Get(), blendFactor, 0xFFFFFFFF);
	m_dx11Context->RSSetState(m_sdfRasterizerState.Get());

	/// 定数バッファを設定する（b0=projection, b1=style）
	ID3D11Buffer* vsCBs[] = {m_constantBuffer->getD3DBuffer()};
	m_dx11Context->VSSetConstantBuffers(0, 1, vsCBs);
	ID3D11Buffer* psCBs[] = {m_sdfStyleBuffer->getD3DBuffer()};
	m_dx11Context->PSSetConstantBuffers(1, 1, psCBs);

	/// 頂点・インデックスバッファを設定する（StyledVertex2Dストライド）
	ID3D11Buffer* vb = m_sdfVertexBuffer->getD3DBuffer();
	constexpr UINT stride = sizeof(StyledVertex2D);
	constexpr UINT offset = 0;
	m_dx11Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
	m_dx11Context->IASetIndexBuffer(
		m_sdfIndexBuffer->getD3DBuffer(),
		DXGI_FORMAT_R32_UINT, 0);

	/// 描画する
	m_dx11Context->DrawIndexed(
		static_cast<UINT>(indices.size()), 0, 0);

	/// デフォルトパイプラインを復元する（後続のsubmitBatchに影響しないよう）
	if (m_pipeline)
	{
		m_pipeline->bind(m_dx11Context);
	}
}

} // namespace mitiru::render

#endif // _WIN32
