// mitiru-material water source=ec0c6222b3323332
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
@binding(8) @group(0) var decalTex_0 : texture_2d<f32>;

@binding(9) @group(0) var vfxColor_0 : texture_2d_array<f32>;

@binding(11) @group(0) var vfxSampler_0 : sampler;

@binding(10) @group(0) var vfxData_0 : texture_2d_array<f32>;

@binding(2) @group(0) var shadowNear_0 : texture_depth_2d;

@binding(4) @group(0) var shadowPcf_0 : sampler_comparison;

@binding(3) @group(0) var shadowFar_0 : texture_depth_2d;

@binding(5) @group(0) var giProbes_0 : texture_2d<f32>;

@binding(6) @group(0) var giVisibility_0 : texture_2d<f32>;

@binding(7) @group(0) var giSampler_0 : sampler;

@binding(13) @group(0) var tileTex_0 : texture_2d<u32>;

@binding(12) @group(0) var localTex_0 : texture_2d<f32>;

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
    var p_0 : vec2<f32> = i_1.worldPos_1.xz * vec2<f32>(_S9.x);
    var t_1 : f32 = i_1.time_0 * _S9.y;
    var _S10 : f32 = p_0.x;
    var a_0 : f32 = _S10 * 3.0f + t_1;
    var _S11 : f32 = p_0.y;
    var b_0 : f32 = _S11 * 4.3000001907348633f - t_1 * 1.2999999523162842f;
    var c_1 : f32 = (_S10 + _S11) * 2.0999999046325684f + t_1 * 0.69999998807907104f;
    var height_0 : f32 = (sin(a_0) + sin(b_0) + sin(c_1)) * 0.33000001311302185f;
    var _S12 : f32 = cos(c_1) * 2.0999999046325684f;
    var slope_0 : vec2<f32> = vec2<f32>(cos(a_0) * 3.0f + _S12, cos(b_0) * 4.3000001907348633f + _S12) * vec2<f32>(0.11999999731779099f) * vec2<f32>(_S9.z);
    (*o_0).normal_1 = normalize(vec3<f32>(- slope_0.x, 1.0f, - slope_0.y));
    (*o_0).baseColor_0 = mix(vec3<f32>(0.029999999329447746f, 0.18000000715255737f, 0.44999998807907104f), vec3<f32>(0.25f, 0.75f, 0.85000002384185791f), vec3<f32>(saturate(height_0 * 0.5f + 0.5f)));
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
    const _S13 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    so_0.emissive_0 = _S13;
    so_0.roughness_0 = 0.5f;
    so_0.metallic_0 = 0.0f;
    so_0.ao_0 = 1.0f;
    so_0.discardPixel_0 = false;
    so_0.clearCoat_0 = 0.0f;
    so_0.clearCoatRoughness_0 = 0.0f;
    so_0.anisotropy_0 = 0.0f;
    so_0.anisotropyRotation_0 = 0.0f;
    so_0.sheenColor_0 = _S13;
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

fn decalEdge_0( x_1 : f32,  soft_0 : f32) -> f32
{
    var _S14 : f32;
    if((soft_0 > 0.0f))
    {
        _S14 = saturate((0.5f - abs(x_1)) / (0.5f * soft_0));
    }
    else
    {
        _S14 = 1.0f;
    }
    return _S14;
}

fn decalDisc_0( p_1 : vec2<f32>,  soft_1 : f32) -> f32
{
    var r_0 : f32 = length(p_1) * 2.0f;
    var _S15 : f32;
    if((soft_1 > 0.0f))
    {
        _S15 = saturate((1.0f - r_0) / soft_1);
    }
    else
    {
        _S15 = step(r_0, 1.0f);
    }
    return _S15;
}

fn applyDecals_0( worldPos_3 : vec3<f32>,  Ng_1 : vec3<f32>,  albedo_1 : ptr<function, vec3<f32>>,  N_2 : ptr<function, vec3<f32>>)
{
    var _S16 : u32 = u32(frame_0.decalInfo_0.x + 0.5f);
    var _S17 : vec3<f32> = dpdx(worldPos_3);
    var _S18 : vec3<f32> = dpdy(worldPos_3);
    var i_3 : u32 = u32(0);
    loop
    {
        if((i_3 < _S16))
        {
        }
        else
        {
            break;
        }
        var row_0 : i32 = i32(i_3);
        var _S19 : vec3<i32> = vec3<i32>(i32(0), row_0, i32(0));
        var toDecal0_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S19)).xy, ((_S19)).z));
        var _S20 : vec3<i32> = vec3<i32>(i32(1), row_0, i32(0));
        var toDecal1_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S20)).xy, ((_S20)).z));
        var _S21 : vec3<i32> = vec3<i32>(i32(2), row_0, i32(0));
        var _S22 : vec3<i32> = vec3<i32>(i32(3), row_0, i32(0));
        var dcolor_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S22)).xy, ((_S22)).z));
        var _S23 : vec3<i32> = vec3<i32>(i32(4), row_0, i32(0));
        var axisZ_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S23)).xy, ((_S23)).z));
        var _S24 : vec3<i32> = vec3<i32>(i32(5), row_0, i32(0));
        var axisX_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S24)).xy, ((_S24)).z));
        var _S25 : vec3<i32> = vec3<i32>(i32(6), row_0, i32(0));
        var params_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S25)).xy, ((_S25)).z));
        var _S26 : vec3<i32> = vec3<i32>(i32(7), row_0, i32(0));
        var fade_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S26)).xy, ((_S26)).z));
        var wp_0 : vec4<f32> = vec4<f32>(worldPos_3, 1.0f);
        var _S27 : f32 = dot(toDecal0_0, wp_0);
        var _S28 : f32 = dot(toDecal1_0, wp_0);
        var _S29 : f32 = dot((textureLoad((decalTex_0), ((_S21)).xy, ((_S21)).z)), wp_0);
        if((any((((abs(vec3<f32>(_S27, _S28, _S29))) > vec3<f32>(0.5f, 0.5f, 0.5f))))))
        {
            i_3 = i_3 + u32(1);
            continue;
        }
        var _S30 : f32 = fade_0.y;
        var soft_2 : f32 = axisZ_0.w;
        var a_1 : f32 = dcolor_0.w * saturate((dot(Ng_1, axisZ_0.xyz) - _S30) / max(fade_0.x - _S30, 9.9999997473787516e-05f)) * decalEdge_0(_S29, soft_2);
        var _S31 : f32 = _S27 + 0.5f;
        var _S32 : f32 = 0.5f - _S28;
        var _S33 : f32 = params_0.x;
        var uvw_0 : vec3<f32> = vec3<f32>(_S31, _S32, _S33);
        var _S34 : vec3<f32> = toDecal0_0.xyz;
        var _S35 : vec3<f32> = toDecal1_0.xyz;
        var gx_0 : vec2<f32> = vec2<f32>(dot(_S34, _S17), - dot(_S35, _S17));
        var gy_0 : vec2<f32> = vec2<f32>(dot(_S34, _S18), - dot(_S35, _S18));
        var _S36 : vec4<f32> = vec4<f32>(1.0f, 1.0f, 1.0f, decalDisc_0(vec2<f32>(_S27, _S28), soft_2));
        var tex_0 : vec4<f32>;
        if((_S33 >= 0.0f))
        {
            tex_0 = (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((uvw_0)).xy, i32(((uvw_0)).z), (gx_0), (gy_0)));
        }
        else
        {
            tex_0 = _S36;
        }
        var a_2 : f32 = a_1 * tex_0.w;
        if((a_2 <= 0.0f))
        {
            i_3 = i_3 + u32(1);
            continue;
        }
        (*albedo_1) = mix((*albedo_1), dcolor_0.xyz * tex_0.xyz, vec3<f32>(a_2));
        var _S37 : f32 = params_0.y;
        if((_S37 >= 0.0f))
        {
            var _S38 : vec3<f32> = vec3<f32>(vec2<f32>(_S31, _S32), _S37);
            var xy_1 : vec2<f32> = (textureSampleGrad((vfxData_0), (vfxSampler_0), ((_S38)).xy, i32(((_S38)).z), (gx_0), (gy_0))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f);
            var _S39 : vec3<f32> = axisX_0.xyz;
            var T_1 : vec3<f32> = _S39 - (*N_2) * vec3<f32>(dot(_S39, (*N_2)));
            var T_2 : vec3<f32>;
            if(((dot(T_1, T_1)) > 9.99999993922529029e-09f))
            {
                T_2 = normalize(T_1);
            }
            else
            {
                T_2 = _S39;
            }
            (*N_2) = normalize(mix((*N_2), normalize(T_2 * vec3<f32>(xy_1.x) + cross((*N_2), T_2) * vec3<f32>(xy_1.y) + (*N_2) * vec3<f32>(sqrt(saturate(1.0f - dot(xy_1, xy_1))))), vec3<f32>((a_2 * axisX_0.w))));
        }
        i_3 = i_3 + u32(1);
    }
    return;
}

