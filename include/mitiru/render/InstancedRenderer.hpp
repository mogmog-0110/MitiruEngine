#pragma once

/// @file InstancedRenderer.hpp
/// @brief GPUインスタンス描画
/// @details DX11のDrawIndexedInstanced()を使い、同一メッシュの大量描画を
///          1回のドローコールで実行するインスタンスレンダラー。
///          頂点バッファスロット0にメッシュ頂点、スロット1にインスタンスデータを
///          バインドし、頂点シェーダーでワールド変換を適用する。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

namespace mitiru::render
{

/// @brief インスタンスデータ構造体
/// @details 1インスタンスあたりのGPUデータ。頂点バッファスロット1から読み取る。
struct alignas(16) InstanceData
{
	float world[4][4] = {      ///< ワールド変換行列（4x4）
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};
	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};      ///< インスタンス色（RGBA）
	float customData[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< カスタムデータ（用途自由）
};

/// @brief インスタンス描画用頂点シェーダー（HLSL埋め込み）
/// @details スロット0: メッシュ頂点（POSITION, NORMAL, TEXCOORD）
///          スロット1: インスタンスデータ（INST_WORLD行0-3, INST_COLOR, INST_CUSTOM）
inline constexpr const char* kInstancedVS = R"hlsl(
cbuffer CbViewProj : register(b0)
{
    float4x4 viewProj;
};

struct VSInput
{
    // Per-vertex (slot 0)
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;

    // Per-instance (slot 1)
    float4 instWorld0 : INST_WORLD0;
    float4 instWorld1 : INST_WORLD1;
    float4 instWorld2 : INST_WORLD2;
    float4 instWorld3 : INST_WORLD3;
    float4 instColor  : INST_COLOR;
    float4 instCustom : INST_CUSTOM;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
    float4 custom   : CUSTOM;
};

PSInput main(VSInput input)
{
    PSInput output;

    float4x4 world = float4x4(
        input.instWorld0,
        input.instWorld1,
        input.instWorld2,
        input.instWorld3
    );

    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, viewProj);
    output.normal   = mul(float4(input.normal, 0.0), world).xyz;
    output.texcoord = input.texcoord;
    output.color    = input.instColor;
    output.custom   = input.instCustom;

    return output;
}
)hlsl";

/// @brief インスタンス描画用ピクセルシェーダー（HLSL埋め込み）
inline constexpr const char* kInstancedPS = R"hlsl(
struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
    float4 custom   : CUSTOM;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(0.3, -1.0, 0.5));
    float ndotl = saturate(dot(normalize(input.normal), -lightDir));
    float3 lit = input.color.rgb * (ndotl * 0.7 + 0.3);
    return float4(lit, input.color.a);
}
)hlsl";

