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
    @align(16) lightTiles_0 : vec4<f32>,
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

@binding(13) @group(0) var tileTex_0 : texture_2d<u32>;

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
    @interpolate(flat, either) @location(5) diffuse_1 : vec4<f32>,
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
    o_0.diffuse_1 = draw_0.diffuse_0;
    return o_0;
}

struct vertexInput_1
{
    @location(0) pos_2 : vec3<f32>,
    @location(6) normal_1 : vec3<f32>,
    @location(7) uv_2 : vec2<f32>,
    @location(8) color_2 : vec4<f32>,
    @location(1) w0_0 : vec4<f32>,
    @location(2) w1_0 : vec4<f32>,
    @location(3) w2_0 : vec4<f32>,
    @location(4) w3_0 : vec4<f32>,
    @location(5) diffuse_2 : vec4<f32>,
};

@vertex
fn vsMainInst( _S9 : vertexInput_1) -> VSOut_0
{
    var world_1 : mat4x4<f32> = mat4x4<f32>(vec4<f32>(_S9.w0_0.x, _S9.w1_0.x, _S9.w2_0.x, _S9.w3_0.x), vec4<f32>(_S9.w0_0.y, _S9.w1_0.y, _S9.w2_0.y, _S9.w3_0.y), vec4<f32>(_S9.w0_0.z, _S9.w1_0.z, _S9.w2_0.z, _S9.w3_0.z), vec4<f32>(_S9.w0_0.w, _S9.w1_0.w, _S9.w2_0.w, _S9.w3_0.w));
    var wp_1 : vec4<f32> = (((vec4<f32>(_S9.pos_2, 1.0f)) * (world_1)));
    var o_1 : VSOut_0;
    o_1.worldPos_0 = wp_1.xyz;
    o_1.worldNorm_0 = transformNormal_0(mat3x3<f32>(world_1[i32(0)].xyz, world_1[i32(1)].xyz, world_1[i32(2)].xyz), _S9.normal_1);
    var _S10 : vec4<f32> = (((wp_1) * (mat4x4<f32>(frame_0.viewProj_0.data_0[i32(0)][i32(0)], frame_0.viewProj_0.data_0[i32(1)][i32(0)], frame_0.viewProj_0.data_0[i32(2)][i32(0)], frame_0.viewProj_0.data_0[i32(3)][i32(0)], frame_0.viewProj_0.data_0[i32(0)][i32(1)], frame_0.viewProj_0.data_0[i32(1)][i32(1)], frame_0.viewProj_0.data_0[i32(2)][i32(1)], frame_0.viewProj_0.data_0[i32(3)][i32(1)], frame_0.viewProj_0.data_0[i32(0)][i32(2)], frame_0.viewProj_0.data_0[i32(1)][i32(2)], frame_0.viewProj_0.data_0[i32(2)][i32(2)], frame_0.viewProj_0.data_0[i32(3)][i32(2)], frame_0.viewProj_0.data_0[i32(0)][i32(3)], frame_0.viewProj_0.data_0[i32(1)][i32(3)], frame_0.viewProj_0.data_0[i32(2)][i32(3)], frame_0.viewProj_0.data_0[i32(3)][i32(3)]))));
    o_1.pos_0 = _S10;
    o_1.viewDepth_0 = _S10.w;
    o_1.uv_0 = _S9.uv_2;
    o_1.color_0 = vec4<f32>(srgbToLinear_0(_S9.color_2.xyz), _S9.color_2.w);
    o_1.diffuse_1 = _S9.diffuse_2;
    return o_1;
}

struct SilhouetteOut_0
{
    @builtin(position) pos_3 : vec4<f32>,
    @location(0) viewDepth_1 : f32,
};

struct vertexInput_2
{
    @location(0) pos_4 : vec3<f32>,
    @location(1) normal_2 : vec3<f32>,
    @location(2) uv_3 : vec2<f32>,
    @location(3) color_3 : vec4<f32>,
};

@vertex
fn vsSilhouette( _S11 : vertexInput_2) -> SilhouetteOut_0
{
    var rel_0 : vec3<f32> = (((vec4<f32>(_S11.pos_4, 1.0f)) * (mat4x4<f32>(draw_0.world_0.data_0[i32(0)][i32(0)], draw_0.world_0.data_0[i32(1)][i32(0)], draw_0.world_0.data_0[i32(2)][i32(0)], draw_0.world_0.data_0[i32(3)][i32(0)], draw_0.world_0.data_0[i32(0)][i32(1)], draw_0.world_0.data_0[i32(1)][i32(1)], draw_0.world_0.data_0[i32(2)][i32(1)], draw_0.world_0.data_0[i32(3)][i32(1)], draw_0.world_0.data_0[i32(0)][i32(2)], draw_0.world_0.data_0[i32(1)][i32(2)], draw_0.world_0.data_0[i32(2)][i32(2)], draw_0.world_0.data_0[i32(3)][i32(2)], draw_0.world_0.data_0[i32(0)][i32(3)], draw_0.world_0.data_0[i32(1)][i32(3)], draw_0.world_0.data_0[i32(2)][i32(3)], draw_0.world_0.data_0[i32(3)][i32(3)])))).xyz - frame_0.cameraPos_0.xyz;
    var d_0 : f32 = length(rel_0);
    var o_2 : SilhouetteOut_0;
    var _S12 : vec4<f32> = (((vec4<f32>(frame_0.cameraPos_0.xyz + rel_0 * vec3<f32>((max(d_0 - frame_0.fadeSilParams_0.x, 0.0010000000474974513f) / max(d_0, 9.99999997475242708e-07f))), 1.0f)) * (mat4x4<f32>(frame_0.viewProj_0.data_0[i32(0)][i32(0)], frame_0.viewProj_0.data_0[i32(1)][i32(0)], frame_0.viewProj_0.data_0[i32(2)][i32(0)], frame_0.viewProj_0.data_0[i32(3)][i32(0)], frame_0.viewProj_0.data_0[i32(0)][i32(1)], frame_0.viewProj_0.data_0[i32(1)][i32(1)], frame_0.viewProj_0.data_0[i32(2)][i32(1)], frame_0.viewProj_0.data_0[i32(3)][i32(1)], frame_0.viewProj_0.data_0[i32(0)][i32(2)], frame_0.viewProj_0.data_0[i32(1)][i32(2)], frame_0.viewProj_0.data_0[i32(2)][i32(2)], frame_0.viewProj_0.data_0[i32(3)][i32(2)], frame_0.viewProj_0.data_0[i32(0)][i32(3)], frame_0.viewProj_0.data_0[i32(1)][i32(3)], frame_0.viewProj_0.data_0[i32(2)][i32(3)], frame_0.viewProj_0.data_0[i32(3)][i32(3)]))));
    o_2.pos_3 = _S12;
    o_2.viewDepth_1 = _S12.w;
    return o_2;
}

fn neutralShoulder_0( c_1 : vec3<f32>) -> vec3<f32>
{
    var _S13 : f32 = max(c_1.x, max(c_1.y, c_1.z));
    if((_S13 <= 0.89999997615814209f))
    {
        return c_1;
    }
    return c_1 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S13 + 0.10000002384185791f - 0.89999997615814209f)) / _S13));
}

fn storeHdr_0( c_2 : vec3<f32>,  scale_0 : f32,  pixel_0 : vec2<f32>) -> vec3<f32>
{
    if((scale_0 >= 1.0f))
    {
        return c_2;
    }
    const _S14 : vec2<f32> = vec2<f32>(0.067110560834407806f, 0.0058371499180793762f);
    var n0_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0, _S14)));
    var n1_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0 + vec2<f32>(17.0f, 31.0f), _S14)));
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
fn fsSilhouette( _S15 : pixelInput_0, @builtin(sample_index) sampleIndex_0 : u32, @builtin(position) pos_5 : vec4<f32>) -> pixelOutput_0
{
    var _S16 : vec2<f32> = pos_5.xy;
    var e_0 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(_S16)), (i32(sampleIndex_0))));
    var v_0 : u32 = (((((u32(e_0.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_0.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_0.z * 255.0f + 0.5f)));
    if((v_0 >= u32(16777215)))
    {
        discard;
    }
    if(((f32(v_0) / 1.6777215e+07f * frame_0.viewInfo_0.x) >= (_S15.viewDepth_2)))
    {
        discard;
    }
    var _S17 : pixelOutput_0 = pixelOutput_0( vec4<f32>(storeHdr_0(frame_0.fadeSilColor_0.xyz, frame_0.lightInfo_0.y, _S16), frame_0.fadeSilColor_0.w) );
    return _S17;
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
fn fsSilhouetteCompat( _S18 : pixelInput_1, @builtin(position) pos_6 : vec4<f32>) -> pixelOutput_1
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
        var e_1 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(pos_6.xy)), (s_0)));
        var v_1 : u32 = (((((u32(e_1.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_1.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_1.z * 255.0f + 0.5f)));
        var sceneDepth_0 : f32 = f32(v_1) / 1.6777215e+07f * frame_0.viewInfo_0.x;
        var _S19 : bool;
        if((v_1 < u32(16777215)))
        {
            _S19 = (sceneDepth_0 < (_S18.viewDepth_3));
        }
        else
        {
            _S19 = false;
        }
        var _S20 : i32;
        if(_S19)
        {
            _S20 = i32(1);
        }
        else
        {
            _S20 = i32(0);
        }
        var hit_1 : i32 = hit_0 + _S20;
        s_0 = s_0 + i32(1);
        hit_0 = hit_1;
    }
    if(hit_0 == i32(0))
    {
        discard;
    }
    var _S21 : pixelOutput_1 = pixelOutput_1( vec4<f32>(storeHdr_0(frame_0.fadeSilColor_0.xyz, frame_0.lightInfo_0.y, pos_6.xy), frame_0.fadeSilColor_0.w * f32(hit_0) * 0.25f) );
    return _S21;
}

fn baseTexel_0( uv_4 : vec2<f32>) -> vec4<f32>
{
    if(((draw_0.mapFlags_0.x) < 0.5f))
    {
        return vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    }
    var t_0 : vec4<f32>;
    var _S22 : vec4<f32>;
    if(((draw_0.mapParams_0.y) > 0.5f))
    {
        _S22 = (textureSample((matBase_0), (matSamplerNearest_0), (uv_4)));
    }
    else
    {
        _S22 = (textureSample((matBase_0), (matSampler_0), (uv_4)));
    }
    t_0 = _S22;
    if(((draw_0.mapParams_0.z) > 0.5f))
    {
        var _S23 : vec3<f32> = srgbToLinear_0(t_0.xyz);
        t_0.x = _S23.x;
        t_0.y = _S23.y;
        t_0.z = _S23.z;
    }
    return t_0;
}

fn perturbNormal_0( N_0 : vec3<f32>,  worldPos_1 : vec3<f32>,  uv_5 : vec2<f32>) -> vec3<f32>
{
    if(((draw_0.mapFlags_0.y) < 0.5f))
    {
        return N_0;
    }
    var _S24 : vec2<f32> = ((textureSample((matNormal_0), (matSampler_0), (uv_5))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f)) * vec2<f32>(draw_0.mapParams_0.x);
    var xy_0 : vec2<f32> = _S24;
    xy_0[i32(1)] = - _S24.y;
    var tn_0 : vec3<f32> = vec3<f32>(xy_0, sqrt(saturate(1.0f - dot(xy_0, xy_0))));
    var duv1_0 : vec2<f32> = dpdx(uv_5);
    var duv2_0 : vec2<f32> = dpdy(uv_5);
    var dp2perp_0 : vec3<f32> = cross(dpdy(worldPos_1), N_0);
    var dp1perp_0 : vec3<f32> = cross(N_0, dpdx(worldPos_1));
    var T_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.x) + dp1perp_0 * vec3<f32>(duv2_0.x);
    var B_0 : vec3<f32> = dp2perp_0 * vec3<f32>(duv1_0.y) + dp1perp_0 * vec3<f32>(duv2_0.y);
    var invmax_0 : f32 = - rsqrt_0(max(max(dot(T_0, T_0), dot(B_0, B_0)), 9.99999968265522539e-21f));
    return normalize(T_0 * vec3<f32>((invmax_0 * tn_0.x)) + B_0 * vec3<f32>((invmax_0 * tn_0.y)) + N_0 * vec3<f32>(tn_0.z));
}