fn shadowSlope_0( N_3 : vec3<f32>,  L_0 : vec3<f32>) -> f32
{
    var _S40 : f32 = max(saturate(dot(N_3, L_0)), 0.10000000149011612f);
    return min(sqrt(1.0f - _S40 * _S40) / _S40, 6.0f);
}

fn shadowLookup_0( vp_0 : mat4x4<f32>,  cinfo_0 : vec4<f32>,  P_0 : vec3<f32>,  N_4 : vec3<f32>,  slope_1 : f32,  ndc_0 : ptr<function, vec3<f32>>,  bias_0 : ptr<function, f32>)
{
    var _S41 : f32 = max(frame_0.shadowSplit_0.z, 1.0f);
    var q_0 : vec3<f32>;
    if(((frame_0.shadowSplit_0.w) > 0.0f))
    {
        (*bias_0) = frame_0.shadowSplit_0.w * (1.0f + _S41 * slope_1);
        q_0 = P_0;
    }
    else
    {
        var _S42 : f32 = cinfo_0.x;
        var _S43 : vec3<f32> = P_0 + N_4 * vec3<f32>(_S42);
        (*bias_0) = cinfo_0.y * _S42 * (0.5f + _S41 * slope_1);
        q_0 = _S43;
    }
    var c_2 : vec4<f32> = (((vec4<f32>(q_0, 1.0f)) * (vp_0)));
    (*ndc_0) = c_2.xyz / vec3<f32>(max(c_2.w, 9.9999997473787516e-05f));
    return;
}

fn pcfNear_0( uv_4 : vec2<f32>,  depthRef_0 : f32,  texel_0 : vec2<f32>) -> f32
{
    var y_0 : i32;
    var x_2 : i32;
    var shadow_0 : f32;
    if(((frame_0.shadowSplit_0.z) <= 1.0f))
    {
        y_0 = i32(-1);
        shadow_0 = 0.0f;
        loop
        {
            if((y_0 <= i32(1)))
            {
            }
            else
            {
                break;
            }
            x_2 = i32(-1);
            loop
            {
                if((x_2 <= i32(1)))
                {
                }
                else
                {
                    break;
                }
                var shadow_1 : f32 = shadow_0 + (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_4 + vec2<f32>(f32(x_2), f32(y_0)) * texel_0), (depthRef_0)));
                x_2 = x_2 + i32(1);
                shadow_0 = shadow_1;
            }
            y_0 = y_0 + i32(1);
        }
        return shadow_0 / 9.0f;
    }
    var _S44 : vec2<f32> = texel_0 * vec2<f32>(0.5f);
    y_0 = i32(-2);
    shadow_0 = 0.0f;
    loop
    {
        if((y_0 <= i32(2)))
        {
        }
        else
        {
            break;
        }
        x_2 = i32(-2);
        loop
        {
            if((x_2 <= i32(2)))
            {
            }
            else
            {
                break;
            }
            var _S45 : f32 = f32(x_2);
            var _S46 : f32 = f32(y_0);
            var shadow_2 : f32 = shadow_0 + (3.0f - abs(_S45)) * (3.0f - abs(_S46)) * (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_4 + vec2<f32>(_S45, _S46) * _S44), (depthRef_0)));
            x_2 = x_2 + i32(1);
            shadow_0 = shadow_2;
        }
        y_0 = y_0 + i32(1);
    }
    return shadow_0 / 81.0f;
}

fn samplePcfNear_0( ndc_1 : vec3<f32>,  bias_1 : f32) -> f32
{
    var _S47 : f32 = ndc_1.x * 0.5f + 0.5f;
    var _S48 : f32 = - ndc_1.y * 0.5f + 0.5f;
    var uv_5 : vec2<f32> = vec2<f32>(_S47, _S48);
    var _S49 : f32 = ndc_1.z;
    var depthRef_1 : f32 = _S49 - bias_1;
    var _S50 : bool;
    if((_S47 < 0.0f))
    {
        _S50 = true;
    }
    else
    {
        _S50 = (_S48 < 0.0f);
    }
    if(_S50)
    {
        _S50 = true;
    }
    else
    {
        _S50 = (_S47 > 1.0f);
    }
    if(_S50)
    {
        _S50 = true;
    }
    else
    {
        _S50 = (_S48 > 1.0f);
    }
    if(_S50)
    {
        _S50 = true;
    }
    else
    {
        _S50 = (_S49 < 0.0f);
    }
    if(_S50)
    {
        _S50 = true;
    }
    else
    {
        _S50 = (_S49 > 1.0f);
    }
    if(_S50)
    {
        return 1.0f;
    }
    var t_2 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.x;
    return pcfNear_0(uv_5, depthRef_1, vec2<f32>(t_2, t_2));
}

fn pcfAtlas_0( uv_6 : vec2<f32>,  depthRef_2 : f32,  texel_1 : vec2<f32>,  uMin_0 : f32,  uMax_0 : f32) -> f32
{
    var y_1 : i32;
    var x_3 : i32;
    var shadow_3 : f32;
    if(((frame_0.shadowSplit_0.z) <= 1.0f))
    {
        y_1 = i32(-1);
        shadow_3 = 0.0f;
        loop
        {
            if((y_1 <= i32(1)))
            {
            }
            else
            {
                break;
            }
            x_3 = i32(-1);
            loop
            {
                if((x_3 <= i32(1)))
                {
                }
                else
                {
                    break;
                }
                var shadow_4 : f32 = shadow_3 + (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_6.x + f32(x_3) * texel_1.x, uMin_0, uMax_0), uv_6.y + f32(y_1) * texel_1.y)), (depthRef_2)));
                x_3 = x_3 + i32(1);
                shadow_3 = shadow_4;
            }
            y_1 = y_1 + i32(1);
        }
        return shadow_3 / 9.0f;
    }
    var _S51 : vec2<f32> = texel_1 * vec2<f32>(0.5f);
    y_1 = i32(-2);
    shadow_3 = 0.0f;
    loop
    {
        if((y_1 <= i32(2)))
        {
        }
        else
        {
            break;
        }
        x_3 = i32(-2);
        loop
        {
            if((x_3 <= i32(2)))
            {
            }
            else
            {
                break;
            }
            var _S52 : f32 = f32(x_3);
            var _S53 : f32 = f32(y_1);
            var shadow_5 : f32 = shadow_3 + (3.0f - abs(_S52)) * (3.0f - abs(_S53)) * (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_6.x + _S52 * _S51.x, uMin_0, uMax_0), uv_6.y + _S53 * _S51.y)), (depthRef_2)));
            x_3 = x_3 + i32(1);
            shadow_3 = shadow_5;
        }
        y_1 = y_1 + i32(1);
    }
    return shadow_3 / 81.0f;
}

