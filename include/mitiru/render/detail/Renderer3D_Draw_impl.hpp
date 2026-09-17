#pragma once

/// @file Renderer3D_Draw_impl.hpp
/// @brief Renderer3D のフレーム描画、定数バッファ更新、テクスチャ転送の実装

#include <mitiru/render/Renderer3D.hpp>

#ifdef _WIN32

namespace mitiru::render
{

inline void Renderer3D::beginFrame(const sgc::Colorf& clearColor)
{
	MITIRU_ZONE_NAMED("Render::Dx11::BeginFrame");
	if (!m_initialized)
	{
		return;
	}

	m_frameActive = true;
	m_drawCallCount = 0;
	m_culledCount = 0;
	m_occludedCount = 0;
	m_outlineQueue.clear();
	m_skyboxDrawnThisFrame = false;

	/// windowless (G2) では swap chain がないため、実際の描画先 RTV で判定する。
	auto* rtv = m_device->currentRenderTargetView();
	if (rtv)
	{
		const float color[4] = {
			clearColor.r, clearColor.g, clearColor.b, clearColor.a
		};
		m_d3dContext->ClearRenderTargetView(rtv, color);

		if (m_depthStencilView)
		{
			m_d3dContext->ClearDepthStencilView(
				m_depthStencilView.Get(),
				D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
				1.0f, 0);
		}

		m_d3dContext->OMSetRenderTargets(
			1, &rtv, m_depthStencilView.Get());
	}

	D3D11_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_d3dContext->RSSetViewports(1, &vp);

	m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());
	m_d3dContext->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (m_rasterizerDirty)
	{
		createRasterizerState();
		m_rasterizerDirty = false;
	}
	m_d3dContext->RSSetState(m_rasterizerState.Get());

	m_d3dContext->OMSetDepthStencilState(
		m_depthStencilState.Get(), 0);

	if (m_renderState.blendEnabled)
	{
		const float blendFactor[4] = {0, 0, 0, 0};
		m_d3dContext->OMSetBlendState(
			m_blendState.Get(), blendFactor, 0xFFFFFFFF);
	}
	else
	{
		m_d3dContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}
}

inline void Renderer3D::drawMesh(const Mesh& mesh,
              const sgc::Mat4f& worldTransform,
              const Material& material)
{
	if (!m_initialized || mesh.vertexCount() == 0)
	{
		return;
	}

	// world 変換したローカル AABB が視錐台の外なら描画しない。
	if (m_frustumCullingEnabled && !m_frustum.isMeshVisible(mesh.localAABB(), worldTransform))
	{
		++m_culledCount;
		return;
	}

	// 前フレームの深度で完全に隠れている場合は描画しない。
	if (m_occlusionCullingEnabled && m_occlusionCuller.hasDepth())
	{
		const CullAABB worldBox = worldOcclusionAABB(mesh.localAABB(), worldTransform);
		const auto viewProj = occlusionViewProj();
		if (m_occlusionCuller.isOccluded(worldBox, viewProj.data()))
		{
			++m_occludedCount;
			return;
		}
	}


	// skybox は最初の drawMesh より前に描画する。
	drawSkyboxIfNeeded();

	updateTransformCB(worldTransform);

	updateLightingCB(material);

	/// マルチライト経路ではライト配列 CB を b2 にバインドする。
	if (m_useMultiLight)
	{
		updateLightArrayCB();
	}

	/// material.albedoTexture を優先し、null の場合は setTexture または clearTexture で設定した m_currentSRV を使う。
	ID3D11ShaderResourceView* srv = nullptr;
	if (material.albedoTexture)
	{
		srv = getOrUploadAlbedoSrv(material.albedoTexture);
	}
	if (!srv && m_currentSRV)
	{
		srv = m_currentSRV.Get();
	}
	if (!srv && m_defaultWhiteSRV)
	{
		srv = m_defaultWhiteSRV.Get();
	}
	bindAlbedoSrvIfChanged(srv);

	/// Mesh が変わっていなければ VB と IB のキャッシュを再利用する。
	const auto [vb, ib] = getOrUploadMeshBuffers(mesh);
	if (!vb)
	{
		return;
	}

	const UINT stride = sizeof(Vertex3D);
	const UINT offset = 0;
	m_d3dContext->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

	const auto& indices = mesh.indices();
	if (ib)
	{
		m_d3dContext->IASetIndexBuffer(
			ib, DXGI_FORMAT_R32_UINT, 0);
		m_d3dContext->DrawIndexed(
			static_cast<UINT>(indices.size()), 0, 0);
	}
	else
	{
		m_d3dContext->Draw(
			static_cast<UINT>(mesh.vertices().size()), 0);
	}

	++m_drawCallCount;

}

