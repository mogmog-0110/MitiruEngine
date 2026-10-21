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
};

@binding(1) @group(0) var<uniform> draw_0 : DrawCB_std140_0;
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
};

@binding(0) @group(0) var<uniform> frame_0 : FrameCB_std140_0;
@binding(0) @group(1) var fogLineDepth_0 : texture_multisampled_2d<f32>;

@binding(1) @group(1) var matBase_0 : texture_2d<f32>;

@binding(5) @group(1) var matSamplerNearest_0 : sampler;

@binding(4) @group(1) var matSampler_0 : sampler;

@binding(2) @group(1) var matNormal_0 : texture_2d<f32>;

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

@binding(12) @group(0) var localTex_0 : texture_2d<f32>;

@binding(3) @group(1) var matEmissive_0 : texture_2d<f32>;

var<private> kOccluderBayer_0 : array<f32, i32(16)> = array<f32, i32(16)>( 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f );
fn rsqrt_0( x_0 : f32) -> f32
{
    return 1.0f / sqrt(x_0);
}

fn transformNormal_0( m_0 : mat3x3<f32>,  n_0 : vec3<f32>) -> vec3<f32>
{
    var c0_0 : vec3<f32> = cross(m_0[i32(1)], m_0[i32(2)]);
    var r_0 : vec3<f32> = vec3<f32>(dot(c0_0, n_0), dot(cross(m_0[i32(2)], m_0[i32(0)]), n_0), dot(cross(m_0[i32(0)], m_0[i32(1)]), n_0));
    var _S1 : f32;
    if(((dot(m_0[i32(0)], c0_0)) < 0.0f))
    {
        _S1 = -1.0f;
    }
    else
    {
        _S1 = 1.0f;
    }
    return normalize(r_0 * vec3<f32>(_S1));
}

fn srgbToLinear_0( c_0 : vec3<f32>) -> vec3<f32>
{
    var _S2 : vec3<f32> = vec3<f32>(max(max(c_0.x, c_0.y), max(c_0.z, 1.0f)));
    var _S3 : vec3<f32> = c_0 / _S2;
    var lo_0 : vec3<f32> = _S3 / vec3<f32>(12.920000076293945f);
    var hi_0 : vec3<f32> = pow((max(_S3, vec3<f32>(0.040449999272823334f, 0.040449999272823334f, 0.040449999272823334f)) + vec3<f32>(0.054999999701976776f)) / vec3<f32>(1.0549999475479126f), vec3<f32>(2.4000000953674316f, 2.4000000953674316f, 2.4000000953674316f));
    var _S4 : f32;
    if(((_S3.x) <= 0.040449999272823334f))
    {
        _S4 = lo_0.x;
    }
    else
    {
        _S4 = hi_0.x;
    }
    var _S5 : f32;
    if(((_S3.y) <= 0.040449999272823334f))
    {
        _S5 = lo_0.y;
    }
    else
    {
        _S5 = hi_0.y;
    }
    var _S6 : f32;
    if(((_S3.z) <= 0.040449999272823334f))
    {
        _S6 = lo_0.z;
    }
    else
    {
        _S6 = hi_0.z;
    }
    return vec3<f32>(_S4, _S5, _S6) * _S2;
}

struct VSOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) worldPos_0 : vec3<f32>,
    @location(1) worldNorm_0 : vec3<f32>,
    @location(2) color_0 : vec4<f32>,
    @location(3) viewDepth_0 : f32,
    @location(4) uv_0 : vec2<f32>,
};

struct vertexInput_0
{
    @location(0) pos_1 : vec3<f32>,
    @location(1) normal_0 : vec3<f32>,
    @location(2) uv_1 : vec2<f32>,
    @location(3) color_1 : vec4<f32>,
};

@vertex
fn vsMain( _S7 : vertexInput_0) -> VSOut_0
{
    var wp_0 : vec4<f32> = (((vec4<f32>(_S7.pos_1, 1.0f)) * (mat4x4<f32>(draw_0.world_0.data_0[i32(0)][i32(0)], draw_0.world_0.data_0[i32(1)][i32(0)], draw_0.world_0.data_0[i32(2)][i32(0)], draw_0.world_0.data_0[i32(3)][i32(0)], draw_0.world_0.data_0[i32(0)][i32(1)], draw_0.world_0.data_0[i32(1)][i32(1)], draw_0.world_0.data_0[i32(2)][i32(1)], draw_0.world_0.data_0[i32(3)][i32(1)], draw_0.world_0.data_0[i32(0)][i32(2)], draw_0.world_0.data_0[i32(1)][i32(2)], draw_0.world_0.data_0[i32(2)][i32(2)], draw_0.world_0.data_0[i32(3)][i32(2)], draw_0.world_0.data_0[i32(0)][i32(3)], draw_0.world_0.data_0[i32(1)][i32(3)], draw_0.world_0.data_0[i32(2)][i32(3)], draw_0.world_0.data_0[i32(3)][i32(3)]))));
    var o_0 : VSOut_0;
    o_0.worldPos_0 = wp_0.xyz;
    o_0.worldNorm_0 = transformNormal_0(mat3x3<f32>(vec3<f32>(draw_0.world_0.data_0[i32(0)][i32(0)], draw_0.world_0.data_0[i32(1)][i32(0)], draw_0.world_0.data_0[i32(2)][i32(0)]), vec3<f32>(draw_0.world_0.data_0[i32(0)][i32(1)], draw_0.world_0.data_0[i32(1)][i32(1)], draw_0.world_0.data_0[i32(2)][i32(1)]), vec3<f32>(draw_0.world_0.data_0[i32(0)][i32(2)], draw_0.world_0.data_0[i32(1)][i32(2)], draw_0.world_0.data_0[i32(2)][i32(2)])), _S7.normal_0);
    var _S8 : vec4<f32> = (((wp_0) * (mat4x4<f32>(frame_0.viewProj_0.data_0[i32(0)][i32(0)], frame_0.viewProj_0.data_0[i32(1)][i32(0)], frame_0.viewProj_0.data_0[i32(2)][i32(0)], frame_0.viewProj_0.data_0[i32(3)][i32(0)], frame_0.viewProj_0.data_0[i32(0)][i32(1)], frame_0.viewProj_0.data_0[i32(1)][i32(1)], frame_0.viewProj_0.data_0[i32(2)][i32(1)], frame_0.viewProj_0.data_0[i32(3)][i32(1)], frame_0.viewProj_0.data_0[i32(0)][i32(2)], frame_0.viewProj_0.data_0[i32(1)][i32(2)], frame_0.viewProj_0.data_0[i32(2)][i32(2)], frame_0.viewProj_0.data_0[i32(3)][i32(2)], frame_0.viewProj_0.data_0[i32(0)][i32(3)], frame_0.viewProj_0.data_0[i32(1)][i32(3)], frame_0.viewProj_0.data_0[i32(2)][i32(3)], frame_0.viewProj_0.data_0[i32(3)][i32(3)]))));
    o_0.pos_0 = _S8;
    o_0.viewDepth_0 = _S8.w;
    o_0.uv_0 = _S7.uv_1;
    o_0.color_0 = vec4<f32>(srgbToLinear_0(_S7.color_1.xyz), _S7.color_1.w);
    return o_0;
}

struct SilhouetteOut_0
{
    @builtin(position) pos_2 : vec4<f32>,
    @location(0) viewDepth_1 : f32,
};

struct vertexInput_1
{
    @location(0) pos_3 : vec3<f32>,
    @location(1) normal_1 : vec3<f32>,
    @location(2) uv_2 : vec2<f32>,
    @location(3) color_2 : vec4<f32>,
};

@vertex
fn vsSilhouette( _S9 : vertexInput_1) -> SilhouetteOut_0
{
    var rel_0 : vec3<f32> = (((vec4<f32>(_S9.pos_3, 1.0f)) * (mat4x4<f32>(draw_0.world_0.data_0[i32(0)][i32(0)], draw_0.world_0.data_0[i32(1)][i32(0)], draw_0.world_0.data_0[i32(2)][i32(0)], draw_0.world_0.data_0[i32(3)][i32(0)], draw_0.world_0.data_0[i32(0)][i32(1)], draw_0.world_0.data_0[i32(1)][i32(1)], draw_0.world_0.data_0[i32(2)][i32(1)], draw_0.world_0.data_0[i32(3)][i32(1)], draw_0.world_0.data_0[i32(0)][i32(2)], draw_0.world_0.data_0[i32(1)][i32(2)], draw_0.world_0.data_0[i32(2)][i32(2)], draw_0.world_0.data_0[i32(3)][i32(2)], draw_0.world_0.data_0[i32(0)][i32(3)], draw_0.world_0.data_0[i32(1)][i32(3)], draw_0.world_0.data_0[i32(2)][i32(3)], draw_0.world_0.data_0[i32(3)][i32(3)])))).xyz - frame_0.cameraPos_0.xyz;
    var d_0 : f32 = length(rel_0);
    var o_1 : SilhouetteOut_0;
    var _S10 : vec4<f32> = (((vec4<f32>(frame_0.cameraPos_0.xyz + rel_0 * vec3<f32>((max(d_0 - frame_0.fadeSilParams_0.x, 0.0010000000474974513f) / max(d_0, 9.99999997475242708e-07f))), 1.0f)) * (mat4x4<f32>(frame_0.viewProj_0.data_0[i32(0)][i32(0)], frame_0.viewProj_0.data_0[i32(1)][i32(0)], frame_0.viewProj_0.data_0[i32(2)][i32(0)], frame_0.viewProj_0.data_0[i32(3)][i32(0)], frame_0.viewProj_0.data_0[i32(0)][i32(1)], frame_0.viewProj_0.data_0[i32(1)][i32(1)], frame_0.viewProj_0.data_0[i32(2)][i32(1)], frame_0.viewProj_0.data_0[i32(3)][i32(1)], frame_0.viewProj_0.data_0[i32(0)][i32(2)], frame_0.viewProj_0.data_0[i32(1)][i32(2)], frame_0.viewProj_0.data_0[i32(2)][i32(2)], frame_0.viewProj_0.data_0[i32(3)][i32(2)], frame_0.viewProj_0.data_0[i32(0)][i32(3)], frame_0.viewProj_0.data_0[i32(1)][i32(3)], frame_0.viewProj_0.data_0[i32(2)][i32(3)], frame_0.viewProj_0.data_0[i32(3)][i32(3)]))));
    o_1.pos_2 = _S10;
    o_1.viewDepth_1 = _S10.w;
    return o_1;
}

fn neutralShoulder_0( c_1 : vec3<f32>) -> vec3<f32>
{
    var _S11 : f32 = max(c_1.x, max(c_1.y, c_1.z));
    if((_S11 <= 0.89999997615814209f))
    {
        return c_1;
    }
    return c_1 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S11 + 0.10000002384185791f - 0.89999997615814209f)) / _S11));
}

fn storeHdr_0( c_2 : vec3<f32>,  scale_0 : f32,  pixel_0 : vec2<f32>) -> vec3<f32>
{
    if((scale_0 >= 1.0f))
    {
        return c_2;
    }
    const _S12 : vec2<f32> = vec2<f32>(0.067110560834407806f, 0.0058371499180793762f);
    var n0_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0, _S12)));
    var n1_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0 + vec2<f32>(17.0f, 31.0f), _S12)));
    return neutralShoulder_0(c_2 * vec3<f32>(scale_0)) + vec3<f32>(n0_0 + n1_0 - 1.0f, n1_0 + n0_0 - 1.0f, n0_0 - n1_0) * vec3<f32>(0.00097751710563898087f);
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) viewDepth_2 : f32,
};

