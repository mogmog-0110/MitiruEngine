#pragma once

/// @file Renderer3D_Draw_impl.hpp
/// @brief Renderer3D のフレーム描画・定数バッファ更新・テクスチャ転送の実装本体（Renderer3D.hpp から機械的分割）

#include <mitiru/render/Renderer3D.hpp>

#ifdef _WIN32

namespace mitiru::render
{

/// @brief フレーム描画を開始する
/// @param clearColor 画面クリア色
inline void Renderer3D::beginFrame(const sgc::Colorf& clearColor)
{
	MITIRU_ZONE_NAMED("Render::Dx11::BeginFrame");
	if (!m_initialized)
	{
		return;
	}

	m_frameActive = true;
	m_drawCallCount = 0;
	m_outlineQueue.clear();
	m_skyboxDrawnThisFrame = false;

	/// レンダーターゲットと深度バッファを設定する
	auto* swapChain = m_device->getSwapChain();
	if (!swapChain)
	{
		return;
	}

	auto* rtv = swapChain->getRenderTargetView();
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

	/// ビューポートを設定する
	D3D11_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_d3dContext->RSSetViewports(1, &vp);

	/// シェーダーを設定する
	m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());
	m_d3dContext->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/// ラスタライザが変更されていれば再作成する
	if (m_rasterizerDirty)
	{
		createRasterizerState();
		m_rasterizerDirty = false;
	}
	m_d3dContext->RSSetState(m_rasterizerState.Get());

	/// 深度ステンシルステートを設定する
	m_d3dContext->OMSetDepthStencilState(
		m_depthStencilState.Get(), 0);

	/// ブレンドステートを設定する
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

/// @brief メッシュを描画する
/// @param mesh 描画するメッシュ
/// @param worldTransform ワールド変換行列
/// @param material マテリアル
inline void Renderer3D::drawMesh(const Mesh& mesh,
              const sgc::Mat4f& worldTransform,
              const Material& material)
{
	if (!m_initialized || mesh.vertexCount() == 0)
	{
		return;
	}

	// アウトラインは無効（ポストプロセス方式で別途実装予定）

	// skybox が必要なら最初の drawMesh の前に描画する
	drawSkyboxIfNeeded();

	/// トランスフォーム定数バッファを更新する
	updateTransformCB(worldTransform);

	/// ライティング定数バッファを更新する
	updateLightingCB(material);

	/// マルチライト経路ならライト配列 CB (b2) も毎フレーム更新する
	if (m_useMultiLight)
	{
		updateLightArrayCB();
	}

	/// テクスチャとサンプラーをバインドする
	/// material.albedoTexture が優先。null なら setTexture / clearTexture で
	/// 設定された m_currentSRV を使う（後方互換）。
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
	if (srv)
	{
		m_d3dContext->PSSetShaderResources(0, 1, &srv);
	}
	if (m_samplerState)
	{
		ID3D11SamplerState* sampler = m_samplerState.Get();
		m_d3dContext->PSSetSamplers(0, 1, &sampler);
	}

	/// 頂点バッファを作成してバインドする
	const auto& verts = mesh.vertices();
	const auto vbSize = static_cast<UINT>(
		verts.size() * sizeof(Vertex3D));

	auto vb = createDynamicVertexBuffer(verts.data(), vbSize);
	if (!vb)
	{
		return;
	}

	const UINT stride = sizeof(Vertex3D);
	const UINT offset = 0;
	m_d3dContext->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);

	/// インデックスバッファを作成してバインドする（あれば）
	const auto& indices = mesh.indices();
	if (!indices.empty())
	{
		const auto ibSize = static_cast<UINT>(
			indices.size() * sizeof(uint32_t));

		auto ib = createDynamicIndexBuffer(indices.data(), ibSize);
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

	++m_drawCallCount;

	// アウトラインはdrawMesh内で完結（endFrame不要）
}

/// @brief マルチライト CB を更新して b2 にバインドする
/// @details `useMultiLight()` が true のとき drawMesh から呼ばれる。
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

/// @brief トランスフォーム定数バッファを更新する
/// @param worldTransform ワールド行列
/// @details glmを経由してHLSL互換のrow-majorレイアウトに変換する。
///          sgc::Mat4fのメモリレイアウトがHLSLと一致しない問題を回避。
inline void Renderer3D::updateTransformCB(const sgc::Mat4f& worldTransform)
{
	CbTransform cb;
	// glm経由でHLSL互換レイアウトに変換
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

/// @brief ライティング定数バッファを更新する
/// @param material マテリアル
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

/// @brief 動的頂点バッファを作成する
/// @param data 頂点データ
/// @param sizeBytes データサイズ
/// @return 作成されたバッファ
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

/// @brief 動的インデックスバッファを作成する
/// @param data インデックスデータ
/// @param sizeBytes データサイズ
/// @return 作成されたバッファ
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

/// @brief テクスチャデータをGPUにアップロードする
/// @param tex アップロードするテクスチャ
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

/// @brief Material.albedoTexture 用の SRV を取得（必要なら upload + cache）
/// @details `setTexture` の global state とは独立した per-Texture* キャッシュ。
///          同じ `Texture*` は 1 度しかアップロードしない。
inline ID3D11ShaderResourceView* Renderer3D::getOrUploadAlbedoSrv(const Texture* tex)
{
	if (!tex || !tex->valid()) return nullptr;
	auto it = m_albedoSrvCache.find(tex);
	if (it != m_albedoSrvCache.end())
	{
		return it->second.Get();
	}
	// texture を upload する (uploadTexture と同様だが cache に格納する)
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

/// @brief アウトラインパスでメッシュを描画する
/// @brief アウトラインパス（drawMesh内から呼ばれる、メイン描画の前に実行）
/// シェーダー・ラスタライザを切替→描画→即座に復元
inline void Renderer3D::drawOutlinePass(const Mesh& mesh, const sgc::Mat4f& worldTransform)
{
	// アウトラインシェーダーに切替
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
	// 即座にメインシェーダーに復元（次の行でメイン描画が行われるため）
	m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());
	m_d3dContext->RSSetState(m_rasterizerState.Get());
}

/// @brief アウトライン描画（旧API、互換用）
/// @param mesh 描画するメッシュ
/// @param worldTransform ワールド変換行列
inline void Renderer3D::drawMeshOutline(const Mesh& mesh, const sgc::Mat4f& worldTransform)
{
	if (!m_outlineVS || !m_outlinePS)
	{
		return;
	}

	/// アウトラインシェーダーに切り替える
	m_d3dContext->VSSetShader(m_outlineVS.Get(), nullptr, 0);
	m_d3dContext->PSSetShader(m_outlinePS.Get(), nullptr, 0);
	m_d3dContext->IASetInputLayout(m_outlineInputLayout.Get());
	m_d3dContext->RSSetState(m_outlineFrontCull.Get());

	/// トランスフォーム定数バッファを更新する（メインパスと同じ）
	updateTransformCB(worldTransform);

	/// 頂点バッファを作成してバインドする
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

	/// インデックスバッファを作成してバインドする（あれば）
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

/// @brief skybox を描画する（drawMesh の最初の呼び出しで一度だけ）
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