fn decalEdge_0( x_1 : f32,  soft_0 : f32) -> f32
{
    var _S25 : f32;
    if((soft_0 > 0.0f))
    {
        _S25 = saturate((0.5f - abs(x_1)) / (0.5f * soft_0));
    }
    else
    {
        _S25 = 1.0f;
    }
    return _S25;
}

fn decalDisc_0( p_0 : vec2<f32>,  soft_1 : f32) -> f32
{
    var r_1 : f32 = length(p_0) * 2.0f;
    var _S26 : f32;
    if((soft_1 > 0.0f))
    {
        _S26 = saturate((1.0f - r_1) / soft_1);
    }
    else
    {
        _S26 = step(r_1, 1.0f);
    }
    return _S26;
}

fn applyDecals_0( worldPos_2 : vec3<f32>,  Ng_0 : vec3<f32>,  albedo_0 : ptr<function, vec3<f32>>,  N_1 : ptr<function, vec3<f32>>)
{
    var _S27 : u32 = u32(frame_0.decalInfo_0.x + 0.5f);
    var _S28 : vec3<f32> = dpdx(worldPos_2);
    var _S29 : vec3<f32> = dpdy(worldPos_2);
    var i_0 : u32 = u32(0);
    loop
    {
        if((i_0 < _S27))
        {
        }
        else
        {
            break;
        }
        var row_0 : i32 = i32(i_0);
        var _S30 : vec3<i32> = vec3<i32>(i32(0), row_0, i32(0));
        var toDecal0_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S30)).xy, ((_S30)).z));
        var _S31 : vec3<i32> = vec3<i32>(i32(1), row_0, i32(0));
        var toDecal1_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S31)).xy, ((_S31)).z));
        var _S32 : vec3<i32> = vec3<i32>(i32(2), row_0, i32(0));
        var _S33 : vec3<i32> = vec3<i32>(i32(3), row_0, i32(0));
        var dcolor_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S33)).xy, ((_S33)).z));
        var _S34 : vec3<i32> = vec3<i32>(i32(4), row_0, i32(0));
        var axisZ_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S34)).xy, ((_S34)).z));
        var _S35 : vec3<i32> = vec3<i32>(i32(5), row_0, i32(0));
        var axisX_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S35)).xy, ((_S35)).z));
        var _S36 : vec3<i32> = vec3<i32>(i32(6), row_0, i32(0));
        var params_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S36)).xy, ((_S36)).z));
        var _S37 : vec3<i32> = vec3<i32>(i32(7), row_0, i32(0));
        var fade_0 : vec4<f32> = (textureLoad((decalTex_0), ((_S37)).xy, ((_S37)).z));
        var wp_2 : vec4<f32> = vec4<f32>(worldPos_2, 1.0f);
        var _S38 : f32 = dot(toDecal0_0, wp_2);
        var _S39 : f32 = dot(toDecal1_0, wp_2);
        var _S40 : f32 = dot((textureLoad((decalTex_0), ((_S32)).xy, ((_S32)).z)), wp_2);
        if((any((((abs(vec3<f32>(_S38, _S39, _S40))) > vec3<f32>(0.5f, 0.5f, 0.5f))))))
        {
            i_0 = i_0 + u32(1);
            continue;
        }
        var _S41 : f32 = fade_0.y;
        var soft_2 : f32 = axisZ_0.w;
        var a_0 : f32 = dcolor_0.w * saturate((dot(Ng_0, axisZ_0.xyz) - _S41) / max(fade_0.x - _S41, 9.9999997473787516e-05f)) * decalEdge_0(_S40, soft_2);
        var _S42 : f32 = _S38 + 0.5f;
        var _S43 : f32 = 0.5f - _S39;
        var _S44 : f32 = params_0.x;
        var uvw_0 : vec3<f32> = vec3<f32>(_S42, _S43, _S44);
        var _S45 : vec3<f32> = toDecal0_0.xyz;
        var _S46 : vec3<f32> = toDecal1_0.xyz;
        var gx_0 : vec2<f32> = vec2<f32>(dot(_S45, _S28), - dot(_S46, _S28));
        var gy_0 : vec2<f32> = vec2<f32>(dot(_S45, _S29), - dot(_S46, _S29));
        var _S47 : vec4<f32> = vec4<f32>(1.0f, 1.0f, 1.0f, decalDisc_0(vec2<f32>(_S38, _S39), soft_2));
        var tex_0 : vec4<f32>;
        if((_S44 >= 0.0f))
        {
            tex_0 = (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((uvw_0)).xy, i32(((uvw_0)).z), (gx_0), (gy_0)));
        }
        else
        {
            tex_0 = _S47;
        }
        var a_1 : f32 = a_0 * tex_0.w;
        if((a_1 <= 0.0f))
        {
            i_0 = i_0 + u32(1);
            continue;
        }
        (*albedo_0) = mix((*albedo_0), dcolor_0.xyz * tex_0.xyz, vec3<f32>(a_1));
        var _S48 : f32 = params_0.y;
        if((_S48 >= 0.0f))
        {
            var _S49 : vec3<f32> = vec3<f32>(vec2<f32>(_S42, _S43), _S48);
            var xy_1 : vec2<f32> = (textureSampleGrad((vfxData_0), (vfxSampler_0), ((_S49)).xy, i32(((_S49)).z), (gx_0), (gy_0))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f);
            var _S50 : vec3<f32> = axisX_0.xyz;
            var T_1 : vec3<f32> = _S50 - (*N_1) * vec3<f32>(dot(_S50, (*N_1)));
            var T_2 : vec3<f32>;
            if(((dot(T_1, T_1)) > 9.99999993922529029e-09f))
            {
                T_2 = normalize(T_1);
            }
            else
            {
                T_2 = _S50;
            }
            (*N_1) = normalize(mix((*N_1), normalize(T_2 * vec3<f32>(xy_1.x) + cross((*N_1), T_2) * vec3<f32>(xy_1.y) + (*N_1) * vec3<f32>(sqrt(saturate(1.0f - dot(xy_1, xy_1))))), vec3<f32>((a_1 * axisX_0.w))));
        }
        i_0 = i_0 + u32(1);
    }
    return;
}

