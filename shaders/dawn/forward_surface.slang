// 局所光・材質の画像・デカール (forward.slang から include される)

// ── 局所光 (DX12LitShaders.hpp の localLightAt / shadeLocal と同じ式)。DX12 は froxel のビット集合で絞る。ここは画面を 16 画素の格子 (既定。8 と 32 も選べる) に割り、
// 自分の格子に届く光だけを当てる (DawnLocalLights)。toonRamp が微分を使うので、範囲の外の灯も飛ばさず shape = 0 で通す (分岐の中で微分を取らない)。
// 格子を使うときのループの回数は 1 フレームで 1 つの値で、自分の格子の光の数を超えた分は何も足さない (shape = 0)。格子の外の光も shape = 0 なので、
// 足す光と順は全部を当てる形と同じで、絵は変わらない ──

float3 accumulateLocalLights(float3 worldPos, float3 albedo, float3 N, float3 V, bool phong, float2 pixel)
{
    uint count = uint(frame.lightInfo.x + 0.5);
    bool tiled = frame.lightTiles.x > 0.5;
    uint first = 0u;
    uint own = count;
    if (tiled)
    {
        int tileSize = int(frame.lightTiles.x + 0.5);
        uint head = tileTex.Load(int3(int2(pixel) / tileSize, 0)).x;
        own = head & 511u;
        first = head >> 9u;
        count = uint(frame.lightTiles.z + 0.5);
    }
    int listRow = int(frame.lightTiles.y + 0.5);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint k = 0u; k < count; ++k)
    {
        bool valid = k < own;
        int row = int(k);
        if (tiled)
        {
            uint at = first + min(k, max(own, 1u) - 1u);
            row = int(tileTex.Load(int3(int(at & 1023u), listRow + int(at >> 10u), 0)).x);
        }
        float4 t0 = localTex.Load(int3(0, row, 0));   // xyz = 位置 w = 届く距離
        float4 t1 = localTex.Load(int3(1, row, 0));   // rgb = 色 * 強さ w = スポットの傾き
        float4 t2 = localTex.Load(int3(2, row, 0));   // xyz = スポットの向き w = スポットの切片
        float3 d = t0.xyz - worldPos;
        float dist2 = dot(d, d);
        float r2 = t0.w * t0.w;
        float3 L = d * rsqrt(max(dist2, 1e-8));
        float win = saturate(1.0 - (dist2 * dist2) / (r2 * r2));
        float spot = saturate(dot(-L, t2.xyz) * t1.w + t2.w);
        float shape = (valid && dist2 < r2) ? win * win * spot * spot : 0.0;
        float ndl = saturate(dot(N, L));
        if (phong)
        {
            float spec = pow(saturate(dot(N, normalize(L + V))), max(draw.phong.w, 1.0)) * ndl;
            sum += (albedo * ndl + draw.phong.rgb * spec) * t1.rgb * (shape / (dist2 + 1.0));
        }
        else
        {
            float level = (frame.toonParams.x < 0.5) ? toonWrap(ndl) * shape : toonRamp(ndl * shape);
            sum += albedo * t1.rgb * level;
        }
    }
    return sum;
}

// ── 材質の画像 (DX12LitShaders.hpp の sampleAlbedo / perturbNormal / sampleEmissive と同じ式) ──

float4 baseTexel(float2 uv)
{
    if (draw.mapFlags.x < 0.5) { return float4(1.0, 1.0, 1.0, 1.0); }
    float4 t = (draw.mapParams.y > 0.5) ? matBase.Sample(matSamplerNearest, uv) : matBase.Sample(matSampler, uv);
    // 副ビューの出力は sRGB の値を 8bit の素の形式で持つ (互換レベルは同じ画像を 2 つの形式で読めない) ので、ここで線形へ戻す
    if (draw.mapParams.z > 0.5) { t.rgb = srgbToLinear(t.rgb); }
    return t;
}

// 接線を頂点に持たないので、画面上の位置と UV の微分から接空間を組む (Schuler の cotangent frame)。
// 画面の y は下向きなので T と B は u・v が増える向きの逆を指し、invmax の符号で戻す。glTF の法線マップの緑は v が減る向き
float3 perturbNormal(float3 N, float3 worldPos, float2 uv)
{
    if (draw.mapFlags.y < 0.5) { return N; }
    float2 xy = (matNormal.Sample(matSampler, uv).rg * 2.0 - 1.0) * draw.mapParams.x;
    xy.y = -xy.y;
    float3 tn = float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = -rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return normalize(T * (invmax * tn.x) + B * (invmax * tn.y) + N * tn.z);
}

