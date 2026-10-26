#pragma once

/// @file DefaultShaders2DLit.hpp
/// @brief 2D ライトを当てるピクセルシェーダー (DX12)
/// @details 光を使わない描画は DEFAULT_PS_2D のまま変えない。こちらは光が有効なフレームだけが使う別のピクセルシェーダーで、
///          DEFAULT_PS_2D の 3 通りの色の出し方 (頂点色・テクスチャ x 頂点色・距離場の文字) をそのまま残し、最後に光を掛ける。
///          式は render/Light2DMath.hpp の evaluate と tangentToScreen が正本。変えるときは両方を直す。

#include <string_view>

namespace mitiru::render
{

/// @brief 定数バッファ (b0、ピクセル側)
///        uMode  : x = 色の出し方 (0 頂点色、1 テクスチャ x 頂点色、2 距離場)、y = 距離場の幅、z = 法線マップあり、w = 1 なら point で読む
///        uMap   : 論理座標 = (SV_POSITION.xy - zw) * xy
///        uAmbient : rgb = 環境光 (色 x 強さ)、w = 灯の数
///        uLights: 1 灯 3 つの float4 (Light2DMath.hpp の PackedLight)
constexpr std::string_view LIT_PS_2D = R"hlsl(
Texture2D    tex0     : register(t0);
Texture2D    tex1     : register(t1);
SamplerState sLinear  : register(s0);
SamplerState sPoint   : register(s1);

cbuffer LitConstants : register(b0)
{
	float4 uMode;
	float4 uMap;
	float4 uAmbient;
	float4 uLights[48];
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	// 微分は分岐の前に取る (分岐の中では値が定まらない)
	const float2 duvdx = ddx(input.texCoord);
	const float2 duvdy = ddy(input.texCoord);
	const bool usePoint = uMode.w > 0.5f;

	float4 baseColor = input.color;
	if (uMode.x > 1.5f)
	{
		float4 s = tex0.Sample(sLinear, input.texCoord);
		float distance = max(min(s.r, s.g), min(max(s.r, s.g), s.b));
		float texW, texH;
		tex0.GetDimensions(texW, texH);
		float2 unitRange = uMode.y / float2(texW, texH);
		float2 screenTexSize = 1.0f / fwidth(input.texCoord);
		float screenRange = max(0.5f * dot(unitRange, screenTexSize), 1.0f);
		float opacity = saturate(screenRange * (distance - 0.5f) + 0.5f);
		baseColor = float4(input.color.rgb, input.color.a * opacity);
	}
	else if (uMode.x > 0.5f)
	{
		float4 texColor = usePoint ? tex0.Sample(sPoint, input.texCoord) : tex0.Sample(sLinear, input.texCoord);
		baseColor = texColor * input.color;
	}

	const bool hasNormal = uMode.z > 0.5f;
	float3 normal = float3(0.0f, 0.0f, 1.0f);
	if (hasNormal)
	{
		float3 t = (usePoint ? tex1.Sample(sPoint, input.texCoord) : tex1.Sample(sLinear, input.texCoord)).xyz * 2.0f - 1.0f;
		const float tl = length(t);
		t = tl < 1e-6f ? float3(0.0f, 0.0f, 1.0f) : t / tl;
		const float det = duvdx.x * duvdy.y - duvdy.x * duvdx.y;
		const float sgn = det < 0.0f ? -1.0f : 1.0f;
		float2 tDir = float2(duvdy.y, -duvdx.y);
		float2 bDir = float2(-duvdy.x, duvdx.x);
		const float tLen = length(tDir);
		const float bLen = length(bDir);
		if (tLen > 1e-12f && bLen > 1e-12f)
		{
			tDir = tDir / tLen * sgn;
			bDir = bDir / bLen * sgn;
			normal = float3(t.x * tDir + t.y * -bDir, t.z);
		}
	}

	const float2 p = (input.position.xy - uMap.zw) * uMap.xy;
	float3 total = uAmbient.rgb;
	const int count = (int)uAmbient.w;
	[loop]
	for (int i = 0; i < count; ++i)
	{
		const float4 a = uLights[i * 3 + 0];   // x, y, 高さ, 半径
		const float4 b = uLights[i * 3 + 1];   // 色 x 強さ, 減衰の指数
		const float4 c = uLights[i * 3 + 2];   // 円錐の向き, cos(外側), cos(内側)
		const float2 d = a.xy - p;
		const float dist = length(d);
		if (dist >= a.w) { continue; }
		float weight = pow(1.0f - dist / a.w, b.w);
		if (c.z > -1.5f)
		{
			float2 toPixel = c.xy;
			if (dist > 1e-5f) { toPixel = -d / dist; }
			weight *= smoothstep(c.z, c.w, dot(c.xy, toPixel));
		}
		if (hasNormal)
		{
			float3 l = float3(d, a.z);
			l = l / max(length(l), 1e-6f);
			weight *= max(dot(normal, l), 0.0f);
		}
		total += b.xyz * weight;
	}
	return float4(baseColor.rgb * total, baseColor.a);
}
)hlsl";

}  // namespace mitiru::render