fn applyDecals_1( worldPos_3 : vec3<f32>,  Ng_1 : vec3<f32>,  albedo_1 : ptr<function, vec3<f32>>,  N_2 : ptr<function, vec3<f32>>)
{
    var _S51 : u32 = u32(frame_0.decalInfo_0.x + 0.5f);
    var _S52 : vec3<f32> = dpdx(worldPos_3);
    var _S53 : vec3<f32> = dpdy(worldPos_3);
    var i_1 : u32 = u32(0);
    loop
    {
        if((i_1 < _S51))
        {
        }
        else
        {
            break;
        }
        var row_1 : i32 = i32(i_1);
        var _S54 : vec3<i32> = vec3<i32>(i32(0), row_1, i32(0));
        var toDecal0_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S54)).xy, ((_S54)).z));
        var _S55 : vec3<i32> = vec3<i32>(i32(1), row_1, i32(0));
        var toDecal1_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S55)).xy, ((_S55)).z));
        var _S56 : vec3<i32> = vec3<i32>(i32(2), row_1, i32(0));
        var _S57 : vec3<i32> = vec3<i32>(i32(3), row_1, i32(0));
        var dcolor_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S57)).xy, ((_S57)).z));
        var _S58 : vec3<i32> = vec3<i32>(i32(4), row_1, i32(0));
        var axisZ_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S58)).xy, ((_S58)).z));
        var _S59 : vec3<i32> = vec3<i32>(i32(5), row_1, i32(0));
        var axisX_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S59)).xy, ((_S59)).z));
        var _S60 : vec3<i32> = vec3<i32>(i32(6), row_1, i32(0));
        var params_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S60)).xy, ((_S60)).z));
        var _S61 : vec3<i32> = vec3<i32>(i32(7), row_1, i32(0));
        var fade_1 : vec4<f32> = (textureLoad((decalTex_0), ((_S61)).xy, ((_S61)).z));
        var wp_3 : vec4<f32> = vec4<f32>(worldPos_3, 1.0f);
        var _S62 : f32 = dot(toDecal0_1, wp_3);
        var _S63 : f32 = dot(toDecal1_1, wp_3);
        var _S64 : f32 = dot((textureLoad((decalTex_0), ((_S56)).xy, ((_S56)).z)), wp_3);
        if((any((((abs(vec3<f32>(_S62, _S63, _S64))) > vec3<f32>(0.5f, 0.5f, 0.5f))))))
        {
            i_1 = i_1 + u32(1);
            continue;
        }
        var _S65 : f32 = fade_1.y;
        var soft_3 : f32 = axisZ_1.w;
        var a_2 : f32 = dcolor_1.w * saturate((dot(Ng_1, axisZ_1.xyz) - _S65) / max(fade_1.x - _S65, 9.9999997473787516e-05f)) * decalEdge_0(_S64, soft_3);
        var _S66 : f32 = _S62 + 0.5f;
        var _S67 : f32 = 0.5f - _S63;
        var _S68 : f32 = params_1.x;
        var uvw_1 : vec3<f32> = vec3<f32>(_S66, _S67, _S68);
        var _S69 : vec3<f32> = toDecal0_1.xyz;
        var _S70 : vec3<f32> = toDecal1_1.xyz;
        var gx_1 : vec2<f32> = vec2<f32>(dot(_S69, _S52), - dot(_S70, _S52));
        var gy_1 : vec2<f32> = vec2<f32>(dot(_S69, _S53), - dot(_S70, _S53));
        var _S71 : vec4<f32> = vec4<f32>(1.0f, 1.0f, 1.0f, decalDisc_0(vec2<f32>(_S62, _S63), soft_3));
        var tex_1 : vec4<f32>;
        if((_S68 >= 0.0f))
        {
            tex_1 = (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((uvw_1)).xy, i32(((uvw_1)).z), (gx_1), (gy_1)));
        }
        else
        {
            tex_1 = _S71;
        }
        var a_3 : f32 = a_2 * tex_1.w;
        if((a_3 <= 0.0f))
        {
            i_1 = i_1 + u32(1);
            continue;
        }
        (*albedo_1) = mix((*albedo_1), dcolor_1.xyz * tex_1.xyz, vec3<f32>(a_3));
        var _S72 : f32 = params_1.y;
        if((_S72 >= 0.0f))
        {
            var _S73 : vec3<f32> = vec3<f32>(vec2<f32>(_S66, _S67), _S72);
            var xy_2 : vec2<f32> = (textureSampleGrad((vfxData_0), (vfxSampler_0), ((_S73)).xy, i32(((_S73)).z), (gx_1), (gy_1))).xy * vec2<f32>(2.0f) - vec2<f32>(1.0f);
            var _S74 : vec3<f32> = axisX_1.xyz;
            var T_3 : vec3<f32> = _S74 - (*N_2) * vec3<f32>(dot(_S74, (*N_2)));
            var T_4 : vec3<f32>;
            if(((dot(T_3, T_3)) > 9.99999993922529029e-09f))
            {
                T_4 = normalize(T_3);
            }
            else
            {
                T_4 = _S74;
            }
            (*N_2) = normalize(mix((*N_2), normalize(T_4 * vec3<f32>(xy_2.x) + cross((*N_2), T_4) * vec3<f32>(xy_2.y) + (*N_2) * vec3<f32>(sqrt(saturate(1.0f - dot(xy_2, xy_2))))), vec3<f32>((a_3 * axisX_1.w))));
        }
        i_1 = i_1 + u32(1);
    }
    return;
}

fn shadowSlope_0( N_3 : vec3<f32>,  L_0 : vec3<f32>) -> f32
{
    var _S75 : f32 = max(saturate(dot(N_3, L_0)), 0.10000000149011612f);
    return min(sqrt(1.0f - _S75 * _S75) / _S75, 6.0f);
}

fn shadowLookup_0( vp_0 : mat4x4<f32>,  cinfo_0 : vec4<f32>,  P_0 : vec3<f32>,  N_4 : vec3<f32>,  slope_0 : f32,  ndc_0 : ptr<function, vec3<f32>>,  bias_0 : ptr<function, f32>)
{
    var _S76 : f32 = max(frame_0.shadowSplit_0.z, 1.0f);
    var q_0 : vec3<f32>;
    if(((frame_0.shadowSplit_0.w) > 0.0f))
    {
        (*bias_0) = frame_0.shadowSplit_0.w * (1.0f + _S76 * slope_0);
        q_0 = P_0;
    }
    else
    {
        var _S77 : f32 = cinfo_0.x;
        var _S78 : vec3<f32> = P_0 + N_4 * vec3<f32>(_S77);
        (*bias_0) = cinfo_0.y * _S77 * (0.5f + _S76 * slope_0);
        q_0 = _S78;
    }
    var c_3 : vec4<f32> = (((vec4<f32>(q_0, 1.0f)) * (vp_0)));
    (*ndc_0) = c_3.xyz / vec3<f32>(max(c_3.w, 9.9999997473787516e-05f));
    return;
}

fn pcfNear_0( uv_6 : vec2<f32>,  depthRef_0 : f32,  texel_0 : vec2<f32>) -> f32
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
                var shadow_1 : f32 = shadow_0 + (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_6 + vec2<f32>(f32(x_2), f32(y_0)) * texel_0), (depthRef_0)));
                x_2 = x_2 + i32(1);
                shadow_0 = shadow_1;
            }
            y_0 = y_0 + i32(1);
        }
        return shadow_0 / 9.0f;
    }
    var _S79 : vec2<f32> = texel_0 * vec2<f32>(0.5f);
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
            var _S80 : f32 = f32(x_2);
            var _S81 : f32 = f32(y_0);
            var shadow_2 : f32 = shadow_0 + (3.0f - abs(_S80)) * (3.0f - abs(_S81)) * (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_6 + vec2<f32>(_S80, _S81) * _S79), (depthRef_0)));
            x_2 = x_2 + i32(1);
            shadow_0 = shadow_2;
        }
        y_0 = y_0 + i32(1);
    }
    return shadow_0 / 81.0f;
}

fn samplePcfNear_0( ndc_1 : vec3<f32>,  bias_1 : f32) -> f32
{
    var _S82 : f32 = ndc_1.x * 0.5f + 0.5f;
    var _S83 : f32 = - ndc_1.y * 0.5f + 0.5f;
    var uv_7 : vec2<f32> = vec2<f32>(_S82, _S83);
    var _S84 : f32 = ndc_1.z;
    var depthRef_1 : f32 = _S84 - bias_1;
    var _S85 : bool;
    if((_S82 < 0.0f))
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S83 < 0.0f);
    }
    if(_S85)
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S82 > 1.0f);
    }
    if(_S85)
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S83 > 1.0f);
    }
    if(_S85)
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S84 < 0.0f);
    }
    if(_S85)
    {
        _S85 = true;
    }
    else
    {
        _S85 = (_S84 > 1.0f);
    }
    if(_S85)
    {
        return 1.0f;
    }
    var t_1 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.x;
    return pcfNear_0(uv_7, depthRef_1, vec2<f32>(t_1, t_1));
}

fn pcfAtlas_0( uv_8 : vec2<f32>,  depthRef_2 : f32,  texel_1 : vec2<f32>,  uMin_0 : f32,  uMax_0 : f32) -> f32
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
                var shadow_4 : f32 = shadow_3 + (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_8.x + f32(x_3) * texel_1.x, uMin_0, uMax_0), uv_8.y + f32(y_1) * texel_1.y)), (depthRef_2)));
                x_3 = x_3 + i32(1);
                shadow_3 = shadow_4;
            }
            y_1 = y_1 + i32(1);
        }
        return shadow_3 / 9.0f;
    }
    var _S86 : vec2<f32> = texel_1 * vec2<f32>(0.5f);
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
            var _S87 : f32 = f32(x_3);
            var _S88 : f32 = f32(y_1);
            var shadow_5 : f32 = shadow_3 + (3.0f - abs(_S87)) * (3.0f - abs(_S88)) * (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (vec2<f32>(clamp(uv_8.x + _S87 * _S86.x, uMin_0, uMax_0), uv_8.y + _S88 * _S86.y)), (depthRef_2)));
            x_3 = x_3 + i32(1);
            shadow_3 = shadow_5;
        }
        y_1 = y_1 + i32(1);
    }
    return shadow_3 / 81.0f;
}

fn samplePcfAtlas_0( ndc_2 : vec3<f32>,  column_0 : f32,  bias_2 : f32) -> f32
{
    var _S89 : f32 = ndc_2.x * 0.5f + 0.5f;
    var _S90 : f32 = - ndc_2.y * 0.5f + 0.5f;
    var _S91 : f32 = ndc_2.z;
    var depthRef_3 : f32 = _S91 - bias_2;
    var _S92 : bool;
    if((_S89 < 0.0f))
    {
        _S92 = true;
    }
    else
    {
        _S92 = (_S90 < 0.0f);
    }
    if(_S92)
    {
        _S92 = true;
    }
    else
    {
        _S92 = (_S89 > 1.0f);
    }
    if(_S92)
    {
        _S92 = true;
    }
    else
    {
        _S92 = (_S90 > 1.0f);
    }
    if(_S92)
    {
        _S92 = true;
    }
    else
    {
        _S92 = (_S91 < 0.0f);
    }
    if(_S92)
    {
        _S92 = true;
    }
    else
    {
        _S92 = (_S91 > 1.0f);
    }
    if(_S92)
    {
        return 1.0f;
    }
    var texelV_0 : f32 = frame_0.shadowSplit_0.z * frame_0.shadowTexel_0.y;
    var texelU_0 : f32 = texelV_0 * 0.5f;
    var _S93 : f32 = texelU_0 * 0.5f;
    return pcfAtlas_0(vec2<f32>((_S89 + column_0) * 0.5f, _S90), depthRef_3, vec2<f32>(texelU_0, texelV_0), column_0 * 0.5f + _S93, (column_0 + 1.0f) * 0.5f - _S93);
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
    var _S94 : bool;
    if(((frame_0.shadowTexel_0.z) < 1.5f))
    {
        _S94 = true;
    }
    else
    {
        _S94 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.x));
    }
    if(_S94)
    {
        shadowLookup_0(mat4x4<f32>(frame_0.shadowVP0_0.data_0[i32(0)][i32(0)], frame_0.shadowVP0_0.data_0[i32(1)][i32(0)], frame_0.shadowVP0_0.data_0[i32(2)][i32(0)], frame_0.shadowVP0_0.data_0[i32(3)][i32(0)], frame_0.shadowVP0_0.data_0[i32(0)][i32(1)], frame_0.shadowVP0_0.data_0[i32(1)][i32(1)], frame_0.shadowVP0_0.data_0[i32(2)][i32(1)], frame_0.shadowVP0_0.data_0[i32(3)][i32(1)], frame_0.shadowVP0_0.data_0[i32(0)][i32(2)], frame_0.shadowVP0_0.data_0[i32(1)][i32(2)], frame_0.shadowVP0_0.data_0[i32(2)][i32(2)], frame_0.shadowVP0_0.data_0[i32(3)][i32(2)], frame_0.shadowVP0_0.data_0[i32(0)][i32(3)], frame_0.shadowVP0_0.data_0[i32(1)][i32(3)], frame_0.shadowVP0_0.data_0[i32(2)][i32(3)], frame_0.shadowVP0_0.data_0[i32(3)][i32(3)]), frame_0.shadowC0_0, worldPos_4, N_5, slope_1, &(ndc_3), &(bias_3));
        return samplePcfNear_0(ndc_3, bias_3);
    }
    if(((frame_0.shadowTexel_0.z) < 2.5f))
    {
        _S94 = true;
    }
    else
    {
        _S94 = (distanceFromCamera_0 < (frame_0.shadowSplit_0.y));
    }
    if(_S94)
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
    var _S95 : vec3<i32> = vec3<i32>(i32(((probe_0 & (u32(255)))) * u32(7) + k_0), i32((probe_0 >> (u32(8)))), i32(0));
    return (textureLoad((giProbes_0), ((_S95)).xy, ((_S95)).z));
}