/// @brief マルチライト CB を更新して b2 にバインドする
inline void Renderer3D::updateLightArrayCB()
{
	const auto cb = LightArrayCB::fromLights(
		std::span<const Light>(m_lights.data(), m_lights.size()),
		m_sceneAmbient);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	const HRESULT hr = m_d3dContext->Map(
		m_cbLightArray.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		std::memcpy(mapped.pData, &cb, sizeof(cb));
		m_d3dContext->Unmap(m_cbLightArray.Get(), 0);
	}

	ID3D11Buffer* buf = m_cbLightArray.Get();
	m_d3dContext->PSSetConstantBuffers(2, 1, &buf);
}

/// @brief HLSL の row-major レイアウトに合わせてトランスフォーム定数バッファを更新する
/// @param worldTransform ワールド行列
/// @details sgc::Mat4f と HLSL のメモリレイアウトの違いを glm 経由で吸収する。
inline void Renderer3D::updateTransformCB(const sgc::Mat4f& worldTransform)
{
	CbTransform cb;
	glm::mat4 world = toGlm(worldTransform);
	glm::mat4 view  = toGlm(m_viewMatrix);
	glm::mat4 proj  = toGlm(m_projMatrix);
	toHLSL(cb.world, world);
	toHLSL(cb.view, view);
	toHLSL(cb.projection, proj);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = m_d3dContext->Map(
		m_cbTransform.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		std::memcpy(mapped.pData, &cb, sizeof(cb));
		m_d3dContext->Unmap(m_cbTransform.Get(), 0);
	}

	ID3D11Buffer* buf = m_cbTransform.Get();
	m_d3dContext->VSSetConstantBuffers(0, 1, &buf);
}

inline void Renderer3D::updateLightingCB(const Material& material)
{
	CbLighting cb;
	cb.lightDir[0] = m_light.direction.x;
	cb.lightDir[1] = m_light.direction.y;
	cb.lightDir[2] = m_light.direction.z;
	cb.lightDir[3] = 0.0f;

	cb.lightColor[0] = m_light.color.r * m_light.intensity;
	cb.lightColor[1] = m_light.color.g * m_light.intensity;
	cb.lightColor[2] = m_light.color.b * m_light.intensity;
	cb.lightColor[3] = 1.0f;

	cb.ambientColor[0] = m_sceneAmbient.r;
	cb.ambientColor[1] = m_sceneAmbient.g;
	cb.ambientColor[2] = m_sceneAmbient.b;
	cb.ambientColor[3] = 1.0f;

	cb.cameraPos[0] = m_cameraPosition.x;
	cb.cameraPos[1] = m_cameraPosition.y;
	cb.cameraPos[2] = m_cameraPosition.z;
	cb.cameraPos[3] = 1.0f;

	cb.materialDiffuse[0] = material.diffuse.r;
	cb.materialDiffuse[1] = material.diffuse.g;
	cb.materialDiffuse[2] = material.diffuse.b;
	cb.materialDiffuse[3] = material.diffuse.a;

	cb.materialSpecular[0] = material.specular.r;
	cb.materialSpecular[1] = material.specular.g;
	cb.materialSpecular[2] = material.specular.b;
	cb.materialSpecular[3] = material.specular.a;

	cb.materialShininess = material.shininess;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = m_d3dContext->Map(
		m_cbLighting.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		std::memcpy(mapped.pData, &cb, sizeof(cb));
		m_d3dContext->Unmap(m_cbLighting.Get(), 0);
	}

	ID3D11Buffer* buf = m_cbLighting.Get();
	m_d3dContext->PSSetConstantBuffers(1, 1, &buf);
}

inline Renderer3D::ComPtr<ID3D11Buffer> Renderer3D::createDynamicVertexBuffer(
	const void* data, UINT sizeBytes)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeBytes;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = data;

	ComPtr<ID3D11Buffer> buffer;
	m_d3dDevice->CreateBuffer(&desc, &initData, buffer.GetAddressOf());
	return buffer;
}