@fragment
fn fsSilhouette( _S13 : pixelInput_0, @builtin(sample_index) sampleIndex_0 : u32, @builtin(position) pos_4 : vec4<f32>) -> pixelOutput_0
{
    var _S14 : vec2<f32> = pos_4.xy;
    var e_0 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(_S14)), (i32(sampleIndex_0))));
    var v_0 : u32 = (((((u32(e_0.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_0.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_0.z * 255.0f + 0.5f)));
    if((v_0 >= u32(16777215)))
    {
        discard;
    }
    if(((f32(v_0) / 1.6777215e+07f * frame_0.viewInfo_0.x) >= (_S13.viewDepth_2)))
    {
        discard;
    }
    var _S15 : pixelOutput_0 = pixelOutput_0( vec4<f32>(storeHdr_0(frame_0.fadeSilColor_0.xyz, frame_0.lightInfo_0.y, _S14), frame_0.fadeSilColor_0.w) );
    return _S15;
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) viewDepth_3 : f32,
};

@fragment
fn fsSilhouetteCompat( _S16 : pixelInput_1, @builtin(position) pos_5 : vec4<f32>) -> pixelOutput_1
{
    var s_0 : i32 = i32(0);
    var hit_0 : i32 = i32(0);
    loop
    {
        if((s_0 < i32(4)))
        {
        }
        else
        {
            break;
        }
        var e_1 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(pos_5.xy)), (s_0)));
        var v_1 : u32 = (((((u32(e_1.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_1.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_1.z * 255.0f + 0.5f)));
        var sceneDepth_0 : f32 = f32(v_1) / 1.6777215e+07f * frame_0.viewInfo_0.x;
        var _S17 : bool;
        if((v_1 < u32(16777215)))
        {
            _S17 = (sceneDepth_0 < (_S16.viewDepth_3));
        }
        else
        {
            _S17 = false;
        }
        var _S18 : i32;
        if(_S17)
        {
            _S18 = i32(1);
        }
        else
        {
            _S18 = i32(0);
        }
        var hit_1 : i32 = hit_0 + _S18;
        s_0 = s_0 + i32(1);
        hit_0 = hit_1;
    }
    if(hit_0 == i32(0))
    {
        discard;
    }
    var _S19 : pixelOutput_1 = pixelOutput_1( vec4<f32>(storeHdr_0(frame_0.fadeSilColor_0.xyz, frame_0.lightInfo_0.y, pos_5.xy), frame_0.fadeSilColor_0.w * f32(hit_0) * 0.25f) );
    return _S19;
}

fn baseTexel_0( uv_3 : vec2<f32>) -> vec4<f32>
{
    if(((draw_0.mapFlags_0.x) < 0.5f))
    {
        return vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    }
    var t_0 : vec4<f32>;
    var _S20 : vec4<f32>;
    if(((draw_0.mapParams_0.y) > 0.5f))
    {
        _S20 = (textureSample((matBase_0), (matSamplerNearest_0), (uv_3)));
    }
    else
    {
        _S20 = (textureSample((matBase_0), (matSampler_0), (uv_3)));
    }
    t_0 = _S20;
    if(((draw_0.mapParams_0.z) > 0.5f))
    {
        var _S21 : vec3<f32> = srgbToLinear_0(t_0.xyz);
        t_0.x = _S21.x;
        t_0.y = _S21.y;
        t_0.z = _S21.z;
    }
    return t_0;
}

fn perturbNormal_0( N_0 : vec3<f32>,  worldPos_1 : vec3<f32>,  uv_4 : vec2<f32>) -> vec3<f32>
{
    if(((draw_0.mapFlags_0.y) < 0.5f))
    {
        return N_0;
    }
    var _S22 : vec2<f32> = ((textureSample((matNormal_0), (matSampler_0), (uv_4))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f)) * vec2<f32>(draw_0.mapParams_0.x);
    var xy_0 : vec2<f32> = _S22;
    xy_0[i32(1)] = - _S22.y;
    var tn_0 : vec3<f32> = vec3<f32>(xy_0, sqrt(saturate(1.0f - dot(xy_0, xy_0))));
    var duv1_0 : vec2<f32> = dpdx(uv_4);
    var duv2_0 : vec2<f32> = dpdy(uv_4);
    var dp2perp_0 : vec3<f32> = cross(dpdy(worldPos_1), N_0);
    var dp1perp_0 : vec3<f32> = cross(N_0, dpdx(worldPos_1));
    var T_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.x) + dp1perp_0 * vec3<f32>(duv2_0.x);
    var B_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.y) + dp1perp_0 * vec3<f32>(duv2_0.y);
    var invmax_0 : f32 = - rsqrt_0(max(max(dot(T_0, T_0), dot(B_0, B_0)), 9.99999968265522539e-21f));
    return normalize(T_0 * vec3<f32>((invmax_0 * tn_0.x)) + B_0 * vec3<f32>((invmax_0 * tn_0.y)) + N_0 * vec3<f32>(tn_0.z));
}

fn decalEdge_0( x_1 : f32,  soft_0 : f32) -> f32
{
    var _S23 : f32;
    if((soft_0 > 0.0f))
    {
        _S23 = saturate((0.5f - abs(x_1)) / (0.5f * soft_0));
    }
    else
    {
        _S23 = 1.0f;
    }
    return _S23;
}

fn decalDisc_0( p_0 : vec2<f32>,  soft_1 : f32) -> f32
{
    var r_1 : f32 = length(p_0) * 2.0f;
    var _S24 : f32;
    if((soft_1 > 0.0f))
    {
        _S24 = saturate((1.0f - r_1) / soft_1);
    }
    else
    {
        _S24 = step(r_1, 1.0f);
    }
    return _S24;
}

fn applyDecals_0( worldPos_2 : vec3<f32>,  Ng_0 : vec3<f32>,  albedo_0 : ptr<function, vec3<f32>>,  N_1 : ptr<function, vec3<f32>>)
{
    var _S25 : u32 = u32(frame_0.decalInfo_0.x + 0.5f);
    var _S26 : vec3<f32> = dpdx(worldPos_2);
    var _S27 : vec3<f32> = dpdy(worldPos_2);
    var i_0 : u32 = u32(0);
    loop
    {
        if((i_0 < _S25))
        {
        }
        else
        {
            break;
        }
        var row_0 : i32 = i32(i_0);
        var _S28 : vec3<i32> = vec3<i32>(i32(0), row_0, i32(0));
        var toDecal0_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S28)).xy, ((_S28)).z));
        var _S29 : vec3<i32> = vec3<i32>(i32(1), row_0, i32(0));
        var toDecal1_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S29)).xy, ((_S29)).z));
        var _S30 : vec3<i32> = vec3<i32>(i32(2), row_0, i32(0));
        var _S31 : vec3<i32> = vec3<i32>(i32(3), row_0, i32(0));
        var dcolor_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S31)).xy, ((_S31)).z));
        var _S32 : vec3<i32> = vec3<i32>(i32(4), row_0, i32(0));
        var axisZ_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S32)).xy, ((_S32)).z));
        var _S33 : vec3<i32> = vec3<i32>(i32(5), row_0, i32(0));
        var axisX_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S33)).xy, ((_S33)).z));
        var _S34 : vec3<i32> = vec3<i32>(i32(6), row_0, i32(0));
        var params_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S34)).xy, ((_S34)).z));
        var _S35 : vec3<i32> = vec3<i32>(i32(7), row_0, i32(0));
        var fade_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S35)).xy, ((_S35)).z));
        var wp_1 : vec4<f32> = vec4<f32>(worldPos_2, 1.0f);
        var _S36 : f32 = dot(toDecal0_0, wp_1);
        var _S37 : f32 = dot(toDecal1_0, wp_1);
        var _S38 : f32 = dot((textureLoad((decalTex_0), ((_S30)).xy, ((_S30)).z)), wp_1);
        if((any((((abs(vec3<f32>(_S36, _S37, _S38))) > vec3<f32>(0.5f, 0.5f, 0.5f))))))
        {
            i_0 = i_0 + u32(1);
            continue;
        }
        var _S39 : f32 = fade_0.y;
        var soft_2 : f32 = axisZ_0.w;
        var a_0 : f32 = dcolor_0.w * saturate((dot(Ng_0, axisZ_0.xyz) - _S39) / max(fade_0.x - _S39, 9.9999997473787516e-05f)) * decalEdge_0(_S38, soft_2);
        var _S40 : f32 = _S36 + 0.5f;
        var _S41 : f32 = 0.5f - _S37;
        var _S42 : f32 = params_0.x;
        var uvw_0 : vec3<f32> = vec3<f32>(_S40, _S41, _S42);
        var _S43 : vec3<f32> = toDecal0_0.xyz;
        var _S44 : vec3<f32> = toDecal1_0.xyz;
        var gx_0 : vec2<f32> = vec2<f32>(dot(_S43, _S26), - dot(_S44, _S26));
        var gy_0 : vec2<f32> = vec2<f32>(dot(_S43, _S27), - dot(_S44, _S27));
        var _S45 : vec4<f32> = vec4<f32>(1.0f, 1.0f, 1.0f, decalDisc_0(vec2<f32>(_S36, _S37), soft_2));
        var tex_0 : vec4<f32>;
        if((_S42 >= 0.0f))
        {
            tex_0 = (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((uvw_0)).xy, i32(((uvw_0)).z), (gx_0), (gy_0)));
        }
        else
        {
            tex_0 = _S45;
        }
        var a_1 : f32 = a_0 * tex_0.w;
        if((a_1 <= 0.0f))
        {
            i_0 = i_0 + u32(1);
            continue;
        }
        (*albedo_0) = mix((*albedo_0), dcolor_0.xyz * tex_0.xyz, vec3<f32>(a_1));
        var _S46 : f32 = params_0.y;
        if((_S46 >= 0.0f))
        {
            var _S47 : vec3<f32> = vec3<f32>(vec2<f32>(_S40, _S41), _S46);
            var xy_1 : vec2<f32> = (textureSampleGrad((vfxData_0), (vfxSampler_0), ((_S47)).xy, i32(((_S47)).z), (gx_0), (gy_0))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f);
            var _S48 : vec3<f32> = axisX_0.xyz;
            var T_1 : vec3<f32> = _S48 - (*N_1) * vec3<f32>(dot(_S48, (*N_1)));
            var T_2 : vec3<f32>;
            if(((dot(T_1, T_1)) > 9.99999993922529029e-09f))
            {
                T_2 = normalize(T_1);
            }
            else
            {
                T_2 = _S48;
            }
            (*N_1) = normalize(mix((*N_1), normalize(T_2 * vec3<f32>(xy_1.x) + cross((*N_1), T_2) * vec3<f32>(xy_1.y) + (*N_1) * vec3<f32>(sqrt(saturate(1.0f - dot(xy_1, xy_1))))), vec3<f32>((a_1 * axisX_0.w))));
        }
        i_0 = i_0 + u32(1);
    }
    return;
}