fn samplePcfAtlas_0( ndc_2 : vec3<f32>,  column_0 : f32,  bias_2 : f32) -> f32
{
    var _S54 : f32 = ndc_2.x * 0.5f + 0.5f;
    var _S55 : f32 = - ndc_2.y * 0.5f + 0.5f;
    var _S56 : f32 = ndc_2.z;
    var depthRef_3 : f32 = _S56 - bias_2;
    var _S57 : bool;
    if((_S54 < 0.0f))
    {
        _S57 = true;
    }
    else
    {
        _S57 = (_S55 < 0.0f);
    }
    if(_S57)
    {
        _S57 = true;
    }
    else
    {
        _S57 = (_S54 > 1.0f);
    }
    if(_S57)
    {
        _S57 = true;
    }
    else
    {
        _S57 = (_S55 > 1.0f);
    }
    if(_S57)
    {
        _S57 = true;
    }
    else
    {
        _S57 = (_S56 < 0.0f);
    }
    if(_S57)
    {
        _S57 = true;
    }
    else
    {
        _S57 = (_S56 > 1.0f);
    }
    if(_S57)
    {
        return 1.0f;
    }
    var texelV_0 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.y;
    var texelU_0 : f32 = texelV_0 * 0.5f;
    var _S58 : f32 = texelU_0 * 0.5f;
    return pcfAtlas_0(vec2<f32>((_S54 + column_0) * 0.5f, _S55), depthRef_3, vec2<f32>(texelU_0, texelV_0), column_0 * 0.5f + _S58, (column_0 + 1.0f) * 0.5f - _S58);
}

fn sampleCascadedShadow_0( worldPos_4 : vec3<f32>,  N_5 : vec3<f32>,  L_1 : vec3<f32>,  distanceFromCamera_0 : f32) -> f32
{
    if(((frame_0.shadowTexel_0.z) < 0.5f))
    {
        return 1.0f;
    }
    var slope_2 : f32 = shadowSlope_0(N_5, L_1);
    var ndc_3 : vec3<f32>;
    var bias_3 : f32;
    var _S59 : bool;
    if(((frame_0.shadowTexel_0.z) < 1.5f))
    {
        _S59 = true;
    }
    else
    {
        _S59 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.x));
    }
    if(_S59)
    {
        shadowLookup_0(mat4x4<f32>(frame_0.shadowVP0_0.data_0[i32(0)][i32(0)], frame_0.shadowVP0_0.data_0[i32(1)][i32(0)], frame_0.shadowVP0_0.data_0[i32(2)][i32(0)], frame_0.shadowVP0_0.data_0[i32(3)][i32(0)], frame_0.shadowVP0_0.data_0[i32(0)][i32(1)], frame_0.shadowVP0_0.data_0[i32(1)][i32(1)], frame_0.shadowVP0_0.data_0[i32(2)][i32(1)], frame_0.shadowVP0_0.data_0[i32(3)][i32(1)], frame_0.shadowVP0_0.data_0[i32(0)][i32(2)], frame_0.shadowVP0_0.data_0[i32(1)][i32(2)], frame_0.shadowVP0_0.data_0[i32(2)][i32(2)], frame_0.shadowVP0_0.data_0[i32(3)][i32(2)], frame_0.shadowVP0_0.data_0[i32(0)][i32(3)], frame_0.shadowVP0_0.data_0[i32(1)][i32(3)], frame_0.shadowVP0_0.data_0[i32(2)][i32(3)], frame_0.shadowVP0_0.data_0[i32(3)][i32(3)]), frame_0.shadowC0_0, worldPos_4, N_5, slope_2, &(ndc_3), &(bias_3));
        return samplePcfNear_0(ndc_3, bias_3);
    }
    if(((frame_0.shadowTexel_0.z) < 2.5f))
    {
        _S59 = true;
    }
    else
    {
        _S59 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.y));
    }
    if(_S59)
    {
        shadowLookup_0(mat4x4<f32>(frame_0.shadowVP1_0.data_0[i32(0)][i32(0)], frame_0.shadowVP1_0.data_0[i32(1)][i32(0)], frame_0.shadowVP1_0.data_0[i32(2)][i32(0)], frame_0.shadowVP1_0.data_0[i32(3)][i32(0)], frame_0.shadowVP1_0.data_0[i32(0)][i32(1)], frame_0.shadowVP1_0.data_0[i32(1)][i32(1)], frame_0.shadowVP1_0.data_0[i32(2)][i32(1)], frame_0.shadowVP1_0.data_0[i32(3)][i32(1)], frame_0.shadowVP1_0.data_0[i32(0)][i32(2)], frame_0.shadowVP1_0.data_0[i32(1)][i32(2)], frame_0.shadowVP1_0.data_0[i32(2)][i32(2)], frame_0.shadowVP1_0.data_0[i32(3)][i32(2)], frame_0.shadowVP1_0.data_0[i32(0)][i32(3)], frame_0.shadowVP1_0.data_0[i32(1)][i32(3)], frame_0.shadowVP1_0.data_0[i32(2)][i32(3)], frame_0.shadowVP1_0.data_0[i32(3)][i32(3)]), frame_0.shadowC1_0, worldPos_4, N_5, slope_2, &(ndc_3), &(bias_3));
        return samplePcfAtlas_0(ndc_3, 0.0f, bias_3);
    }
    shadowLookup_0(mat4x4<f32>(frame_0.shadowVP2_0.data_0[i32(0)][i32(0)], frame_0.shadowVP2_0.data_0[i32(1)][i32(0)], frame_0.shadowVP2_0.data_0[i32(2)][i32(0)], frame_0.shadowVP2_0.data_0[i32(3)][i32(0)], frame_0.shadowVP2_0.data_0[i32(0)][i32(1)], frame_0.shadowVP2_0.data_0[i32(1)][i32(1)], frame_0.shadowVP2_0.data_0[i32(2)][i32(1)], frame_0.shadowVP2_0.data_0[i32(3)][i32(1)], frame_0.shadowVP2_0.data_0[i32(0)][i32(2)], frame_0.shadowVP2_0.data_0[i32(1)][i32(2)], frame_0.shadowVP2_0.data_0[i32(2)][i32(2)], frame_0.shadowVP2_0.data_0[i32(3)][i32(2)], frame_0.shadowVP2_0.data_0[i32(0)][i32(3)], frame_0.shadowVP2_0.data_0[i32(1)][i32(3)], frame_0.shadowVP2_0.data_0[i32(2)][i32(3)], frame_0.shadowVP2_0.data_0[i32(3)][i32(3)]), frame_0.shadowC2_0, worldPos_4, N_5, slope_2, &(ndc_3), &(bias_3));
    return samplePcfAtlas_0(ndc_3, 1.0f, bias_3);
}