float3 sampleEmissive(float2 uv)
{
    float3 e = draw.emissiveFactor.rgb;
    if (draw.mapFlags.z > 0.5) { e *= matEmissive.Sample(matSampler, uv).rgb; }
    return e;
}

// ── 投影デカール (DX12DecalShaders.hpp の applyDecals と同じ式)。DX12 は froxel のビット集合で絞るが、ここは全部を順に当てる ──

float decalEdge(float x, float soft)
{
    return (soft > 0.0) ? saturate((0.5 - abs(x)) / (0.5 * soft)) : 1.0;
}

// 層が無い時の形。中心 0、縁 0.5 の円を soft の幅でぼかす
float decalDisc(float2 p, float soft)
{
    float r = length(p) * 2.0;
    return (soft > 0.0) ? saturate((1.0 - r) / soft) : step(r, 1.0);
}

void applyDecals(float3 worldPos, float3 Ng, inout float3 albedo, inout float3 N)
{
    uint count = uint(frame.decalInfo.x + 0.5);
    // 微分は分岐の外で取り、面の位置の微分から uv の微分を作って mip を選ぶ
    float3 dpdx = ddx(worldPos);
    float3 dpdy = ddy(worldPos);
    for (uint i = 0u; i < count; ++i)
    {
        int row = int(i);
        float4 toDecal0 = decalTex.Load(int3(0, row, 0));
        float4 toDecal1 = decalTex.Load(int3(1, row, 0));
        float4 toDecal2 = decalTex.Load(int3(2, row, 0));
        float4 dcolor   = decalTex.Load(int3(3, row, 0));
        float4 axisZ    = decalTex.Load(int3(4, row, 0));
        float4 axisX    = decalTex.Load(int3(5, row, 0));
        float4 params   = decalTex.Load(int3(6, row, 0));
        float4 fade     = decalTex.Load(int3(7, row, 0));
        float4 wp = float4(worldPos, 1.0);
        float3 p = float3(dot(toDecal0, wp), dot(toDecal1, wp), dot(toDecal2, wp));
        if (any(abs(p) > float3(0.5, 0.5, 0.5))) { continue; }
        float facing = saturate((dot(Ng, axisZ.xyz) - fade.y) / max(fade.x - fade.y, 1e-4));
        float soft = axisZ.w;
        float a = dcolor.a * facing * decalEdge(p.z, soft);
        float3 uvw = float3(p.x + 0.5, 0.5 - p.y, params.x);
        float2 gx = float2(dot(toDecal0.xyz, dpdx), -dot(toDecal1.xyz, dpdx));
        float2 gy = float2(dot(toDecal0.xyz, dpdy), -dot(toDecal1.xyz, dpdy));
        float4 tex = float4(1.0, 1.0, 1.0, decalDisc(p.xy, soft));
        if (params.x >= 0.0) { tex = vfxColor.SampleGrad(vfxSampler, uvw, gx, gy); }
        a *= tex.a;
        if (a <= 0.0) { continue; }
        albedo = lerp(albedo, dcolor.rgb * tex.rgb, a);
        if (params.y >= 0.0)
        {
            float2 xy = vfxData.SampleGrad(vfxSampler, float3(uvw.xy, params.y), gx, gy).rg * 2.0 - 1.0;
            float3 T = axisX.xyz - N * dot(axisX.xyz, N);
            T = (dot(T, T) > 1e-8) ? normalize(T) : axisX.xyz;
            float3 B = cross(N, T);
            // 層の v は箱の -y へ進むので、緑は箱の +y (= B) を指す
            float3 nd = normalize(T * xy.x + B * xy.y + N * sqrt(saturate(1.0 - dot(xy, xy))));
            N = normalize(lerp(N, nd, a * axisX.w));
        }
    }
}

#ifdef MITIRU_USER_MATERIAL
// ── 自作の材質 (ADR 0100)。tools/bake_materials.py が MITIRU_USER_MATERIAL を定義し、利用者の material() と前置きを
// user_material.slang として差し込んで fsMain だけを WGSL にする。DX12 の DX12UserMaterialShaders.hpp の kGlue と同じ手順 ──

static bool g_userDiscard = false;

float4 mitiruUserVec(int i) { return draw.userParams[i]; }

#include "user_material.slang"

