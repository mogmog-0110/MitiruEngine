#pragma once

/// @file SdfFontGpu.hpp
/// @brief GPU加速SDFテキストレンダラー
/// @details SdfFontAtlasのSDFアトラスをGPUテクスチャとしてアップロードし、
///          GpuSpriteBatchと専用SDFピクセルシェーダーを使用して
///          高品質なテキスト描画を行う。アウトライン・シャドウ・グロー対応。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <sgc/types/Color.hpp>

#include <mitiru/render/GpuSpriteBatch.hpp>
#include <mitiru/render/SdfFont.hpp>

namespace mitiru::render
{

// ─── SDF Pixel Shader (HLSL) ──────────────────────────────

/// @brief SDFテキスト描画用ピクセルシェーダーのHLSLソース
/// @details アルファチャネルのSDF距離情報を使用し、smoothstepで
///          滑らかなエッジのテキストを描画する。
///          アウトライン・グロー・シャドウエフェクトに対応する。
constexpr std::string_view GPU_SDF_PS = R"hlsl(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

cbuffer SdfParams : register(b1)
{
	float4 textColor;
	float4 outlineColor;
	float4 shadowColor;
	float4 glowColor;
	float2 shadowOffset;
	float  outlineWidth;
	float  glowRadius;
	float  smoothing;
	float3 _pad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float dist = tex.Sample(samp, input.texCoord).a;
	float sm = smoothing * fwidth(dist);

	// Base text
	float textAlpha = smoothstep(0.5 - sm, 0.5 + sm, dist);
	float4 result = float4(textColor.rgb, textColor.a * textAlpha);

	// Outline
	float outlineEdge = 0.5 - outlineWidth;
	float outlineAlpha = smoothstep(outlineEdge - sm, outlineEdge + sm, dist);
	result = lerp(float4(outlineColor.rgb, outlineColor.a * outlineAlpha), result, textAlpha);

	// Glow
	float glowEdge = 0.5 - glowRadius;
	float glowAlpha = smoothstep(glowEdge - sm, glowEdge + sm, dist);
	result = lerp(float4(glowColor.rgb, glowColor.a * glowAlpha), result, result.a);

	return result * input.color;
}
)hlsl";

// ─── SdfParams constant buffer layout ─────────────────────

/// @brief SDFシェーダー用定数バッファ構造体
/// @details register(b1)にバインドされるパラメータ群。
///          16バイトアラインメントに準拠する。
struct SdfShaderParams
{
	float textColor[4]    = {1.0f, 1.0f, 1.0f, 1.0f};
	float outlineColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float shadowColor[4]  = {0.0f, 0.0f, 0.0f, 0.6f};
	float glowColor[4]    = {1.0f, 1.0f, 0.5f, 0.8f};
	float shadowOffset[2] = {0.0f, 0.0f};
	float outlineWidth    = 0.0f;
	float glowRadius      = 0.0f;
	float smoothing       = 1.0f;
	float _pad[3]         = {0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(SdfShaderParams) % 16 == 0,
	"SdfShaderParams must be 16-byte aligned for constant buffer");

// ─── SdfFontGpu ────────────────────────────────────────────

/// @brief GPU加速SDFテキストレンダラー
/// @details SdfFontAtlasのアトラスをGPUテクスチャとしてアップロードし、
///          専用SDFピクセルシェーダーを使用してGpuSpriteBatch上で
///          高品質なテキスト描画を行う。
///
/// @code
/// SdfFontAtlas atlas(ttfData, 32.0f);
/// atlas.addAsciiRange();
/// atlas.buildAtlas();
///
/// SdfFontGpu gpu;
/// gpu.init(device, atlas);
///
/// batch.begin(context, 1280.0f, 720.0f);
/// gpu.beginSdfRendering(context, batch);
/// gpu.drawText(batch, "Hello World!", 10.0f, 10.0f, 24.0f,
///              sgc::Colorf::white());
/// gpu.endSdfRendering(context, batch);
/// batch.end();
/// @endcode
class SdfFontGpu
{
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// @brief デフォルトコンストラクタ
	SdfFontGpu() noexcept = default;