inline Renderer3D::ComPtr<ID3D11Buffer> Renderer3D::createDynamicIndexBuffer(
	const void* data, UINT sizeBytes)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeBytes;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = data;

	ComPtr<ID3D11Buffer> buffer;
	m_d3dDevice->CreateBuffer(&desc, &initData, buffer.GetAddressOf());
	return buffer;
}

/// @brief drawMesh 用の VB と IB を取得する
/// @details mesh.revision() がキャッシュ済みの値と異なるときだけ CreateBuffer を呼ぶ。
inline std::pair<ID3D11Buffer*, ID3D11Buffer*> Renderer3D::getOrUploadMeshBuffers(const Mesh& mesh)
{
	auto& entry = m_meshBufferCache[&mesh];
	if (entry.revision != mesh.revision())
	{
		const auto& verts = mesh.vertices();
		entry.vb = createDynamicVertexBuffer(
			verts.data(), static_cast<UINT>(verts.size() * sizeof(Vertex3D)));

		const auto& indices = mesh.indices();
		if (indices.empty())
		{
			entry.ib.Reset();
		}
		else
		{
			entry.ib = createDynamicIndexBuffer(
				indices.data(), static_cast<UINT>(indices.size() * sizeof(uint32_t)));
		}

		entry.revision = mesh.revision();
	}
	return {entry.vb.Get(), entry.ib.Get()};
}

inline void Renderer3D::uploadTexture(const Texture& tex)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = static_cast<UINT>(tex.width());
	desc.Height = static_cast<UINT>(tex.height());
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = tex.pixels().data();
	initData.SysMemPitch = static_cast<UINT>(tex.width()) * 4;

	ComPtr<ID3D11Texture2D> texture2D;
	HRESULT hr = m_d3dDevice->CreateTexture2D(
		&desc, &initData, texture2D.GetAddressOf());
	if (FAILED(hr))
	{
		return;
	}

	m_currentSRV.Reset();
	m_d3dDevice->CreateShaderResourceView(
		texture2D.Get(), nullptr, m_currentSRV.GetAddressOf());
}

/// @brief Material.albedoTexture 用の SRV を取得する
/// @details setTexture の状態とは別に Texture ポインタ単位でキャッシュし、同じ Texture は 1 度だけアップロードする。
inline ID3D11ShaderResourceView* Renderer3D::getOrUploadAlbedoSrv(const Texture* tex)
{
	if (!tex || !tex->valid()) return nullptr;
	auto it = m_albedoSrvCache.find(tex);
	if (it != m_albedoSrvCache.end())
	{
		return it->second.Get();
	}
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width            = static_cast<UINT>(tex->width());
	desc.Height           = static_cast<UINT>(tex->height());
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DEFAULT;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem     = tex->pixels().data();
	initData.SysMemPitch = static_cast<UINT>(tex->width()) * 4;

	ComPtr<ID3D11Texture2D> texture2D;
	if (FAILED(m_d3dDevice->CreateTexture2D(
			&desc, &initData, texture2D.GetAddressOf())))
	{
		return nullptr;
	}
	ComPtr<ID3D11ShaderResourceView> srv;
	if (FAILED(m_d3dDevice->CreateShaderResourceView(
			texture2D.Get(), nullptr, srv.GetAddressOf())))
	{
		return nullptr;
	}
	auto* raw = srv.Get();
	m_albedoSrvCache.emplace(tex, std::move(srv));
	return raw;
}

/// @brief メイン描画の前にアウトラインを描画し、変更したステートを復元する
inline void Renderer3D::drawOutlinePass(const Mesh& mesh, const sgc::Mat4f& worldTransform)
{
	m_d3dContext->VSSetShader(m_outlineVS.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_outlinePS.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_outlineInputLayout.Get());
	m_d3dContext->RSSetState(m_outlineFrontCull.Get());

	updateTransformCB(worldTransform);

	const auto& verts = mesh.vertices();
	auto vb = createDynamicVertexBuffer(
		verts.data(), static_cast<UINT>(verts.size() * sizeof(Vertex3D)));
	if (!vb) goto restore;

	{
		UINT stride = sizeof(Vertex3D), off = 0;
		m_d3dContext->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &off);

		const auto& indices = mesh.indices();
		if (!indices.empty())
		{
			auto ib = createDynamicIndexBuffer(
				indices.data(), static_cast<UINT>(indices.size() * sizeof(uint32_t)));
			if (ib)
			{
				m_d3dContext->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
				m_d3dContext->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
			}
		}
		else
		{
			m_d3dContext->Draw(static_cast<UINT>(verts.size()), 0);
		}
	}

	restore:
	m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());
	m_d3dContext->RSSetState(m_rasterizerState.Get());
}

