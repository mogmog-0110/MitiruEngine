#pragma once

/// @file Dx11CommandList.hpp
/// @brief DirectX 11コマンドリスト実装
/// @details D3D11即時コンテキストをラップし、ICommandListインターフェースを実装する。
///          描画コマンドを直接GPUに発行する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>

#include <d3d11.h>
#include <wrl/client.h>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11Pipeline.hpp>
#include <mitiru/gfx/dx11/Dx11RenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11コマンドリスト実装
/// @details 即時コンテキストを使い、描画コマンドを直接発行する。
class Dx11CommandList final : public ICommandList
{
public:
	/// @brief コンストラクタ
	/// @param context D3D11即時コンテキスト
	explicit Dx11CommandList(ID3D11DeviceContext* context)
		: m_context(context)
	{
		if (!context)
		{
			throw std::runtime_error(
				"Dx11CommandList: context is null");
		}
	}

	/// @brief コマンド記録を開始する
	void begin() override
	{
		m_recording = true;
	}

	/// @brief コマンド記録を終了する
	void end() override
	{
		m_recording = false;
	}

	/// @brief レンダーターゲットを設定する
	/// @param target 描画先レンダーターゲット
	void setRenderTarget(IRenderTarget* target) override
	{
		if (!m_recording || !target)
		{
			return;
		}

		/// Dx11RenderTargetからRTVを取得する
		auto* dx11RT = dynamic_cast<Dx11RenderTarget*>(target);
		if (!dx11RT)
		{
			return;
		}

		auto* rtv = dx11RT->getRTV();
		if (rtv)
		{
			m_context->OMSetRenderTargets(1, &rtv, nullptr);
		}
	}

	/// @brief レンダーターゲットをクリアする
	/// @param color クリア色
	void clearRenderTarget(const sgc::Colorf& color) override
	{
		if (!m_recording)
		{
			return;
		}

		/// 現在バインドされているRTVを取得してクリアする
		ID3D11RenderTargetView* currentRTV = nullptr;
		m_context->OMGetRenderTargets(1, &currentRTV, nullptr);
		if (currentRTV)
		{
			const float clearColor[4] = {
				color.r, color.g, color.b, color.a
			};
			m_context->ClearRenderTargetView(currentRTV, clearColor);
			currentRTV->Release();
		}
	}

	/// @brief パイプライン状態を設定する
	/// @param pipeline 使用するパイプライン
	void setPipeline(IPipeline* pipeline) override
	{
		if (!m_recording || !pipeline)
		{
			return;
		}

		auto* dx11Pipeline = dynamic_cast<Dx11Pipeline*>(pipeline);
		if (dx11Pipeline)
		{
			dx11Pipeline->bind(m_context);
		}
	}

	/// @brief 頂点バッファを設定する
	/// @param buffer 頂点バッファ
	void setVertexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer)
		{
			return;
		}

		auto* dx11Buf = dynamic_cast<Dx11Buffer*>(buffer);
		if (!dx11Buf)
		{
			return;
		}

		ID3D11Buffer* d3dBuf = dx11Buf->getD3DBuffer();
		const UINT stride = sizeof(float) * 8;  ///< Vertex2D: pos(2) + uv(2) + color(4)
		const UINT offset = 0;
		m_context->IASetVertexBuffers(0, 1, &d3dBuf, &stride, &offset);
	}

	/// @brief インデックスバッファを設定する
	/// @param buffer インデックスバッファ
	void setIndexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer)
		{
			return;
		}

		auto* dx11Buf = dynamic_cast<Dx11Buffer*>(buffer);
		if (!dx11Buf)
		{
			return;
		}

		m_context->IASetIndexBuffer(
			dx11Buf->getD3DBuffer(),
			DXGI_FORMAT_R32_UINT, 0);
	}

	/// @brief ビューポートを設定する
	/// @param width ビューポート幅
	/// @param height ビューポート高さ
	void setViewport(float width, float height)
	{
		if (!m_recording)
		{
			return;
		}

		D3D11_VIEWPORT vp = {};
		vp.Width = width;
		vp.Height = height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		m_context->RSSetViewports(1, &vp);
	}

	/// @brief 定数バッファをVSにバインドする
	/// @param slot スロット番号
	/// @param buffer 定数バッファ
	void setVSConstantBuffer(UINT slot, Dx11Buffer* buffer)
	{
		if (!m_recording || !buffer)
		{
			return;
		}

		ID3D11Buffer* d3dBuf = buffer->getD3DBuffer();
		m_context->VSSetConstantBuffers(slot, 1, &d3dBuf);
	}

	/// @brief インデックス付き描画を実行する
	/// @param indexCount 描画するインデックス数
	/// @param startIndex 開始インデックスオフセット
	/// @param baseVertex ベース頂点オフセット
	void drawIndexed(std::uint32_t indexCount,
	                 std::uint32_t startIndex,
	                 std::int32_t baseVertex) override
	{
		if (!m_recording)
		{
			return;
		}

		m_context->DrawIndexed(
			indexCount, startIndex, baseVertex);
	}

	/// @brief 頂点のみで描画を実行する
	/// @param vertexCount 描画する頂点数
	/// @param startVertex 開始頂点オフセット
	void draw(std::uint32_t vertexCount,
	          std::uint32_t startVertex) override
	{
		if (!m_recording)
		{
			return;
		}

		m_context->Draw(vertexCount, startVertex);
	}

	/// @brief 内部のD3D11コンテキストを取得する
	[[nodiscard]] ID3D11DeviceContext* getD3DContext() const noexcept
	{
		return m_context;
	}

private:
	ID3D11DeviceContext* m_context = nullptr;  ///< D3D11コンテキスト（非所有）
	bool m_recording = false;                  ///< 記録中フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
