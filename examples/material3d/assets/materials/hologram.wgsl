// mitiru-material hologram source=96f9e98e36724975
struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct DrawCB_std140_0
{
    @align(16) world_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) diffuse_0 : vec4<f32>,
    @align(16) tintAdd_0 : vec4<f32>,
    @align(16) toonMaterial_0 : vec4<f32>,
    @align(16) rampParams_0 : vec4<f32>,
    @align(16) rampThresh_0 : vec4<f32>,
    @align(16) tint0_0 : vec4<f32>,
    @align(16) tint1_0 : vec4<f32>,
    @align(16) tint2_0 : vec4<f32>,
    @align(16) tint3_0 : vec4<f32>,
    @align(16) accRim_0 : vec4<f32>,
    @align(16) accGloss_0 : vec4<f32>,
    @align(16) accRimLimit_0 : vec4<f32>,
    @align(16) metalA_0 : vec4<f32>,
    @align(16) metalB_0 : vec4<f32>,
    @align(16) metalSky_0 : vec4<f32>,
    @align(16) metalHorizon_0 : vec4<f32>,
    @align(16) metalGround_0 : vec4<f32>,
    @align(16) misc_0 : vec4<f32>,
    @align(16) phong_0 : vec4<f32>,
    @align(16) mapFlags_0 : vec4<f32>,
    @align(16) mapParams_0 : vec4<f32>,
    @align(16) emissiveFactor_0 : vec4<f32>,
    @align(16) userParams_0 : array<vec4<f32>, i32(4)>,
    @align(16) userInfo_0 : vec4<f32>,
};

@binding(1) @group(0) var<uniform> draw_0 : DrawCB_std140_0;
@binding(1) @group(1) var matBase_0 : texture_2d<f32>;

@binding(5) @group(1) var matSamplerNearest_0 : sampler;

@binding(4) @group(1) var matSampler_0 : sampler;

@binding(2) @group(1) var matNormal_0 : texture_2d<f32>;

struct FrameCB_std140_0
{
    @align(16) viewProj_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) cameraPos_0 : vec4<f32>,
    @align(16) lightDir_0 : vec4<f32>,
    @align(16) lightColor_0 : vec4<f32>,
    @align(16) ambientSky_0 : vec4<f32>,
    @align(16) ambientGround_0 : vec4<f32>,
    @align(16) shadowTint_0 : vec4<f32>,
    @align(16) toonMidTint_0 : vec4<f32>,
    @align(16) toonParams_0 : vec4<f32>,
    @align(16) rimParams_0 : vec4<f32>,
    @align(16) fogColor_0 : vec4<f32>,
    @align(16) fogParams_0 : vec4<f32>,
    @align(16) skyZenith_0 : vec4<f32>,
    @align(16) skyNadir_0 : vec4<f32>,
    @align(16) camRight_0 : vec4<f32>,
    @align(16) camUp_0 : vec4<f32>,
    @align(16) camForward_0 : vec4<f32>,
    @align(16) shadowVP0_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) shadowVP1_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) shadowVP2_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) shadowC0_0 : vec4<f32>,
    @align(16) shadowC1_0 : vec4<f32>,
    @align(16) shadowC2_0 : vec4<f32>,
    @align(16) shadowSplit_0 : vec4<f32>,
    @align(16) shadowTexel_0 : vec4<f32>,
    @align(16) viewInfo_0 : vec4<f32>,
    @align(16) giOrigin_0 : vec4<f32>,
    @align(16) giSpacing_0 : vec4<f32>,
    @align(16) giDims_0 : vec4<f32>,
    @align(16) giParams_0 : vec4<f32>,
    @align(16) fogA_0 : vec4<f32>,
    @align(16) fogB_0 : vec4<f32>,
    @align(16) fogC_0 : vec4<f32>,
    @align(16) fogD_0 : vec4<f32>,
    @align(16) ambientFlat_0 : vec4<f32>,
    @align(16) decalInfo_0 : vec4<f32>,
    @align(16) lightInfo_0 : vec4<f32>,
    @align(16) fadeCamera_0 : vec4<f32>,
    @align(16) fadeParams_0 : vec4<f32>,
    @align(16) fadeTargets_0 : array<vec4<f32>, i32(4)>,
    @align(16) fadeCones_0 : array<vec4<f32>, i32(4)>,
    @align(16) fadeSilColor_0 : vec4<f32>,
    @align(16) fadeSilParams_0 : vec4<f32>,
    @align(16) lightTiles_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> frame_0 : FrameCB_std140_0;