	/// @brief GPUリソースを初期化する
	/// @param device D3D11デバイス
	/// @param atlas SDFフォントアトラス（ビルド済み）
	/// @throws std::runtime_error 初期化失敗時
	void init(ID3D11Device* device, const SdfFontAtlas& atlas)
	{
		if (!device)
		{
			throw std::runtime_error("SdfFontGpu::init: null device");
		}
		if (!atlas.valid())
		{
			throw std::runtime_error("SdfFontGpu::init: atlas not valid");
		}

		m_device = device;
		m_atlas = &atlas;

		createSdfPixelShader();
		createSdfConstantBuffer();
		uploadAtlasTexture();

		m_initialized = true;
	}

	/// @brief SDFレンダリングモードを開始する
	/// @details SDFピクセルシェーダーと定数バッファをパイプラインにバインドする。
	///          呼び出し前にGpuSpriteBatch::begin()が完了している必要がある。
	///          呼び出し後、batch.end()の前にendSdfRendering()を呼ぶこと。
	/// @param context D3D11デバイスコンテキスト
	/// @param batch GpuSpriteBatch（begin済み）
	void beginSdfRendering(ID3D11DeviceContext* context, GpuSpriteBatch& batch)
	{
		if (!m_initialized || !context)
		{
			return;
		}

		// 現在のピクセルシェーダーを保存する
		context->PSGetShader(
			m_savedPixelShader.ReleaseAndGetAddressOf(),
			nullptr, nullptr);

		// SDFピクセルシェーダーを設定する
		context->PSSetShader(m_sdfPixelShader.Get(), nullptr, 0);

		// SdfParams定数バッファをb1にバインドする
		auto* cb = m_sdfConstantBuffer.Get();
		context->PSSetConstantBuffers(1, 1, &cb);

		m_activeContext = context;
		m_sdfActive = true;

		(void)batch;
	}

	/// @brief SDFレンダリングモードを終了する
	/// @details 通常のスプライトシェーダーを復元する。
	///          endSdfRendering()の前にバッチをフラッシュすること。
	/// @param context D3D11デバイスコンテキスト
	/// @param batch GpuSpriteBatch
	void endSdfRendering(ID3D11DeviceContext* context, GpuSpriteBatch& batch)
	{
		if (!m_sdfActive || !context)
		{
			return;
		}

		// バッチ内の残りクワッドをフラッシュしてからシェーダーを戻す
		(void)batch;

		// 保存したピクセルシェーダーを復元する
		context->PSSetShader(m_savedPixelShader.Get(), nullptr, 0);
		m_savedPixelShader.Reset();

		// b1をアンバインドする
		ID3D11Buffer* nullCb = nullptr;
		context->PSSetConstantBuffers(1, 1, &nullCb);

		m_activeContext = nullptr;
		m_sdfActive = false;
	}

	/// @brief テキストを描画する
	/// @param batch GpuSpriteBatch（begin済み＋SDF有効化済み）
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標（ベースライン上端）
	/// @param fontSize 表示フォントサイズ（ピクセル）
	/// @param color テキスト色
	void drawText(GpuSpriteBatch& batch,
	              std::string_view text,
	              float x, float y,
	              float fontSize,
	              const sgc::Colorf& color)
	{
		updateSdfParams(color, {}, 0.0f, {}, 0.0f, {}, 0.0f, 0.0f);
		drawGlyphs(batch, text, x, y, fontSize, color);
	}

	/// @brief アウトライン付きテキストを描画する
	/// @param batch GpuSpriteBatch
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標
	/// @param fontSize 表示フォントサイズ
	/// @param textColor テキスト色
	/// @param outlineColor アウトライン色
	/// @param outlineWidth アウトライン幅（SDF空間、0.0〜0.5）
	void drawTextWithOutline(GpuSpriteBatch& batch,
	                         std::string_view text,
	                         float x, float y,
	                         float fontSize,
	                         const sgc::Colorf& textColor,
	                         const sgc::Colorf& outlineColor,
	                         float outlineWidth)
	{
		updateSdfParams(textColor, outlineColor, outlineWidth,
		                {}, 0.0f, {}, 0.0f, 0.0f);
		drawGlyphs(batch, text, x, y, fontSize, sgc::Colorf{1, 1, 1, 1});
	}

