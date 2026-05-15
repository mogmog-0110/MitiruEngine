#pragma once

/// @file GpuSpriteBatch.hpp
/// @brief DX11 GPU加速2Dスプライトバッチ
/// @details テクスチャ付きクワッドをバッチ描画するGPUパイプライン。
///          動的頂点バッファ・テクスチャ切り替え時フラッシュ・
///          アルファブレンド・バイリニアフィルタリングをサポートする。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::render
{

// ─── HLSL Shaders ──────────────────────────────────────────

/// @brief テクスチャ対応2D頂点シェーダーのHLSLソース
constexpr std::string_view GPU_SPRITE_VS = R"hlsl(
cbuffer Constants : register(b0)
{
	float4x4 projection;
};

struct VSInput
{
	float2 position : POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(projection, float4(input.position, 0.0f, 1.0f));
	output.texCoord = input.texCoord;
	output.color = input.color;
	return output;
}
)hlsl";

/// @brief テクスチャサンプリング対応2DピクセルシェーダーのHLSLソース
constexpr std::string_view GPU_SPRITE_PS = R"hlsl(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	return tex.Sample(samp, input.texCoord) * input.color;
}
)hlsl";

// ─── Enums ─────────────────────────────────────────────────

/// @brief スプライトバッチ用ブレンドモード
enum class SpriteBlendMode : std::uint8_t
{
	AlphaBlend, ///< SrcAlpha / InvSrcAlpha
	Additive,   ///< SrcAlpha / One
	Multiply,   ///< DestColor / Zero
	None        ///< ブレンドなし
};

/// @brief テクスチャサンプリングモード
enum class SamplerMode : std::uint8_t
{
	Bilinear,    ///< バイリニアフィルタリング
	Point,       ///< ポイント（ニアレスト）フィルタリング
	Anisotropic  ///< 異方性フィルタリング
};

// ─── GpuTexture2D ──────────────────────────────────────────

/// @brief GPU側2Dテクスチャ
/// @details RGBA8ピクセルデータからID3D11Texture2D+SRVを生成する。
///          1x1白ピクセルのデフォルトテクスチャ生成もサポートする。
class GpuTexture2D
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ（空テクスチャ）
	GpuTexture2D() noexcept = default;

	/// @brief RGBAピクセルデータからテクスチャを生成する
	/// @param device D3D11デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param pixels RGBA8形式のピクセルデータ
	/// @return 生成されたテクスチャ
	[[nodiscard]] static GpuTexture2D createFromPixels(
		ID3D11Device* device,
		int width, int height,
		std::span<const std::uint8_t> pixels)
	{
		if (!device || width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"GpuTexture2D: invalid parameters");
		}

		const auto expectedSize =
			static_cast<std::size_t>(width) * height * 4;
		if (pixels.size() < expectedSize)
		{
			throw std::runtime_error(
				"GpuTexture2D: pixel data too small");
		}

		GpuTexture2D tex;
		tex.m_width = width;
		tex.m_height = height;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = pixels.data();
		initData.SysMemPitch = static_cast<UINT>(width) * 4;

		HRESULT hr = device->CreateTexture2D(
			&desc, &initData, tex.m_texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuTexture2D: CreateTexture2D failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(
			tex.m_texture.Get(), &srvDesc,
			tex.m_srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuTexture2D: CreateShaderResourceView failed");
		}

		return tex;
	}

	/// @brief render::Textureからテクスチャを生成する
	/// @param device D3D11デバイス
	/// @param texture ソーステクスチャ（RGBA8ピクセルバッファ）
	/// @return 生成されたテクスチャ
	template <typename TextureT>
	[[nodiscard]] static GpuTexture2D createFromTexture(
		ID3D11Device* device,
		const TextureT& texture)
	{
		return createFromPixels(
			device,
			texture.width(),
			texture.height(),
			std::span<const std::uint8_t>(
				texture.pixels().data(),
				texture.pixels().size()));
	}

	/// @brief 1x1白ピクセルのデフォルトテクスチャを生成する
	/// @param device D3D11デバイス
	/// @return 1x1白テクスチャ
	[[nodiscard]] static GpuTexture2D createWhitePixel(
		ID3D11Device* device)
	{
		constexpr std::array<std::uint8_t, 4> white = {
			255, 255, 255, 255
		};
		return createFromPixels(device, 1, 1,
			std::span<const std::uint8_t>(white.data(), white.size()));
	}

	/// @brief テクスチャ幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief テクスチャ高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief シェーダーリソースビューを取得する
	[[nodiscard]] ID3D11ShaderResourceView* getSRV() const noexcept
	{
		return m_srv.Get();
	}

	/// @brief テクスチャが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept
	{
		return m_srv.Get() != nullptr;
	}