fn giOctEncode_0( n_1 : vec3<f32>) -> vec2<f32>
{
    var _S96 : f32 = n_1.x;
    var _S97 : f32 = n_1.z;
    var _S98 : f32 = n_1.y;
    var uv_9 : vec2<f32> = vec2<f32>(_S96, _S97) / vec2<f32>((abs(_S96) + abs(_S98) + abs(_S97)));
    var uv_10 : vec2<f32>;
    if((_S98 < 0.0f))
    {
        var _S99 : f32;
        if(((uv_9.x) >= 0.0f))
        {
            _S99 = 1.0f;
        }
        else
        {
            _S99 = -1.0f;
        }
        var _S100 : f32;
        if(((uv_9.y) >= 0.0f))
        {
            _S100 = 1.0f;
        }
        else
        {
            _S100 = -1.0f;
        }
        uv_10 = (vec2<f32>(1.0f, 1.0f) - abs(uv_9.yx)) * vec2<f32>(_S99, _S100);
    }
    else
    {
        uv_10 = uv_9;
    }
    return uv_10;
}

fn giVisibilityAt_0( probe_1 : u32,  dir_0 : vec3<f32>) -> vec2<f32>
{
    var tiles_0 : u32 = u32(frame_0.giDims_0.w);
    var _S101 : u32 = probe_1 % tiles_0;
    var _S102 : f32 = f32(_S101);
    var _S103 : u32 = probe_1 / tiles_0;
    var _S104 : vec2<f32> = vec2<f32>(0.5f);
    return (textureSampleLevel((giVisibility_0), (giSampler_0), ((vec2<f32>(_S102, f32(_S103)) * vec2<f32>(18.0f) + vec2<f32>(1.0f) + (giOctEncode_0(dir_0) * _S104 + _S104) * vec2<f32>(16.0f)) * frame_0.giParams_0.zw), (0.0f)).xy);
}

fn giProbeIrradiance_0( probe_2 : u32,  n_2 : vec3<f32>) -> vec3<f32>
{
    var a0_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(0));
    var a1_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(1));
    var a2_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(2));
    var a3_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(3));
    var a4_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(4));
    var a5_0 : vec4<f32> = giProbeTexel_0(probe_2, u32(5));
    var _S105 : f32 = n_2.y;
    var _S106 : f32 = n_2.z;
    var _S107 : f32 = n_2.x;
    var _S108 : f32 = 1.0925480127334595f * _S107;
    return max(a0_0.xyz * vec3<f32>(0.28209498524665833f) + vec3<f32>(a0_0.w, a1_0.xy) * vec3<f32>((0.48860299587249756f * _S105)) + vec3<f32>(a1_0.zw, a2_0.x) * vec3<f32>((0.48860299587249756f * _S106)) + a2_0.yzw * vec3<f32>((0.48860299587249756f * _S107)) + a3_0.xyz * vec3<f32>((_S108 * _S105)) + vec3<f32>(a3_0.w, a4_0.xy) * vec3<f32>((1.0925480127334595f * _S105 * _S106)) + vec3<f32>(a4_0.zw, a5_0.x) * vec3<f32>((0.31539198756217957f * (3.0f * _S106 * _S106 - 1.0f))) + a5_0.yzw * vec3<f32>((_S108 * _S106)) + giProbeTexel_0(probe_2, u32(6)).xyz * vec3<f32>((0.54627400636672974f * (_S107 * _S107 - _S105 * _S105))), vec3<f32>(0.0f, 0.0f, 0.0f));
}

fn giIrradiance_0( P_1 : vec3<f32>,  N_7 : vec3<f32>,  V_0 : vec3<f32>,  ok_0 : ptr<function, bool>) -> vec3<f32>
{
    var _S109 : vec3<i32> = vec3<i32>(frame_0.giDims_0.xyz);
    const _S110 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S111 : vec3<i32> = _S109 - vec3<i32>(i32(1), i32(1), i32(1));
    var c_4 : vec3<f32> = clamp((P_1 + (N_7 * vec3<f32>(0.20000000298023224f) + V_0 * vec3<f32>(0.80000001192092896f)) * vec3<f32>(frame_0.giParams_0.x) - frame_0.giOrigin_0.xyz) / frame_0.giSpacing_0.xyz, _S110, vec3<f32>(_S111));
    var _S112 : vec3<f32> = frame_0.giOrigin_0.xyz + c_4 * frame_0.giSpacing_0.xyz;
    var _S113 : vec3<i32> = min(vec3<i32>(floor(c_4)), max(_S109 - vec3<i32>(i32(2), i32(2), i32(2)), vec3<i32>(i32(0), i32(0), i32(0))));
    var _S114 : vec3<f32> = saturate(c_4 - vec3<f32>(_S113));
    var i_2 : i32 = i32(0);
    var sum_0 : vec3<f32> = _S110;
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
        var _S115 : vec3<i32> = min(_S113 + off_0, _S111);
        var idx_0 : u32 = u32(_S115.x + _S109.x * (_S115.y + _S109.y * _S115.z));
        if(((giProbeTexel_0(idx_0, u32(6)).w) < 0.5f))
        {
            i_2 = i_2 + i32(1);
            continue;
        }
        var probeP_0 : vec3<f32> = frame_0.giOrigin_0.xyz + vec3<f32>(_S115) * frame_0.giSpacing_0.xyz;
        var tri_0 : vec3<f32> = mix(vec3<f32>(1.0f, 1.0f, 1.0f) - _S114, _S114, vec3<f32>(off_0));
        var wrap_0 : f32 = (dot(normalize(probeP_0 - P_1 + vec3<f32>(9.99999997475242708e-07f, 9.99999997475242708e-07f, 9.99999997475242708e-07f)), N_7) + 1.0f) * 0.5f;
        var w_0 : f32 = wrap_0 * wrap_0 + 0.20000000298023224f;
        var toPoint_0 : vec3<f32> = _S112 - probeP_0;
        var r_2 : f32 = length(toPoint_0);
        var _S116 : vec3<f32>;
        if((r_2 > 9.9999997473787516e-05f))
        {
            _S116 = toPoint_0 / vec3<f32>(r_2);
        }
        else
        {
            _S116 = vec3<f32>(0.0f, 1.0f, 0.0f);
        }
        var m_1 : vec2<f32> = giVisibilityAt_0(idx_0, _S116);
        var _S117 : f32 = m_1.x;
        var w_1 : f32;
        if((r_2 > _S117))
        {
            var var_0 : f32 = abs(m_1.y - _S117 * _S117);
            var d_1 : f32 = r_2 - _S117;
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
    var _S118 : bool = (wsum_0 > 9.99999997475242708e-07f);
    (*ok_0) = _S118;
    if(_S118)
    {
        sum_0 = sum_0 / vec3<f32>(wsum_0);
    }
    else
    {
        sum_0 = _S110;
    }
    return sum_0;
}

fn giIrradiance_1( P_2 : vec3<f32>,  N_8 : vec3<f32>,  V_1 : vec3<f32>,  ok_1 : ptr<function, bool>) -> vec3<f32>
{
    var _S119 : vec3<i32> = vec3<i32>(frame_0.giDims_0.xyz);
    const _S120 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S121 : vec3<i32> = _S119 - vec3<i32>(i32(1), i32(1), i32(1));
    var c_5 : vec3<f32> = clamp((P_2 + (N_8 * vec3<f32>(0.20000000298023224f) + V_1 * vec3<f32>(0.80000001192092896f)) * vec3<f32>(frame_0.giParams_0.x) - frame_0.giOrigin_0.xyz) / frame_0.giSpacing_0.xyz, _S120, vec3<f32>(_S121));
    var _S122 : vec3<f32> = frame_0.giOrigin_0.xyz + c_5 * frame_0.giSpacing_0.xyz;
    var _S123 : vec3<i32> = min(vec3<i32>(floor(c_5)), max(_S119 - vec3<i32>(i32(2), i32(2), i32(2)), vec3<i32>(i32(0), i32(0), i32(0))));
    var _S124 : vec3<f32> = saturate(c_5 - vec3<f32>(_S123));
    var i_3 : i32 = i32(0);
    var sum_1 : vec3<f32> = _S120;
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
        var _S125 : vec3<i32> = min(_S123 + off_1, _S121);
        var idx_1 : u32 = u32(_S125.x + _S119.x * (_S125.y + _S119.y * _S125.z));
        if(((giProbeTexel_0(idx_1, u32(6)).w) < 0.5f))
        {
            i_3 = i_3 + i32(1);
            continue;
        }
        var probeP_1 : vec3<f32> = frame_0.giOrigin_0.xyz + vec3<f32>(_S125) * frame_0.giSpacing_0.xyz;
        var tri_1 : vec3<f32> = mix(vec3<f32>(1.0f, 1.0f, 1.0f) - _S124, _S124, vec3<f32>(off_1));
        var wrap_1 : f32 = (dot(normalize(probeP_1 - P_2 + vec3<f32>(9.99999997475242708e-07f, 9.99999997475242708e-07f, 9.99999997475242708e-07f)), N_8) + 1.0f) * 0.5f;
        var w_4 : f32 = wrap_1 * wrap_1 + 0.20000000298023224f;
        var toPoint_1 : vec3<f32> = _S122 - probeP_1;
        var r_3 : f32 = length(toPoint_1);
        var _S126 : vec3<f32>;
        if((r_3 > 9.9999997473787516e-05f))
        {
            _S126 = toPoint_1 / vec3<f32>(r_3);
        }
        else
        {
            _S126 = vec3<f32>(0.0f, 1.0f, 0.0f);
        }
        var m_2 : vec2<f32> = giVisibilityAt_0(idx_1, _S126);
        var _S127 : f32 = m_2.x;
        var w_5 : f32;
        if((r_3 > _S127))
        {
            var var_1 : f32 = abs(m_2.y - _S127 * _S127);
            var d_2 : f32 = r_3 - _S127;
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
    var _S128 : bool = (wsum_2 > 9.99999997475242708e-07f);
    (*ok_1) = _S128;
    if(_S128)
    {
        sum_1 = sum_1 / vec3<f32>(wsum_2);
    }
    else
    {
        sum_1 = _S120;
    }
    return sum_1;
}

fn giToonCoord_0( e_2 : vec3<f32>) -> f32
{
    var bands_0 : f32 = frame_0.giParams_0.y;
    var _S129 : f32 = max(max(e_2.x, e_2.y), e_2.z);
    var _S130 : bool;
    if((bands_0 < 1.5f))
    {
        _S130 = true;
    }
    else
    {
        _S130 = (_S129 <= 9.9999997473787516e-06f);
    }
    var _S131 : f32;
    if(_S130)
    {
        _S131 = -1.0f;
    }
    else
    {
        _S131 = _S129 / (_S129 + 1.0f) * bands_0;
    }
    return _S131;
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
    var _S132 : f32 = max(p_1.y, min(grad_0, 0.25f));
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
        var _S133 : i32 = k_1 + i32(1);
        var _S134 : f32;
        if((f32(_S133) < (p_1.x)))
        {
            _S134 = smoothstep(th_0[k_1] - _S132, th_0[k_1] + _S132, lambert_0);
        }
        else
        {
            _S134 = 0.0f;
        }
        var level_1 : f32 = level_0 + _S134;
        k_1 = _S133;
        level_0 = level_1;
    }
    return level_0;
}

fn toonShadowedLevel_0( level_2 : f32,  lit_0 : f32,  p_2 : vec4<f32>) -> f32
{
    var _S135 : f32 = p_2.z;
    var down_0 : f32;
    if((_S135 > 0.5f))
    {
        down_0 = max(level_2 - _S135, 0.0f);
    }
    else
    {
        down_0 = 0.0f;
    }
    return mix(down_0, level_2, lit_0);
}

fn toonBandTint_0( level_3 : f32,  t0_0 : vec4<f32>,  t1_0 : vec4<f32>,  t2_0 : vec4<f32>,  t3_0 : vec4<f32>) -> vec3<f32>
{
    var _S136 : vec3<f32> = t0_0.xyz;
    var _S137 : vec3<f32> = t1_0.xyz;
    var _S138 : vec3<f32> = t2_0.xyz;
    return _S136 + vec3<f32>(saturate(level_3)) * (_S137 - _S136) + vec3<f32>(saturate(level_3 - 1.0f)) * (_S138 - _S137) + vec3<f32>(saturate(level_3 - 2.0f)) * (t3_0.xyz - _S138);
}

fn toonMetalSteps_0( t_2 : f32,  bands_1 : f32,  aa_0 : f32) -> f32
{
    if((bands_1 < 0.5f))
    {
        return saturate(t_2);
    }
    var _S139 : f32 = clamp(round(bands_1), 1.0f, 8.0f);
    var _S140 : f32 = max(aa_0, 0.0020000000949949026f);
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
        var _S141 : f32 = f32(k_2);
        var th_1 : f32 = _S141 / _S139;
        var _S142 : f32;
        if((_S141 < _S139))
        {
            _S142 = smoothstep(th_1 - _S140, th_1 + _S140, t_2);
        }
        else
        {
            _S142 = 0.0f;
        }
        var level_5 : f32 = level_4 + _S142;
        k_2 = k_2 + i32(1);
        level_4 = level_5;
    }
    return level_4 / max(_S139 - 1.0f, 1.0f);
}