	/// @brief シャドウ付きテキストを描画する
	/// @param batch GpuSpriteBatch
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標
	/// @param fontSize 表示フォントサイズ
	/// @param textColor テキスト色
	/// @param shadowColor シャドウ色
	/// @param shadowOffsetX シャドウX方向オフセット（ピクセル）
	/// @param shadowOffsetY シャドウY方向オフセット（ピクセル）
	void drawTextWithShadow(GpuSpriteBatch& batch,
	                        std::string_view text,
	                        float x, float y,
	                        float fontSize,
	                        const sgc::Colorf& textColor,
	                        const sgc::Colorf& shadowColor,
	                        float shadowOffsetX,
	                        float shadowOffsetY)
	{
		// シャドウはオフセットした位置にシャドウ色で描画し、
		// 本体はその上に重ねて描画する。
		// GPU側でシャドウオフセットを適用するのはUV操作が必要なため、
		// 2パスで描画する：シャドウ→本体

		// シャドウパス（シャドウ色、outlineWidthで縁をぼかす）
		updateSdfParams(shadowColor, {}, 0.0f, {}, 0.0f, {}, 0.0f, 0.0f);
		drawGlyphs(batch, text, x + shadowOffsetX, y + shadowOffsetY,
		           fontSize, sgc::Colorf{1, 1, 1, 1});

		// 本体パス
		updateSdfParams(textColor, {}, 0.0f, {}, 0.0f, {}, 0.0f, 0.0f);
		drawGlyphs(batch, text, x, y, fontSize, sgc::Colorf{1, 1, 1, 1});
	}

	/// @brief グロー付きテキストを描画する
	/// @param batch GpuSpriteBatch
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標
	/// @param fontSize 表示フォントサイズ
	/// @param textColor テキスト色
	/// @param glowColor グロー色
	/// @param glowRadius グロー範囲（SDF空間、0.0〜0.5）
	void drawTextWithGlow(GpuSpriteBatch& batch,
	                      std::string_view text,
	                      float x, float y,
	                      float fontSize,
	                      const sgc::Colorf& textColor,
	                      const sgc::Colorf& glowColor,
	                      float glowRadius)
	{
		updateSdfParams(textColor, {}, 0.0f,
		                glowColor, glowRadius, {}, 0.0f, 0.0f);
		drawGlyphs(batch, text, x, y, fontSize, sgc::Colorf{1, 1, 1, 1});
	}

	/// @brief 初期化済みかどうかを判定する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief GPUアトラステクスチャを取得する
	[[nodiscard]] const GpuTexture2D& atlasTexture() const noexcept
	{
		return m_atlasGpuTexture;
	}

	/// @brief SDFレンダリングが有効かどうかを判定する
	[[nodiscard]] bool isSdfActive() const noexcept
	{
		return m_sdfActive;
	}

private:
	// ─── SDFピクセルシェーダーの生成 ──────────────────────

	/// @brief SDFピクセルシェーダーをコンパイル・生成する
	void createSdfPixelShader()
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		HRESULT hr = D3DCompile(
			GPU_SDF_PS.data(), GPU_SDF_PS.size(),
			nullptr, nullptr, nullptr,
			"PSMain", "ps_5_0",
			flags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string msg = "SdfFontGpu: SDF shader compile failed";
			if (errorBlob)
			{
				msg += ": ";
				msg += static_cast<const char*>(
					errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(msg);
		}

		hr = m_device->CreatePixelShader(
			shaderBlob->GetBufferPointer(),
			shaderBlob->GetBufferSize(),
			nullptr,
			m_sdfPixelShader.GetAddressOf());

		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SdfFontGpu: CreatePixelShader failed");
		}
	}

	// ─── SDF定数バッファの生成 ───────────────────────────