fn applyDecals_1( worldPos_3 : vec3<f32>,  Ng_1 : vec3<f32>,  albedo_1 : ptr<function, vec3<f32>>,  N_2 : ptr<function, vec3<f32>>)
{
    var _S49 : u32 = u32(frame_0.decalInfo_0.x + 0.5f);
    var _S50 : vec3<f32> = dpdx(worldPos_3);
    var _S51 : vec3<f32> = dpdy(worldPos_3);
    var i_1 : u32 = u32(0);
    loop
    {
        if((i_1 < _S49))
        {
        }
        else
        {
            break;
        }
        var row_1 : i32 = i32(i_1);
        var _S52 : vec3<i32> = vec3<i32>(i32(0), row_1, i32(0));
        var toDecal0_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S52)).xy, ((_S52)).z));
        var _S53 : vec3<i32> = vec3<i32>(i32(1), row_1, i32(0));
        var toDecal1_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S53)).xy, ((_S53)).z));
        var _S54 : vec3<i32> = vec3<i32>(i32(2), row_1, i32(0));
        var _S55 : vec3<i32> = vec3<i32>(i32(3), row_1, i32(0));
        var dcolor_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S55)).xy, ((_S55)).z));
        var _S56 : vec3<i32> = vec3<i32>(i32(4), row_1, i32(0));
        var axisZ_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S56)).xy, ((_S56)).z));
        var _S57 : vec3<i32> = vec3<i32>(i32(5), row_1, i32(0));
        var axisX_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S57)).xy, ((_S57)).z));
        var _S58 : vec3<i32> = vec3<i32>(i32(6), row_1, i32(0));
        var params_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S58)).xy, ((_S58)).z));
        var _S59 : vec3<i32> = vec3<i32>(i32(7), row_1, i32(0));
        var fade_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S59)).xy, ((_S59)).z));
        var wp_2 : vec4<f32> = vec4<f32>(worldPos_3, 1.0f);
        var _S60 : f32 = dot(toDecal0_1, wp_2);
        var _S61 : f32 = dot(toDecal1_1, wp_2);
        var _S62 : f32 = dot((textureLoad((decalTex_0), ((_S54)).xy, ((_S54)).z)), wp_2);
        if((any((((abs(vec3<f32>(_S60, _S61, _S62))) > vec3<f32>(0.5f, 0.5f, 0.5f))))))
        {
            i_1 = i_1 + u32(1);
            continue;
        }
        var _S63 : f32 = fade_1.y;
        var soft_3 : f32 = axisZ_1.w;
        var a_2 : f32 = dcolor_1.w * saturate((dot(Ng_1, axisZ_1.xyz) - _S63) / max(fade_1.x - _S63, 9.9999997473787516e-05f)) * decalEdge_0(_S62, soft_3);
        var _S64 : f32 = _S60 + 0.5f;
        var _S65 : f32 = 0.5f - _S61;
        var _S66 : f32 = params_1.x;
        var uvw_1 : vec3<f32> = vec3<f32>(_S64, _S65, _S66);
        var _S67 : vec3<f32> = toDecal0_1.xyz;
        var _S68 : vec3<f32> = toDecal1_1.xyz;
        var gx_1 : vec2<f32> = vec2<f32>(dot(_S67, _S50), - dot(_S68, _S50));
        var gy_1 : vec2<f32> = vec2<f32>(dot(_S67, _S51), - dot(_S68, _S51));
        var _S69 : vec4<f32> = vec4<f32>(1.0f, 1.0f, 1.0f, decalDisc_0(vec2<f32>(_S60, _S61), soft_3));
        var tex_1 : vec4<f32>;
        if((_S66 >= 0.0f))
        {
            tex_1 = (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((uvw_1)).xy, i32(((uvw_1)).z), (gx_1), (gy_1)));
        }
        else
        {
            tex_1 = _S69;
        }
        var a_3 : f32 = a_2 * tex_1.w;
        if((a_3 <= 0.0f))
        {
            i_1 = i_1 + u32(1);
            continue;
        }
        (*albedo_1) = mix((*albedo_1), dcolor_1.xyz * tex_1.xyz, vec3<f32>(a_3));
        var _S70 : f32 = params_1.y;
        if((_S70 >= 0.0f))
        {
            var _S71 : vec3<f32> = vec3<f32>(vec2<f32>(_S64, _S65), _S70);
            var xy_2 : vec2<f32> = (textureSampleGrad((vfxData_0), (vfxSampler_0), ((_S71)).xy, i32(((_S71)).z), (gx_1), (gy_1))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f);
            var _S72 : vec3<f32> = axisX_1.xyz;
            var T_3 : vec3<f32> = _S72 - (*N_2) * vec3<f32>(dot(_S72, (*N_2)));
            var T_4 : vec3<f32>;
            if(((dot(T_3, T_3)) > 9.99999993922529029e-09f))
            {
                T_4 = normalize(T_3);
            }
            else
            {
                T_4 = _S72;
            }
            (*N_2) = normalize(mix((*N_2), normalize(T_4 * vec3<f32>(xy_2.x) + cross((*N_2), T_4) * vec3<f32>(xy_2.y) + (*N_2) * vec3<f32>(sqrt(saturate(1.0f - dot(xy_2, xy_2))))), vec3<f32>((a_3 * axisX_1.w))));
        }
        i_1 = i_1 + u32(1);
    }
    return;
}

fn shadowSlope_0( N_3 : vec3<f32>,  L_0 : vec3<f32>) -> f32
{
    var _S73 : f32 = max(saturate(dot(N_3, L_0)), 0.10000000149011612f);
    return min(sqrt(1.0f - _S73 * _S73) / _S73, 6.0f);
}

fn shadowLookup_0( vp_0 : mat4x4<f32>,  cinfo_0 : vec4<f32>,  P_0 : vec3<f32>,  N_4 : vec3<f32>,  slope_0 : f32,  ndc_0 : ptr<function, vec3<f32>>,  bias_0 : ptr<function, f32>)
{
    var _S74 : f32 = max(frame_0.shadowSplit_0.z, 1.0f);
    var q_0 : vec3<f32>;
    if(((frame_0.shadowSplit_0.w) > 0.0f))
    {
        (*bias_0) = frame_0.shadowSplit_0.w * (1.0f + _S74 * slope_0);
        q_0 = P_0;
    }
    else
    {
        var _S75 : f32 = cinfo_0.x;
        var _S76 : vec3<f32> = P_0 + N_4 * vec3<f32>(_S75);
        (*bias_0) = cinfo_0.y * _S75 * (0.5f + _S74 * slope_0);
        q_0 = _S76;
    }
    var c_3 : vec4<f32> = (((vec4<f32>(q_0, 1.0f)) * (vp_0)));
    (*ndc_0) = c_3.xyz / vec3<f32>(max(c_3.w, 9.9999997473787516e-05f));
    return;
}

fn pcfNear_0( uv_5 : vec2<f32>,  depthRef_0 : f32,  texel_0 : vec2<f32>) -> f32
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
                var shadow_1 : f32 = shadow_0 + (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_5 + vec2<f32>(f32(x_2), f32(y_0)) * texel_0), (depthRef_0)));
                x_2 = x_2 + i32(1);
                shadow_0 = shadow_1;
            }
            y_0 = y_0 + i32(1);
        }
        return shadow_0 / 9.0f;
    }
    var _S77 : vec2<f32> = texel_0 * vec2<f32>(0.5f);
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
            var _S78 : f32 = f32(x_2);
            var _S79 : f32 = f32(y_0);
            var shadow_2 : f32 = shadow_0 + (3.0f - abs(_S78)) * (3.0f - abs(_S79)) * (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_5 + vec2<f32>(_S78, _S79) * _S77), (depthRef_0)));
            x_2 = x_2 + i32(1);
            shadow_0 = shadow_2;
        }
        y_0 = y_0 + i32(1);
    }
    return shadow_0 / 81.0f;
}

fn samplePcfNear_0( ndc_1 : vec3<f32>,  bias_1 : f32) -> f32
{
    var _S80 : f32 = ndc_1.x * 0.5f + 0.5f;
    var _S81 : f32 = - ndc_1.y * 0.5f + 0.5f;
    var uv_6 : vec2<f32> = vec2<f32>(_S80, _S81);
    var _S82 : f32 = ndc_1.z;
    var depthRef_1 : f32 = _S82 - bias_1;
    var _S83 : bool;
    if((_S80 < 0.0f))
    {
        _S83 = true;
    }
    else
    {
        _S83 = (_S81 < 0.0f);
    }
    if(_S83)
    {
        _S83 = true;
    }
    else
    {
        _S83 = (_S80 > 1.0f);
    }
    if(_S83)
    {
        _S83 = true;
    }
    else
    {
        _S83 = (_S81 > 1.0f);
    }
    if(_S83)
    {
        _S83 = true;
    }
    else
    {
        _S83 = (_S82 < 0.0f);
    }
    if(_S83)
    {
        _S83 = true;
    }
    else
    {
        _S83 = (_S82 > 1.0f);
    }
    if(_S83)
    {
        return 1.0f;
    }
    var t_1 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.x;
    return pcfNear_0(uv_6, depthRef_1, vec2<f32>(t_1, t_1));
}

fn pcfAtlas_0( uv_7 : vec2<f32>,  depthRef_2 : f32,  texel_1 : vec2<f32>,  uMin_0 : f32,  uMax_0 : f32) -> f32
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
                var shadow_4 : f32 = shadow_3 + (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_7.x + f32(x_3) * texel_1.x, uMin_0, uMax_0), uv_7.y + f32(y_1) * texel_1.y)), (depthRef_2)));
                x_3 = x_3 + i32(1);
                shadow_3 = shadow_4;
            }
            y_1 = y_1 + i32(1);
        }
        return shadow_3 / 9.0f;
    }
    var _S84 : vec2<f32> = texel_1 * vec2<f32>(0.5f);
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
            var _S85 : f32 = f32(x_3);
            var _S86 : f32 = f32(y_1);
            var shadow_5 : f32 = shadow_3 + (3.0f - abs(_S85)) * (3.0f - abs(_S86)) * (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_7.x + _S85 * _S84.x, uMin_0, uMax_0), uv_7.y + _S86 * _S84.y)), (depthRef_2)));
            x_3 = x_3 + i32(1);
            shadow_3 = shadow_5;
        }
        y_1 = y_1 + i32(1);
    }
    return shadow_3 / 81.0f;
}

fn samplePcfAtlas_0( ndc_2 : vec3<f32>,  column_0 : f32,  bias_2 : f32) -> f32
{
    var _S87 : f32 = ndc_2.x * 0.5f + 0.5f;
    var _S88 : f32 = - ndc_2.y * 0.5f + 0.5f;
    var _S89 : f32 = ndc_2.z;
    var depthRef_3 : f32 = _S89 - bias_2;
    var _S90 : bool;
    if((_S87 < 0.0f))
    {
        _S90 = true;
    }
    else
    {
        _S90 = (_S88 < 0.0f);
    }
    if(_S90)
    {
        _S90 = true;
    }
    else
    {
        _S90 = (_S87 > 1.0f);
    }
    if(_S90)
    {
        _S90 = true;
    }
    else
    {
        _S90 = (_S88 > 1.0f);
    }
    if(_S90)
    {
        _S90 = true;
    }
    else
    {
        _S90 = (_S89 < 0.0f);
    }
    if(_S90)
    {
        _S90 = true;
    }
    else
    {
        _S90 = (_S89 > 1.0f);
    }
    if(_S90)
    {
        return 1.0f;
    }
    var texelV_0 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.y;
    var texelU_0 : f32 = texelV_0 * 0.5f;
    var _S91 : f32 = texelU_0 * 0.5f;
    return pcfAtlas_0(vec2<f32>((_S87 + column_0) * 0.5f, _S88), depthRef_3, vec2<f32>(texelU_0, texelV_0), column_0 * 0.5f + _S91, (column_0 + 1.0f) * 0.5f - _S91);
}