fn hemisphereAmbient_0( N_6 : vec3<f32>) -> vec3<f32>
{
    return mix(frame_0.ambientGround_0.xyz, frame_0.ambientSky_0.xyz, vec3<f32>((N_6.y * 0.5f + 0.5f)));
}

fn giProbeTexel_0( probe_0 : u32,  k_0 : u32) -> vec4<f32>
{
    var _S60 : vec3<i32> = vec3<i32>(i32(((probe_0 & (u32(255)))) * u32(7) + k_0), i32((probe_0 >> (u32(8)))), i32(0));
    return (textureLoad((giProbes_0), ((_S60)).xy, ((_S60)).z));
}

fn giOctEncode_0( n_0 : vec3<f32>) -> vec2<f32>
{
    var _S61 : f32 = n_0.x;
    var _S62 : f32 = n_0.z;
    var _S63 : f32 = n_0.y;
    var uv_7 : vec2<f32> = vec2<f32>(_S61, _S62) / vec2<f32>((abs(_S61) + abs(_S63) + abs(_S62)));
    var uv_8 : vec2<f32>;
    if((_S63 < 0.0f))
    {
        var _S64 : f32;
        if(((uv_7.x) >= 0.0f))
        {
            _S64 = 1.0f;
        }
        else
        {
            _S64 = -1.0f;
        }
        var _S65 : f32;
        if(((uv_7.y) >= 0.0f))
        {
            _S65 = 1.0f;
        }
        else
        {
            _S65 = -1.0f;
        }
        uv_8 = (vec2<f32>(1.0f, 1.0f) - abs(uv_7.yx)) * vec2<f32>(_S64, _S65);
    }
    else
    {
        uv_8 = uv_7;
    }
    return uv_8;
}

fn giVisibilityAt_0( probe_1 : u32,  dir_0 : vec3<f32>) -> vec2<f32>
{
    var tiles_0 : u32 = u32(frame_0.giDims_0.w);
    var _S66 : u32 = probe_1 % tiles_0;
    var _S67 : f32 = f32(_S66);
    var _S68 : u32 = probe_1 / tiles_0;
    var _S69 : vec2<f32> = vec2<f32>(0.5f);
    return (textureSampleLevel((giVisibility_0), (giSampler_0), ((vec2<f32>(_S67, f32(_S68)) * vec2<f32>(18.0f) + vec2<f32>(1.0f) + (giOctEncode_0(dir_0) * _S69 + _S69) * vec2<f32>(16.0f)) * frame_0.giParams_0.zw), (0.0f)).xy);
}

fn giProbeIrradiance_0( probe_2 : u32,  n_1 : vec3<f32>) -> vec3<f32>
{
    var a0_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(0));
    var a1_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(1));
    var a2_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(2));
    var a3_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(3));
    var a4_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(4));
    var a5_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(5));
    var _S70 : f32 = n_1.y;
    var _S71 : f32 = n_1.z;
    var _S72 : f32 = n_1.x;
    var _S73 : f32 = 1.0925480127334595f * _S72;
    return max(a0_0.xyz * vec3<f32>(0.28209498524665833f) + vec3<f32>(a0_0.w, a1_0.xy) * vec3<f32>((0.48860299587249756f * _S70)) + vec3<f32>(a1_0.zw, a2_0.x) * vec3<f32>((0.48860299587249756f * _S71)) + a2_0.yzw * vec3<f32>((0.48860299587249756f * _S72)) + a3_0.xyz * vec3<f32>((_S73 * _S70)) + vec3<f32>(a3_0.w, a4_0.xy) * vec3<f32>((1.0925480127334595f * _S70 * _S71)) + vec3<f32>(a4_0.zw, a5_0.x) * vec3<f32>((0.31539198756217957f * (3.0f * _S71 * _S71 - 1.0f))) + a5_0.yzw * vec3<f32>((_S73 * _S71)) + giProbeTexel_0(probe_2, u32(6)).xyz * vec3<f32>((0.54627400636672974f * (_S72 * _S72 - _S70 * _S70))), vec3<f32>(0.0f, 0.0f, 0.0f));
}

fn giIrradiance_0( P_1 : vec3<f32>,  N_7 : vec3<f32>,  V_1 : vec3<f32>,  ok_0 : ptr<function, bool>) -> vec3<f32>
{
    var _S74 : vec3<i32> = vec3<i32>(frame_0.giDims_0.xyz);
    const _S75 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S76 : vec3<i32> = _S74 - vec3<i32>(i32(1), i32(1), i32(1));
    var c_3 : vec3<f32> = clamp((P_1 + (N_7 * vec3<f32>(0.20000000298023224f) + V_1 * vec3<f32>(0.80000001192092896f)) * vec3<f32>(frame_0.giParams_0.x) - frame_0.giOrigin_0.xyz) / frame_0.giSpacing_0.xyz, _S75, vec3<f32>(_S76));
    var _S77 : vec3<f32> = frame_0.giOrigin_0.xyz + c_3 * frame_0.giSpacing_0.xyz;
    var _S78 : vec3<i32> = min(vec3<i32>(floor(c_3)), max(_S74 - vec3<i32>(i32(2), i32(2), i32(2)), vec3<i32>(i32(0), i32(0), i32(0))));
    var _S79 : vec3<f32> = saturate(c_3 - vec3<f32>(_S78));
    var i_4 : i32 = i32(0);
    var sum_0 : vec3<f32> = _S75;
    var wsum_0 : f32 = 0.0f;
    loop
    {
        if((i_4 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var off_0 : vec3<i32> = vec3<i32>((i_4 & (i32(1))), (((i_4 >> (u32(1)))) & (i32(1))), (((i_4 >> (u32(2)))) & (i32(1))));
        var _S80 : vec3<i32> = min(_S78 + off_0, _S76);
        var idx_0 : u32 = u32(_S80.x + _S74.x * (_S80.y + _S74.y * _S80.z));
        if(((giProbeTexel_0(idx_0, u32(6)).w) < 0.5f))
        {
            i_4 = i_4 + i32(1);
            continue;
        }
        var probeP_0 : vec3<f32> = frame_0.giOrigin_0.xyz + vec3<f32>(_S80) * frame_0.giSpacing_0.xyz;
        var tri_0 : vec3<f32> = mix(vec3<f32>(1.0f, 1.0f, 1.0f) - _S79, _S79, vec3<f32>(off_0));
        var wrap_0 : f32 = (dot(normalize(probeP_0 - P_1 + vec3<f32>(9.99999997475242708e-07f, 9.99999997475242708e-07f, 9.99999997475242708e-07f)), N_7) + 1.0f) * 0.5f;
        var w_0 : f32 = wrap_0 * wrap_0 + 0.20000000298023224f;
        var toPoint_0 : vec3<f32> = _S77 - probeP_0;
        var r_1 : f32 = length(toPoint_0);
        var _S81 : vec3<f32>;
        if((r_1 > 9.9999997473787516e-05f))
        {
            _S81 = toPoint_0 / vec3<f32>(r_1);
        }
        else
        {
            _S81 = vec3<f32>(0.0f, 1.0f, 0.0f);
        }
        var m_0 : vec2<f32> = giVisibilityAt_0(idx_0, _S81);
        var _S82 : f32 = m_0.x;
        var w_1 : f32;
        if((r_1 > _S82))
        {
            var var_0 : f32 = abs(m_0.y - _S82 * _S82);
            var d_0 : f32 = r_1 - _S82;
            var ch_0 : f32 = var_0 / (var_0 + d_0 * d_0);
            w_1 = w_0 * (ch_0 * ch_0 * ch_0);
        }
        else
        {
            w_1 = w_0;
        }
        var w_2 : f32;
        if((w_1 < 0.20000000298023224f))
        {
            w_2 = w_1 * (w_1 * w_1 * 25.0f);
        }
        else
        {
            w_2 = w_1;
        }
        var w_3 : f32 = w_2 * (tri_0.x * tri_0.y * tri_0.z);
        var wsum_1 : f32 = wsum_0 + w_3;
        sum_0 = sum_0 + giProbeIrradiance_0(idx_0, N_7) * vec3<f32>(w_3);
        wsum_0 = wsum_1;
        i_4 = i_4 + i32(1);
    }
    var _S83 : bool = (wsum_0 > 9.99999997475242708e-07f);
    (*ok_0) = _S83;
    if(_S83)
    {
        sum_0 = sum_0 / vec3<f32>(wsum_0);
    }
    else
    {
        sum_0 = _S75;
    }
    return sum_0;
}

fn giToonCoord_0( e_0 : vec3<f32>) -> f32
{
    var bands_0 : f32 = frame_0.giParams_0.y;
    var _S84 : f32 = max(max(e_0.x, e_0.y), e_0.z);
    var _S85 : bool;
    if((bands_0 < 1.5f))
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S84 <= 9.9999997473787516e-06f);
    }
    var _S86 : f32;
    if(_S85)
    {
        _S86 = -1.0f;
    }
    else
    {
        _S86 = _S84 / (_S84 + 1.0f) * bands_0;
    }
    return _S86;
}