/// @brief GPUインスタンスレンダラー
/// @details 同一メッシュの大量描画を1回のDrawIndexedInstanced()で実行する。
///          addInstance()でインスタンスをキューに追加し、flush()でまとめて描画する。
///
/// @code
/// mitiru::render::InstancedRenderer instRenderer;
/// instRenderer.init(device, context, 10000);
///
/// instRenderer.beginBatch(context, meshVB, meshIB, indexCount);
/// for (auto& obj : objects)
/// {
///     instRenderer.addInstance(obj.worldMatrix, obj.color);
/// }
/// instRenderer.flush(context);
/// @endcode
class InstancedRenderer
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	InstancedRenderer() noexcept = default;

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 現在キューに入っているインスタンス数を取得する
	[[nodiscard]] std::uint32_t instanceCount() const noexcept
	{
		return static_cast<std::uint32_t>(m_instances.size());
	}

	/// @brief 最大インスタンス数を取得する
	[[nodiscard]] std::uint32_t maxInstances() const noexcept
	{
		return m_maxInstances;
	}

	/// @brief ドローコール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 描画されたインスタンス総数を取得する
	[[nodiscard]] int totalInstancesDrawn() const noexcept
	{
		return m_totalInstancesDrawn;
	}

	/// @brief レンダラーを初期化する
	/// @param device D3D11デバイス
	/// @param context D3D11デバイスコンテキスト
	/// @param maxInstances 1バッチあたりの最大インスタンス数
	void init(ID3D11Device* device,
	          ID3D11DeviceContext* context,
	          std::uint32_t maxInstances = 10000)
	{
		if (!device || !context)
		{
			throw std::invalid_argument(
				"InstancedRenderer::init: null device or context");
		}
		if (maxInstances == 0)
		{
			throw std::invalid_argument(
				"InstancedRenderer::init: maxInstances must be > 0");
		}

		m_device = device;
		m_maxInstances = maxInstances;

		createInstanceBuffer();
		compileShaders();
		createInputLayout();
		createConstantBuffer();

		m_instances.reserve(maxInstances);
		m_initialized = true;
		m_drawCallCount = 0;
		m_totalInstancesDrawn = 0;
	}

	/// @brief バッチ描画を開始する
	/// @param context D3D11デバイスコンテキスト
	/// @param meshVertexBuffer メッシュ頂点バッファ（スロット0）
	/// @param meshIndexBuffer メッシュインデックスバッファ
	/// @param indexCount インデックス数
	void beginBatch(ID3D11DeviceContext* context,
	                ID3D11Buffer* meshVertexBuffer,
	                ID3D11Buffer* meshIndexBuffer,
	                std::uint32_t indexCount)
	{
		if (!m_initialized)
		{
			return;
		}

		m_currentMeshVB = meshVertexBuffer;
		m_currentMeshIB = meshIndexBuffer;
		m_currentIndexCount = indexCount;
		m_instances.clear();
	}

	/// @brief インスタンスをキューに追加する
	/// @param worldMatrix ワールド変換行列（float[4][4]）
	/// @param color インスタンス色（RGBA float[4]）
	void addInstance(const float worldMatrix[4][4],
	                 const float color[4])
	{
		if (m_instances.size() >= m_maxInstances)
		{
			return;  ///< キャパシティ超過は無視する
		}

		InstanceData inst;
		std::memcpy(inst.world, worldMatrix, sizeof(float) * 16);
		std::memcpy(inst.color, color, sizeof(float) * 4);
		m_instances.push_back(inst);
	}

	/// @brief インスタンスをキューに追加する（InstanceData直接指定）
	/// @param data インスタンスデータ
	void addInstance(const InstanceData& data)
	{
		if (m_instances.size() >= m_maxInstances)
		{
			return;
		}
		m_instances.push_back(data);
	}

	/// @brief ビュー射影行列を設定する
	/// @param viewProj ビュー射影行列（float[4][4]）
	void setViewProjection(const float viewProj[4][4])
	{
		std::memcpy(m_viewProj, viewProj, sizeof(float) * 16);
	}

	/// @brief キューに入っているインスタンスをまとめて描画する
	/// @param context D3D11デバイスコンテキスト
	void flush(ID3D11DeviceContext* context)
	{
		if (!m_initialized || m_instances.empty())
		{
			return;
		}
		if (!m_currentMeshVB || !m_currentMeshIB)
		{
			return;
		}

		/// インスタンスバッファを更新する（Map/Unmap）
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_instanceBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		const auto copySize =
			sizeof(InstanceData) * m_instances.size();
		std::memcpy(mapped.pData, m_instances.data(), copySize);
		context->Unmap(m_instanceBuffer.Get(), 0);

		/// 定数バッファを更新する
		context->UpdateSubresource(
			m_cbViewProj.Get(), 0, nullptr,
			m_viewProj, 0, 0);

		/// シェーダーを設定する
		context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
		context->IASetInputLayout(m_inputLayout.Get());
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// 定数バッファをバインドする
		ID3D11Buffer* cbs[] = {m_cbViewProj.Get()};
		context->VSSetConstantBuffers(0, 1, cbs);

		/// 頂点バッファをバインドする（スロット0: メッシュ、スロット1: インスタンス）
		static constexpr UINT kDefaultVertexStride =
			sizeof(float) * 8;  ///< pos(3) + normal(3) + uv(2)
		ID3D11Buffer* vbs[2] = {m_currentMeshVB, m_instanceBuffer.Get()};
		constexpr UINT strides[2] = {
			kDefaultVertexStride,
			sizeof(InstanceData)
		};
		constexpr UINT offsets[2] = {0, 0};
		context->IASetVertexBuffers(0, 2, vbs, strides, offsets);

		/// インデックスバッファをバインドする
		context->IASetIndexBuffer(
			m_currentMeshIB, DXGI_FORMAT_R32_UINT, 0);

		/// インスタンス描画を実行する
		const auto instCount =
			static_cast<UINT>(m_instances.size());
		context->DrawIndexedInstanced(
			m_currentIndexCount, instCount, 0, 0, 0);

		m_drawCallCount++;
		m_totalInstancesDrawn += static_cast<int>(instCount);
		m_instances.clear();
	}

	/// @brief フレーム統計をリセットする
	void resetStats() noexcept
	{
		m_drawCallCount = 0;
		m_totalInstancesDrawn = 0;
	}