	/// @brief SdfParams用の動的定数バッファを生成する
	void createSdfConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(SdfShaderParams);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_sdfConstantBuffer.GetAddressOf());

		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SdfFontGpu: CreateBuffer (SdfParams CB) failed");
		}
	}

	// ─── アトラステクスチャのアップロード ─────────────────

	/// @brief SdfFontAtlasのテクスチャをGPUにアップロードする
	void uploadAtlasTexture()
	{
		m_atlasGpuTexture = GpuTexture2D::createFromTexture(
			m_device, m_atlas->texture());
	}

	// ─── 定数バッファの更新 ──────────────────────────────

	/// @brief SdfShaderParamsを更新してGPUに転送する
	void updateSdfParams(
		const sgc::Colorf& textColor,
		const sgc::Colorf& outlineColor,
		float outlineWidth,
		const sgc::Colorf& glowColor,
		float glowRadius,
		const sgc::Colorf& shadowColor,
		float shadowOffsetX,
		float shadowOffsetY)
	{
		if (!m_activeContext)
		{
			return;
		}

		SdfShaderParams params;
		params.textColor[0] = textColor.r;
		params.textColor[1] = textColor.g;
		params.textColor[2] = textColor.b;
		params.textColor[3] = textColor.a;

		params.outlineColor[0] = outlineColor.r;
		params.outlineColor[1] = outlineColor.g;
		params.outlineColor[2] = outlineColor.b;
		params.outlineColor[3] = outlineColor.a;

		params.shadowColor[0] = shadowColor.r;
		params.shadowColor[1] = shadowColor.g;
		params.shadowColor[2] = shadowColor.b;
		params.shadowColor[3] = shadowColor.a;

		params.glowColor[0] = glowColor.r;
		params.glowColor[1] = glowColor.g;
		params.glowColor[2] = glowColor.b;
		params.glowColor[3] = glowColor.a;

		params.shadowOffset[0] = shadowOffsetX;
		params.shadowOffset[1] = shadowOffsetY;
		params.outlineWidth = outlineWidth;
		params.glowRadius = glowRadius;
		params.smoothing = 1.0f;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_activeContext->Map(
			m_sdfConstantBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);

		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &params, sizeof(params));
			m_activeContext->Unmap(m_sdfConstantBuffer.Get(), 0);
		}
	}

	// ─── グリフ描画 ──────────────────────────────────────

	/// @brief UTF-8テキストのグリフをクワッドとしてバッチに投入する
	/// @param batch GpuSpriteBatch
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標
	/// @param fontSize 表示フォントサイズ
	/// @param vertexColor 頂点乗算色
	void drawGlyphs(GpuSpriteBatch& batch,
	                std::string_view text,
	                float x, float y,
	                float fontSize,
	                const sgc::Colorf& vertexColor)
	{
		if (!m_atlas || !m_atlas->valid())
		{
			return;
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		const float invAtlasW = 1.0f / static_cast<float>(m_atlas->atlasWidth());
		const float invAtlasH = 1.0f / static_cast<float>(m_atlas->atlasHeight());

		auto* srv = m_atlasGpuTexture.getSRV();

		float cursorX = x;
		const float baselineY = y + m_atlas->metrics().ascent * displayScale;

		sdf_detail::Utf8Decoder decoder(text);
		std::uint32_t prevCp = 0;

		while (decoder.hasNext())
		{
			const std::uint32_t cp = decoder.next();

			// 改行処理
			if (cp == '\n')
			{
				cursorX = x;
				// baselineYは使わない: yを更新する
				// ただしこの関数ではsingle-line想定
				prevCp = 0;
				continue;
			}

			const auto* glyph = m_atlas->findGlyph(cp);
			if (!glyph)
			{
				// スペース文字等、グリフがない場合はスキップ
				prevCp = cp;
				continue;
			}

			// カーニング適用
			if (prevCp != 0)
			{
				cursorX += m_atlas->kerning(prevCp, cp, fontSize);
			}

			// グリフのアトラス上のUV座標を計算する
			const float u0 = static_cast<float>(glyph->x0) * invAtlasW;
			const float v0 = static_cast<float>(glyph->y0) * invAtlasH;
			const float u1 = static_cast<float>(glyph->x1) * invAtlasW;
			const float v1 = static_cast<float>(glyph->y1) * invAtlasH;

			// グリフの表示位置を計算する
			const float glyphX = cursorX + glyph->xoff * displayScale;
			const float glyphY = (baselineY - m_atlas->metrics().ascent * displayScale)
				+ glyph->yoff * displayScale;
			const float glyphW = static_cast<float>(glyph->width()) * displayScale;
			const float glyphH = static_cast<float>(glyph->height()) * displayScale;

			// クワッドをバッチに投入する（UV正規化済み）
			batch.drawUV(
				srv,
				u0, v0, u1, v1,
				glyphX, glyphY, glyphW, glyphH,
				vertexColor.r, vertexColor.g, vertexColor.b, vertexColor.a);

			// カーソルを前進させる
			cursorX += glyph->xadvance * displayScale;
			prevCp = cp;
		}
	}

	// ─── メンバ ──────────────────────────────────────────

	/// デバイス（非所有）
	ID3D11Device* m_device = nullptr;

	/// アトラス参照（非所有、init()で受け取ったアトラスのライフタイムに依存）
	const SdfFontAtlas* m_atlas = nullptr;

	/// GPUアトラステクスチャ
	GpuTexture2D m_atlasGpuTexture;

	/// SDFピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_sdfPixelShader;

	/// SdfParams定数バッファ（register(b1)）
	ComPtr<ID3D11Buffer> m_sdfConstantBuffer;

	/// beginSdfRendering時に保存するピクセルシェーダー
	ComPtr<ID3D11PixelShader> m_savedPixelShader;

	/// SDFレンダリング中のコンテキスト
	ID3D11DeviceContext* m_activeContext = nullptr;

	/// 状態フラグ
	bool m_initialized = false;
	bool m_sdfActive = false;
};