void applyUserSurface(VSOut i, float3 Ng, float3 V, inout float3 albedo, inout float3 N, inout float alpha, out float3 emissive)
{
    SurfaceIn si;
    si.uv = i.uv;
    si.worldPos = i.worldPos;
    si.normal = Ng;
    si.view = V;
    si.vertexColor = i.color;
    si.time = draw.userInfo.x;
    si.pixel = i.pos.xy;
    SurfaceOut so;
    so.baseColor = albedo;
    so.alpha = alpha;
    so.normal = N;
    so.emissive = float3(0.0, 0.0, 0.0);
    so.roughness = 0.5;
    so.metallic = 0.0;
    so.ao = 1.0;
    so.discardPixel = false;
    so.clearCoat = 0.0;
    so.clearCoatRoughness = 0.0;
    so.anisotropy = 0.0;
    so.anisotropyRotation = 0.0;
    so.sheenColor = float3(0.0, 0.0, 0.0);
    so.sheenRoughness = 0.0;
    material(si, so);
    g_userDiscard = so.discardPixel;
    albedo = so.baseColor;
    if (any(so.normal != N)) { N = normalize(so.normal); }
    alpha = so.alpha;
    emissive = so.emissive;
}
#endif

// 色と透明度。fsMain (下地も書く不透明) と fsMainColor (色だけの半透明) が共有する。N と V は下地の詰めに返す
float4 shadeScene(VSOut i, out float3 N, out float3 V)
{
    N = normalize(i.worldNorm);   // 輪郭線の下地へ返すのは面の法線
    float4 texel = baseTexel(i.uv);
    float3 Nl = perturbNormal(N, i.worldPos, i.uv);
    float3 albedo = i.diffuse.rgb * i.color.rgb * texel.rgb;
#ifdef MITIRU_USER_MATERIAL
    float userAlpha = i.diffuse.a * i.color.a * texel.a;
    float3 userEmissive;
    applyUserSurface(i, N, normalize(frame.cameraPos.xyz - i.worldPos), albedo, Nl, userAlpha, userEmissive);
#ifdef MITIRU_USER_UNLIT
    V = normalize(frame.cameraPos.xyz - i.worldPos);
    return float4(applyFog(albedo + userEmissive + sampleEmissive(i.uv) + draw.tintAdd.rgb, i.worldPos), userAlpha);
#endif
#endif
    applyDecals(i.worldPos, N, albedo, Nl);
    float3 L = normalize(-frame.lightDir.xyz);
    V = normalize(frame.cameraPos.xyz - i.worldPos);
    float castShadow = sampleCascadedShadow(i.worldPos, Nl, L, length(frame.cameraPos.xyz - i.worldPos));
    float alpha = i.diffuse.a * i.color.a * texel.a;
#ifdef MITIRU_USER_MATERIAL
    alpha = userAlpha;
#endif
    float3 lightColor = frame.lightColor.rgb;
    float3 ambient = hemisphereAmbient(Nl) * albedo * 0.30;
    if (frame.giOrigin.w > 0.5)
    {
        bool giOk;
        float3 e = giIrradiance(i.worldPos, Nl, V, giOk);
        float x = giToonCoord(e);
        float aa = fwidth(x);
        if (giOk) { ambient = giToonBandAt(e, x, aa) * (frame.giSpacing.w / kPi) * albedo; }
    }
    float3 color;

    if (draw.rampParams.x > 0.5)
    {
        float3 reflection;
        float3 styled = styledToonAlbedo(albedo, Nl, V, L, saturate(dot(Nl, L)), castShadow, reflection);
        float diffusePart = 1.0 - draw.metalA.x;
        color = styled * lightColor + reflection * (1.0 - lightColor) + ambient * diffusePart;
        color += accumulateLocalLights(i.worldPos, albedo, Nl, V, false, i.pos.xy) * diffusePart;
    }
    else
    {
        float lambert = saturate(dot(Nl, L)) * castShadow;
        float3 tone = toonTone(toonRamp(lambert));
        color = albedo * tone * lightColor + ambient;
        color += accumulateLocalLights(i.worldPos, albedo, Nl, V, false, i.pos.xy);
        if (frame.toonParams.z > 0.0)
        {
            float3 H = normalize(L + V);
            float spec = pow(saturate(dot(Nl, H)), max(frame.toonParams.w * draw.toonMaterial.w, 1.0));
            float half_ = toonEdgeHalfWidth(frame.toonParams.y * 0.5, fwidth(spec));
            float lit = smoothstep(0.5 - half_, 0.5 + half_, spec) * step(0.001, lambert);
            color += lightColor * draw.toonMaterial.rgb * frame.toonParams.z * lit;
        }
        color += frame.rimParams.rgb * pow(1.0 - saturate(dot(Nl, V)), frame.rimParams.w);
    }
    color += sampleEmissive(i.uv) + draw.tintAdd.rgb;
#ifdef MITIRU_USER_MATERIAL
    color += userEmissive;
#endif
    color = applyFog(color, i.worldPos);
    return float4(color, alpha);
}