fn sampleCascadedShadow_0( worldPos_4 : vec3<f32>,  N_5 : vec3<f32>,  L_1 : vec3<f32>,  distanceFromCamera_0 : f32) -> f32
{
    if(((frame_0.shadowTexel_0.z) < 0.5f))
    {
        return 1.0f;
    }
    var slope_1 : f32 = shadowSlope_0(N_5, L_1);
    var ndc_3 : vec3<f32>;
    var bias_3 : f32;
    var _S92 : bool;
    if(((frame_0.shadowTexel_0.z) < 1.5f))
    {
        _S92 = true;
    }
    else
    {
        _S92 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.x));
    }
    if(_S92)
    {
        shadowLookup_0(mat4x4<f32>(frame_0.shadowVP0_0.data_0[i32(0)][i32(0)], frame_0.shadowVP0_0.data_0[i32(1)][i32(0)], frame_0.shadowVP0_0.data_0[i32(2)][i32(0)], frame_0.shadowVP0_0.data_0[i32(3)][i32(0)], frame_0.shadowVP0_0.data_0[i32(0)][i32(1)], frame_0.shadowVP0_0.data_0[i32(1)][i32(1)], frame_0.shadowVP0_0.data_0[i32(2)][i32(1)], frame_0.shadowVP0_0.data_0[i32(3)][i32(1)], frame_0.shadowVP0_0.data_0[i32(0)][i32(2)], frame_0.shadowVP0_0.data_0[i32(1)][i32(2)], frame_0.shadowVP0_0.data_0[i32(2)][i32(2)], frame_0.shadowVP0_0.data_0[i32(3)][i32(2)], frame_0.shadowVP0_0.data_0[i32(0)][i32(3)], frame_0.shadowVP0_0.data_0[i32(1)][i32(3)], frame_0.shadowVP0_0.data_0[i32(2)][i32(3)], frame_0.shadowVP0_0.data_0[i32(3)][i32(3)]), frame_0.shadowC0_0, worldPos_4, N_5, slope_1, &(ndc_3), &(bias_3));
        return samplePcfNear_0(ndc_3, bias_3);
    }
    if(((frame_0.shadowTexel_0.z) < 2.5f))
    {
        _S92 = true;
    }
    else
    {
        _S92 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.y));
    }
    if(_S92)
    {
        shadowLookup_0(mat4x4<f32>(frame_0.shadowVP1_0.data_0[i32(0)][i32(0)], frame_0.shadowVP1_0.data_0[i32(1)][i32(0)], frame_0.shadowVP1_0.data_0[i32(2)][i32(0)], frame_0.shadowVP1_0.data_0[i32(3)][i32(0)], frame_0.shadowVP1_0.data_0[i32(0)][i32(1)], frame_0.shadowVP1_0.data_0[i32(1)][i32(1)], frame_0.shadowVP1_0.data_0[i32(2)][i32(1)], frame_0.shadowVP1_0.data_0[i32(3)][i32(1)], frame_0.shadowVP1_0.data_0[i32(0)][i32(2)], frame_0.shadowVP1_0.data_0[i32(1)][i32(2)], frame_0.shadowVP1_0.data_0[i32(2)][i32(2)], frame_0.shadowVP1_0.data_0[i32(3)][i32(2)], frame_0.shadowVP1_0.data_0[i32(0)][i32(3)], frame_0.shadowVP1_0.data_0[i32(1)][i32(3)], frame_0.shadowVP1_0.data_0[i32(2)][i32(3)], frame_0.shadowVP1_0.data_0[i32(3)][i32(3)]), frame_0.shadowC1_0, worldPos_4, N_5, slope_1, &(ndc_3), &(bias_3));
        return samplePcfAtlas_0(ndc_3, 0.0f, bias_3);
    }
    shadowLookup_0(mat4x4<f32>(frame_0.shadowVP2_0.data_0[i32(0)][i32(0)], frame_0.shadowVP2_0.data_0[i32(1)][i32(0)], frame_0.shadowVP2_0.data_0[i32(2)][i32(0)], frame_0.shadowVP2_0.data_0[i32(3)][i32(0)], frame_0.shadowVP2_0.data_0[i32(0)][i32(1)], frame_0.shadowVP2_0.data_0[i32(1)][i32(1)], frame_0.shadowVP2_0.data_0[i32(2)][i32(1)], frame_0.shadowVP2_0.data_0[i32(3)][i32(1)], frame_0.shadowVP2_0.data_0[i32(0)][i32(2)], frame_0.shadowVP2_0.data_0[i32(1)][i32(2)], frame_0.shadowVP2_0.data_0[i32(2)][i32(2)], frame_0.shadowVP2_0.data_0[i32(3)][i32(2)], frame_0.shadowVP2_0.data_0[i32(0)][i32(3)], frame_0.shadowVP2_0.data_0[i32(1)][i32(3)], frame_0.shadowVP2_0.data_0[i32(2)][i32(3)], frame_0.shadowVP2_0.data_0[i32(3)][i32(3)]), frame_0.shadowC2_0, worldPos_4, N_5, slope_1, &(ndc_3), &(bias_3));
    return samplePcfAtlas_0(ndc_3, 1.0f, bias_3);
}

fn hemisphereAmbient_0( N_6 : vec3<f32>) -> vec3<f32>
{
    return mix(frame_0.ambientGround_0.xyz, frame_0.ambientSky_0.xyz, vec3<f32>((N_6.y * 0.5f + 0.5f)));
}

fn giProbeTexel_0( probe_0 : u32,  k_0 : u32) -> vec4<f32>
{
    var _S93 : vec3<i32> = vec3<i32>(i32(((probe_0 & (u32(255)))) * u32(7) + k_0), i32((probe_0 >> (u32(8)))), i32(0));
    return (textureLoad((giProbes_0), ((_S93)).xy, ((_S93)).z));
}

fn giOctEncode_0( n_1 : vec3<f32>) -> vec2<f32>
{
    var _S94 : f32 = n_1.x;
    var _S95 : f32 = n_1.z;
    var _S96 : f32 = n_1.y;
    var uv_8 : vec2<f32> = vec2<f32>(_S94, _S95) / vec2<f32>((abs(_S94) + abs(_S96) + abs(_S95)));
    var uv_9 : vec2<f32>;
    if((_S96 < 0.0f))
    {
        var _S97 : f32;
        if(((uv_8.x) >= 0.0f))
        {
            _S97 = 1.0f;
        }
        else
        {
            _S97 = -1.0f;
        }
        var _S98 : f32;
        if(((uv_8.y) >= 0.0f))
        {
            _S98 = 1.0f;
        }
        else
        {
            _S98 = -1.0f;
        }
        uv_9 = (vec2<f32>(1.0f, 1.0f) - abs(uv_8.yx)) * vec2<f32>(_S97, _S98);
    }
    else
    {
        uv_9 = uv_8;
    }
    return uv_9;
}

fn giVisibilityAt_0( probe_1 : u32,  dir_0 : vec3<f32>) -> vec2<f32>
{
    var tiles_0 : u32 = u32(frame_0.giDims_0.w);
    var _S99 : u32 = probe_1 % tiles_0;
    var _S100 : f32 = f32(_S99);
    var _S101 : u32 = probe_1 / tiles_0;
    var _S102 : vec2<f32> = vec2<f32>(0.5f);
    return (textureSampleLevel((giVisibility_0), (giSampler_0), ((vec2<f32>(_S100, f32(_S101)) * vec2<f32>(18.0f) + vec2<f32>(1.0f) + (giOctEncode_0(dir_0) * _S102 + _S102) * vec2<f32>(16.0f)) * frame_0.giParams_0.zw), (0.0f)).xy);
}

fn giProbeIrradiance_0( probe_2 : u32,  n_2 : vec3<f32>) -> vec3<f32>
{
    var a0_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(0));
    var a1_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(1));
    var a2_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(2));
    var a3_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(3));
    var a4_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(4));
    var a5_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(5));
    var _S103 : f32 = n_2.y;
    var _S104 : f32 = n_2.z;
    var _S105 : f32 = n_2.x;
    var _S106 : f32 = 1.0925480127334595f * _S105;
    return max(a0_0.xyz * vec3<f32>(0.28209498524665833f) + vec3<f32>(a0_0.w, a1_0.xy) * vec3<f32>((0.48860299587249756f * _S103)) + vec3<f32>(a1_0.zw, a2_0.x) * vec3<f32>((0.48860299587249756f * _S104)) + a2_0.yzw * vec3<f32>((0.48860299587249756f * _S105)) + a3_0.xyz * vec3<f32>((_S106 * _S103)) + vec3<f32>(a3_0.w, a4_0.xy) * vec3<f32>((1.0925480127334595f * _S103 * _S104)) + vec3<f32>(a4_0.zw, a5_0.x) * vec3<f32>((0.31539198756217957f * (3.0f * _S104 * _S104 - 1.0f))) + a5_0.yzw * vec3<f32>((_S106 * _S104)) + giProbeTexel_0(probe_2, u32(6)).xyz * vec3<f32>((0.54627400636672974f * (_S105 * _S105 - _S103 * _S103))), vec3<f32>(0.0f, 0.0f, 0.0f));
}