@binding(3) @group(1) var matEmissive_0 : texture_2d<f32>;

var<private> kOccluderBayer_0 : array<f32, i32(16)> = array<f32, i32(16)>( 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f );
fn rsqrt_0( x_0 : f32) -> f32
{
    return 1.0f / sqrt(x_0);
}

fn srgbToLinear_0( c_0 : vec3<f32>) -> vec3<f32>
{
    var _S1 : vec3<f32> = vec3<f32>(max(max(c_0.x, c_0.y), max(c_0.z, 1.0f)));
    var _S2 : vec3<f32> = c_0 / _S1;
    var lo_0 : vec3<f32> = _S2 / vec3<f32>(12.920000076293945f);
    var hi_0 : vec3<f32> = pow((max(_S2, vec3<f32>(0.040449999272823334f, 0.040449999272823334f, 0.040449999272823334f)) + vec3<f32>(0.054999999701976776f)) / vec3<f32>(1.0549999475479126f), vec3<f32>(2.4000000953674316f, 2.4000000953674316f, 2.4000000953674316f));
    var _S3 : f32;
    if(((_S2.x) <= 0.040449999272823334f))
    {
        _S3 = lo_0.x;
    }
    else
    {
        _S3 = hi_0.x;
    }
    var _S4 : f32;
    if(((_S2.y) <= 0.040449999272823334f))
    {
        _S4 = lo_0.y;
    }
    else
    {
        _S4 = hi_0.y;
    }
    var _S5 : f32;
    if(((_S2.z) <= 0.040449999272823334f))
    {
        _S5 = lo_0.z;
    }
    else
    {
        _S5 = hi_0.z;
    }
    return vec3<f32>(_S3, _S4, _S5) * _S1;
}

fn baseTexel_0( uv_0 : vec2<f32>) -> vec4<f32>
{
    if(((draw_0.mapFlags_0.x) < 0.5f))
    {
        return vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    }
    var t_0 : vec4<f32>;
    var _S6 : vec4<f32>;
    if(((draw_0.mapParams_0.y) > 0.5f))
    {
        _S6 = (textureSample((matBase_0), (matSamplerNearest_0), (uv_0)));
    }
    else
    {
        _S6 = (textureSample((matBase_0), (matSampler_0), (uv_0)));
    }
    t_0 = _S6;
    if(((draw_0.mapParams_0.z) > 0.5f))
    {
        var _S7 : vec3<f32> = srgbToLinear_0(t_0.xyz);
        t_0.x = _S7.x;
        t_0.y = _S7.y;
        t_0.z = _S7.z;
    }
    return t_0;
}

fn perturbNormal_0( N_0 : vec3<f32>,  worldPos_0 : vec3<f32>,  uv_1 : vec2<f32>) -> vec3<f32>
{
    if(((draw_0.mapFlags_0.y) < 0.5f))
    {
        return N_0;
    }
    var _S8 : vec2<f32> = ((textureSample((matNormal_0), (matSampler_0), (uv_1))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f)) * vec2<f32>(draw_0.mapParams_0.x);
    var xy_0 : vec2<f32> = _S8;
    xy_0[i32(1)] = - _S8.y;
    var tn_0 : vec3<f32> = vec3<f32>(xy_0, sqrt(saturate(1.0f - dot(xy_0, xy_0))));
    var duv1_0 : vec2<f32> = dpdx(uv_1);
    var duv2_0 : vec2<f32> = dpdy(uv_1);
    var dp2perp_0 : vec3<f32> = cross(dpdy(worldPos_0), N_0);
    var dp1perp_0 : vec3<f32> = cross(N_0, dpdx(worldPos_0));
    var T_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.x) + dp1perp_0 * vec3<f32>(duv2_0.x);
    var B_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.y) + dp1perp_0 * vec3<f32>(duv2_0.y);
    var invmax_0 : f32 = - rsqrt_0(max(max(dot(T_0, T_0), dot(B_0, B_0)), 9.99999968265522539e-21f));
    return normalize(T_0 * vec3<f32>((invmax_0 * tn_0.x)) + B_0 * vec3<f32>((invmax_0 * tn_0.y)) + N_0 * vec3<f32>(tn_0.z));
}

fn mitiruUserVec_0( i_0 : i32) -> vec4<f32>
{
    return draw_0.userParams_0[i_0];
}

struct SurfaceIn_0
{
     uv_2 : vec2<f32>,
     worldPos_1 : vec3<f32>,
     normal_0 : vec3<f32>,
     view_0 : vec3<f32>,
     vertexColor_0 : vec4<f32>,
     time_0 : f32,
     pixel_0 : vec2<f32>,
};