fn giToonBandAt_0( e_1 : vec3<f32>,  x_4 : f32,  w_4 : f32) -> vec3<f32>
{
    if((x_4 < 0.0f))
    {
        return e_1;
    }
    var q_1 : f32 = (floor(x_4) + smoothstep(1.0f - max(w_4, 9.9999997473787516e-05f), 1.0f, fract(x_4)) + 0.5f) / frame_0.giParams_0.y;
    return e_1 * vec3<f32>((q_1 / max(1.0f - q_1, 0.0010000000474974513f) / max(max(e_1.x, e_1.y), e_1.z)));
}

fn toonBandLevel_0( lambert_0 : f32,  p_2 : vec4<f32>,  th_0 : vec4<f32>,  grad_0 : f32) -> f32
{
    var _S87 : f32 = max(p_2.y, min(grad_0, 0.25f));
    var k_1 : i32 = i32(0);
    var level_0 : f32 = 0.0f;
    loop
    {
        if((k_1 < i32(3)))
        {
        }
        else
        {
            break;
        }
        var _S88 : i32 = k_1 + i32(1);
        var _S89 : f32;
        if((f32(_S88) < (p_2.x)))
        {
            _S89 = smoothstep(th_0[k_1] - _S87, th_0[k_1] + _S87, lambert_0);
        }
        else
        {
            _S89 = 0.0f;
        }
        var level_1 : f32 = level_0 + _S89;
        k_1 = _S88;
        level_0 = level_1;
    }
    return level_0;
}

fn toonShadowedLevel_0( level_2 : f32,  lit_0 : f32,  p_3 : vec4<f32>) -> f32
{
    var _S90 : f32 = p_3.z;
    var down_0 : f32;
    if((_S90 > 0.5f))
    {
        down_0 = max(level_2 - _S90, 0.0f);
    }
    else
    {
        down_0 = 0.0f;
    }
    return mix(down_0, level_2, lit_0);
}

fn toonBandTint_0( level_3 : f32,  t0_0 : vec4<f32>,  t1_0 : vec4<f32>,  t2_0 : vec4<f32>,  t3_0 : vec4<f32>) -> vec3<f32>
{
    var _S91 : vec3<f32> = t0_0.xyz;
    var _S92 : vec3<f32> = t1_0.xyz;
    var _S93 : vec3<f32> = t2_0.xyz;
    return _S91 + vec3<f32>(saturate(level_3)) * (_S92 - _S91) + vec3<f32>(saturate(level_3 - 1.0f)) * (_S93 - _S92) + vec3<f32>(saturate(level_3 - 2.0f)) * (t3_0.xyz - _S93);
}