fn giIrradiance_0( P_1 : vec3<f32>,  N_7 : vec3<f32>,  V_0 : vec3<f32>,  ok_0 : ptr<function, bool>) -> vec3<f32>
{
    var _S107 : vec3<i32> = vec3<i32>(frame_0.giDims_0.xyz);
    const _S108 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S109 : vec3<i32> = _S107 - vec3<i32>(i32(1), i32(1), i32(1));
    var c_4 : vec3<f32> = clamp((P_1 + (N_7 * vec3<f32>(0.20000000298023224f) + V_0 * vec3<f32>(0.80000001192092896f)) * vec3<f32>(frame_0.giParams_0.x) - frame_0.giOrigin_0.xyz) / frame_0.giSpacing_0.xyz, _S108, vec3<f32>(_S109));
    var _S110 : vec3<f32> = frame_0.giOrigin_0.xyz + c_4 * frame_0.giSpacing_0.xyz;
    var _S111 : vec3<i32> = min(vec3<i32>(floor(c_4)), max(_S107 - vec3<i32>(i32(2), i32(2), i32(2)), vec3<i32>(i32(0), i32(0), i32(0))));
    var _S112 : vec3<f32> = saturate(c_4 - vec3<f32>(_S111));
    var i_2 : i32 = i32(0);
    var sum_0 : vec3<f32> = _S108;
    var wsum_0 : f32 = 0.0f;
    loop
    {
        if((i_2 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var off_0 : vec3<i32> = vec3<i32>((i_2 & (i32(1))), (((i_2 >> (u32(1)))) & (i32(1))), (((i_2 >> (u32(2)))) & (i32(1))));
        var _S113 : vec3<i32> = min(_S111 + off_0, _S109);
        var idx_0 : u32 = u32(_S113.x + _S107.x * (_S113.y + _S107.y * _S113.z));
        if(((giProbeTexel_0(idx_0, u32(6)).w) < 0.5f))
        {
            i_2 = i_2 + i32(1);
            continue;
        }
        var probeP_0 : vec3<f32> = frame_0.giOrigin_0.xyz + vec3<f32>(_S113) * frame_0.giSpacing_0.xyz;
        var tri_0 : vec3<f32> = mix(vec3<f32>(1.0f, 1.0f, 1.0f) - _S112, _S112, vec3<f32>(off_0));
        var wrap_0 : f32 = (dot(normalize(probeP_0 - P_1 + vec3<f32>(9.99999997475242708e-07f, 9.99999997475242708e-07f, 9.99999997475242708e-07f)), N_7) + 1.0f) * 0.5f;
        var w_0 : f32 = wrap_0 * wrap_0 + 0.20000000298023224f;
        var toPoint_0 : vec3<f32> = _S110 - probeP_0;
        var r_2 : f32 = length(toPoint_0);
        var _S114 : vec3<f32>;
        if((r_2 > 9.9999997473787516e-05f))
        {
            _S114 = toPoint_0 / vec3<f32>(r_2);
        }
        else
        {
            _S114 = vec3<f32>(0.0f, 1.0f, 0.0f);
        }
        var m_1 : vec2<f32> = giVisibilityAt_0(idx_0, _S114);
        var _S115 : f32 = m_1.x;
        var w_1 : f32;
        if((r_2 > _S115))
        {
            var var_0 : f32 = abs(m_1.y - _S115 * _S115);
            var d_1 : f32 = r_2 - _S115;
            var ch_0 : f32 = var_0 / (var_0 + d_1 * d_1);
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
        i_2 = i_2 + i32(1);
    }
    var _S116 : bool = (wsum_0 > 9.99999997475242708e-07f);
    (*ok_0) = _S116;
    if(_S116)
    {
        sum_0 = sum_0 / vec3<f32>(wsum_0);
    }
    else
    {
        sum_0 = _S108;
    }
    return sum_0;
}

fn giIrradiance_1( P_2 : vec3<f32>,  N_8 : vec3<f32>,  V_1 : vec3<f32>,  ok_1 : ptr<function, bool>) -> vec3<f32>
{
    var _S117 : vec3<i32> = vec3<i32>(frame_0.giDims_0.xyz);
    const _S118 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S119 : vec3<i32> = _S117 - vec3<i32>(i32(1), i32(1), i32(1));
    var c_5 : vec3<f32> = clamp((P_2 + (N_8 * vec3<f32>(0.20000000298023224f) + V_1 * vec3<f32>(0.80000001192092896f)) * vec3<f32>(frame_0.giParams_0.x) - frame_0.giOrigin_0.xyz) / frame_0.giSpacing_0.xyz, _S118, vec3<f32>(_S119));
    var _S120 : vec3<f32> = frame_0.giOrigin_0.xyz + c_5 * frame_0.giSpacing_0.xyz;
    var _S121 : vec3<i32> = min(vec3<i32>(floor(c_5)), max(_S117 - vec3<i32>(i32(2), i32(2), i32(2)), vec3<i32>(i32(0), i32(0), i32(0))));
    var _S122 : vec3<f32> = saturate(c_5 - vec3<f32>(_S121));
    var i_3 : i32 = i32(0);
    var sum_1 : vec3<f32> = _S118;
    var wsum_2 : f32 = 0.0f;
    loop
    {
        if((i_3 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var off_1 : vec3<i32> = vec3<i32>((i_3 & (i32(1))), (((i_3 >> (u32(1)))) & (i32(1))), (((i_3 >> (u32(2)))) & (i32(1))));
        var _S123 : vec3<i32> = min(_S121 + off_1, _S119);
        var idx_1 : u32 = u32(_S123.x + _S117.x * (_S123.y + _S117.y * _S123.z));
        if(((giProbeTexel_0(idx_1, u32(6)).w) < 0.5f))
        {
            i_3 = i_3 + i32(1);
            continue;
        }
        var probeP_1 : vec3<f32> = frame_0.giOrigin_0.xyz + vec3<f32>(_S123) * frame_0.giSpacing_0.xyz;
        var tri_1 : vec3<f32> = mix(vec3<f32>(1.0f, 1.0f, 1.0f) - _S122, _S122, vec3<f32>(off_1));
        var wrap_1 : f32 = (dot(normalize(probeP_1 - P_2 + vec3<f32>(9.99999997475242708e-07f, 9.99999997475242708e-07f, 9.99999997475242708e-07f)), N_8) + 1.0f) * 0.5f;
        var w_4 : f32 = wrap_1 * wrap_1 + 0.20000000298023224f;
        var toPoint_1 : vec3<f32> = _S120 - probeP_1;
        var r_3 : f32 = length(toPoint_1);
        var _S124 : vec3<f32>;
        if((r_3 > 9.9999997473787516e-05f))
        {
            _S124 = toPoint_1 / vec3<f32>(r_3);
        }
        else
        {
            _S124 = vec3<f32>(0.0f, 1.0f, 0.0f);
        }
        var m_2 : vec2<f32> = giVisibilityAt_0(idx_1, _S124);
        var _S125 : f32 = m_2.x;
        var w_5 : f32;
        if((r_3 > _S125))
        {
            var var_1 : f32 = abs(m_2.y - _S125 * _S125);
            var d_2 : f32 = r_3 - _S125;
            var ch_1 : f32 = var_1 / (var_1 + d_2 * d_2);
            w_5 = w_4 * (ch_1 * ch_1 * ch_1);
        }
        else
        {
            w_5 = w_4;
        }
        var w_6 : f32;
        if((w_5 < 0.20000000298023224f))
        {
            w_6 = w_5 * (w_5 * w_5 * 25.0f);
        }
        else
        {
            w_6 = w_5;
        }
        var w_7 : f32 = w_6 * (tri_1.x * tri_1.y * tri_1.z);
        var wsum_3 : f32 = wsum_2 + w_7;
        sum_1 = sum_1 + giProbeIrradiance_0(idx_1, N_8) * vec3<f32>(w_7);
        wsum_2 = wsum_3;
        i_3 = i_3 + i32(1);
    }
    var _S126 : bool = (wsum_2 > 9.99999997475242708e-07f);
    (*ok_1) = _S126;
    if(_S126)
    {
        sum_1 = sum_1 / vec3<f32>(wsum_2);
    }
    else
    {
        sum_1 = _S118;
    }
    return sum_1;
}

fn giToonCoord_0( e_2 : vec3<f32>) -> f32
{
    var bands_0 : f32 = frame_0.giParams_0.y;
    var _S127 : f32 = max(max(e_2.x, e_2.y), e_2.z);
    var _S128 : bool;
    if((bands_0 < 1.5f))
    {
        _S128 = true;
    }
    else
    {
        _S128 = (_S127 <= 9.9999997473787516e-06f);
    }
    var _S129 : f32;
    if(_S128)
    {
        _S129 = -1.0f;
    }
    else
    {
        _S129 = _S127 / (_S127 + 1.0f) * bands_0;
    }
    return _S129;
}

fn giToonBandAt_0( e_3 : vec3<f32>,  x_4 : f32,  w_8 : f32) -> vec3<f32>
{
    if((x_4 < 0.0f))
    {
        return e_3;
    }
    var q_1 : f32 = (floor(x_4) + smoothstep(1.0f - max(w_8, 9.9999997473787516e-05f), 1.0f, fract(x_4)) + 0.5f) / frame_0.giParams_0.y;
    return e_3 * vec3<f32>((q_1 / max(1.0f - q_1, 0.0010000000474974513f) / max(max(e_3.x, e_3.y), e_3.z)));
}

fn toonBandLevel_0( lambert_0 : f32,  p_1 : vec4<f32>,  th_0 : vec4<f32>,  grad_0 : f32) -> f32
{
    var _S130 : f32 = max(p_1.y, min(grad_0, 0.25f));
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
        var _S131 : i32 = k_1 + i32(1);
        var _S132 : f32;
        if((f32(_S131) < (p_1.x)))
        {
            _S132 = smoothstep(th_0[k_1] - _S130, th_0[k_1] + _S130, lambert_0);
        }
        else
        {
            _S132 = 0.0f;
        }
        var level_1 : f32 = level_0 + _S132;
        k_1 = _S131;
        level_0 = level_1;
    }
    return level_0;
}

fn toonShadowedLevel_0( level_2 : f32,  lit_0 : f32,  p_2 : vec4<f32>) -> f32
{
    var _S133 : f32 = p_2.z;
    var down_0 : f32;
    if((_S133 > 0.5f))
    {
        down_0 = max(level_2 - _S133, 0.0f);
    }
    else
    {
        down_0 = 0.0f;
    }
    return mix(down_0, level_2, lit_0);
}

fn toonBandTint_0( level_3 : f32,  t0_0 : vec4<f32>,  t1_0 : vec4<f32>,  t2_0 : vec4<f32>,  t3_0 : vec4<f32>) -> vec3<f32>
{
    var _S134 : vec3<f32> = t0_0.xyz;
    var _S135 : vec3<f32> = t1_0.xyz;
    var _S136 : vec3<f32> = t2_0.xyz;
    return _S134 + vec3<f32>(saturate(level_3)) * (_S135 - _S134) + vec3<f32>(saturate(level_3 - 1.0f)) * (_S136 - _S135) + vec3<f32>(saturate(level_3 - 2.0f)) * (t3_0.xyz - _S136);
}

fn toonMetalSteps_0( t_2 : f32,  bands_1 : f32,  aa_0 : f32) -> f32
{
    if((bands_1 < 0.5f))
    {
        return saturate(t_2);
    }
    var _S137 : f32 = clamp(round(bands_1), 1.0f, 8.0f);
    var _S138 : f32 = max(aa_0, 0.0020000000949949026f);
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
        var _S139 : f32 = f32(k_2);
        var th_1 : f32 = _S139 / _S137;
        var _S140 : f32;
        if((_S139 < _S137))
        {
            _S140 = smoothstep(th_1 - _S138, th_1 + _S138, t_2);
        }
        else
        {
            _S140 = 0.0f;
        }
        var level_5 : f32 = level_4 + _S140;
        k_2 = k_2 + i32(1);
        level_4 = level_5;
    }
    return level_4 / max(_S137 - 1.0f, 1.0f);
}

fn toonMetalEnv_0( N_9 : vec3<f32>,  V_2 : vec3<f32>) -> vec3<f32>
{
    var h_0 : f32 = draw_0.metalB_0.x;
    var _S141 : f32 = reflect((vec3<f32>(0) - V_2), N_9).y;
    var _S142 : f32 = max(draw_0.metalB_0.y, (fwidth((_S141))));
    var skyT_0 : f32 = saturate((_S141 - h_0) / max(1.0f - h_0, 0.0010000000474974513f));
    var groundT_0 : f32 = saturate((h_0 - _S141) / max(1.0f + h_0, 0.0010000000474974513f));
    return mix(mix(draw_0.metalGround_0.xyz, draw_0.metalHorizon_0.xyz, vec3<f32>(toonMetalSteps_0(groundT_0, draw_0.metalA_0.w, (fwidth((groundT_0)))))), mix(draw_0.metalHorizon_0.xyz, draw_0.metalSky_0.xyz, vec3<f32>(toonMetalSteps_0(skyT_0, draw_0.metalA_0.w, (fwidth((skyT_0)))))), vec3<f32>(smoothstep(h_0 - _S142, h_0 + _S142, _S141)));
}

fn styledToonAlbedo_0( albedo_2 : vec3<f32>,  N_10 : vec3<f32>,  V_3 : vec3<f32>,  L_2 : vec3<f32>,  lambert_1 : f32,  castShadow_0 : f32,  unlitReflection_0 : ptr<function, vec3<f32>>) -> vec3<f32>
{
    var bandTint_0 : vec3<f32> = toonBandTint_0(toonShadowedLevel_0(toonBandLevel_0(lambert_1, draw_0.rampParams_0, draw_0.rampThresh_0, (fwidth((lambert_1)))), castShadow_0, draw_0.rampParams_0), draw_0.tint0_0, draw_0.tint1_0, draw_0.tint2_0, draw_0.tint3_0);
    var c_6 : vec3<f32> = albedo_2 * bandTint_0;
    (*unlitReflection_0) = vec3<f32>(0.0f, 0.0f, 0.0f);
    var c_7 : vec3<f32>;
    if(((draw_0.metalA_0.x) > 0.0f))
    {
        const _S143 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        var refl_0 : vec3<f32> = toonMetalEnv_0(N_10, V_3) * mix(_S143, albedo_2, vec3<f32>(draw_0.metalA_0.y)) * mix(_S143, bandTint_0, vec3<f32>(draw_0.metalA_0.z));
        (*unlitReflection_0) = refl_0 * vec3<f32>(draw_0.metalA_0.x);
        c_7 = mix(c_6, refl_0, vec3<f32>(draw_0.metalA_0.x));
    }
    else
    {
        c_7 = c_6;
    }
    if(((draw_0.accRim_0.w) > 0.0f))
    {
        var ndv_0 : f32 = saturate(dot(N_10, V_3));
        var _S144 : f32 = max(max((fwidth((ndv_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.y);
        var lit_1 : f32 = lambert_1 * castShadow_0;
        var _S145 : f32 = max((fwidth((lit_1))), 9.9999997473787516e-05f);
        var side_0 : f32;
        if(((draw_0.accRimLimit_0.x) >= 0.99900001287460327f))
        {
            side_0 = 1.0f;
        }
        else
        {
            side_0 = 1.0f - smoothstep(draw_0.accRimLimit_0.x - _S145, draw_0.accRimLimit_0.x + _S145, lit_1);
        }
        var rim_0 : f32 = (1.0f - smoothstep(draw_0.accRim_0.w - _S144, draw_0.accRim_0.w + _S144, ndv_0)) * side_0;
        var _S146 : vec3<f32> = mix(c_7, draw_0.accRim_0.xyz, vec3<f32>(rim_0));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - rim_0));
        c_7 = _S146;
    }
    if(((draw_0.accGloss_0.w) > 0.0f))
    {
        var nh_0 : f32 = saturate(dot(N_10, normalize(L_2 + V_3)));
        var _S147 : f32 = max(max((fwidth((nh_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.z);
        var g_0 : f32 = smoothstep(draw_0.accGloss_0.w - _S147, draw_0.accGloss_0.w + _S147, nh_0) * step(0.5f, castShadow_0) * step(0.0010000000474974513f, lambert_1);
        var gMix_0 : f32 = g_0 * min(draw_0.accRimLimit_0.w, 1.0f);
        var _S148 : vec3<f32> = mix(c_7, draw_0.accGloss_0.xyz, vec3<f32>(gMix_0)) + draw_0.accGloss_0.xyz * vec3<f32>((g_0 * max(draw_0.accRimLimit_0.w - 1.0f, 0.0f)));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - gMix_0));
        c_7 = _S148;
    }
    return c_7;
}

fn toonWrap_0( lambert_2 : f32) -> f32
{
    var _S149 : f32 = max(frame_0.toonParams_0.y, 0.0010000000474974513f);
    return saturate((lambert_2 + _S149) / (1.0f + _S149));
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
    var _S150 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((lambert_3))));
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
        var _S151 : f32 = f32(k_3);
        var th_2 : f32 = _S151 / bands_2;
        var _S152 : f32;
        if((_S151 < bands_2))
        {
            _S152 = smoothstep(th_2 - _S150, th_2 + _S150, lambert_3);
        }
        else
        {
            _S152 = 0.0f;
        }
        var level_7 : f32 = level_6 + _S152;
        k_3 = k_3 + i32(1);
        level_6 = level_7;
    }
    return level_6 / (bands_2 - 1.0f);
}

fn accumulateLocalLights_0( worldPos_5 : vec3<f32>,  albedo_3 : vec3<f32>,  N_11 : vec3<f32>,  V_4 : vec3<f32>,  phong_1 : bool) -> vec3<f32>
{
    var _S153 : u32 = u32(frame_0.lightInfo_0.x + 0.5f);
    const _S154 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var i_4 : u32 = u32(0);
    var sum_2 : vec3<f32> = _S154;
    loop
    {
        if((i_4 < _S153))
        {
        }
        else
        {
            break;
        }
        var row_2 : i32 = i32(i_4);
        var _S155 : vec3<i32> = vec3<i32>(i32(0), row_2, i32(0));
        var t0_1 : vec4<f32> = (textureLoad((localTex_0), ((_S155)).xy, ((_S155)).z));
        var _S156 : vec3<i32> = vec3<i32>(i32(1), row_2, i32(0));
        var t1_1 : vec4<f32> = (textureLoad((localTex_0), ((_S156)).xy, ((_S156)).z));
        var _S157 : vec3<i32> = vec3<i32>(i32(2), row_2, i32(0));
        var t2_1 : vec4<f32> = (textureLoad((localTex_0), ((_S157)).xy, ((_S157)).z));
        var d_3 : vec3<f32> = t0_1.xyz - worldPos_5;
        var dist2_0 : f32 = dot(d_3, d_3);
        var _S158 : f32 = t0_1.w;
        var r2_0 : f32 = _S158 * _S158;
        var L_3 : vec3<f32> = d_3 * vec3<f32>(rsqrt_0(max(dist2_0, 9.99999993922529029e-09f)));
        var win_0 : f32 = saturate(1.0f - dist2_0 * dist2_0 / (r2_0 * r2_0));
        var spot_0 : f32 = saturate(dot((vec3<f32>(0) - L_3), t2_1.xyz) * t1_1.w + t2_1.w);
        var shape_0 : f32;
        if((dist2_0 < r2_0))
        {
            shape_0 = win_0 * win_0 * spot_0 * spot_0;
        }
        else
        {
            shape_0 = 0.0f;
        }
        var ndl_0 : f32 = saturate(dot(N_11, L_3));
        if(phong_1)
        {
            sum_2 = sum_2 + (albedo_3 * vec3<f32>(ndl_0) + draw_0.phong_0.xyz * vec3<f32>((pow(saturate(dot(N_11, normalize(L_3 + V_4))), max(draw_0.phong_0.w, 1.0f)) * ndl_0))) * t1_1.xyz * vec3<f32>((shape_0 / (dist2_0 + 1.0f)));
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
            sum_2 = sum_2 + albedo_3 * t1_1.xyz * vec3<f32>(level_8);
        }
        i_4 = i_4 + u32(1);
    }
    return sum_2;
}

fn toonTone_0( t_3 : f32) -> vec3<f32>
{
    const white_0 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
    if(((frame_0.toonParams_0.x) < 2.5f))
    {
        return mix(frame_0.shadowTint_0.xyz, white_0, vec3<f32>(t_3));
    }
    var _S159 : vec3<f32>;
    if((t_3 < 0.5f))
    {
        _S159 = mix(frame_0.shadowTint_0.xyz, frame_0.toonMidTint_0.xyz, vec3<f32>((t_3 * 2.0f)));
    }
    else
    {
        _S159 = mix(frame_0.toonMidTint_0.xyz, white_0, vec3<f32>((t_3 * 2.0f - 1.0f)));
    }
    return _S159;
}

fn sampleEmissive_0( uv_10 : vec2<f32>) -> vec3<f32>
{
    var e_4 : vec3<f32> = draw_0.emissiveFactor_0.xyz;
    var e_5 : vec3<f32>;
    if(((draw_0.mapFlags_0.z) > 0.5f))
    {
        e_5 = e_4 * (textureSample((matEmissive_0), (matSampler_0), (uv_10))).xyz;
    }
    else
    {
        e_5 = e_4;
    }
    return e_5;
}

fn applyFog_0( color_3 : vec3<f32>,  worldPos_6 : vec3<f32>) -> vec3<f32>
{
    if(((frame_0.fogParams_0.z) < 0.5f))
    {
        return color_3;
    }
    return mix(color_3, frame_0.fogColor_0.xyz, vec3<f32>(saturate((length(frame_0.cameraPos_0.xyz - worldPos_6) - frame_0.fogParams_0.x) / max(frame_0.fogParams_0.y - frame_0.fogParams_0.x, 0.0010000000474974513f))));
}

fn shadeScene_0( i_5 : VSOut_0,  N_12 : ptr<function, vec3<f32>>,  V_5 : ptr<function, vec3<f32>>) -> vec4<f32>
{
    var _S160 : vec3<f32> = normalize(i_5.worldNorm_0);
    (*N_12) = _S160;
    var texel_2 : vec4<f32> = baseTexel_0(i_5.uv_0);
    var Nl_0 : vec3<f32> = perturbNormal_0(_S160, i_5.worldPos_0, i_5.uv_0);
    var albedo_4 : vec3<f32> = draw_0.diffuse_0.xyz * i_5.color_0.xyz * texel_2.xyz;
    applyDecals_1(i_5.worldPos_0, _S160, &(albedo_4), &(Nl_0));
    var L_4 : vec3<f32> = normalize((vec3<f32>(0) - frame_0.lightDir_0.xyz));
    (*V_5) = normalize(frame_0.cameraPos_0.xyz - i_5.worldPos_0);
    var castShadow_1 : f32 = sampleCascadedShadow_0(i_5.worldPos_0, Nl_0, L_4, length(frame_0.cameraPos_0.xyz - i_5.worldPos_0));
    var alpha_0 : f32 = draw_0.diffuse_0.w * i_5.color_0.w * texel_2.w;
    var lightColor_1 : vec3<f32> = frame_0.lightColor_0.xyz;
    var _S161 : vec3<f32> = hemisphereAmbient_0(Nl_0) * albedo_4 * vec3<f32>(0.30000001192092896f);
    var ambient_0 : vec3<f32>;
    if(((frame_0.giOrigin_0.w) > 0.5f))
    {
        var giOk_0 : bool;
        var e_6 : vec3<f32> = giIrradiance_1(i_5.worldPos_0, Nl_0, (*V_5), &(giOk_0));
        var x_5 : f32 = giToonCoord_0(e_6);
        var aa_1 : f32 = (fwidth((x_5)));
        if(giOk_0)
        {
            ambient_0 = giToonBandAt_0(e_6, x_5, aa_1) * vec3<f32>((frame_0.giSpacing_0.w / 3.1415927410125732f)) * albedo_4;
        }
        else
        {
            ambient_0 = _S161;
        }
    }
    else
    {
        ambient_0 = _S161;
    }
    var color_4 : vec3<f32>;
    if(((draw_0.rampParams_0.x) > 0.5f))
    {
        var reflection_0 : vec3<f32>;
        var styled_0 : vec3<f32> = styledToonAlbedo_0(albedo_4, Nl_0, (*V_5), L_4, saturate(dot(Nl_0, L_4)), castShadow_1, &(reflection_0));
        var _S162 : vec3<f32> = vec3<f32>((1.0f - draw_0.metalA_0.x));
        color_4 = styled_0 * lightColor_1 + reflection_0 * (vec3<f32>(1.0f) - lightColor_1) + ambient_0 * _S162 + accumulateLocalLights_0(i_5.worldPos_0, albedo_4, Nl_0, (*V_5), false) * _S162;
    }
    else
    {
        var lambert_4 : f32 = saturate(dot(Nl_0, L_4)) * castShadow_1;
        var color_5 : vec3<f32> = albedo_4 * toonTone_0(toonRamp_0(lambert_4)) * lightColor_1 + ambient_0 + accumulateLocalLights_0(i_5.worldPos_0, albedo_4, Nl_0, (*V_5), false);
        if(((frame_0.toonParams_0.z) > 0.0f))
        {
            var spec_0 : f32 = pow(saturate(dot(Nl_0, normalize(L_4 + (*V_5)))), max(frame_0.toonParams_0.w * draw_0.toonMaterial_0.w, 1.0f));
            var half_1 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((spec_0))));
            color_4 = color_5 + lightColor_1 * draw_0.toonMaterial_0.xyz * vec3<f32>(frame_0.toonParams_0.z) * vec3<f32>((smoothstep(0.5f - half_1, 0.5f + half_1, spec_0) * step(0.0010000000474974513f, lambert_4)));
        }
        else
        {
            color_4 = color_5;
        }
        color_4 = color_4 + frame_0.rimParams_0.xyz * vec3<f32>(pow(1.0f - saturate(dot(Nl_0, (*V_5))), frame_0.rimParams_0.w));
    }
    return vec4<f32>(applyFog_0(color_4 + (sampleEmissive_0(i_5.uv_0) + draw_0.tintAdd_0.xyz), i_5.worldPos_0), alpha_0);
}

fn occluderFadeAmount_0( W_0 : vec3<f32>,  N_13 : vec3<f32>) -> f32
{
    var _S163 : u32 = u32(frame_0.fadeCamera_0.w);
    var v_2 : vec3<f32> = W_0 - frame_0.fadeCamera_0.xyz;
    var _S164 : vec3<f32>;
    if(((dot(N_13, v_2)) > 0.0f))
    {
        _S164 = (vec3<f32>(0) - N_13);
    }
    else
    {
        _S164 = N_13;
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
        if((i_6 >= _S163))
        {
            break;
        }
        var T_5 : vec3<f32> = frame_0.fadeTargets_0[i_6].xyz;
        var r_4 : f32 = frame_0.fadeTargets_0[i_6].w;
        var axis_0 : vec3<f32> = frame_0.fadeCones_0[i_6].xyz;
        var s_1 : f32 = dot(v_2, axis_0);
        var _S165 : f32 = max(amount_0, (1.0f - smoothstep(1.0f - frame_0.fadeParams_0.y, 1.0f, length(v_2 - axis_0 * vec3<f32>(s_1)) / max(s_1 * frame_0.fadeCones_0[i_6].w, 9.9999997473787516e-06f))) * saturate((dot(T_5 - frame_0.fadeCamera_0.xyz, axis_0) - s_1) / r_4) * smoothstep(0.25f * r_4, -0.25f * r_4, dot(T_5 - W_0, _S164)));
        var i_7 : u32 = i_6 + u32(1);
        amount_0 = _S165;
        i_6 = i_7;
    }
    return amount_0 * frame_0.fadeParams_0.x;
}

fn occluderFaded_0( pixel_1 : vec2<f32>,  W_1 : vec3<f32>,  N_14 : vec3<f32>) -> bool
{
    if(((frame_0.fadeCamera_0.w) < 0.5f))
    {
        return false;
    }
    var p_3 : vec2<u32> = (vec2<u32>(pixel_1) & (vec2<u32>(u32(3), u32(3))));
    return ((occluderFadeAmount_0(W_1, N_14 * vec3<f32>(rsqrt_0(max(dot(N_14, N_14), 9.99999968265522539e-21f))))) > (fract((kOccluderBayer_0[p_3.y * u32(4) + p_3.x] + 0.5f) / 16.0f + frame_0.fadeParams_0.z)));
}

fn packSlotNdv_0( slot_0 : f32,  ndv_1 : f32) -> f32
{
    return (slot_0 * 32.0f + round(saturate(ndv_1) * 31.0f)) / 255.0f;
}

fn packLinearDepth_0( viewDepth_4 : f32) -> vec4<f32>
{
    var v_3 : u32 = u32(saturate(viewDepth_4 / max(frame_0.viewInfo_0.x, 0.0010000000474974513f)) * 1.6777215e+07f + 0.5f);
    return vec4<f32>(f32((((v_3 >> (u32(16)))) & (u32(255)))), f32((((v_3 >> (u32(8)))) & (u32(255)))), f32((v_3 & (u32(255)))), 255.0f) / vec4<f32>(255.0f);
}

struct FsOut_0
{
    @location(0) color_6 : vec4<f32>,
    @location(1) normal_2 : vec4<f32>,
    @location(2) depth_0 : vec4<f32>,
};

fn packOutput_0( color_7 : vec3<f32>,  alpha_1 : f32,  Ng_2 : vec3<f32>,  V_6 : vec3<f32>,  viewDepth_5 : f32,  pixel_2 : vec2<f32>) -> FsOut_0
{
    var o_2 : FsOut_0;
    o_2.color_6 = vec4<f32>(storeHdr_0(color_7, frame_0.lightInfo_0.y, pixel_2), alpha_1);
    var _S166 : vec3<f32> = vec3<f32>(0.5f);
    o_2.normal_2 = vec4<f32>(Ng_2 * _S166 + _S166, packSlotNdv_0(draw_0.rampParams_0.w, dot(Ng_2, V_6)));
    if(((draw_0.misc_0.x) > 0.5f))
    {
        const _S167 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        o_2.normal_2.x = _S167.x;
        o_2.normal_2.y = _S167.y;
        o_2.normal_2.z = _S167.z;
    }
    o_2.depth_0 = packLinearDepth_0(viewDepth_5);
    return o_2;
}

struct pixelInput_2
{
    @location(0) worldPos_7 : vec3<f32>,
    @location(1) worldNorm_1 : vec3<f32>,
    @location(2) color_8 : vec4<f32>,
    @location(3) viewDepth_6 : f32,
    @location(4) uv_11 : vec2<f32>,
};

@fragment
fn fsMain( _S168 : pixelInput_2, @builtin(position) pos_6 : vec4<f32>) -> FsOut_0
{
    var _S169 : VSOut_0 = VSOut_0( pos_6, _S168.worldPos_7, _S168.worldNorm_1, _S168.color_8, _S168.viewDepth_6, _S168.uv_11 );
    var N_15 : vec3<f32>;
    var V_7 : vec3<f32>;
    var c_8 : vec4<f32> = shadeScene_0(_S169, &(N_15), &(V_7));
    var _S170 : bool;
    if(((draw_0.misc_0.y) < 0.5f))
    {
        _S170 = occluderFaded_0(pos_6.xy, _S168.worldPos_7, _S168.worldNorm_1);
    }
    else
    {
        _S170 = false;
    }
    if(_S170)
    {
        discard;
    }
    if(((draw_0.mapFlags_0.w) > 0.0f))
    {
        _S170 = ((baseTexel_0(_S168.uv_11).w) < (draw_0.mapFlags_0.w));
    }
    else
    {
        _S170 = false;
    }
    if(_S170)
    {
        discard;
    }
    return packOutput_0(c_8.xyz, c_8.w, N_15, V_7, _S168.viewDepth_6, pos_6.xy);
}

fn shadePhong_0( i_8 : VSOut_0) -> vec4<f32>
{
    var Ng_3 : vec3<f32> = normalize(i_8.worldNorm_0);
    var texel_3 : vec4<f32> = baseTexel_0(i_8.uv_0);
    var N_16 : vec3<f32> = perturbNormal_0(Ng_3, i_8.worldPos_0, i_8.uv_0);
    var L_5 : vec3<f32> = normalize((vec3<f32>(0) - frame_0.lightDir_0.xyz));
    var V_8 : vec3<f32> = normalize(frame_0.cameraPos_0.xyz - i_8.worldPos_0);
    var albedo_5 : vec3<f32> = draw_0.diffuse_0.xyz * i_8.color_0.xyz * texel_3.xyz;
    applyDecals_0(i_8.worldPos_0, Ng_3, &(albedo_5), &(N_16));
    var alpha_2 : f32 = draw_0.diffuse_0.w * i_8.color_0.w * texel_3.w;
    var castShadow_2 : f32 = sampleCascadedShadow_0(i_8.worldPos_0, N_16, L_5, length(frame_0.cameraPos_0.xyz - i_8.worldPos_0));
    var _S171 : vec3<f32> = frame_0.ambientFlat_0.xyz;
    var ambient_1 : vec3<f32>;
    if(((frame_0.giOrigin_0.w) > 0.5f))
    {
        var giOk_1 : bool;
        var e_7 : vec3<f32> = giIrradiance_0(i_8.worldPos_0, N_16, V_8, &(giOk_1));
        if(giOk_1)
        {
            ambient_1 = e_7 * vec3<f32>((frame_0.giSpacing_0.w / 3.1415927410125732f));
        }
        else
        {
            ambient_1 = _S171;
        }
    }
    else
    {
        ambient_1 = _S171;
    }
    var NdotL_0 : f32 = saturate(dot(N_16, L_5));
    return vec4<f32>(storeHdr_0(applyFog_0(ambient_1 * albedo_5 + frame_0.lightColor_0.xyz * albedo_5 * vec3<f32>(NdotL_0) * vec3<f32>(castShadow_2) + frame_0.lightColor_0.xyz * draw_0.phong_0.xyz * vec3<f32>((pow(saturate(dot(N_16, normalize(L_5 + V_8))), max(draw_0.phong_0.w, 1.0f)) * NdotL_0 * castShadow_2)) + accumulateLocalLights_0(i_8.worldPos_0, albedo_5, N_16, V_8, true) + (sampleEmissive_0(i_8.uv_0) + draw_0.tintAdd_0.xyz), i_8.worldPos_0), frame_0.lightInfo_0.y, i_8.pos_0.xy), alpha_2);
}

struct pixelOutput_2
{
    @location(0) output_2 : vec4<f32>,
};

struct pixelInput_3
{
    @location(0) worldPos_8 : vec3<f32>,
    @location(1) worldNorm_2 : vec3<f32>,
    @location(2) color_9 : vec4<f32>,
    @location(3) viewDepth_7 : f32,
    @location(4) uv_12 : vec2<f32>,
};

@fragment
fn fsMainColor( _S172 : pixelInput_3, @builtin(position) pos_7 : vec4<f32>) -> pixelOutput_2
{
    var _S173 : VSOut_0 = VSOut_0( pos_7, _S172.worldPos_8, _S172.worldNorm_2, _S172.color_9, _S172.viewDepth_7, _S172.uv_12 );
    var _S174 : vec4<f32> = shadePhong_0(_S173);
    var _S175 : pixelOutput_2 = pixelOutput_2( _S174 );
    return _S175;
}

struct OitOut_0
{
    @location(0) accum_0 : vec4<f32>,
    @location(1) reveal_0 : f32,
};

struct pixelInput_4
{
    @location(0) worldPos_9 : vec3<f32>,
    @location(1) worldNorm_3 : vec3<f32>,
    @location(2) color_10 : vec4<f32>,
    @location(3) viewDepth_8 : f32,
    @location(4) uv_13 : vec2<f32>,
};

@fragment
fn fsMainOit( _S176 : pixelInput_4, @builtin(position) pos_8 : vec4<f32>) -> OitOut_0
{
    var _S177 : VSOut_0 = VSOut_0( pos_8, _S176.worldPos_9, _S176.worldNorm_3, _S176.color_10, _S176.viewDepth_8, _S176.uv_13 );
    var c_9 : vec4<f32> = shadePhong_0(_S177);
    var _S178 : f32 = c_9.w;
    var o_3 : OitOut_0;
    o_3.accum_0 = vec4<f32>(c_9.xyz * vec3<f32>(_S178), _S178) * vec4<f32>(clamp(pow(min(1.0f, _S178 * 10.0f) + 0.0099999997764825821f, 3.0f) * 1000.0f * pow(1.0f - pos_8.z * 0.89999997615814209f, 3.0f), 0.0099999997764825821f, 3000.0f));
    o_3.reveal_0 = _S178;
    return o_3;
}

struct SkyOut_0
{
    @builtin(position) pos_9 : vec4<f32>,
    @location(0) ndc_4 : vec2<f32>,
};

@vertex
fn vsSky(@builtin(vertex_index) vid_0 : u32) -> SkyOut_0
{
    var o_4 : SkyOut_0;
    var _S179 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f);
    o_4.ndc_4 = _S179;
    o_4.pos_9 = vec4<f32>(_S179, 0.0f, 1.0f);
    return o_4;
}

fn srgbToLinearChannel_0( x_6 : f32) -> f32
{
    var _S180 : f32;
    if((x_6 <= 0.040449999272823334f))
    {
        _S180 = x_6 / 12.920000076293945f;
    }
    else
    {
        _S180 = pow((x_6 + 0.054999999701976776f) / 1.0549999475479126f, 2.4000000953674316f);
    }
    return _S180;
}

struct pixelInput_5
{
    @location(0) ndc_5 : vec2<f32>,
};

@fragment
fn fsSky( _S181 : pixelInput_5, @builtin(position) pos_10 : vec4<f32>) -> FsOut_0
{
    var dir_1 : vec3<f32> = normalize(frame_0.camForward_0.xyz + vec3<f32>(_S181.ndc_5.x) * frame_0.camRight_0.xyz + vec3<f32>(_S181.ndc_5.y) * frame_0.camUp_0.xyz);
    var a_4 : vec3<f32> = abs(dir_1);
    var _S182 : f32 = max(a_4.x, a_4.z);
    var c_10 : vec3<f32>;
    if(((a_4.y) >= _S182))
    {
        if(((dir_1.y) > 0.0f))
        {
            c_10 = frame_0.skyZenith_0.xyz;
        }
        else
        {
            c_10 = frame_0.skyNadir_0.xyz;
        }
    }
    else
    {
        c_10 = mix(frame_0.skyNadir_0.xyz, frame_0.skyZenith_0.xyz, vec3<f32>((clamp(dir_1.y / _S182, -1.0f, 1.0f) * 0.5f + 0.5f)));
    }
    var o_5 : FsOut_0;
    o_5.color_6 = vec4<f32>(storeHdr_0(vec3<f32>(srgbToLinearChannel_0(c_10.x), srgbToLinearChannel_0(c_10.y), srgbToLinearChannel_0(c_10.z)), frame_0.lightInfo_0.y, pos_10.xy), 1.0f);
    o_5.normal_2 = vec4<f32>(0.5f, 0.5f, 0.5f, 0.0f);
    o_5.depth_0 = vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    return o_5;
}

fn fogHg_0( g_1 : f32,  c_11 : f32) -> f32
{
    var g2_0 : f32 = g_1 * g_1;
    return (1.0f - g2_0) / (12.566370964050293f * pow(max(1.0f + g2_0 - 2.0f * g_1 * c_11, 9.9999997473787516e-05f), 1.5f));
}

fn fogShadowTap_0( nearMap_0 : bool,  clip_0 : vec4<f32>,  column_1 : f32,  columns_0 : f32) -> f32
{
    var ndc_6 : vec3<f32> = clip_0.xyz / vec3<f32>(max(clip_0.w, 9.9999997473787516e-05f));
    var _S183 : f32 = ndc_6.x * 0.5f + 0.5f;
    var uv_14 : vec2<f32> = vec2<f32>(_S183, - ndc_6.y * 0.5f + 0.5f);
    var _S184 : bool;
    if((_S183 < 0.0f))
    {
        _S184 = true;
    }
    else
    {
        _S184 = ((uv_14.x) > 1.0f);
    }
    if(_S184)
    {
        _S184 = true;
    }
    else
    {
        _S184 = ((uv_14.y) < 0.0f);
    }
    if(_S184)
    {
        _S184 = true;
    }
    else
    {
        _S184 = ((uv_14.y) > 1.0f);
    }
    if(_S184)
    {
        _S184 = true;
    }
    else
    {
        _S184 = ((ndc_6.z) < 0.0f);
    }
    if(_S184)
    {
        _S184 = true;
    }
    else
    {
        _S184 = ((ndc_6.z) > 1.0f);
    }
    if(_S184)
    {
        return 1.0f;
    }
    uv_14[i32(0)] = (uv_14.x + column_1) / columns_0;
    var depthRef_4 : f32 = ndc_6.z - 0.0020000000949949026f;
    var _S185 : f32;
    if(nearMap_0)
    {
        _S185 = (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_14), (depthRef_4)));
    }
    else
    {
        _S185 = (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (uv_14), (depthRef_4)));
    }
    return _S185;
}