fn toonMetalEnv_0( N_9 : vec3<f32>,  V_2 : vec3<f32>) -> vec3<f32>
{
    var h_0 : f32 = draw_0.metalB_0.x;
    var _S143 : f32 = reflect((vec3<f32>(0) - V_2), N_9).y;
    var _S144 : f32 = max(draw_0.metalB_0.y, (fwidth((_S143))));
    var skyT_0 : f32 = saturate((_S143 - h_0) / max(1.0f - h_0, 0.0010000000474974513f));
    var groundT_0 : f32 = saturate((h_0 - _S143) / max(1.0f + h_0, 0.0010000000474974513f));
    return mix(mix(draw_0.metalGround_0.xyz, draw_0.metalHorizon_0.xyz, vec3<f32>(toonMetalSteps_0(groundT_0, draw_0.metalA_0.w, (fwidth((groundT_0)))))), mix(draw_0.metalHorizon_0.xyz, draw_0.metalSky_0.xyz, vec3<f32>(toonMetalSteps_0(skyT_0, draw_0.metalA_0.w, (fwidth((skyT_0)))))), vec3<f32>(smoothstep(h_0 - _S144, h_0 + _S144, _S143)));
}

fn styledToonAlbedo_0( albedo_2 : vec3<f32>,  N_10 : vec3<f32>,  V_3 : vec3<f32>,  L_2 : vec3<f32>,  lambert_1 : f32,  castShadow_0 : f32,  unlitReflection_0 : ptr<function, vec3<f32>>) -> vec3<f32>
{
    var bandTint_0 : vec3<f32> = toonBandTint_0(toonShadowedLevel_0(toonBandLevel_0(lambert_1, draw_0.rampParams_0, draw_0.rampThresh_0, (fwidth((lambert_1)))), castShadow_0, draw_0.rampParams_0), draw_0.tint0_0, draw_0.tint1_0, draw_0.tint2_0, draw_0.tint3_0);
    var c_6 : vec3<f32> = albedo_2 * bandTint_0;
    (*unlitReflection_0) = vec3<f32>(0.0f, 0.0f, 0.0f);
    var c_7 : vec3<f32>;
    if(((draw_0.metalA_0.x) > 0.0f))
    {
        const _S145 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        var refl_0 : vec3<f32> = toonMetalEnv_0(N_10, V_3) * mix(_S145, albedo_2, vec3<f32>(draw_0.metalA_0.y)) * mix(_S145, bandTint_0, vec3<f32>(draw_0.metalA_0.z));
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
        var _S146 : f32 = max(max((fwidth((ndv_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.y);
        var lit_1 : f32 = lambert_1 * castShadow_0;
        var _S147 : f32 = max((fwidth((lit_1))), 9.9999997473787516e-05f);
        var side_0 : f32;
        if(((draw_0.accRimLimit_0.x) >= 0.99900001287460327f))
        {
            side_0 = 1.0f;
        }
        else
        {
            side_0 = 1.0f - smoothstep(draw_0.accRimLimit_0.x - _S147, draw_0.accRimLimit_0.x + _S147, lit_1);
        }
        var rim_0 : f32 = (1.0f - smoothstep(draw_0.accRim_0.w - _S146, draw_0.accRim_0.w + _S146, ndv_0)) * side_0;
        var _S148 : vec3<f32> = mix(c_7, draw_0.accRim_0.xyz, vec3<f32>(rim_0));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - rim_0));
        c_7 = _S148;
    }
    if(((draw_0.accGloss_0.w) > 0.0f))
    {
        var nh_0 : f32 = saturate(dot(N_10, normalize(L_2 + V_3)));
        var _S149 : f32 = max(max((fwidth((nh_0))), 9.9999997473787516e-05f), draw_0.accRimLimit_0.z);
        var g_0 : f32 = smoothstep(draw_0.accGloss_0.w - _S149, draw_0.accGloss_0.w + _S149, nh_0) * step(0.5f, castShadow_0) * step(0.0010000000474974513f, lambert_1);
        var gMix_0 : f32 = g_0 * min(draw_0.accRimLimit_0.w, 1.0f);
        var _S150 : vec3<f32> = mix(c_7, draw_0.accGloss_0.xyz, vec3<f32>(gMix_0)) + draw_0.accGloss_0.xyz * vec3<f32>((g_0 * max(draw_0.accRimLimit_0.w - 1.0f, 0.0f)));
        (*unlitReflection_0) = (*unlitReflection_0) * vec3<f32>((1.0f - gMix_0));
        c_7 = _S150;
    }
    return c_7;
}

fn toonWrap_0( lambert_2 : f32) -> f32
{
    var _S151 : f32 = max(frame_0.toonParams_0.y, 0.0010000000474974513f);
    return saturate((lambert_2 + _S151) / (1.0f + _S151));
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
    var _S152 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((lambert_3))));
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
        var _S153 : f32 = f32(k_3);
        var th_2 : f32 = _S153 / bands_2;
        var _S154 : f32;
        if((_S153 < bands_2))
        {
            _S154 = smoothstep(th_2 - _S152, th_2 + _S152, lambert_3);
        }
        else
        {
            _S154 = 0.0f;
        }
        var level_7 : f32 = level_6 + _S154;
        k_3 = k_3 + i32(1);
        level_6 = level_7;
    }
    return level_6 / (bands_2 - 1.0f);
}