/// @brief 旧 API と互換性を保つためのアウトライン描画
/// @param mesh 描画するメッシュ
/// @param worldTransform ワールド変換行列
inline void Renderer3D::drawMeshOutline(const Mesh& mesh, const sgc::Mat4f& worldTransform)
{
	if (!m_outlineVS || !m_outlinePS)
	{
		return;
	}

	m_d3dContext->VSSetShader(m_outlineVS.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_outlinePS.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_outlineInputLayout.Get());
	m_d3dContext->RSSetState(m_outlineFrontCull.Get());

	updateTransformCB(worldTransform);

	const auto& verts = mesh.vertices();
	auto vb = createDynamicVertexBuffer(
		verts.data(),
		static_cast<UINT>(verts.size() * sizeof(Vertex3D)));
	if (!vb)
	{
		return;
	}

	UINT stride = sizeof(Vertex3D);
	UINT offset = 0;
	m_d3dContext->IASetVertexBuffers(
		0, 1, vb.GetAddressOf(), &stride, &offset);

	const auto& indices = mesh.indices();
	if (!indices.empty())
	{
		auto ib = createDynamicIndexBuffer(
			indices.data(),
			static_cast<UINT>(indices.size() * sizeof(uint32_t)));
		if (!ib)
		{
			return;
		}
		m_d3dContext->IASetIndexBuffer(
			ib.Get(), DXGI_FORMAT_R32_UINT, 0);
		m_d3dContext->DrawIndexed(
			static_cast<UINT>(indices.size()), 0, 0);
	}
	else
	{
		m_d3dContext->Draw(
			static_cast<UINT>(verts.size()), 0);
	}
}

/// @brief sgc::Mat4f をインスタンス頂点属性の行データへ変換する
inline Renderer3D::InstanceData Renderer3D::toInstanceData(const sgc::Mat4f& world) noexcept
{
	// 頂点属性から作る float4x4 では暗黙の転置がないため、glm の列を row0 から row3 として格納する。
	const glm::mat4 g = toGlm(world);
	float colMajor[4][4];
	toColumnMajor(colMajor, g);

	InstanceData data;
	std::memcpy(data.row0, colMajor[0], sizeof(data.row0));
	std::memcpy(data.row1, colMajor[1], sizeof(data.row1));
	std::memcpy(data.row2, colMajor[2], sizeof(data.row2));
	std::memcpy(data.row3, colMajor[3], sizeof(data.row3));
	return data;
}

/// @brief インスタンシング用の頂点シェーダーと入力レイアウトを必要なときだけ生成する
inline void Renderer3D::ensureInstancedPipeline()
{
	if (m_instancedVS || m_instancedPipelineFailed)
	{
		return;
	}

	try
	{
		auto vsBlob = compileHLSL(INSTANCED_VS_3D, "VSMain", "vs_5_0");
		auto multiVsBlob = compileHLSL(MULTI_LIGHT_VS_3D_INSTANCED, "VSMain", "vs_5_0");

		const D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA,   0},
			{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA,   0},
			{"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
		};

		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11VertexShader> multiVs;
		ComPtr<ID3D11InputLayout> layoutObj;
		HRESULT hr = m_d3dDevice->CreateVertexShader(
			vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.GetAddressOf());
		if (SUCCEEDED(hr))
		{
			hr = m_d3dDevice->CreateVertexShader(
				multiVsBlob->GetBufferPointer(), multiVsBlob->GetBufferSize(),
				nullptr, multiVs.GetAddressOf());
		}
		if (SUCCEEDED(hr))
		{
			hr = m_d3dDevice->CreateInputLayout(
				layout, static_cast<UINT>(sizeof(layout) / sizeof(layout[0])),
				vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
				layoutObj.GetAddressOf());
		}
		if (FAILED(hr))
		{
			m_instancedPipelineFailed = true;
			return;
		}
		m_instancedVS = vs;
		m_instancedMultiVS = multiVs;
		m_instancedInputLayout = layoutObj;
	}
	catch (const std::exception&)
	{
		m_instancedPipelineFailed = true;
	}
}