fn fogSunShadow_0( wp_3 : vec3<f32>,  dist_0 : f32) -> f32
{
    var _S186 : bool;
    if(((frame_0.fogC_0.w) < 0.5f))
    {
        _S186 = true;
    }
    else
    {
        _S186 = ((frame_0.shadowTexel_0.z) < 0.5f);
    }
    if(_S186)
    {
        return 1.0f;
    }
    if(((frame_0.shadowTexel_0.z) < 1.5f))
    {
        _S186 = true;
    }
    else
    {
        _S186 = (dist_0 < (frame_0.shadowSplit_0.x));
    }
    if(_S186)
    {
        return fogShadowTap_0(true, (((vec4<f32>(wp_3, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP0_0.data_0[i32(0)][i32(0)], frame_0.shadowVP0_0.data_0[i32(1)][i32(0)], frame_0.shadowVP0_0.data_0[i32(2)][i32(0)], frame_0.shadowVP0_0.data_0[i32(3)][i32(0)], frame_0.shadowVP0_0.data_0[i32(0)][i32(1)], frame_0.shadowVP0_0.data_0[i32(1)][i32(1)], frame_0.shadowVP0_0.data_0[i32(2)][i32(1)], frame_0.shadowVP0_0.data_0[i32(3)][i32(1)], frame_0.shadowVP0_0.data_0[i32(0)][i32(2)], frame_0.shadowVP0_0.data_0[i32(1)][i32(2)], frame_0.shadowVP0_0.data_0[i32(2)][i32(2)], frame_0.shadowVP0_0.data_0[i32(3)][i32(2)], frame_0.shadowVP0_0.data_0[i32(0)][i32(3)], frame_0.shadowVP0_0.data_0[i32(1)][i32(3)], frame_0.shadowVP0_0.data_0[i32(2)][i32(3)], frame_0.shadowVP0_0.data_0[i32(3)][i32(3)])))), 0.0f, 1.0f);
    }
    if(((frame_0.shadowTexel_0.z) < 2.5f))
    {
        _S186 = true;
    }
    else
    {
        _S186 = (dist_0 < (frame_0.shadowSplit_0.y));
    }
    if(_S186)
    {
        return fogShadowTap_0(false, (((vec4<f32>(wp_3, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP1_0.data_0[i32(0)][i32(0)], frame_0.shadowVP1_0.data_0[i32(1)][i32(0)], frame_0.shadowVP1_0.data_0[i32(2)][i32(0)], frame_0.shadowVP1_0.data_0[i32(3)][i32(0)], frame_0.shadowVP1_0.data_0[i32(0)][i32(1)], frame_0.shadowVP1_0.data_0[i32(1)][i32(1)], frame_0.shadowVP1_0.data_0[i32(2)][i32(1)], frame_0.shadowVP1_0.data_0[i32(3)][i32(1)], frame_0.shadowVP1_0.data_0[i32(0)][i32(2)], frame_0.shadowVP1_0.data_0[i32(1)][i32(2)], frame_0.shadowVP1_0.data_0[i32(2)][i32(2)], frame_0.shadowVP1_0.data_0[i32(3)][i32(2)], frame_0.shadowVP1_0.data_0[i32(0)][i32(3)], frame_0.shadowVP1_0.data_0[i32(1)][i32(3)], frame_0.shadowVP1_0.data_0[i32(2)][i32(3)], frame_0.shadowVP1_0.data_0[i32(3)][i32(3)])))), 0.0f, 2.0f);
    }
    return fogShadowTap_0(false, (((vec4<f32>(wp_3, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP2_0.data_0[i32(0)][i32(0)], frame_0.shadowVP2_0.data_0[i32(1)][i32(0)], frame_0.shadowVP2_0.data_0[i32(2)][i32(0)], frame_0.shadowVP2_0.data_0[i32(3)][i32(0)], frame_0.shadowVP2_0.data_0[i32(0)][i32(1)], frame_0.shadowVP2_0.data_0[i32(1)][i32(1)], frame_0.shadowVP2_0.data_0[i32(2)][i32(1)], frame_0.shadowVP2_0.data_0[i32(3)][i32(1)], frame_0.shadowVP2_0.data_0[i32(0)][i32(2)], frame_0.shadowVP2_0.data_0[i32(1)][i32(2)], frame_0.shadowVP2_0.data_0[i32(2)][i32(2)], frame_0.shadowVP2_0.data_0[i32(3)][i32(2)], frame_0.shadowVP2_0.data_0[i32(0)][i32(3)], frame_0.shadowVP2_0.data_0[i32(1)][i32(3)], frame_0.shadowVP2_0.data_0[i32(2)][i32(3)], frame_0.shadowVP2_0.data_0[i32(3)][i32(3)])))), 1.0f, 2.0f);
}