struct SurfaceOut_0
{
     baseColor_0 : vec3<f32>,
     alpha_0 : f32,
     normal_1 : vec3<f32>,
     emissive_0 : vec3<f32>,
     roughness_0 : f32,
     metallic_0 : f32,
     ao_0 : f32,
     discardPixel_0 : bool,
     clearCoat_0 : f32,
     clearCoatRoughness_0 : f32,
     anisotropy_0 : f32,
     anisotropyRotation_0 : f32,
     sheenColor_0 : vec3<f32>,
     sheenRoughness_0 : f32,
};

fn material_0( i_1 : SurfaceIn_0,  o_0 : ptr<function, SurfaceOut_0>)
{
    var _S9 : vec4<f32> = mitiruUserVec_0(i32(0));
    if(((fract(i_1.pixel_0.y * 0.25f - i_1.time_0 * _S9.x)) < 0.40000000596046448f))
    {
        (*o_0).discardPixel_0 = true;
    }
    (*o_0).baseColor_0 = vec3<f32>(0.10000000149011612f, 0.80000001192092896f, 1.0f) * vec3<f32>((0.30000001192092896f + pow(1.0f - saturate(dot(i_1.normal_0, i_1.view_0)), 2.0f)));
    (*o_0).emissive_0 = vec3<f32>(0.0f, 0.25f, 0.40000000596046448f) * vec3<f32>(_S9.y);
    return;
}

var<private> g_userDiscard_0 : bool;

struct VSOut_0
{
     pos_0 : vec4<f32>,
     worldPos_2 : vec3<f32>,
     worldNorm_0 : vec3<f32>,
     color_0 : vec4<f32>,
     viewDepth_0 : f32,
     uv_3 : vec2<f32>,
    diffuse_1 : vec4<f32>,
};

fn applyUserSurface_0( i_2 : VSOut_0,  Ng_0 : vec3<f32>,  V_0 : vec3<f32>,  albedo_0 : ptr<function, vec3<f32>>,  N_1 : ptr<function, vec3<f32>>,  alpha_1 : ptr<function, f32>,  emissive_1 : ptr<function, vec3<f32>>)
{
    var si_0 : SurfaceIn_0;
    si_0.uv_2 = i_2.uv_3;
    si_0.worldPos_1 = i_2.worldPos_2;
    si_0.normal_0 = Ng_0;
    si_0.view_0 = V_0;
    si_0.vertexColor_0 = i_2.color_0;
    si_0.time_0 = draw_0.userInfo_0.x;
    si_0.pixel_0 = i_2.pos_0.xy;
    var so_0 : SurfaceOut_0;
    so_0.baseColor_0 = (*albedo_0);
    so_0.alpha_0 = (*alpha_1);
    so_0.normal_1 = (*N_1);
    const _S10 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    so_0.emissive_0 = _S10;
    so_0.roughness_0 = 0.5f;
    so_0.metallic_0 = 0.0f;
    so_0.ao_0 = 1.0f;
    so_0.discardPixel_0 = false;
    so_0.clearCoat_0 = 0.0f;
    so_0.clearCoatRoughness_0 = 0.0f;
    so_0.anisotropy_0 = 0.0f;
    so_0.anisotropyRotation_0 = 0.0f;
    so_0.sheenColor_0 = _S10;
    so_0.sheenRoughness_0 = 0.0f;
    material_0(si_0, &(so_0));
    g_userDiscard_0 = so_0.discardPixel_0;
    (*albedo_0) = so_0.baseColor_0;
    if((any(((so_0.normal_1) != (*N_1)))))
    {
        (*N_1) = normalize(so_0.normal_1);
    }
    (*alpha_1) = so_0.alpha_0;
    (*emissive_1) = so_0.emissive_0;
    return;
}

fn sampleEmissive_0( uv_4 : vec2<f32>) -> vec3<f32>
{
    var e_0 : vec3<f32> = draw_0.emissiveFactor_0.xyz;
    var e_1 : vec3<f32>;
    if(((draw_0.mapFlags_0.z) > 0.5f))
    {
        e_1 = e_0 * (textureSample((matEmissive_0), (matSampler_0), (uv_4))).xyz;
    }
    else
    {
        e_1 = e_0;
    }
    return e_1;
}