fn accumulateLocalLights_0( worldPos_5 : vec3<f32>,  albedo_3 : vec3<f32>,  N_11 : vec3<f32>,  V_4 : vec3<f32>,  phong_1 : bool,  pixel_1 : vec2<f32>) -> vec3<f32>
{
    var count_0 : u32 = u32(frame_0.lightInfo_0.x + 0.5f);
    var tiled_0 : bool = ((frame_0.lightTiles_0.x) > 0.5f);
    var count_1 : u32;
    var own_0 : u32;
    var first_0 : u32;
    if(tiled_0)
    {
        var _S155 : vec2<i32> = vec2<i32>(pixel_1) / vec2<i32>(i32(frame_0.lightTiles_0.x + 0.5f));
        var _S156 : vec3<i32> = vec3<i32>(_S155, i32(0));
        var _S157 : u32 = (textureLoad((tileTex_0), ((_S156)).xy, ((_S156)).z).x);
        var _S158 : u32 = (_S157 & (u32(511)));
        var _S159 : u32 = (_S157 >> (u32(9)));
        count_1 = u32(frame_0.lightTiles_0.z + 0.5f);
        own_0 = _S158;
        first_0 = _S159;
    }
    else
    {
        count_1 = count_0;
        own_0 = count_0;
        first_0 = u32(0);
    }
    var _S160 : i32 = i32(frame_0.lightTiles_0.y + 0.5f);
    const _S161 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var k_4 : u32 = u32(0);
    var sum_2 : vec3<f32> = _S161;
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
        var _S162 : i32 = i32(k_4);
        var row_2 : i32;
        if(tiled_0)
        {
            var at_0 : u32 = first_0 + min(k_4, max(own_0, u32(1)) - u32(1));
            var _S163 : vec3<i32> = vec3<i32>(i32((at_0 & (u32(1023)))), _S160 + i32((at_0 >> (u32(10)))), i32(0));
            row_2 = i32((textureLoad((tileTex_0), ((_S163)).xy, ((_S163)).z).x));
        }
        else
        {
            row_2 = _S162;
        }
        var _S164 : vec3<i32> = vec3<i32>(i32(0), row_2, i32(0));
        var t0_1 : vec4<f32> = (textureLoad((localTex_0), ((_S164)).xy, ((_S164)).z));
        var _S165 : vec3<i32> = vec3<i32>(i32(1), row_2, i32(0));
        var t1_1 : vec4<f32> = (textureLoad((localTex_0), ((_S165)).xy, ((_S165)).z));
        var _S166 : vec3<i32> = vec3<i32>(i32(2), row_2, i32(0));
        var t2_1 : vec4<f32> = (textureLoad((localTex_0), ((_S166)).xy, ((_S166)).z));
        var d_3 : vec3<f32> = t0_1.xyz - worldPos_5;
        var dist2_0 : f32 = dot(d_3, d_3);
        var _S167 : f32 = t0_1.w;
        var r2_0 : f32 = _S167 * _S167;
        var L_3 : vec3<f32> = d_3 * vec3<f32>(rsqrt_0(max(dist2_0, 9.99999993922529029e-09f)));
        var win_0 : f32 = saturate(1.0f - dist2_0 * dist2_0 / (r2_0 * r2_0));
        var spot_0 : f32 = saturate(dot((vec3<f32>(0) - L_3), t2_1.xyz) * t1_1.w + t2_1.w);
        var _S168 : bool;
        if(valid_0)
        {
            _S168 = (dist2_0 < r2_0);
        }
        else
        {
            _S168 = false;
        }
        var shape_0 : f32;
        if(_S168)
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
        k_4 = k_4 + u32(1);
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
    var _S169 : vec3<f32>;
    if((t_3 < 0.5f))
    {
        _S169 = mix(frame_0.shadowTint_0.xyz, frame_0.toonMidTint_0.xyz, vec3<f32>((t_3 * 2.0f)));
    }
    else
    {
        _S169 = mix(frame_0.toonMidTint_0.xyz, white_0, vec3<f32>((t_3 * 2.0f - 1.0f)));
    }
    return _S169;
}

fn sampleEmissive_0( uv_11 : vec2<f32>) -> vec3<f32>
{
    var e_4 : vec3<f32> = draw_0.emissiveFactor_0.xyz;
    var e_5 : vec3<f32>;
    if(((draw_0.mapFlags_0.z) > 0.5f))
    {
        e_5 = e_4 * (textureSample((matEmissive_0), (matSampler_0), (uv_11))).xyz;
    }
    else
    {
        e_5 = e_4;
    }
    return e_5;
}

fn applyFog_0( color_4 : vec3<f32>,  worldPos_6 : vec3<f32>) -> vec3<f32>
{
    if(((frame_0.fogParams_0.z) < 0.5f))
    {
        return color_4;
    }
    return mix(color_4, frame_0.fogColor_0.xyz, vec3<f32>(saturate((length(frame_0.cameraPos_0.xyz - worldPos_6) - frame_0.fogParams_0.x) / max(frame_0.fogParams_0.y - frame_0.fogParams_0.x, 0.0010000000474974513f))));
}

fn shadeScene_0( i_4 : VSOut_0,  N_12 : ptr<function, vec3<f32>>,  V_5 : ptr<function, vec3<f32>>) -> vec4<f32>
{
    var _S170 : vec3<f32> = normalize(i_4.worldNorm_0);
    (*N_12) = _S170;
    var texel_2 : vec4<f32> = baseTexel_0(i_4.uv_0);
    var Nl_0 : vec3<f32> = perturbNormal_0(_S170, i_4.worldPos_0, i_4.uv_0);
    var albedo_4 : vec3<f32> = i_4.diffuse_1.xyz * i_4.color_0.xyz * texel_2.xyz;
    applyDecals_1(i_4.worldPos_0, _S170, &(albedo_4), &(Nl_0));
    var L_4 : vec3<f32> = normalize((vec3<f32>(0) - frame_0.lightDir_0.xyz));
    (*V_5) = normalize(frame_0.cameraPos_0.xyz - i_4.worldPos_0);
    var castShadow_1 : f32 = sampleCascadedShadow_0(i_4.worldPos_0, Nl_0, L_4, length(frame_0.cameraPos_0.xyz - i_4.worldPos_0));
    var alpha_0 : f32 = i_4.diffuse_1.w * i_4.color_0.w * texel_2.w;
    var lightColor_1 : vec3<f32> = frame_0.lightColor_0.xyz;
    var _S171 : vec3<f32> = hemisphereAmbient_0(Nl_0) * albedo_4 * vec3<f32>(0.30000001192092896f);
    var ambient_0 : vec3<f32>;
    if(((frame_0.giOrigin_0.w) > 0.5f))
    {
        var giOk_0 : bool;
        var e_6 : vec3<f32> = giIrradiance_1(i_4.worldPos_0, Nl_0, (*V_5), &(giOk_0));
        var x_5 : f32 = giToonCoord_0(e_6);
        var aa_1 : f32 = (fwidth((x_5)));
        if(giOk_0)
        {
            ambient_0 = giToonBandAt_0(e_6, x_5, aa_1) * vec3<f32>((frame_0.giSpacing_0.w / 3.1415927410125732f)) * albedo_4;
        }
        else
        {
            ambient_0 = _S171;
        }
    }
    else
    {
        ambient_0 = _S171;
    }
    var color_5 : vec3<f32>;
    if(((draw_0.rampParams_0.x) > 0.5f))
    {
        var reflection_0 : vec3<f32>;
        var styled_0 : vec3<f32> = styledToonAlbedo_0(albedo_4, Nl_0, (*V_5), L_4, saturate(dot(Nl_0, L_4)), castShadow_1, &(reflection_0));
        var _S172 : vec3<f32> = vec3<f32>((1.0f - draw_0.metalA_0.x));
        var color_6 : vec3<f32> = styled_0 * lightColor_1 + reflection_0 * (vec3<f32>(1.0f) - lightColor_1) + ambient_0 * _S172;
        var _S173 : vec3<f32> = accumulateLocalLights_0(i_4.worldPos_0, albedo_4, Nl_0, (*V_5), false, i_4.pos_0.xy);
        color_5 = color_6 + _S173 * _S172;
    }
    else
    {
        var lambert_4 : f32 = saturate(dot(Nl_0, L_4)) * castShadow_1;
        var color_7 : vec3<f32> = albedo_4 * toonTone_0(toonRamp_0(lambert_4)) * lightColor_1 + ambient_0;
        var _S174 : vec3<f32> = accumulateLocalLights_0(i_4.worldPos_0, albedo_4, Nl_0, (*V_5), false, i_4.pos_0.xy);
        var color_8 : vec3<f32> = color_7 + _S174;
        if(((frame_0.toonParams_0.z) > 0.0f))
        {
            var spec_0 : f32 = pow(saturate(dot(Nl_0, normalize(L_4 + (*V_5)))), max(frame_0.toonParams_0.w * draw_0.toonMaterial_0.w, 1.0f));
            var half_1 : f32 = toonEdgeHalfWidth_0(frame_0.toonParams_0.y * 0.5f, (fwidth((spec_0))));
            color_5 = color_8 + lightColor_1 * draw_0.toonMaterial_0.xyz * vec3<f32>(frame_0.toonParams_0.z) * vec3<f32>((smoothstep(0.5f - half_1, 0.5f + half_1, spec_0) * step(0.0010000000474974513f, lambert_4)));
        }
        else
        {
            color_5 = color_8;
        }
        color_5 = color_5 + frame_0.rimParams_0.xyz * vec3<f32>(pow(1.0f - saturate(dot(Nl_0, (*V_5))), frame_0.rimParams_0.w));
    }
    return vec4<f32>(applyFog_0(color_5 + (sampleEmissive_0(i_4.uv_0) + draw_0.tintAdd_0.xyz), i_4.worldPos_0), alpha_0);
}

fn occluderFadeAmount_0( W_0 : vec3<f32>,  N_13 : vec3<f32>) -> f32
{
    var _S175 : u32 = u32(frame_0.fadeCamera_0.w);
    var v_2 : vec3<f32> = W_0 - frame_0.fadeCamera_0.xyz;
    var _S176 : vec3<f32>;
    if(((dot(N_13, v_2)) > 0.0f))
    {
        _S176 = (vec3<f32>(0) - N_13);
    }
    else
    {
        _S176 = N_13;
    }
    var amount_0 : f32 = 0.0f;
    var i_5 : u32 = u32(0);
    loop
    {
        if((i_5 < u32(4)))
        {
        }
        else
        {
            break;
        }
        if((i_5 >= _S175))
        {
            break;
        }
        var T_5 : vec3<f32> = frame_0.fadeTargets_0[i_5].xyz;
        var r_4 : f32 = frame_0.fadeTargets_0[i_5].w;
        var axis_0 : vec3<f32> = frame_0.fadeCones_0[i_5].xyz;
        var s_1 : f32 = dot(v_2, axis_0);
        var _S177 : f32 = max(amount_0, (1.0f - smoothstep(1.0f - frame_0.fadeParams_0.y, 1.0f, length(v_2 - axis_0 * vec3<f32>(s_1)) / max(s_1 * frame_0.fadeCones_0[i_5].w, 9.9999997473787516e-06f))) * saturate((dot(T_5 - frame_0.fadeCamera_0.xyz, axis_0) - s_1) / r_4) * smoothstep(0.25f * r_4, -0.25f * r_4, dot(T_5 - W_0, _S176)));
        var i_6 : u32 = i_5 + u32(1);
        amount_0 = _S177;
        i_5 = i_6;
    }
    return amount_0 * frame_0.fadeParams_0.x;
}