/// @brief kInstanceBatchMax 個分のインスタンス頂点バッファを必要なときだけ生成する
inline bool Renderer3D::ensureInstanceBuffer()
{
	if (m_instanceVB)
	{
		return true;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = static_cast<UINT>(kInstanceBatchMax * sizeof(InstanceData));
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	const HRESULT hr = m_d3dDevice->CreateBuffer(&desc, nullptr, m_instanceVB.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		m_instanceScratch.reserve(kInstanceBatchMax);
	}
	return SUCCEEDED(hr);
}

/// @brief drawMesh と同じ規則でインスタンス描画用のテクスチャとサンプラーをバインドする
inline void Renderer3D::bindMaterialTexture(const Material& material)
{
	ID3D11ShaderResourceView* srv = nullptr;
	if (material.albedoTexture)
	{
		srv = getOrUploadAlbedoSrv(material.albedoTexture);
	}
	if (!srv && m_currentSRV) { srv = m_currentSRV.Get(); }
	if (!srv && m_defaultWhiteSRV) { srv = m_defaultWhiteSRV.Get(); }
	bindAlbedoSrvIfChanged(srv);
}

/// @brief PS スロット 0 の SRV とサンプラーが直前の値と異なるときだけ設定する
inline void Renderer3D::bindAlbedoSrvIfChanged(ID3D11ShaderResourceView* srv)
{
	if (srv && srv != m_boundPSSrv0)
	{
		m_d3dContext->PSSetShaderResources(0, 1, &srv);
		m_boundPSSrv0 = srv;
	}
	if (m_samplerState && m_samplerState.Get() != m_boundPSSampler0)
	{
		ID3D11SamplerState* sampler = m_samplerState.Get();
		m_d3dContext->PSSetSamplers(0, 1, &sampler);
		m_boundPSSampler0 = sampler;
	}
}

/// @brief 同じメッシュを複数のワールド行列で GPU instancing 描画する
inline void Renderer3D::drawMeshInstanced(const Mesh& mesh,
                                          std::span<const sgc::Mat4f> worlds,
                                          const Material* material)
{
	if (!m_initialized || mesh.vertexCount() == 0 || worlds.empty())
	{
		return;
	}

	ensureInstancedPipeline();
	const Material& mat = material ? *material : Material{};
	if (!m_instancedVS || !m_instancedInputLayout || !ensureInstanceBuffer())
	{
		// instancing を使えない場合は drawMesh のループに切り替える。
		for (const auto& world : worlds) { drawMesh(mesh, world, mat); }
		return;
	}

	drawSkyboxIfNeeded();
	updateTransformCB(sgc::Mat4f::identity());  // View/Projection のみ使う。World は各インスタンス属性側
	updateLightingCB(mat);
	if (m_useMultiLight) { updateLightArrayCB(); }
	bindMaterialTexture(mat);

	const auto& verts = mesh.vertices();
	auto vb = createDynamicVertexBuffer(verts.data(), static_cast<UINT>(verts.size() * sizeof(Vertex3D)));
	if (!vb) { return; }
	const auto& indices = mesh.indices();
	ComPtr<ID3D11Buffer> ib;
	if (!indices.empty())
	{
		ib = createDynamicIndexBuffer(indices.data(), static_cast<UINT>(indices.size() * sizeof(uint32_t)));
		if (!ib) { return; }
	}

	auto* vs = (m_useMultiLight && m_instancedMultiVS) ? m_instancedMultiVS.Get() : m_instancedVS.Get();
	m_d3dContext->VSSetShader(vs, nullptr, 0);
	m_d3dContext->IASetInputLayout(m_instancedInputLayout.Get());

	for (std::size_t offset = 0; offset < worlds.size(); offset += kInstanceBatchMax)
	{
		const std::size_t batchCount = std::min(kInstanceBatchMax, worlds.size() - offset);
		m_instanceScratch.clear();
		for (std::size_t i = 0; i < batchCount; ++i)
		{
			const auto& world = worlds[offset + i];
			if (m_frustumCullingEnabled && !m_frustum.isMeshVisible(mesh.localAABB(), world))
			{
				++m_culledCount;
				continue;
			}
			if (m_occlusionCullingEnabled && m_occlusionCuller.hasDepth() &&
			    m_occlusionCuller.isOccluded(worldOcclusionAABB(mesh.localAABB(), world),
			                                 occlusionViewProj().data()))
			{
				++m_occludedCount;
				continue;
			}
			if (m_instanceScratch.size() >= m_instanceScratch.capacity())
			{
				debug::warnOnce("render.instancing.scratch_realloc",
					"Renderer3D: m_instanceScratch は予約容量 kInstanceBatchMax を"
					"超えて再確保されています（インスタンス描画のホットパスで allocation 発生）");
			}
			m_instanceScratch.push_back(toInstanceData(world));
		}
		if (!m_instanceScratch.empty())
		{
			drawInstanceBatch(vb.Get(), ib.Get(), static_cast<UINT>(verts.size()),
			                   static_cast<UINT>(indices.size()));
		}
	}

	// 次の drawMesh のために通常描画用のステートへ戻す。
	m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());
	ID3D11Buffer* nullVB = nullptr;
	const UINT zero = 0;
	m_d3dContext->IASetVertexBuffers(1, 1, &nullVB, &zero, &zero);
}