private:
	/// @brief 動的インスタンスバッファを作成する
	void createInstanceBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = static_cast<UINT>(
			sizeof(InstanceData) * m_maxInstances);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_instanceBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: instance buffer creation failed");
		}
	}

	/// @brief シェーダーをコンパイルする
	void compileShaders()
	{
		/// 頂点シェーダーのコンパイル
		ComPtr<ID3DBlob> vsBlob;
		ComPtr<ID3DBlob> errorBlob;

		HRESULT hr = D3DCompile(
			kInstancedVS, std::strlen(kInstancedVS),
			"InstancedVS", nullptr, nullptr,
			"main", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			vsBlob.GetAddressOf(),
			errorBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: VS compilation failed");
		}

		hr = m_device->CreateVertexShader(
			vsBlob->GetBufferPointer(),
			vsBlob->GetBufferSize(),
			nullptr,
			m_vertexShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: CreateVertexShader failed");
		}

		m_vsBytecode = vsBlob;

		/// ピクセルシェーダーのコンパイル
		ComPtr<ID3DBlob> psBlob;

		hr = D3DCompile(
			kInstancedPS, std::strlen(kInstancedPS),
			"InstancedPS", nullptr, nullptr,
			"main", "ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			psBlob.GetAddressOf(),
			errorBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: PS compilation failed");
		}

		hr = m_device->CreatePixelShader(
			psBlob->GetBufferPointer(),
			psBlob->GetBufferSize(),
			nullptr,
			m_pixelShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: CreatePixelShader failed");
		}
	}

	/// @brief 入力レイアウトを作成する
	/// @details スロット0: メッシュ頂点（per-vertex）
	///          スロット1: インスタンスデータ（per-instance）
	void createInputLayout()
	{
		/// 入力要素の定義
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			// スロット0: メッシュ頂点（per-vertex）
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
			 D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
			 D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,
			 D3D11_INPUT_PER_VERTEX_DATA, 0},

			// スロット1: インスタンスデータ（per-instance）
			{"INST_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,  0,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"INST_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"INST_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"INST_WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"INST_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
			{"INST_CUSTOM",0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80,
			 D3D11_INPUT_PER_INSTANCE_DATA, 1},
		};

		HRESULT hr = m_device->CreateInputLayout(
			layout, static_cast<UINT>(std::size(layout)),
			m_vsBytecode->GetBufferPointer(),
			m_vsBytecode->GetBufferSize(),
			m_inputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: CreateInputLayout failed");
		}
	}

	/// @brief ビュー射影定数バッファを作成する
	void createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(float) * 16;  ///< float4x4
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbViewProj.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"InstancedRenderer: constant buffer creation failed");
		}
	}

	ComPtr<ID3D11Device> m_device;               ///< D3D11デバイス
	std::uint32_t m_maxInstances = 10000;       ///< 最大インスタンス数
	bool m_initialized = false;                 ///< 初期化済みフラグ

	ComPtr<ID3D11Buffer> m_instanceBuffer;      ///< 動的インスタンスバッファ
	ComPtr<ID3D11Buffer> m_cbViewProj;          ///< ビュー射影定数バッファ
	ComPtr<ID3D11VertexShader> m_vertexShader;  ///< 頂点シェーダー
	ComPtr<ID3D11PixelShader> m_pixelShader;    ///< ピクセルシェーダー
	ComPtr<ID3D11InputLayout> m_inputLayout;    ///< 入力レイアウト
	ComPtr<ID3DBlob> m_vsBytecode;              ///< VSバイトコード（IL作成用）

	float m_viewProj[4][4] = {};                ///< ビュー射影行列

	/// バッチ描画状態
	ID3D11Buffer* m_currentMeshVB = nullptr;    ///< 現在のメッシュVB（非所有）
	ID3D11Buffer* m_currentMeshIB = nullptr;    ///< 現在のメッシュIB（非所有）
	std::uint32_t m_currentIndexCount = 0;      ///< 現在のインデックス数

	std::vector<InstanceData> m_instances;      ///< インスタンスキュー

	/// 統計
	int m_drawCallCount = 0;                    ///< ドローコール数
	int m_totalInstancesDrawn = 0;              ///< 描画インスタンス総数
};

} // namespace mitiru::render

#endif // _WIN32