fn toonMetalSteps_0( t_3 : f32,  bands_1 : f32,  aa_0 : f32) -> f32
{
    if((bands_1 < 0.5f))
    {
        return saturate(t_3);
    }
    var _S94 : f32 = clamp(round(bands_1), 1.0f, 8.0f);
    var _S95 : f32 = max(aa_0, 0.0020000000949949026f);
    var k_2 : i32 = i32(1);
    var level_4 : f32 = 0.0f;
    loop
    {
        if((k_2 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var _S96 : f32 = f32(k_2);
        var th_1 : f32 = _S96 / _S94;
        var _S97 : f32;
        if((_S96 < _S94))
        {
            _S97 = smoothstep(th_1 - _S95, th_1 + _S95, t_3);
        }
        else
        {
            _S97 = 0.0f;
        }
        var level_5 : f32 = level_4 + _S97;
        k_2 = k_2 + i32(1);
        level_4 = level_5;
    }
    return level_4 / max(_S94 - 1.0f, 1.0f);
}

fn toonMetalEnv_0( N_8 : vec3<f32>,  V_2 : vec3<f32>) -> vec3<f32>
{
    var h_0 : f32 = draw_0.metalB_0.x;
    var _S98 : f32 = reflect((vec3<f32>(0) - V_2), N_8).y;
    var _S99 : f32 = max(draw_0.metalB_0.y, (fwidth((_S98))));
    var skyT_0 : f32 = saturate((_S98 - h_0) / max(1.0f - h_0, 0.0010000000474974513f));
    var groundT_0 : f32 = saturate((h_0 - _S98) / max(1.0f + h_0, 0.0010000000474974513f));
    return mix(mix(draw_0.metalGround_0.xyz, draw_0.metalHorizon_0.xyz, vec3<f32>(toonMetalSteps_0(groundT_0, draw_0.metalA_0.w, (fwidth((groundT_0)))))), mix(draw_0.metalHorizon_0.xyz, draw_0.metalSky_0.xyz, vec3<f32>(toonMetalSteps_0(skyT_0, draw_0.metalA_0.w, (fwidth((skyT_0)))))), vec3<f32>(smoothstep(h_0 - _S99, h_0 + _S99, _S98)));
}

fn styledToonAlbedo_0( albedo_2 : vec3<f32>,  N_9 : vec3<f32>,  V_3 : vec3<f32>,  L_2 : vec3<f32>,  lambert_1 : f32,  castShadow_0 : f32,  unlitReflection_0 : ptr<function, vec3<f32>>) -> vec3<f32>
{
    var bandTint_0 : vec3<f32> = toonBandTint_0(toonShadowedLevel_0(toonBandLevel_0(lambert_1, draw_0.rampParams_0, draw_0.rampThresh_0, (fwidth((lambert_1)))), castShadow_0, draw_0.rampParams_0), draw_0.tint0_0, draw_0.tint1_0, draw_0.tint2_0, draw_0.tint3_0);
    var c_4 : vec3<f32> = albedo_2 * bandTint_0;
    (*unlitReflection_0) = vec3<f32>(0.0f, 0.0f, 0.0f);
    var c_5 : vec3<f32>;
    if(((draw_0.metalA_0.x) > 0.0f))
    {
        const _S100 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        var refl_0 : vec3<f32> = toonMetalEnv_0(N_9, V_3) * mix(_S100, albedo_2, vec3<f32>(draw_0.metalA_0.y)) * mix(_S100, bandTint_0, vec3<f32>(draw_0.metalA_0.z));
        (*unlitReflection_0) = refl_0 * vec3<f32>(draw_0.metalA_0.x);
        c_5 = mix(c_4, refl_0, vec3<f32>(draw_0.metalA_0.x));
    }
    else
    {
        c_5 = c_4;
    }
    if(((draw_0.accRim_0.w) > 0.0f))
    {
        var ndv_0 : f32 = saturate(dot(N_9, V_3));
        var _S101 : f32 = max(max((fwidth((ndv_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.y);
        var lit_1 : f32 = lambert_1 * castShadow_0;
        var _S102 : f32 = max((fwidth((lit_1))), 9.9999997473787516e-05f);
        var side_0 : f32;
        if(((draw_0.accRimLimit_0.x) >= 0.99900001287460327f))
        {
            side_0 = 1.0f;
        }
        else
        {
            side_0 = 1.0f - smoothstep(draw_0.accRimLimit_0.x - _S102, draw_0.accRimLimit_0.x + _S102, lit_1);
        }
        var rim_0 : f32 = (1.0f - smoothstep(draw_0.accRim_0.w - _S101, draw_0.accRim_0.w + _S101, ndv_0)) * side_0;
        var _S103 : vec3<f32> = mix(c_5, draw_0.accRim_0.xyz, vec3<f32>(rim_0));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - rim_0));
        c_5 = _S103;
    }
    if(((draw_0.accGloss_0.w) > 0.0f))
    {
        var nh_0 : f32 = saturate(dot(N_9, normalize(L_2 + V_3)));
        var _S104 : f32 = max(max((fwidth((nh_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.z);
        var g_0 : f32 = smoothstep(draw_0.accGloss_0.w - _S104, draw_0.accGloss_0.w + _S104, nh_0) * step(0.5f, castShadow_0) * step(0.0010000000474974513f, lambert_1);
        var gMix_0 : f32 = g_0 * min(draw_0.accRimLimit_0.w, 1.0f);
        var _S105 : vec3<f32> = mix(c_5, draw_0.accGloss_0.xyz, vec3<f32>(gMix_0)) + draw_0.accGloss_0.xyz * vec3<f32>((g_0 * max(draw_0.accRimLimit_0.w - 1.0f, 0.0f)));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - gMix_0));
        c_5 = _S105;
    }
    return c_5;
}

fn toonWrap_0( lambert_2 : f32) -> f32
{
    var _S106 : f32 = max(frame_0.toonParams_0.y, 0.0010000000474974513f);
    return saturate((lambert_2 + _S106) / (1.0f + _S106));
}

fn toonEdgeHalfWidth_0( half_0 : f32,  grad_1 : f32) -> f32
{
    return max(half_0, min(grad_1, 0.25f));
}

fn toonRamp_0( lambert_3 : f32) -> f32
{
    if(((frame_0.toonParams_0.x) < 0.5f))
    {
        return toonWrap_0(lambert_3);
    }
    var bands_2 : f32 = clamp(frame_0.toonParams_0.x, 2.0f, 4.0f);
    var _S107 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((lambert_3))));
    var k_3 : i32 = i32(1);
    var level_6 : f32 = 0.0f;
    loop
    {
        if((k_3 < i32(4)))
        {
        }
        else
        {
            break;
        }
        var _S108 : f32 = f32(k_3);
        var th_2 : f32 = _S108 / bands_2;
        var _S109 : f32;
        if((_S108 < bands_2))
        {
            _S109 = smoothstep(th_2 - _S107, th_2 + _S107, lambert_3);
        }
        else
        {
            _S109 = 0.0f;
        }
        var level_7 : f32 = level_6 + _S109;
        k_3 = k_3 + i32(1);
        level_6 = level_7;
    }
    return level_6 / (bands_2 - 1.0f);
}

fn accumulateLocalLights_0( worldPos_5 : vec3<f32>,  albedo_3 : vec3<f32>,  N_10 : vec3<f32>,  V_4 : vec3<f32>,  phong_1 : bool,  pixel_1 : vec2<f32>) -> vec3<f32>
{
    var count_0 : u32 = u32(frame_0.lightInfo_0.x + 0.5f);
    var tiled_0 : bool = ((frame_0.lightTiles_0.x) > 0.5f);
    var count_1 : u32;
    var own_0 : u32;
    var first_0 : u32;
    if(tiled_0)
    {
        var _S110 : vec2<i32> = vec2<i32>(pixel_1) / vec2<i32>(i32(frame_0.lightTiles_0.x + 0.5f));
        var _S111 : vec3<i32> = vec3<i32>(_S110, i32(0));
        var _S112 : u32 = (textureLoad((tileTex_0), ((_S111)).xy, ((_S111)).z).x);
        var _S113 : u32 = (_S112 & (u32(511)));
        var _S114 : u32 = (_S112 >> (u32(9)));
        count_1 = u32(frame_0.lightTiles_0.z + 0.5f);
        own_0 = _S113;
        first_0 = _S114;
    }
    else
    {
        count_1 = count_0;
        own_0 = count_0;
        first_0 = u32(0);
    }
    var _S115 : i32 = i32(frame_0.lightTiles_0.y + 0.5f);
    const _S116 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var k_4 : u32 = u32(0);
    var sum_1 : vec3<f32> = _S116;
    loop
    {
        if((k_4 < count_1))
        {
        }
        else
        {
            break;
        }
        var valid_0 : bool = (k_4 < own_0);
        var _S117 : i32 = i32(k_4);
        var row_1 : i32;
        if(tiled_0)
        {
            var at_0 : u32 = first_0 + min(k_4, max(own_0, u32(1)) - u32(1));
            var _S118 : vec3<i32> = vec3<i32>(i32((at_0 & (u32(1023)))), _S115 + i32((at_0 >> (u32(10)))), i32(0));
            row_1 = i32((textureLoad((tileTex_0), ((_S118)).xy, ((_S118)).z).x));
        }
        else
        {
            row_1 = _S117;
        }
        var _S119 : vec3<i32> = vec3<i32>(i32(0), row_1, i32(0));
        var t0_1 : vec4<f32> = (textureLoad((localTex_0), ((_S119)).xy, ((_S119)).z));
        var _S120 : vec3<i32> = vec3<i32>(i32(1), row_1, i32(0));
        var t1_1 : vec4<f32> = (textureLoad((localTex_0), ((_S120)).xy, ((_S120)).z));
        var _S121 : vec3<i32> = vec3<i32>(i32(2), row_1, i32(0));
        var t2_1 : vec4<f32> = (textureLoad((localTex_0), ((_S121)).xy, ((_S121)).z));
        var d_1 : vec3<f32> = t0_1.xyz - worldPos_5;
        var dist2_0 : f32 = dot(d_1, d_1);
        var _S122 : f32 = t0_1.w;
        var r2_0 : f32 = _S122 * _S122;
        var L_3 : vec3<f32> = d_1 * vec3<f32>(rsqrt_0(max(dist2_0, 9.99999993922529029e-09f)));
        var win_0 : f32 = saturate(1.0f - dist2_0 * dist2_0 / (r2_0 * r2_0));
        var spot_0 : f32 = saturate(dot((vec3<f32>(0) - L_3), t2_1.xyz) * t1_1.w + t2_1.w);
        var _S123 : bool;
        if(valid_0)
        {
            _S123 = (dist2_0 < r2_0);
        }
        else
        {
            _S123 = false;
        }
        var shape_0 : f32;
        if(_S123)
        {
            shape_0 = win_0 * win_0 * spot_0 * spot_0;
        }
        else
        {
            shape_0 = 0.0f;
        }
        var ndl_0 : f32 = saturate(dot(N_10, L_3));
        if(phong_1)
        {
            sum_1 = sum_1 + (albedo_3 * vec3<f32>(ndl_0) + draw_0.phong_0.xyz * vec3<f32>((pow(saturate(dot(N_10, normalize(L_3 + V_4))), max(draw_0.phong_0.w, 1.0f)) * ndl_0))) * t1_1.xyz * vec3<f32>((shape_0 / (dist2_0 + 1.0f)));
        }
        else
        {
            var level_8 : f32;
            if(((frame_0.toonParams_0.x) < 0.5f))
            {
                level_8 = toonWrap_0(ndl_0) * shape_0;
            }
            else
            {
                level_8 = toonRamp_0(ndl_0 * shape_0);
            }
            sum_1 = sum_1 + albedo_3 * t1_1.xyz * vec3<f32>(level_8);
        }
        k_4 = k_4 + u32(1);
    }
    return sum_1;
}

fn toonTone_0( t_4 : f32) -> vec3<f32>
{
    const white_0 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
    if(((frame_0.toonParams_0.x) < 2.5f))
    {
        return mix(frame_0.shadowTint_0.xyz, white_0, vec3<f32>(t_4));
    }
    var _S124 : vec3<f32>;
    if((t_4 < 0.5f))
    {
        _S124 = mix(frame_0.shadowTint_0.xyz, frame_0.toonMidTint_0.xyz, vec3<f32>((t_4 * 2.0f)));
    }
    else
    {
        _S124 = mix(frame_0.toonMidTint_0.xyz, white_0, vec3<f32>((t_4 * 2.0f - 1.0f)));
    }
    return _S124;
}

fn sampleEmissive_0( uv_9 : vec2<f32>) -> vec3<f32>
{
    var e_2 : vec3<f32> = draw_0.emissiveFactor_0.xyz;
    var e_3 : vec3<f32>;
    if(((draw_0.mapFlags_0.z) > 0.5f))
    {
        e_3 = e_2 * (textureSample((matEmissive_0), (matSampler_0), (uv_9))).xyz;
    }
    else
    {
        e_3 = e_2;
    }
    return e_3;
}

fn applyFog_0( color_1 : vec3<f32>,  worldPos_6 : vec3<f32>) -> vec3<f32>
{
    if(((frame_0.fogParams_0.z) < 0.5f))
    {
        return color_1;
    }
    return mix(color_1, frame_0.fogColor_0.xyz, vec3<f32>(saturate((length(frame_0.cameraPos_0.xyz - worldPos_6) - frame_0.fogParams_0.x) / max(frame_0.fogParams_0.y - frame_0.fogParams_0.x, 0.0010000000474974513f))));
}

fn shadeScene_0( i_5 : VSOut_0,  N_11 : ptr<function, vec3<f32>>,  V_5 : ptr<function, vec3<f32>>) -> vec4<f32>
{
    var _S125 : vec3<f32> = normalize(i_5.worldNorm_0);
    (*N_11) = _S125;
    var texel_2 : vec4<f32> = baseTexel_0(i_5.uv_3);
    var Nl_0 : vec3<f32> = perturbNormal_0(_S125, i_5.worldPos_2, i_5.uv_3);
    var albedo_4 : vec3<f32> = i_5.diffuse_1.xyz * i_5.color_0.xyz * texel_2.xyz;
    var userAlpha_0 : f32 = i_5.diffuse_1.w * i_5.color_0.w * texel_2.w;
    var userEmissive_0 : vec3<f32>;
    applyUserSurface_0(i_5, _S125, normalize(frame_0.cameraPos_0.xyz - i_5.worldPos_2), &(albedo_4), &(Nl_0), &(userAlpha_0), &(userEmissive_0));
    applyDecals_0(i_5.worldPos_2, _S125, &(albedo_4), &(Nl_0));
    var L_4 : vec3<f32> = normalize((vec3<f32>(0) - frame_0.lightDir_0.xyz));
    (*V_5) = normalize(frame_0.cameraPos_0.xyz - i_5.worldPos_2);
    var castShadow_1 : f32 = sampleCascadedShadow_0(i_5.worldPos_2, Nl_0, L_4, length(frame_0.cameraPos_0.xyz - i_5.worldPos_2));
    var alpha_2 : f32 = userAlpha_0;
    var lightColor_1 : vec3<f32> = frame_0.lightColor_0.xyz;
    var _S126 : vec3<f32> = hemisphereAmbient_0(Nl_0) * albedo_4 * vec3<f32>(0.30000001192092896f);
    var ambient_0 : vec3<f32>;
    if(((frame_0.giOrigin_0.w) > 0.5f))
    {
        var giOk_0 : bool;
        var e_4 : vec3<f32> = giIrradiance_0(i_5.worldPos_2, Nl_0, (*V_5), &(giOk_0));
        var x_5 : f32 = giToonCoord_0(e_4);
        var aa_1 : f32 = (fwidth((x_5)));
        if(giOk_0)
        {
            ambient_0 = giToonBandAt_0(e_4, x_5, aa_1) * vec3<f32>((frame_0.giSpacing_0.w / 3.1415927410125732f)) * albedo_4;
        }
        else
        {
            ambient_0 = _S126;
        }
    }
    else
    {
        ambient_0 = _S126;
    }
    var color_2 : vec3<f32>;
    if(((draw_0.rampParams_0.x) > 0.5f))
    {
        var reflection_0 : vec3<f32>;
        var styled_0 : vec3<f32> = styledToonAlbedo_0(albedo_4, Nl_0, (*V_5), L_4, saturate(dot(Nl_0, L_4)), castShadow_1, &(reflection_0));
        var _S127 : vec3<f32> = vec3<f32>((1.0f - draw_0.metalA_0.x));
        var color_3 : vec3<f32> = styled_0 * lightColor_1 + reflection_0 * (vec3<f32>(1.0f) - lightColor_1) + ambient_0 * _S127;
        var _S128 : vec3<f32> = accumulateLocalLights_0(i_5.worldPos_2, albedo_4, Nl_0, (*V_5), false, i_5.pos_0.xy);
        color_2 = color_3 + _S128 * _S127;
    }
    else
    {
        var lambert_4 : f32 = saturate(dot(Nl_0, L_4)) * castShadow_1;
        var color_4 : vec3<f32> = albedo_4 * toonTone_0(toonRamp_0(lambert_4)) * lightColor_1 + ambient_0;
        var _S129 : vec3<f32> = accumulateLocalLights_0(i_5.worldPos_2, albedo_4, Nl_0, (*V_5), false, i_5.pos_0.xy);
        var color_5 : vec3<f32> = color_4 + _S129;
        if(((frame_0.toonParams_0.z) > 0.0f))
        {
            var spec_0 : f32 = pow(saturate(dot(Nl_0, normalize(L_4 + (*V_5)))), max(frame_0.toonParams_0.w * draw_0.toonMaterial_0.w, 1.0f));
            var half_1 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((spec_0))));
            color_2 = color_5 + lightColor_1 * draw_0.toonMaterial_0.xyz * vec3<f32>(frame_0.toonParams_0.z) * vec3<f32>((smoothstep(0.5f - half_1, 0.5f + half_1, spec_0) * step(0.0010000000474974513f, lambert_4)));
        }
        else
        {
            color_2 = color_5;
        }
        color_2 = color_2 + frame_0.rimParams_0.xyz * vec3<f32>(pow(1.0f - saturate(dot(Nl_0, (*V_5))), frame_0.rimParams_0.w));
    }
    return vec4<f32>(applyFog_0(color_2 + (sampleEmissive_0(i_5.uv_3) + draw_0.tintAdd_0.xyz) + userEmissive_0, i_5.worldPos_2), alpha_2);
}

fn occluderFadeAmount_0( W_0 : vec3<f32>,  N_12 : vec3<f32>) -> f32
{
    var _S130 : u32 = u32(frame_0.fadeCamera_0.w);
    var v_0 : vec3<f32> = W_0 - frame_0.fadeCamera_0.xyz;
    var _S131 : vec3<f32>;
    if(((dot(N_12, v_0)) > 0.0f))
    {
        _S131 = (vec3<f32>(0) - N_12);
    }
    else
    {
        _S131 = N_12;
    }
    var amount_0 : f32 = 0.0f;
    var i_6 : u32 = u32(0);
    loop
    {
        if((i_6 < u32(4)))
        {
        }
        else
        {
            break;
        }
        if((i_6 >= _S130))
        {
            break;
        }
        var T_3 : vec3<f32> = frame_0.fadeTargets_0[i_6].xyz;
        var r_2 : f32 = frame_0.fadeTargets_0[i_6].w;
        var axis_0 : vec3<f32> = frame_0.fadeCones_0[i_6].xyz;
        var s_0 : f32 = dot(v_0, axis_0);
        var _S132 : f32 = max(amount_0, (1.0f - smoothstep(1.0f - frame_0.fadeParams_0.y, 1.0f, length(v_0 - axis_0 * vec3<f32>(s_0)) / max(s_0 * frame_0.fadeCones_0[i_6].w, 9.9999997473787516e-06f))) * saturate((dot(T_3 - frame_0.fadeCamera_0.xyz, axis_0) - s_0) / r_2) * smoothstep(0.25f * r_2, -0.25f * r_2, dot(T_3 - W_0, _S131)));
        var i_7 : u32 = i_6 + u32(1);
        amount_0 = _S132;
        i_6 = i_7;
    }
    return amount_0 * frame_0.fadeParams_0.x;
}

fn occluderFaded_0( pixel_2 : vec2<f32>,  W_1 : vec3<f32>,  N_13 : vec3<f32>) -> bool
{
    if(((frame_0.fadeCamera_0.w) < 0.5f))
    {
        return false;
    }
    var p_4 : vec2<u32> = (vec2<u32>(pixel_2) & (vec2<u32>(u32(3), u32(3))));
    return ((occluderFadeAmount_0(W_1, N_13 * vec3<f32>(rsqrt_0(max(dot(N_13, N_13), 9.99999968265522539e-21f))))) > (fract((kOccluderBayer_0[p_4.y * u32(4) + p_4.x] + 0.5f) / 16.0f + frame_0.fadeParams_0.z)));
}

fn neutralShoulder_0( c_6 : vec3<f32>) -> vec3<f32>
{
    var _S133 : f32 = max(c_6.x, max(c_6.y, c_6.z));
    if((_S133 <= 0.89999997615814209f))
    {
        return c_6;
    }
    return c_6 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S133 + 0.10000002384185791f - 0.89999997615814209f)) / _S133));
}