struct pixelOutput_3
{
    @location(0) output_3 : vec4<f32>,
};

struct pixelInput_6
{
    @location(0) ndc_7 : vec2<f32>,
};

@fragment
fn fsFog( _S187 : pixelInput_6, @builtin(position) pos_11 : vec4<f32>) -> pixelOutput_3
{
    var e_8 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(pos_11.xy)), (i32(0))));
    var v_4 : u32 = (((((u32(e_8.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_8.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_8.z * 255.0f + 0.5f)));
    var range_0 : f32 = frame_0.fogD_0.w;
    var dist_1 : f32;
    if((v_4 >= u32(16777215)))
    {
        dist_1 = range_0;
    }
    else
    {
        dist_1 = min(f32(v_4) / 1.6777215e+07f * frame_0.viewInfo_0.x, range_0);
    }
    var _S188 : f32 = sqrt(saturate(dist_1 / max(range_0, 9.9999997473787516e-05f)));
    var dir_2 : vec3<f32> = normalize(frame_0.camForward_0.xyz + vec3<f32>(_S187.ndc_7.x) * frame_0.camRight_0.xyz + vec3<f32>(_S187.ndc_7.y) * frame_0.camUp_0.xyz);
    var _S189 : f32 = fogHg_0(frame_0.fogB_0.w, dot(dir_2, (vec3<f32>(0) - normalize(frame_0.lightDir_0.xyz))));
    const _S190 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var k_4 : i32 = i32(0);
    var T_6 : f32 = 1.0f;
    var S_0 : vec3<f32> = _S190;
    loop
    {
        if((k_4 < i32(24)))
        {
        }
        else
        {
            break;
        }
        var t0_2 : f32 = _S188 * f32(k_4) / 24.0f;
        var _S191 : i32 = k_4 + i32(1);
        var t1_2 : f32 = _S188 * f32(_S191) / 24.0f;
        var _S192 : f32 = 0.5f * (t0_2 + t1_2);
        var dm_0 : f32 = range_0 * _S192 * _S192;
        var wp_4 : vec3<f32> = frame_0.cameraPos_0.xyz + dir_2 * vec3<f32>(dm_0);
        var _S193 : f32 = max(frame_0.fogA_0.x * exp(- frame_0.fogA_0.y * (wp_4.y - frame_0.fogA_0.z)) + frame_0.fogA_0.w, 0.0f);
        var inject_0 : vec3<f32> = frame_0.fogB_0.xyz * vec3<f32>(_S193) * (frame_0.fogC_0.xyz * vec3<f32>(fogSunShadow_0(wp_4, dm_0)) * vec3<f32>(_S189) + frame_0.fogD_0.xyz);
        var _S194 : f32 = max(_S193, 9.99999997475242708e-07f);
        var segT_0 : f32 = exp(- _S194 * (range_0 * t1_2 * t1_2 - range_0 * t0_2 * t0_2));
        var S_1 : vec3<f32> = S_0 + vec3<f32>(T_6) * (inject_0 - inject_0 * vec3<f32>(segT_0)) / vec3<f32>(_S194);
        var T_7 : f32 = T_6 * segT_0;
        k_4 = _S191;
        T_6 = T_7;
        S_0 = S_1;
    }
    var _S195 : pixelOutput_3 = pixelOutput_3( vec4<f32>(S_0 * vec3<f32>(frame_0.lightInfo_0.y), T_6) );
    return _S195;
}

