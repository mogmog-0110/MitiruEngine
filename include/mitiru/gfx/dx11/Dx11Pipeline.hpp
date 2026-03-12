#pragma once

/// @file Dx11Pipeline.hpp
/// @brief DirectX 11パイプラインステート実装
/// @details 入力レイアウト・ブレンドステート・ラスタライザステートを
///          統合管理するPSOラッパー。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <stdexcept>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>

namespace mitiru::gfx
{

/// @brief パイプライン記述子
/// @details パイプライン生成に必要なパラメータを集約する。
struct Dx11PipelineDesc
{
	Dx11Shader* vertexShader = nullptr;  ///< 頂点シェーダー
	Dx11Shader* pixelShader = nullptr;   ///< ピクセルシェーダー
	BlendMode blendMode = BlendMode::Alpha;   ///< ブレンドモード
	bool wireframe = false;              ///< ワイヤーフレーム表示
	bool scissorEnable = false;          ///< シザー矩形有効化
};

/// @brief DirectX 11パイプラインステート実装
/// @details InputLayout + BlendState + RasterizerState + シェーダーを束ねる。
class Dx11Pipeline final : public IPipeline
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param desc パイプライン記述子
	explicit Dx11Pipeline(ID3D11Device* device, const Dx11PipelineDesc& desc)
		: m_vs(desc.vertexShader)
		, m_ps(desc.pixelShader)
	{
		if (!device || !desc.vertexShader || !desc.pixelShader)
		{
			throw std::runtime_error(
				"Dx11Pipeline: null device or shaders");
		}

		createInputLayout(device, *desc.vertexShader);
		createBlendState(device, desc.blendMode);
		createRasterizerState(device, desc.wireframe, desc.scissorEnable);
		m_valid = true;
	}

	/// @brief パイプラインが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept override
	{
		return m_valid;
	}

	/// @brief パイプラインをデバイスコンテキストにバインドする
	/// @param context D3D11デバイスコンテキスト
	void bind(ID3D11DeviceContext* context) const
	{
		if (!context || !m_valid)
		{
			return;
		}

		/// 入力レイアウトを設定する
		context->IASetInputLayout(m_inputLayout.Get());
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// シェーダーを設定する
		if (m_vs)
		{
			context->VSSetShader(
				m_vs->getVertexShader(), nullptr, 0);
		}
		if (m_ps)
		{
			context->PSSetShader(
				m_ps->getPixelShader(), nullptr, 0);
		}

		/// ブレンドステートを設定する
		const float blendFactor[4] = {0, 0, 0, 0};
		context->OMSetBlendState(
			m_blendState.Get(), blendFactor, 0xFFFFFFFF);

		/// ラスタライザステートを設定する
		context->RSSetState(m_rasterizerState.Get());
	}

private:
	/// @brief Vertex2Dに対応する入力レイアウトを生成する
	/// @param device D3D11デバイス
	/// @param vs 頂点シェーダー（バイトコード取得用）
	void createInputLayout(ID3D11Device* device, const Dx11Shader& vs)
	{
		/// Vertex2D: position(float2) + texCoord(float2) + color(float4)
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
		};

		const auto& bytecode = vs.bytecode();
		HRESULT hr = device->CreateInputLayout(
			layout,
			static_cast<UINT>(std::size(layout)),
			bytecode.data(),
			bytecode.size(),
			m_inputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Pipeline: CreateInputLayout failed");
		}
	}

	/// @brief ブレンドステートを生成する
	/// @param device D3D11デバイス
	/// @param mode ブレンドモード
	void createBlendState(ID3D11Device* device, BlendMode mode)
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;

		auto& rt = desc.RenderTarget[0];
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		switch (mode)
		{
		case BlendMode::None:
			rt.BlendEnable = FALSE;
			break;
		case BlendMode::Alpha:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			break;
		case BlendMode::Additive:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D11_BLEND_ONE;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			break;
		case BlendMode::Multiplicative:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
			rt.DestBlend = D3D11_BLEND_ZERO;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
			rt.DestBlendAlpha = D3D11_BLEND_ZERO;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			break;
		}

		HRESULT hr = device->CreateBlendState(
			&desc, m_blendState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Pipeline: CreateBlendState failed");
		}
	}

	/// @brief ラスタライザステートを生成する
	/// @param device D3D11デバイス
	/// @param wireframe ワイヤーフレーム表示フラグ
	/// @param scissorEnable シザー矩形有効フラグ
	void createRasterizerState(ID3D11Device* device,
	                           bool wireframe,
	                           bool scissorEnable)
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = wireframe
			? D3D11_FILL_WIREFRAME
			: D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthClipEnable = TRUE;
		desc.ScissorEnable = scissorEnable ? TRUE : FALSE;

		HRESULT hr = device->CreateRasterizerState(
			&desc, m_rasterizerState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Pipeline: CreateRasterizerState failed");
		}
	}

	ComPtr<ID3D11InputLayout> m_inputLayout;         ///< 入力レイアウト
	ComPtr<ID3D11BlendState> m_blendState;           ///< ブレンドステート
	ComPtr<ID3D11RasterizerState> m_rasterizerState; ///< ラスタライザステート
	Dx11Shader* m_vs = nullptr;                       ///< 頂点シェーダー（非所有）
	Dx11Shader* m_ps = nullptr;                       ///< ピクセルシェーダー（非所有）
	bool m_valid = false;                             ///< 有効フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
