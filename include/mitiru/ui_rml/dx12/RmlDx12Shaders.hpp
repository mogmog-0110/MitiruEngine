#pragma once
// RmlUi 描画層のシェーダ。1 本の HLSL に全段の入口を並べ、compileDx12Shader で入口ごとに組む。
// 定数は全パイプラインで 1 つの cbuffer (RmlDrawConstants) を共有し、使わない欄は無視する。
// 色は全て乗算済みアルファで扱う (RmlUi がそう渡してくる)。

#include <cstdint>

namespace mitiru::ui_rml::dx12
{

/// HLSL の cbuffer Draw と同じ並び (float2 は 16 バイト境界をまたがない位置に置いてある)。
struct RmlDrawConstants
{
	float transform[16];
	float colorMatrix[16];
	float stopColors[16][4];
	float stopPositions[16];
	float color[4];
	float weights[4];
	float translate[2];
	float gradP[2];
	float gradV[2];
	float texelOffset[2];
	float texCoordMin[2];
	float texCoordMax[2];
	float uvOffset[2];
	float uvScale[2];
	std::int32_t func;
	std::int32_t numStops;
	float opacity;
	float pad;
};
static_assert(sizeof(RmlDrawConstants) == 560, "HLSL cbuffer Draw と大きさが合っていない");

inline constexpr const char* kRmlShaderSource = R"(
cbuffer Draw : register(b0)
{
	float4x4 uTransform;
	float4x4 uColorMatrix;
	float4 uStopColors[16];
	float4 uStopPositions[4];
	float4 uColor;
	float4 uWeights;
	float2 uTranslate;
	float2 uP;
	float2 uV;
	float2 uTexelOffset;
	float2 uTexCoordMin;
	float2 uTexCoordMax;
	float2 uUvOffset;
	float2 uUvScale;
	int uFunc;
	int uNumStops;
	float uOpacity;
	float uPad;
};

Texture2D uTex : register(t0);
Texture2D uMask : register(t1);
SamplerState sWrap : register(s0);
SamplerState sClamp : register(s1);

struct GeoIn { float2 pos : POSITION; float4 color : COLOR; float2 uv : TEXCOORD; };
struct GeoOut { float4 pos : SV_Position; float4 color : COLOR; float2 uv : TEXCOORD0; };
struct FullOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct BlurOut { float4 pos : SV_Position; float2 uv[7] : TEXCOORD0; };

GeoOut VSGeometry(GeoIn i)
{
	GeoOut o;
	o.pos = mul(uTransform, float4(i.pos + uTranslate, 0.0, 1.0));
	o.color = i.color;
	o.uv = i.uv;
	return o;
}

float4 PSColor(GeoOut i) : SV_Target { return i.color; }
float4 PSTexture(GeoOut i) : SV_Target { return i.color * uTex.Sample(sWrap, i.uv); }

float stopPosition(int k) { return uStopPositions[k / 4][k % 4]; }

float4 mixStops(float t)
{
	float4 c = uStopColors[0];
	for (int k = 1; k < uNumStops; ++k)
	{
		c = lerp(c, uStopColors[k], smoothstep(stopPosition(k - 1), stopPosition(k), t));
	}
	return c;
}

float glslMod(float x, float y) { return x - y * floor(x / y); }

float4 PSGradient(GeoOut i) : SV_Target
{
	float t = 0.0;
	int f = uFunc % 3;
	float2 d = i.uv - uP;
	if (f == 0) { t = dot(uV, d) / dot(uV, uV); }
	else if (f == 1) { t = length(uV * d); }
	else
	{
		float2 r = float2(uV.x * d.x + uV.y * d.y, -uV.y * d.x + uV.x * d.y);
		t = 0.5 + atan2(-r.x, r.y) / (2.0 * 3.14159265);
	}
	if (uFunc >= 3)
	{
		float t0 = stopPosition(0);
		float t1 = stopPosition(uNumStops - 1);
		t = t0 + glslMod(t - t0, t1 - t0);
	}
	return i.color * mixStops(t);
}

// 画面全体を覆う三角形 1 枚。頂点バッファを使わない。
FullOut VSFullscreen(uint id : SV_VertexID)
{
	FullOut o;
	float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
	o.pos = float4(p, 0.0, 1.0);
	o.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5) * uUvScale + uUvOffset;
	return o;
}

// 同じ大きさの描画先へ写すときは標本化せず画素をそのまま読む (補間で値が 1 でも動かないように)。
float4 PSCopy(FullOut i) : SV_Target { return uTex.Load(int3(i.pos.xy, 0)) * uOpacity; }
float4 PSSample(FullOut i) : SV_Target { return uTex.Sample(sClamp, i.uv); }

float4 PSColorMatrix(FullOut i) : SV_Target
{
	float4 c = uTex.Load(int3(i.pos.xy, 0));
	return float4(mul(uColorMatrix, c).rgb, c.a);
}

float4 PSBlendMask(FullOut i) : SV_Target
{
	int3 p = int3(i.pos.xy, 0);
	return uTex.Load(p) * uMask.Load(p).a;
}

float inRegion(float2 uv)
{
	float2 s = step(uTexCoordMin, uv) * step(uv, uTexCoordMax);
	return s.x * s.y;
}

float4 PSDropShadow(FullOut i) : SV_Target
{
	return uTex.Sample(sClamp, i.uv).a * inRegion(i.uv) * uColor;
}

BlurOut VSBlur(uint id : SV_VertexID)
{
	FullOut f = VSFullscreen(id);
	BlurOut o;
	o.pos = f.pos;
	[unroll] for (int k = 0; k < 7; ++k) { o.uv[k] = f.uv - float(k - 3) * uTexelOffset; }
	return o;
}

float4 PSBlur(BlurOut i) : SV_Target
{
	float4 c = 0.0;
	[unroll] for (int k = 0; k < 7; ++k)
	{
		c += uTex.Sample(sClamp, i.uv[k]) * inRegion(i.uv[k]) * uWeights[abs(k - 3)];
	}
	return c;
}
)";

} // namespace mitiru::ui_rml::dx12