fn occluderFaded_0( pixel_2 : vec2<f32>,  W_1 : vec3<f32>,  N_14 : vec3<f32>) -> bool
{
    if(((frame_0.fadeCamera_0.w) < 0.5f))
    {
        return false;
    }
    var p_3 : vec2<u32> = (vec2<u32>(pixel_2) & (vec2<u32>(u32(3), u32(3))));
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
    @location(0) color_9 : vec4<f32>,
    @location(1) normal_3 : vec4<f32>,
    @location(2) depth_0 : vec4<f32>,
};

fn packOutput_0( color_10 : vec3<f32>,  alpha_1 : f32,  Ng_2 : vec3<f32>,  V_6 : vec3<f32>,  viewDepth_5 : f32,  pixel_3 : vec2<f32>) -> FsOut_0
{
    var o_3 : FsOut_0;
    o_3.color_9 = vec4<f32>(storeHdr_0(color_10, frame_0.lightInfo_0.y, pixel_3), alpha_1);
    var _S178 : vec3<f32> = vec3<f32>(0.5f);
    o_3.normal_3 = vec4<f32>(Ng_2 * _S178 + _S178, packSlotNdv_0(draw_0.rampParams_0.w, dot(Ng_2, V_6)));
    if(((draw_0.misc_0.x) > 0.5f))
    {
        const _S179 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
        o_3.normal_3.x = _S179.x;
        o_3.normal_3.y = _S179.y;
        o_3.normal_3.z = _S179.z;
    }
    o_3.depth_0 = packLinearDepth_0(viewDepth_5);
    return o_3;
}

struct pixelInput_2
{
    @location(0) worldPos_7 : vec3<f32>,
    @location(1) worldNorm_1 : vec3<f32>,
    @location(2) color_11 : vec4<f32>,
    @location(3) viewDepth_6 : f32,
    @location(4) uv_12 : vec2<f32>,
    @interpolate(flat, either) @location(5) diffuse_3 : vec4<f32>,
};

@fragment
fn fsMain( _S180 : pixelInput_2, @builtin(position) pos_7 : vec4<f32>) -> FsOut_0
{
    var _S181 : VSOut_0 = VSOut_0( pos_7, _S180.worldPos_7, _S180.worldNorm_1, _S180.color_11, _S180.viewDepth_6, _S180.uv_12, _S180.diffuse_3 );
    var N_15 : vec3<f32>;
    var V_7 : vec3<f32>;
    var c_8 : vec4<f32> = shadeScene_0(_S181, &(N_15), &(V_7));
    var _S182 : bool;
    if(((draw_0.misc_0.y) < 0.5f))
    {
        _S182 = occluderFaded_0(pos_7.xy, _S180.worldPos_7, _S180.worldNorm_1);
    }
    else
    {
        _S182 = false;
    }
    if(_S182)
    {
        discard;
    }
    if(((draw_0.mapFlags_0.w) > 0.0f))
    {
        _S182 = ((baseTexel_0(_S180.uv_12).w) < (draw_0.mapFlags_0.w));
    }
    else
    {
        _S182 = false;
    }
    if(_S182)
    {
        discard;
    }
    return packOutput_0(c_8.xyz, c_8.w, N_15, V_7, _S180.viewDepth_6, pos_7.xy);
}

fn shadePhong_0( i_7 : VSOut_0) -> vec4<f32>
{
    var Ng_3 : vec3<f32> = normalize(i_7.worldNorm_0);
    var texel_3 : vec4<f32> = baseTexel_0(i_7.uv_0);
    var N_16 : vec3<f32> = perturbNormal_0(Ng_3, i_7.worldPos_0, i_7.uv_0);
    var L_5 : vec3<f32> = normalize((vec3<f32>(0) - frame_0.lightDir_0.xyz));
    var V_8 : vec3<f32> = normalize(frame_0.cameraPos_0.xyz - i_7.worldPos_0);
    var albedo_5 : vec3<f32> = i_7.diffuse_1.xyz * i_7.color_0.xyz * texel_3.xyz;
    applyDecals_0(i_7.worldPos_0, Ng_3, &(albedo_5), &(N_16));
    var alpha_2 : f32 = i_7.diffuse_1.w * i_7.color_0.w * texel_3.w;
    var castShadow_2 : f32 = sampleCascadedShadow_0(i_7.worldPos_0, N_16, L_5, length(frame_0.cameraPos_0.xyz - i_7.worldPos_0));
    var _S183 : vec3<f32> = frame_0.ambientFlat_0.xyz;
    var ambient_1 : vec3<f32>;
    if(((frame_0.giOrigin_0.w) > 0.5f))
    {
        var giOk_1 : bool;
        var e_7 : vec3<f32> = giIrradiance_0(i_7.worldPos_0, N_16, V_8, &(giOk_1));
        if(giOk_1)
        {
            ambient_1 = e_7 * vec3<f32>((frame_0.giSpacing_0.w / 3.1415927410125732f));
        }
        else
        {
            ambient_1 = _S183;
        }
    }
    else
    {
        ambient_1 = _S183;
    }
    var NdotL_0 : f32 = saturate(dot(N_16, L_5));
    var color_12 : vec3<f32> = ambient_1 * albedo_5 + frame_0.lightColor_0.xyz * albedo_5 * vec3<f32>(NdotL_0) * vec3<f32>(castShadow_2) + frame_0.lightColor_0.xyz * draw_0.phong_0.xyz * vec3<f32>((pow(saturate(dot(N_16, normalize(L_5 + V_8))), max(draw_0.phong_0.w, 1.0f)) * NdotL_0 * castShadow_2));
    var _S184 : vec2<f32> = i_7.pos_0.xy;
    var _S185 : vec3<f32> = accumulateLocalLights_0(i_7.worldPos_0, albedo_5, N_16, V_8, true, _S184);
    return vec4<f32>(storeHdr_0(applyFog_0(color_12 + _S185 + (sampleEmissive_0(i_7.uv_0) + draw_0.tintAdd_0.xyz), i_7.worldPos_0), frame_0.lightInfo_0.y, _S184), alpha_2);
}

struct pixelOutput_2
{
    @location(0) output_2 : vec4<f32>,
};

struct pixelInput_3
{
    @location(0) worldPos_8 : vec3<f32>,
    @location(1) worldNorm_2 : vec3<f32>,
    @location(2) color_13 : vec4<f32>,
    @location(3) viewDepth_7 : f32,
    @location(4) uv_13 : vec2<f32>,
    @interpolate(flat, either) @location(5) diffuse_4 : vec4<f32>,
};

@fragment
fn fsMainColor( _S186 : pixelInput_3, @builtin(position) pos_8 : vec4<f32>) -> pixelOutput_2
{
    var _S187 : VSOut_0 = VSOut_0( pos_8, _S186.worldPos_8, _S186.worldNorm_2, _S186.color_13, _S186.viewDepth_7, _S186.uv_13, _S186.diffuse_4 );
    var _S188 : vec4<f32> = shadePhong_0(_S187);
    var _S189 : pixelOutput_2 = pixelOutput_2( _S188 );
    return _S189;
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
    @location(2) color_14 : vec4<f32>,
    @location(3) viewDepth_8 : f32,
    @location(4) uv_14 : vec2<f32>,
    @interpolate(flat, either) @location(5) diffuse_5 : vec4<f32>,
};

@fragment
fn fsMainOit( _S190 : pixelInput_4, @builtin(position) pos_9 : vec4<f32>) -> OitOut_0
{
    var _S191 : VSOut_0 = VSOut_0( pos_9, _S190.worldPos_9, _S190.worldNorm_3, _S190.color_14, _S190.viewDepth_8, _S190.uv_14, _S190.diffuse_5 );
    var c_9 : vec4<f32> = shadePhong_0(_S191);
    var _S192 : f32 = c_9.w;
    var o_4 : OitOut_0;
    o_4.accum_0 = vec4<f32>(c_9.xyz * vec3<f32>(_S192), _S192) * vec4<f32>(clamp(pow(min(1.0f, _S192 * 10.0f) + 0.0099999997764825821f, 3.0f) * 1000.0f * pow(1.0f - pos_9.z * 0.89999997615814209f, 3.0f), 0.0099999997764825821f, 3000.0f));
    o_4.reveal_0 = _S192;
    return o_4;
}

struct SkyOut_0
{
    @builtin(position) pos_10 : vec4<f32>,
    @location(0) ndc_4 : vec2<f32>,
};

@vertex
fn vsSky(@builtin(vertex_index) vid_0 : u32) -> SkyOut_0
{
    var o_5 : SkyOut_0;
    var _S193 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f);
    o_5.ndc_4 = _S193;
    o_5.pos_10 = vec4<f32>(_S193, 0.0f, 1.0f);
    return o_5;
}

fn srgbToLinearChannel_0( x_6 : f32) -> f32
{
    var _S194 : f32;
    if((x_6 <= 0.040449999272823334f))
    {
        _S194 = x_6 / 12.920000076293945f;
    }
    else
    {
        _S194 = pow((x_6 + 0.054999999701976776f) / 1.0549999475479126f, 2.4000000953674316f);
    }
    return _S194;
}

struct pixelInput_5
{
    @location(0) ndc_5 : vec2<f32>,
};

@fragment
fn fsSky( _S195 : pixelInput_5, @builtin(position) pos_11 : vec4<f32>) -> FsOut_0
{
    var dir_1 : vec3<f32> = normalize(frame_0.camForward_0.xyz + vec3<f32>(_S195.ndc_5.x) * frame_0.camRight_0.xyz + vec3<f32>(_S195.ndc_5.y) * frame_0.camUp_0.xyz);
    var a_4 : vec3<f32> = abs(dir_1);
    var _S196 : f32 = max(a_4.x, a_4.z);
    var c_10 : vec3<f32>;
    if(((a_4.y) >= _S196))
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
        c_10 = mix(frame_0.skyNadir_0.xyz, frame_0.skyZenith_0.xyz, vec3<f32>((clamp(dir_1.y / _S196, -1.0f, 1.0f) * 0.5f + 0.5f)));
    }
    var o_6 : FsOut_0;
    o_6.color_9 = vec4<f32>(storeHdr_0(vec3<f32>(srgbToLinearChannel_0(c_10.x), srgbToLinearChannel_0(c_10.y), srgbToLinearChannel_0(c_10.z)), frame_0.lightInfo_0.y, pos_11.xy), 1.0f);
    o_6.normal_3 = vec4<f32>(0.5f, 0.5f, 0.5f, 0.0f);
    o_6.depth_0 = vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    return o_6;
}