fn applyFog_0( color_1 : vec3<f32>,  worldPos_3 : vec3<f32>) -> vec3<f32>
{
    if(((frame_0.fogParams_0.z) < 0.5f))
    {
        return color_1;
    }
    return mix(color_1, frame_0.fogColor_0.xyz, vec3<f32>(saturate((length(frame_0.cameraPos_0.xyz - worldPos_3) - frame_0.fogParams_0.x) / max(frame_0.fogParams_0.y - frame_0.fogParams_0.x, 0.0010000000474974513f))));
}

fn shadeScene_0( i_3 : VSOut_0,  N_2 : ptr<function, vec3<f32>>,  V_1 : ptr<function, vec3<f32>>) -> vec4<f32>
{
    var _S11 : vec3<f32> = normalize(i_3.worldNorm_0);
    (*N_2) = _S11;
    var texel_0 : vec4<f32> = baseTexel_0(i_3.uv_3);
    var Nl_0 : vec3<f32> = perturbNormal_0(_S11, i_3.worldPos_2, i_3.uv_3);
    var albedo_1 : vec3<f32> = i_3.diffuse_1.xyz * i_3.color_0.xyz * texel_0.xyz;
    var userAlpha_0 : f32 = i_3.diffuse_1.w * i_3.color_0.w * texel_0.w;
    var userEmissive_0 : vec3<f32>;
    applyUserSurface_0(i_3, _S11, normalize(frame_0.cameraPos_0.xyz - i_3.worldPos_2), &(albedo_1), &(Nl_0), &(userAlpha_0), &(userEmissive_0));
    (*V_1) = normalize(frame_0.cameraPos_0.xyz - i_3.worldPos_2);
    return vec4<f32>(applyFog_0(albedo_1 + userEmissive_0 + sampleEmissive_0(i_3.uv_3) + draw_0.tintAdd_0.xyz, i_3.worldPos_2), userAlpha_0);
}

fn occluderFadeAmount_0( W_0 : vec3<f32>,  N_3 : vec3<f32>) -> f32
{
    var _S12 : u32 = u32(frame_0.fadeCamera_0.w);
    var v_0 : vec3<f32> = W_0 - frame_0.fadeCamera_0.xyz;
    var _S13 : vec3<f32>;
    if(((dot(N_3, v_0)) > 0.0f))
    {
        _S13 = (vec3<f32>(0) - N_3);
    }
    else
    {
        _S13 = N_3;
    }
    var amount_0 : f32 = 0.0f;
    var i_4 : u32 = u32(0);
    loop
    {
        if((i_4 < u32(4)))
        {
        }
        else
        {
            break;
        }
        if((i_4 >= _S12))
        {
            break;
        }
        var T_1 : vec3<f32> = frame_0.fadeTargets_0[i_4].xyz;
        var r_0 : f32 = frame_0.fadeTargets_0[i_4].w;
        var axis_0 : vec3<f32> = frame_0.fadeCones_0[i_4].xyz;
        var s_0 : f32 = dot(v_0, axis_0);
        var _S14 : f32 = max(amount_0, (1.0f - smoothstep(1.0f - frame_0.fadeParams_0.y, 1.0f, length(v_0 - axis_0 * vec3<f32>(s_0)) / max(s_0 * frame_0.fadeCones_0[i_4].w, 9.9999997473787516e-06f))) * saturate((dot(T_1 - frame_0.fadeCamera_0.xyz, axis_0) - s_0) / r_0) * smoothstep(0.25f * r_0, -0.25f * r_0, dot(T_1 - W_0, _S13)));
        var i_5 : u32 = i_4 + u32(1);
        amount_0 = _S14;
        i_4 = i_5;
    }
    return amount_0 * frame_0.fadeParams_0.x;
}

fn occluderFaded_0( pixel_1 : vec2<f32>,  W_1 : vec3<f32>,  N_4 : vec3<f32>) -> bool
{
    if(((frame_0.fadeCamera_0.w) < 0.5f))
    {
        return false;
    }
    var p_0 : vec2<u32> = (vec2<u32>(pixel_1) & (vec2<u32>(u32(3), u32(3))));
    return ((occluderFadeAmount_0(W_1, N_4 * vec3<f32>(rsqrt_0(max(dot(N_4, N_4), 9.99999968265522539e-21f))))) > (fract((kOccluderBayer_0[p_0.y * u32(4) + p_0.x] + 0.5f) / 16.0f + frame_0.fadeParams_0.z)));
}

fn neutralShoulder_0( c_1 : vec3<f32>) -> vec3<f32>
{
    var _S15 : f32 = max(c_1.x, max(c_1.y, c_1.z));
    if((_S15 <= 0.89999997615814209f))
    {
        return c_1;
    }
    return c_1 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S15 + 0.10000002384185791f - 0.89999997615814209f)) / _S15));
}