private:
	ComPtr<ID3D11Texture2D> m_texture;
	ComPtr<ID3D11ShaderResourceView> m_srv;
	int m_width = 0;
	int m_height = 0;
};

// ─── GpuSpriteBatchVertex ──────────────────────────────────

/// @brief GPUスプライトバッチ用パック頂点
/// @details pos(float2) + uv(float2) + color(uint32 ABGR) の20バイト頂点。
///          ただしHLSLシェーダーとの互換性のためfloat4色を使用する。
struct GpuSpriteBatchVertex
{
	float x = 0.0f;      ///< スクリーン座標X
	float y = 0.0f;      ///< スクリーン座標Y
	float u = 0.0f;      ///< テクスチャ座標U
	float v = 0.0f;      ///< テクスチャ座標V
	float r = 1.0f;      ///< 色 赤
	float g = 1.0f;      ///< 色 緑
	float b = 1.0f;      ///< 色 青
	float a = 1.0f;      ///< 色 アルファ
};

// ─── GpuSpriteBatch ────────────────────────────────────────

/// @brief DX11 GPU加速2Dスプライトバッチ
/// @details テクスチャ付きクワッドをバッチ蓄積し、テクスチャ切り替え時または
///          end()呼び出し時にフラッシュしてGPU描画を行う。
///
/// @code
/// GpuSpriteBatch batch;
/// batch.init(device);
/// // フレームごと:
/// batch.begin(context, 1280.0f, 720.0f);
/// batch.draw(texSrv, srcRect, dstRect, color, 0.0f);
/// batch.drawRect(dstRect, color);
/// batch.end();
/// @endcode
class GpuSpriteBatch
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	GpuSpriteBatch() noexcept = default;

	/// @brief GPUリソースを初期化する
	/// @param device D3D11デバイス
	/// @param maxSprites 最大スプライト数（デフォルト4096）
	void init(ID3D11Device* device, int maxSprites = 4096)
	{
		if (!device || maxSprites <= 0)
		{
			throw std::runtime_error(
				"GpuSpriteBatch::init: invalid parameters");
		}

		m_device = device;
		m_maxSprites = maxSprites;

		compileShaders();
		createInputLayout();
		createVertexBuffer();
		createIndexBuffer();
		createConstantBuffer();
		createBlendStates();
		createSamplerStates();
		createRasterizerState();

		/// 1x1白ピクセルのデフォルトテクスチャを生成する
		m_whiteTexture = GpuTexture2D::createWhitePixel(device);

		m_initialized = true;
	}

	/// @brief バッチ蓄積を開始する
	/// @param context D3D11デバイスコンテキスト
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @param blendMode ブレンドモード（デフォルトAlphaBlend）
	/// @param samplerMode サンプリングモード（デフォルトBilinear）
	void begin(ID3D11DeviceContext* context,
	           float screenW, float screenH,
	           SpriteBlendMode blendMode = SpriteBlendMode::AlphaBlend,
	           SamplerMode samplerMode = SamplerMode::Bilinear)
	{
		if (!m_initialized || !context)
		{
			return;
		}

		m_context = context;
		m_screenWidth = screenW;
		m_screenHeight = screenH;
		m_currentBlendMode = blendMode;
		m_currentSamplerMode = samplerMode;
		m_vertices.clear();
		m_spriteCount = 0;
		m_flushCount = 0;
		m_currentTextureSrv = nullptr;
		m_recording = true;

		/// 正射影行列を更新する
		updateProjection(screenW, screenH);
	}

	/// @brief テクスチャ付きスプライトを描画する
	/// @param textureSrv テクスチャのSRV（nullptrでデフォルト白テクスチャ）
	/// @param srcX ソース矩形X（テクスチャピクセル座標）
	/// @param srcY ソース矩形Y
	/// @param srcW ソース矩形幅
	/// @param srcH ソース矩形高さ
	/// @param dstX 描画先矩形X（スクリーン座標）
	/// @param dstY 描画先矩形Y
	/// @param dstW 描画先矩形幅
	/// @param dstH 描画先矩形高さ
	/// @param r 乗算色 赤 [0,1]
	/// @param g 乗算色 緑 [0,1]
	/// @param b 乗算色 青 [0,1]
	/// @param a 乗算色 アルファ [0,1]
	/// @param rotation 回転角度（ラジアン）
	void draw(ID3D11ShaderResourceView* textureSrv,
	          float srcX, float srcY, float srcW, float srcH,
	          float dstX, float dstY, float dstW, float dstH,
	          float r, float g, float b, float a,
	          float rotation = 0.0f)
	{
		if (!m_recording)
		{
			return;
		}

		auto* srv = textureSrv ? textureSrv
		                       : m_whiteTexture.getSRV();

		/// テクスチャが変わったらフラッシュする
		if (m_currentTextureSrv && m_currentTextureSrv != srv)
		{
			flush();
		}
		m_currentTextureSrv = srv;

		/// バッファが満杯ならフラッシュする
		if (m_spriteCount >= m_maxSprites)
		{
			flush();
		}

		/// テクスチャサイズを取得してUV正規化する
		/// SRVからリソースを取得する
		float u0 = srcX;
		float v0 = srcY;
		float u1 = srcX + srcW;
		float v1 = srcY + srcH;

		/// テクスチャサイズで正規化する
		ComPtr<ID3D11Resource> resource;
		srv->GetResource(resource.GetAddressOf());
		ComPtr<ID3D11Texture2D> tex2d;
		resource.As(&tex2d);
		if (tex2d)
		{
			D3D11_TEXTURE2D_DESC texDesc = {};
			tex2d->GetDesc(&texDesc);
			const auto tw = static_cast<float>(texDesc.Width);
			const auto th = static_cast<float>(texDesc.Height);
			if (tw > 0.0f && th > 0.0f)
			{
				u0 /= tw;
				v0 /= th;
				u1 /= tw;
				v1 /= th;
			}
		}

		if (rotation == 0.0f)
		{
			pushQuad(dstX, dstY, dstW, dstH,
			         u0, v0, u1, v1,
			         r, g, b, a);
		}
		else
		{
			pushQuadRotated(dstX, dstY, dstW, dstH,
			                u0, v0, u1, v1,
			                r, g, b, a, rotation);
		}

		++m_spriteCount;
	}

	/// @brief 正規化UV座標でスプライトを描画する
	/// @param textureSrv テクスチャのSRV
	/// @param srcUV ソースUV矩形 {u0, v0, u1, v1} [0,1]
	/// @param dstX 描画先矩形X
	/// @param dstY 描画先矩形Y
	/// @param dstW 描画先矩形幅
	/// @param dstH 描画先矩形高さ
	/// @param r 乗算色 赤
	/// @param g 乗算色 緑
	/// @param b 乗算色 青
	/// @param a 乗算色 アルファ
	/// @param rotation 回転角度（ラジアン）
	void drawUV(ID3D11ShaderResourceView* textureSrv,
	            float u0, float v0, float u1, float v1,
	            float dstX, float dstY, float dstW, float dstH,
	            float r, float g, float b, float a,
	            float rotation = 0.0f)
	{
		if (!m_recording)
		{
			return;
		}

		auto* srv = textureSrv ? textureSrv
		                       : m_whiteTexture.getSRV();

		if (m_currentTextureSrv && m_currentTextureSrv != srv)
		{
			flush();
		}
		m_currentTextureSrv = srv;

		if (m_spriteCount >= m_maxSprites)
		{
			flush();
		}

		if (rotation == 0.0f)
		{
			pushQuad(dstX, dstY, dstW, dstH,
			         u0, v0, u1, v1,
			         r, g, b, a);
		}
		else
		{
			pushQuadRotated(dstX, dstY, dstW, dstH,
			                u0, v0, u1, v1,
			                r, g, b, a, rotation);
		}

		++m_spriteCount;
	}

	/// @brief 塗りつぶし矩形を描画する（デフォルト白テクスチャ使用）
	/// @param dstX 矩形X
	/// @param dstY 矩形Y
	/// @param dstW 矩形幅
	/// @param dstH 矩形高さ
	/// @param r 色 赤
	/// @param g 色 緑
	/// @param b 色 青
	/// @param a 色 アルファ
	void drawRect(float dstX, float dstY, float dstW, float dstH,
	              float r, float g, float b, float a)
	{
		if (!m_recording)
		{
			return;
		}

		auto* srv = m_whiteTexture.getSRV();

		if (m_currentTextureSrv && m_currentTextureSrv != srv)
		{
			flush();
		}
		m_currentTextureSrv = srv;

		if (m_spriteCount >= m_maxSprites)
		{
			flush();
		}

		pushQuad(dstX, dstY, dstW, dstH,
		         0.0f, 0.0f, 1.0f, 1.0f,
		         r, g, b, a);

		++m_spriteCount;
	}

	/// @brief バッチをフラッシュし蓄積を終了する
	void end()
	{
		if (!m_recording)
		{
			return;
		}

		flush();
		m_recording = false;
		m_context = nullptr;
	}

	/// @brief 初期化済みかどうかを判定する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 最後のbegin/end間のフラッシュ回数を取得する
	[[nodiscard]] int flushCount() const noexcept
	{
		return m_flushCount;
	}

	/// @brief デフォルト白テクスチャを取得する
	[[nodiscard]] const GpuTexture2D& whiteTexture() const noexcept
	{
		return m_whiteTexture;
	}