fn fogHg_0( g_1 : f32,  c_11 : f32) -> f32
{
    var g2_0 : f32 = g_1 * g_1;
    return (1.0f - g2_0) / (12.566370964050293f * pow(max(1.0f + g2_0 - 2.0f * g_1 * c_11, 9.9999997473787516e-05f), 1.5f));
}

fn fogShadowTap_0( nearMap_0 : bool,  clip_0 : vec4<f32>,  column_1 : f32,  columns_0 : f32) -> f32
{
    var ndc_6 : vec3<f32> = clip_0.xyz / vec3<f32>(max(clip_0.w, 9.9999997473787516e-05f));
    var _S197 : f32 = ndc_6.x * 0.5f + 0.5f;
    var uv_15 : vec2<f32> = vec2<f32>(_S197, - ndc_6.y * 0.5f + 0.5f);
    var _S198 : bool;
    if((_S197 < 0.0f))
    {
        _S198 = true;
    }
    else
    {
        _S198 = ((uv_15.x) > 1.0f);
    }
    if(_S198)
    {
        _S198 = true;
    }
    else
    {
        _S198 = ((uv_15.y) < 0.0f);
    }
    if(_S198)
    {
        _S198 = true;
    }
    else
    {
        _S198 = ((uv_15.y) > 1.0f);
    }
    if(_S198)
    {
        _S198 = true;
    }
    else
    {
        _S198 = ((ndc_6.z) < 0.0f);
    }
    if(_S198)
    {
        _S198 = true;
    }
    else
    {
        _S198 = ((ndc_6.z) > 1.0f);
    }
    if(_S198)
    {
        return 1.0f;
    }
    uv_15[i32(0)] = (uv_15.x + column_1) / columns_0;
    var depthRef_4 : f32 = ndc_6.z - 0.0020000000949949026f;
    var _S199 : f32;
    if(nearMap_0)
    {
        _S199 = (textureSampleCompareLevel((shadowNear_0), (shadowPcf_0), (uv_15), (depthRef_4)));
    }
    else
    {
        _S199 = (textureSampleCompareLevel((shadowFar_0), (shadowPcf_0), (uv_15), (depthRef_4)));
    }
    return _S199;
}

fn fogSunShadow_0( wp_4 : vec3<f32>,  dist_0 : f32) -> f32
{
    var _S200 : bool;
    if(((frame_0.fogC_0.w) < 0.5f))
    {
        _S200 = true;
    }
    else
    {
        _S200 = ((frame_0.shadowTexel_0.z) < 0.5f);
    }
    if(_S200)
    {
        return 1.0f;
    }
    if(((frame_0.shadowTexel_0.z) < 1.5f))
    {
        _S200 = true;
    }
    else
    {
        _S200 = (dist_0 < (frame_0.shadowSplit_0.x));
    }
    if(_S200)
    {
        return fogShadowTap_0(true, (((vec4<f32>(wp_4, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP0_0.data_0[i32(0)][i32(0)], frame_0.shadowVP0_0.data_0[i32(1)][i32(0)], frame_0.shadowVP0_0.data_0[i32(2)][i32(0)], frame_0.shadowVP0_0.data_0[i32(3)][i32(0)], frame_0.shadowVP0_0.data_0[i32(0)][i32(1)], frame_0.shadowVP0_0.data_0[i32(1)][i32(1)], frame_0.shadowVP0_0.data_0[i32(2)][i32(1)], frame_0.shadowVP0_0.data_0[i32(3)][i32(1)], frame_0.shadowVP0_0.data_0[i32(0)][i32(2)], frame_0.shadowVP0_0.data_0[i32(1)][i32(2)], frame_0.shadowVP0_0.data_0[i32(2)][i32(2)], frame_0.shadowVP0_0.data_0[i32(3)][i32(2)], frame_0.shadowVP0_0.data_0[i32(0)][i32(3)], frame_0.shadowVP0_0.data_0[i32(1)][i32(3)], frame_0.shadowVP0_0.data_0[i32(2)][i32(3)], frame_0.shadowVP0_0.data_0[i32(3)][i32(3)])))), 0.0f, 1.0f);
    }
    if(((frame_0.shadowTexel_0.z) < 2.5f))
    {
        _S200 = true;
    }
    else
    {
        _S200 = (dist_0 < (frame_0.shadowSplit_0.y));
    }
    if(_S200)
    {
        return fogShadowTap_0(false, (((vec4<f32>(wp_4, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP1_0.data_0[i32(0)][i32(0)], frame_0.shadowVP1_0.data_0[i32(1)][i32(0)], frame_0.shadowVP1_0.data_0[i32(2)][i32(0)], frame_0.shadowVP1_0.data_0[i32(3)][i32(0)], frame_0.shadowVP1_0.data_0[i32(0)][i32(1)], frame_0.shadowVP1_0.data_0[i32(1)][i32(1)], frame_0.shadowVP1_0.data_0[i32(2)][i32(1)], frame_0.shadowVP1_0.data_0[i32(3)][i32(1)], frame_0.shadowVP1_0.data_0[i32(0)][i32(2)], frame_0.shadowVP1_0.data_0[i32(1)][i32(2)], frame_0.shadowVP1_0.data_0[i32(2)][i32(2)], frame_0.shadowVP1_0.data_0[i32(3)][i32(2)], frame_0.shadowVP1_0.data_0[i32(0)][i32(3)], frame_0.shadowVP1_0.data_0[i32(1)][i32(3)], frame_0.shadowVP1_0.data_0[i32(2)][i32(3)], frame_0.shadowVP1_0.data_0[i32(3)][i32(3)])))), 0.0f, 2.0f);
    }
    return fogShadowTap_0(false, (((vec4<f32>(wp_4, 1.0f)) * (mat4x4<f32>(frame_0.shadowVP2_0.data_0[i32(0)][i32(0)], frame_0.shadowVP2_0.data_0[i32(1)][i32(0)], frame_0.shadowVP2_0.data_0[i32(2)][i32(0)], frame_0.shadowVP2_0.data_0[i32(3)][i32(0)], frame_0.shadowVP2_0.data_0[i32(0)][i32(1)], frame_0.shadowVP2_0.data_0[i32(1)][i32(1)], frame_0.shadowVP2_0.data_0[i32(2)][i32(1)], frame_0.shadowVP2_0.data_0[i32(3)][i32(1)], frame_0.shadowVP2_0.data_0[i32(0)][i32(2)], frame_0.shadowVP2_0.data_0[i32(1)][i32(2)], frame_0.shadowVP2_0.data_0[i32(2)][i32(2)], frame_0.shadowVP2_0.data_0[i32(3)][i32(2)], frame_0.shadowVP2_0.data_0[i32(0)][i32(3)], frame_0.shadowVP2_0.data_0[i32(1)][i32(3)], frame_0.shadowVP2_0.data_0[i32(2)][i32(3)], frame_0.shadowVP2_0.data_0[i32(3)][i32(3)])))), 1.0f, 2.0f);
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
fn fsFog( _S201 : pixelInput_6, @builtin(position) pos_12 : vec4<f32>) -> pixelOutput_3
{
    var e_8 : vec4<f32> = (textureLoad((fogLineDepth_0), (vec2<i32>(pos_12.xy)), (i32(0))));
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
    var _S202 : f32 = sqrt(saturate(dist_1 / max(range_0, 9.9999997473787516e-05f)));
    var dir_2 : vec3<f32> = normalize(frame_0.camForward_0.xyz + vec3<f32>(_S201.ndc_7.x) * frame_0.camRight_0.xyz + vec3<f32>(_S201.ndc_7.y) * frame_0.camUp_0.xyz);
    var _S203 : f32 = fogHg_0(frame_0.fogB_0.w, dot(dir_2, (vec3<f32>(0) - normalize(frame_0.lightDir_0.xyz))));
    const _S204 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var k_5 : i32 = i32(0);
    var T_6 : f32 = 1.0f;
    var S_0 : vec3<f32> = _S204;
    loop
    {
        if((k_5 < i32(24)))
        {
        }
        else
        {
            break;
        }
        var t0_2 : f32 = _S202 * f32(k_5) / 24.0f;
        var _S205 : i32 = k_5 + i32(1);
        var t1_2 : f32 = _S202 * f32(_S205) / 24.0f;
        var _S206 : f32 = 0.5f * (t0_2 + t1_2);
        var dm_0 : f32 = range_0 * _S206 * _S206;
        var wp_5 : vec3<f32> = frame_0.cameraPos_0.xyz + dir_2 * vec3<f32>(dm_0);
        var _S207 : f32 = max(frame_0.fogA_0.x * exp(- frame_0.fogA_0.y * (wp_5.y - frame_0.fogA_0.z)) + frame_0.fogA_0.w, 0.0f);
        var inject_0 : vec3<f32> = frame_0.fogB_0.xyz * vec3<f32>(_S207) * (frame_0.fogC_0.xyz * vec3<f32>(fogSunShadow_0(wp_5, dm_0)) * vec3<f32>(_S203) + frame_0.fogD_0.xyz);
        var _S208 : f32 = max(_S207, 9.99999997475242708e-07f);
        var segT_0 : f32 = exp(- _S208 * (range_0 * t1_2 * t1_2 - range_0 * t0_2 * t0_2));
        var S_1 : vec3<f32> = S_0 + vec3<f32>(T_6) * (inject_0 - inject_0 * vec3<f32>(segT_0)) / vec3<f32>(_S208);
        var T_7 : f32 = T_6 * segT_0;
        k_5 = _S205;
        T_6 = T_7;
        S_0 = S_1;
    }
    var _S209 : pixelOutput_3 = pixelOutput_3( vec4<f32>(S_0 * vec3<f32>(frame_0.lightInfo_0.y), T_6) );
    return _S209;
}