fn storeHdr_0( c_2 : vec3<f32>,  scale_0 : f32,  pixel_2 : vec2<f32>) -> vec3<f32>
{
    if((scale_0 >= 1.0f))
    {
        return c_2;
    }
    const _S16 : vec2<f32> = vec2<f32>(0.067110560834407806f, 0.0058371499180793762f);
    var n0_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_2, _S16)));
    var n1_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_2 + vec2<f32>(17.0f, 31.0f), _S16)));
    return neutralShoulder_0(c_2 * vec3<f32>(scale_0)) + vec3<f32>(n0_0 + n1_0 - 1.0f, n1_0 + n0_0 - 1.0f, n0_0 - n1_0) * vec3<f32>(0.00097751710563898087f);
}

fn packSlotNdv_0( slot_0 : f32,  ndv_0 : f32) -> f32
{
    return (slot_0 * 32.0f + round(saturate(ndv_0) * 31.0f)) / 255.0f;
}

fn packLinearDepth_0( viewDepth_1 : f32) -> vec4<f32>
{
    var v_1 : u32 = u32(saturate(viewDepth_1 / max(frame_0.viewInfo_0.x, 0.0010000000474974513f)) * 1.6777215e+07f + 0.5f);
    return vec4<f32>(f32((((v_1 >> (u32(16)))) & (u32(255)))), f32((((v_1 >> (u32(8)))) & (u32(255)))), f32((v_1 & (u32(255)))), 255.0f) / vec4<f32>(255.0f);
}

struct FsOut_0
{
    @location(0) color_2 : vec4<f32>,
    @location(1) normal_2 : vec4<f32>,
    @location(2) depth_0 : vec4<f32>,
};

fn packOutput_0( color_3 : vec3<f32>,  alpha_2 : f32,  Ng_1 : vec3<f32>,  V_2 : vec3<f32>,  viewDepth_2 : f32,  pixel_3 : vec2<f32>) -> FsOut_0
{
    var o_1 : FsOut_0;
    o_1.color_2 = vec4<f32>(storeHdr_0(color_3, frame_0.lightInfo_0.y, pixel_3), alpha_2);
    var _S17 : vec3<f32> = vec3<f32>(0.5f);
    o_1.normal_2 = vec4<f32>(Ng_1 * _S17 + _S17, packSlotNdv_0(draw_0.rampParams_0.w, dot(Ng_1, V_2)));
    if(((draw_0.misc_0.x) > 0.5f))
    {
        const _S18 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        o_1.normal_2.x = _S18.x;
        o_1.normal_2.y = _S18.y;
        o_1.normal_2.z = _S18.z;
    }
    o_1.depth_0 = packLinearDepth_0(viewDepth_2);
    return o_1;
}

struct pixelInput_0
{
    @location(0) worldPos_4 : vec3<f32>,
    @location(1) worldNorm_1 : vec3<f32>,
    @location(2) color_4 : vec4<f32>,
    @location(3) viewDepth_3 : f32,
    @location(4) uv_5 : vec2<f32>,
    @interpolate(flat, either) @location(5) diffuse_2 : vec4<f32>,
};

@fragment
fn fsMain( _S19 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> FsOut_0
{
    var _S20 : VSOut_0 = VSOut_0( pos_1, _S19.worldPos_4, _S19.worldNorm_1, _S19.color_4, _S19.viewDepth_3, _S19.uv_5, _S19.diffuse_2 );
    g_userDiscard_0 = false;
    var N_5 : vec3<f32>;
    var V_3 : vec3<f32>;
    var c_3 : vec4<f32> = shadeScene_0(_S20, &(N_5), &(V_3));
    var _S21 : bool;
    if(((draw_0.misc_0.y) < 0.5f))
    {
        _S21 = occluderFaded_0(pos_1.xy, _S19.worldPos_4, _S19.worldNorm_1);
    }
    else
    {
        _S21 = false;
    }
    if(_S21)
    {
        discard;
    }
    if(((draw_0.mapFlags_0.w) > 0.0f))
    {
        _S21 = ((baseTexel_0(_S19.uv_5).w) < (draw_0.mapFlags_0.w));
    }
    else
    {
        _S21 = false;
    }
    if(_S21)
    {
        discard;
    }
    if(g_userDiscard_0)
    {
        discard;
    }
    return packOutput_0(c_3.xyz, c_3.w, N_5, V_3, _S19.viewDepth_3, pos_1.xy);
}