fn storeHdr_0( c_7 : vec3<f32>,  scale_0 : f32,  pixel_3 : vec2<f32>) -> vec3<f32>
{
    if((scale_0 >= 1.0f))
    {
        return c_7;
    }
    const _S134 : vec2<f32> = vec2<f32>(0.067110560834407806f, 0.0058371499180793762f);
    var n0_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_3, _S134)));
    var n1_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_3 + vec2<f32>(17.0f, 31.0f), _S134)));
    return neutralShoulder_0(c_7 * vec3<f32>(scale_0)) + vec3<f32>(n0_0 + n1_0 - 1.0f, n1_0 + n0_0 - 1.0f, n0_0 - n1_0) * vec3<f32>(0.00097751710563898087f);
}

fn packSlotNdv_0( slot_0 : f32,  ndv_1 : f32) -> f32
{
    return (slot_0 * 32.0f + round(saturate(ndv_1) * 31.0f)) / 255.0f;
}

fn packLinearDepth_0( viewDepth_1 : f32) -> vec4<f32>
{
    var v_1 : u32 = u32(saturate(viewDepth_1 / max(frame_0.viewInfo_0.x, 0.0010000000474974513f)) * 1.6777215e+07f + 0.5f);
    return vec4<f32>(f32((((v_1 >> (u32(16)))) & (u32(255)))), f32((((v_1 >> (u32(8)))) & (u32(255)))), f32((v_1 & (u32(255)))), 255.0f) / vec4<f32>(255.0f);
}