/// @brief 1 バッチ分のインスタンスデータを Map、Unmap して描画する
inline void Renderer3D::drawInstanceBatch(ID3D11Buffer* vb, ID3D11Buffer* ib,
                                          UINT vertexCount, UINT indexCount)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(m_d3dContext->Map(m_instanceVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		return;
	}
	std::memcpy(mapped.pData, m_instanceScratch.data(),
	            m_instanceScratch.size() * sizeof(InstanceData));
	m_d3dContext->Unmap(m_instanceVB.Get(), 0);

	ID3D11Buffer* buffers[2] = {vb, m_instanceVB.Get()};
	const UINT strides[2] = {sizeof(Vertex3D), sizeof(InstanceData)};
	const UINT offsets[2] = {0, 0};
	m_d3dContext->IASetVertexBuffers(0, 2, buffers, strides, offsets);

	const auto instanceCount = static_cast<UINT>(m_instanceScratch.size());
	if (ib)
	{
		m_d3dContext->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
		m_d3dContext->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
	}
	else
	{
		m_d3dContext->DrawInstanced(vertexCount, instanceCount, 0, 0);
	}
	++m_drawCallCount;
}

/// @brief MSAA 深度の全サンプルから最小値を取り、単一サンプルの R32_FLOAT として間引いて読み戻す
/// @details DX11 では深度を ResolveSubresource できないため、Texture2DMS<float> を読むフルスクリーン PS で変換する。描画ステートは次の beginFrame で再設定される。
inline bool Renderer3D::resolveMultisampledOcclusionDepth(UINT width, UINT height)
{
	if (!m_depthSRV) { return false; }

	ensureOcclusionResolvePipeline();
	if (!m_occlusionResolveVS || !ensureOcclusionResolveTargets(width, height))
	{
		return false;
	}

	// DSV を OM から外し、深度を SRV として読む。
	ID3D11RenderTargetView* rtv = m_occlusionResolveRTV.Get();
	m_d3dContext->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT vp = {};
	vp.Width = static_cast<float>(width);
	vp.Height = static_cast<float>(height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_d3dContext->RSSetViewports(1, &vp);

	m_d3dContext->VSSetShader(m_occlusionResolveVS.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_occlusionResolvePS.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(nullptr);
	m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11ShaderResourceView* depthSrv = m_depthSRV.Get();
	m_d3dContext->PSSetShaderResources(0, 1, &depthSrv);

	m_d3dContext->Draw(3, 0);

	// 次の深度書き込みとの競合を避けるため、SRV のバインドを外す。
	ID3D11ShaderResourceView* nullSrv = nullptr;
	m_d3dContext->PSSetShaderResources(0, 1, &nullSrv);
	// PS スロット 0 を直接変更したため、drawMesh のステートキャッシュを無効にする。
	m_boundPSSrv0 = nullptr;

	m_d3dContext->CopyResource(m_occlusionResolveStaging.Get(), m_occlusionResolveRT.Get());

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(m_d3dContext->Map(m_occlusionResolveStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
	{
		return false;
	}

	const int stride = kOcclusionDownsampleStride;
	const int dsW = std::max(1, static_cast<int>(width) / stride);
	const int dsH = std::max(1, static_cast<int>(height) / stride);
	m_occlusionDepthScratch.resize(static_cast<std::size_t>(dsW) * static_cast<std::size_t>(dsH));

	const auto* base = static_cast<const std::uint8_t*>(mapped.pData);
	for (int y = 0; y < dsH; ++y)
	{
		const int srcY = std::min(y * stride, static_cast<int>(height) - 1);
		const auto* row = reinterpret_cast<const float*>(base + static_cast<std::size_t>(srcY) * mapped.RowPitch);
		for (int x = 0; x < dsW; ++x)
		{
			const int srcX = std::min(x * stride, static_cast<int>(width) - 1);
			m_occlusionDepthScratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(dsW) + static_cast<std::size_t>(x)] =
				row[srcX];
		}
	}

	m_d3dContext->Unmap(m_occlusionResolveStaging.Get(), 0);
	m_occlusionCuller.updateDepth(m_occlusionDepthScratch.data(), dsW, dsH);
	return true;
}

/// @brief 深度バッファを読み戻し、間引いて OcclusionCuller に渡す
inline void Renderer3D::updateOcclusionDepth()
{
	if (!m_depthStencilView)
	{
		return;
	}
	if (m_depthIsMultisampled)
	{
		if (!resolveMultisampledOcclusionDepth(
			static_cast<UINT>(m_config.viewportWidth), static_cast<UINT>(m_config.viewportHeight)))
		{
			debug::warnOnce("render.occlusion.msaa_depth_unsupported",
				"Renderer3D: occlusion depth readback skipped (MSAA min-depth resolve pipeline "
				"unavailable on this device, occlusion culling has no effect)");
		}
		return;
	}

	ComPtr<ID3D11Resource> depthRes;
	m_depthStencilView->GetResource(depthRes.GetAddressOf());
	ComPtr<ID3D11Texture2D> depthTex;
	if (FAILED(depthRes.As(&depthTex)))
	{
		return;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	depthTex->GetDesc(&desc);

	if (!m_occlusionStaging || m_occlusionStagingW != desc.Width || m_occlusionStagingH != desc.Height)
	{
		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		m_occlusionStaging.Reset();
		if (FAILED(m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, m_occlusionStaging.GetAddressOf())))
		{
			return;
		}
		m_occlusionStagingW = desc.Width;
		m_occlusionStagingH = desc.Height;
	}

	m_d3dContext->CopyResource(m_occlusionStaging.Get(), depthTex.Get());

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(m_d3dContext->Map(m_occlusionStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
	{
		return;
	}

	const int stride = kOcclusionDownsampleStride;
	const int dsW = std::max(1, static_cast<int>(desc.Width) / stride);
	const int dsH = std::max(1, static_cast<int>(desc.Height) / stride);
	m_occlusionDepthScratch.resize(static_cast<std::size_t>(dsW) * static_cast<std::size_t>(dsH));

	const auto* base = static_cast<const std::uint8_t*>(mapped.pData);
	for (int y = 0; y < dsH; ++y)
	{
		const int srcY = std::min(y * stride, static_cast<int>(desc.Height) - 1);
		const auto* row = reinterpret_cast<const std::uint32_t*>(base + static_cast<std::size_t>(srcY) * mapped.RowPitch);
		for (int x = 0; x < dsW; ++x)
		{
			const int srcX = std::min(x * stride, static_cast<int>(desc.Width) - 1);
			// D24_UNORM_S8_UINT は下位 24 bit が深度、上位 8 bit がステンシル。
			const std::uint32_t raw = row[srcX];
			m_occlusionDepthScratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(dsW) + static_cast<std::size_t>(x)] =
				static_cast<float>(raw & 0x00FFFFFFu) / 16777215.0f;
		}
	}

	m_d3dContext->Unmap(m_occlusionStaging.Get(), 0);
	m_occlusionCuller.updateDepth(m_occlusionDepthScratch.data(), dsW, dsH);
}

/// @brief 最初の drawMesh のときだけ skybox を描画する
inline void Renderer3D::drawSkyboxIfNeeded()
{
	if (!m_skyboxEnabled) return;
	if (m_skyboxDrawnThisFrame) return;
	if (!m_skyboxImpl.hasValidCubemap()) return;

	if (m_skyboxNeedsInit)
	{
		m_skyboxImpl.initializeDx11(m_device);
		m_skyboxNeedsInit = false;
	}
	if (!m_skyboxImpl.isInitialized()) return;

	m_skyboxImpl.drawDx11(m_d3dContext, m_viewMatrix, m_projMatrix);
	m_skyboxDrawnThisFrame = true;
}

} // namespace mitiru::render

#endif // _WIN32