private:
	// ─── シェーダーコンパイル ───────────────────────────────

	/// @brief HLSL文字列をコンパイルする
	[[nodiscard]] ComPtr<ID3DBlob> compileHLSL(
		std::string_view source,
		const char* entryPoint,
		const char* target) const
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		HRESULT hr = D3DCompile(
			source.data(), source.size(),
			nullptr, nullptr, nullptr,
			entryPoint, target,
			flags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string msg = "GpuSpriteBatch: shader compile failed";
			if (errorBlob)
			{
				msg += ": ";
				msg += static_cast<const char*>(
					errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(msg);
		}

		return shaderBlob;
	}

	/// @brief 頂点・ピクセルシェーダーをコンパイルする
	void compileShaders()
	{
		m_vsByteCode = compileHLSL(GPU_SPRITE_VS, "VSMain", "vs_5_0");

		HRESULT hr = m_device->CreateVertexShader(
			m_vsByteCode->GetBufferPointer(),
			m_vsByteCode->GetBufferSize(),
			nullptr,
			m_vertexShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreateVertexShader failed");
		}

		auto psBlob = compileHLSL(GPU_SPRITE_PS, "PSMain", "ps_5_0");

		hr = m_device->CreatePixelShader(
			psBlob->GetBufferPointer(),
			psBlob->GetBufferSize(),
			nullptr,
			m_pixelShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreatePixelShader failed");
		}
	}

	// ─── リソース生成 ─────────────────────────────────────

	/// @brief GpuSpriteBatchVertex用の入力レイアウトを生成する
	void createInputLayout()
	{
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

		HRESULT hr = m_device->CreateInputLayout(
			layout,
			static_cast<UINT>(std::size(layout)),
			m_vsByteCode->GetBufferPointer(),
			m_vsByteCode->GetBufferSize(),
			m_inputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreateInputLayout failed");
		}
	}

	/// @brief 動的頂点バッファを生成する
	void createVertexBuffer()
	{
		const auto vertexCount =
			static_cast<std::uint32_t>(m_maxSprites) * 4;
		const auto sizeBytes =
			vertexCount * static_cast<std::uint32_t>(sizeof(GpuSpriteBatchVertex));

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeBytes;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_vertexBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreateBuffer (VB) failed");
		}
	}

	/// @brief 静的インデックスバッファを生成する
	/// @details クワッド描画用のインデックスパターンを事前に生成する。
	///          各クワッドは {0,1,2, 0,2,3} パターン。
	void createIndexBuffer()
	{
		const auto spriteCount =
			static_cast<std::uint32_t>(m_maxSprites);
		std::vector<std::uint32_t> indices(
			static_cast<std::size_t>(spriteCount) * 6);

		for (std::uint32_t i = 0; i < spriteCount; ++i)
		{
			const auto vi = i * 4;
			const auto ii = static_cast<std::size_t>(i) * 6;
			indices[ii + 0] = vi + 0;
			indices[ii + 1] = vi + 1;
			indices[ii + 2] = vi + 2;
			indices[ii + 3] = vi + 0;
			indices[ii + 4] = vi + 2;
			indices[ii + 5] = vi + 3;
		}

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = static_cast<UINT>(
			indices.size() * sizeof(std::uint32_t));
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = indices.data();

		HRESULT hr = m_device->CreateBuffer(
			&desc, &initData, m_indexBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreateBuffer (IB) failed");
		}
	}

	/// @brief 定数バッファ（正射影行列）を生成する
	void createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		/// float4x4 = 64バイト、16バイトアラインメント
		desc.ByteWidth = 64;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_constantBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuSpriteBatch: CreateBuffer (CB) failed");
		}
	}

	/// @brief 各ブレンドモード用のブレンドステートを生成する
	void createBlendStates()
	{
		/// AlphaBlend
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

			m_device->CreateBlendState(
				&desc, m_blendStates[0].GetAddressOf());
		}

		/// Additive
		{
			D3D11_BLEND_DESC desc = {};
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			auto& rt = desc.RenderTarget[0];
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D11_BLEND_ONE;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			m_device->CreateBlendState(
				&desc, m_blendStates[1].GetAddressOf());
		}

		/// Multiply
		{
			D3D11_BLEND_DESC desc = {};
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			auto& rt = desc.RenderTarget[0];
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
			rt.DestBlend = D3D11_BLEND_ZERO;
			rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
			rt.DestBlendAlpha = D3D11_BLEND_ZERO;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			m_device->CreateBlendState(
				&desc, m_blendStates[2].GetAddressOf());
		}

		/// None
		{
			D3D11_BLEND_DESC desc = {};
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			auto& rt = desc.RenderTarget[0];
			rt.BlendEnable = FALSE;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			m_device->CreateBlendState(
				&desc, m_blendStates[3].GetAddressOf());
		}
	}

	/// @brief 各サンプリングモード用のサンプラーステートを生成する
	void createSamplerStates()
	{
		/// Bilinear
		{
			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxAnisotropy = 1;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MaxLOD = D3D11_FLOAT32_MAX;

			m_device->CreateSamplerState(
				&desc, m_samplerStates[0].GetAddressOf());
		}

		/// Point
		{
			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxAnisotropy = 1;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MaxLOD = D3D11_FLOAT32_MAX;

			m_device->CreateSamplerState(
				&desc, m_samplerStates[1].GetAddressOf());
		}

		/// Anisotropic
		{
			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = D3D11_FILTER_ANISOTROPIC;
			desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxAnisotropy = 16;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MaxLOD = D3D11_FLOAT32_MAX;

			m_device->CreateSamplerState(
				&desc, m_samplerStates[2].GetAddressOf());
		}
	}

	/// @brief 2D描画用ラスタライザステートを生成する
	void createRasterizerState()
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthClipEnable = TRUE;
		desc.ScissorEnable = FALSE;

		m_device->CreateRasterizerState(
			&desc, m_rasterizerState.GetAddressOf());
	}

	// ─── 描画ヘルパー ─────────────────────────────────────

	/// @brief 正射影行列を定数バッファに書き込む
	void updateProjection(float width, float height)
	{
		if (!m_context || !m_constantBuffer)
		{
			return;
		}

		/// left=0, right=w, top=0, bottom=h, near=0, far=1
		/// 行優先float4x4
		float projection[4][4] = {};
		projection[0][0] = 2.0f / width;
		projection[1][1] = -2.0f / height;
		projection[2][2] = 1.0f;
		projection[3][0] = -1.0f;
		projection[3][1] = 1.0f;
		projection[3][2] = 0.0f;
		projection[3][3] = 1.0f;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_context->Map(
			m_constantBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, projection, sizeof(projection));
			m_context->Unmap(m_constantBuffer.Get(), 0);
		}
	}

	/// @brief 軸整列クワッドの4頂点を蓄積する
	void pushQuad(float dstX, float dstY, float dstW, float dstH,
	              float u0, float v0, float u1, float v1,
	              float r, float g, float b, float a)
	{
		const auto x0 = dstX;
		const auto y0 = dstY;
		const auto x1 = dstX + dstW;
		const auto y1 = dstY + dstH;

		m_vertices.push_back({x0, y0, u0, v0, r, g, b, a});
		m_vertices.push_back({x1, y0, u1, v0, r, g, b, a});
		m_vertices.push_back({x1, y1, u1, v1, r, g, b, a});
		m_vertices.push_back({x0, y1, u0, v1, r, g, b, a});
	}

	/// @brief 回転済みクワッドの4頂点を蓄積する
	void pushQuadRotated(float dstX, float dstY,
	                     float dstW, float dstH,
	                     float u0, float v0,
	                     float u1, float v1,
	                     float r, float g, float b, float a,
	                     float rotation)
	{
		const auto cx = dstX + dstW * 0.5f;
		const auto cy = dstY + dstH * 0.5f;
		const auto hw = dstW * 0.5f;
		const auto hh = dstH * 0.5f;
		const auto cosR = std::cos(rotation);
		const auto sinR = std::sin(rotation);

		/// 左上
		const auto x0 = cx + (-hw * cosR - (-hh) * sinR);
		const auto y0 = cy + (-hw * sinR + (-hh) * cosR);
		/// 右上
		const auto x1 = cx + (hw * cosR - (-hh) * sinR);
		const auto y1 = cy + (hw * sinR + (-hh) * cosR);
		/// 右下
		const auto x2 = cx + (hw * cosR - hh * sinR);
		const auto y2 = cy + (hw * sinR + hh * cosR);
		/// 左下
		const auto x3 = cx + (-hw * cosR - hh * sinR);
		const auto y3 = cy + (-hw * sinR + hh * cosR);

		m_vertices.push_back({x0, y0, u0, v0, r, g, b, a});
		m_vertices.push_back({x1, y1, u1, v0, r, g, b, a});
		m_vertices.push_back({x2, y2, u1, v1, r, g, b, a});
		m_vertices.push_back({x3, y3, u0, v1, r, g, b, a});
	}

	/// @brief 蓄積された頂点をGPUに送信して描画する
	void flush()
	{
		if (m_vertices.empty() || !m_context)
		{
			return;
		}

		/// 頂点バッファを更新する（MAP_WRITE_DISCARD）
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_context->Map(
			m_vertexBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			m_vertices.clear();
			return;
		}

		const auto dataSize = m_vertices.size() *
			sizeof(GpuSpriteBatchVertex);
		std::memcpy(mapped.pData, m_vertices.data(), dataSize);
		m_context->Unmap(m_vertexBuffer.Get(), 0);

		/// パイプラインステートを設定する
		m_context->IASetInputLayout(m_inputLayout.Get());
		m_context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// 頂点バッファをバインドする
		const UINT stride = sizeof(GpuSpriteBatchVertex);
		const UINT offset = 0;
		auto* vb = m_vertexBuffer.Get();
		m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

		/// インデックスバッファをバインドする
		m_context->IASetIndexBuffer(
			m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

		/// シェーダーを設定する
		m_context->VSSetShader(
			m_vertexShader.Get(), nullptr, 0);
		m_context->PSSetShader(
			m_pixelShader.Get(), nullptr, 0);

		/// 定数バッファを設定する
		auto* cb = m_constantBuffer.Get();
		m_context->VSSetConstantBuffers(0, 1, &cb);

		/// テクスチャとサンプラーを設定する
		if (m_currentTextureSrv)
		{
			m_context->PSSetShaderResources(
				0, 1, &m_currentTextureSrv);
		}

		const auto samplerIdx =
			static_cast<std::size_t>(m_currentSamplerMode);
		auto* sampler = m_samplerStates[samplerIdx].Get();
		m_context->PSSetSamplers(0, 1, &sampler);

		/// ブレンドステートを設定する
		const auto blendIdx =
			static_cast<std::size_t>(m_currentBlendMode);
		const float blendFactor[4] = {0, 0, 0, 0};
		m_context->OMSetBlendState(
			m_blendStates[blendIdx].Get(),
			blendFactor, 0xFFFFFFFF);

		/// ラスタライザステートを設定する
		m_context->RSSetState(m_rasterizerState.Get());

		/// ビューポートを設定する
		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = m_screenWidth;
		viewport.Height = m_screenHeight;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_context->RSSetViewports(1, &viewport);

		/// インデックス描画を実行する
		const auto spriteCount =
			static_cast<UINT>(m_vertices.size() / 4);
		m_context->DrawIndexed(spriteCount * 6, 0, 0);

		/// バッチをクリアする
		m_vertices.clear();
		m_spriteCount = 0;
		++m_flushCount;
	}

	// ─── メンバ ───────────────────────────────────────────

	/// デバイス・コンテキスト（非所有）
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;

	/// シェーダー
	ComPtr<ID3DBlob> m_vsByteCode;
	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11InputLayout> m_inputLayout;

	/// バッファ
	ComPtr<ID3D11Buffer> m_vertexBuffer;
	ComPtr<ID3D11Buffer> m_indexBuffer;
	ComPtr<ID3D11Buffer> m_constantBuffer;

	/// ステート
	std::array<ComPtr<ID3D11BlendState>, 4> m_blendStates;
	std::array<ComPtr<ID3D11SamplerState>, 3> m_samplerStates;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;

	/// テクスチャ
	GpuTexture2D m_whiteTexture;
	ID3D11ShaderResourceView* m_currentTextureSrv = nullptr;

	/// バッチデータ
	std::vector<GpuSpriteBatchVertex> m_vertices;

	/// パラメータ
	float m_screenWidth = 0.0f;
	float m_screenHeight = 0.0f;
	int m_maxSprites = 4096;
	int m_spriteCount = 0;
	int m_flushCount = 0;
	SpriteBlendMode m_currentBlendMode = SpriteBlendMode::AlphaBlend;
	SamplerMode m_currentSamplerMode = SamplerMode::Bilinear;

	/// 状態フラグ
	bool m_initialized = false;
	bool m_recording = false;
};

} // namespace mitiru::render

#endif // _WIN32