struct FsOut_0
{
    @location(0) color_6 : vec4<f32>,
    @location(1) normal_2 : vec4<f32>,
    @location(2) depth_0 : vec4<f32>,
};

fn packOutput_0( color_7 : vec3<f32>,  alpha_3 : f32,  Ng_2 : vec3<f32>,  V_6 : vec3<f32>,  viewDepth_2 : f32,  pixel_4 : vec2<f32>) -> FsOut_0
{
    var o_1 : FsOut_0;
    o_1.color_6 = vec4<f32>(storeHdr_0(color_7, frame_0.lightInfo_0.y, pixel_4), alpha_3);
    var _S135 : vec3<f32> = vec3<f32>(0.5f);
    o_1.normal_2 = vec4<f32>(Ng_2 * _S135 + _S135, packSlotNdv_0(draw_0.rampParams_0.w, dot(Ng_2, V_6)));
    if(((draw_0.misc_0.x) > 0.5f))
    {
        const _S136 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        o_1.normal_2.x = _S136.x;
        o_1.normal_2.y = _S136.y;
        o_1.normal_2.z = _S136.z;
    }
    o_1.depth_0 = packLinearDepth_0(viewDepth_2);
    return o_1;
}

struct pixelInput_0
{
    @location(0) worldPos_7 : vec3<f32>,
    @location(1) worldNorm_1 : vec3<f32>,
    @location(2) color_8 : vec4<f32>,
    @location(3) viewDepth_3 : f32,
    @location(4) uv_10 : vec2<f32>,
    @interpolate(flat, either) @location(5) diffuse_2 : vec4<f32>,
};

@fragment
fn fsMain( _S137 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> FsOut_0
{
    var _S138 : VSOut_0 = VSOut_0( pos_1, _S137.worldPos_7, _S137.worldNorm_1, _S137.color_8, _S137.viewDepth_3, _S137.uv_10, _S137.diffuse_2 );
    g_userDiscard_0 = false;
    var N_14 : vec3<f32>;
    var V_7 : vec3<f32>;
    var c_8 : vec4<f32> = shadeScene_0(_S138, &(N_14), &(V_7));
    var _S139 : bool;
    if(((draw_0.misc_0.y) < 0.5f))
    {
        _S139 = occluderFaded_0(pos_1.xy, _S137.worldPos_7, _S137.worldNorm_1);
    }
    else
    {
        _S139 = false;
    }
    if(_S139)
    {
        discard;
    }
    if(((draw_0.mapFlags_0.w) > 0.0f))
    {
        _S139 = ((baseTexel_0(_S137.uv_10).w) < (draw_0.mapFlags_0.w));
    }
    else
    {
        _S139 = false;
    }
    if(_S139)
    {
        discard;
    }
    if(g_userDiscard_0)
    {
        discard;
    }
    return packOutput_0(c_8.xyz, c_8.w, N_14, V_7, _S137.viewDepth_3, pos_1.xy);
}