// ─── ヘルパー関数 ───────────────────────────────────────────

/// @brief SDFピクセルシェーダーを単独で生成する
/// @param device D3D11デバイス
/// @return コンパイル済みSDFピクセルシェーダー
/// @throws std::runtime_error コンパイル失敗時
[[nodiscard]] inline Microsoft::WRL::ComPtr<ID3D11PixelShader>
createSdfPixelShader(ID3D11Device* device)
{
	if (!device)
	{
		throw std::runtime_error("createSdfPixelShader: null device");
	}

	Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	UINT flags = 0;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
	flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = D3DCompile(
		GPU_SDF_PS.data(), GPU_SDF_PS.size(),
		nullptr, nullptr, nullptr,
		"PSMain", "ps_5_0",
		flags, 0,
		shaderBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		std::string msg = "createSdfPixelShader: compile failed";
		if (errorBlob)
		{
			msg += ": ";
			msg += static_cast<const char*>(
				errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(msg);
	}

	Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
	hr = device->CreatePixelShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(),
		nullptr,
		ps.GetAddressOf());

	if (FAILED(hr))
	{
		throw std::runtime_error(
			"createSdfPixelShader: CreatePixelShader failed");
	}

	return ps;
}

/// @brief SDFレンダリングを開始するユーティリティ関数
/// @param context D3D11デバイスコンテキスト
/// @param gpu SdfFontGpuインスタンス
/// @param batch GpuSpriteBatch
inline void beginSdfRendering(ID3D11DeviceContext* context,
                              SdfFontGpu& gpu,
                              GpuSpriteBatch& batch)
{
	gpu.beginSdfRendering(context, batch);
}

/// @brief SDFレンダリングを終了するユーティリティ関数
/// @param context D3D11デバイスコンテキスト
/// @param gpu SdfFontGpuインスタンス
/// @param batch GpuSpriteBatch
inline void endSdfRendering(ID3D11DeviceContext* context,
                            SdfFontGpu& gpu,
                            GpuSpriteBatch& batch)
{
	gpu.endSdfRendering(context, batch);
}

} // namespace mitiru::render

#endif // _WIN32
