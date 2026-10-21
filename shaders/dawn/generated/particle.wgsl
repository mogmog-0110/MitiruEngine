struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct ParticleCB_std140_0
{
    @align(16) viewProj_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) params_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> pcb_0 : ParticleCB_std140_0;
@binding(1) @group(0) var vfxColor_0 : texture_2d_array<f32>;

@binding(2) @group(0) var vfxSampler_0 : sampler;

@binding(3) @group(0) var sceneDepth_0 : texture_multisampled_2d<f32>;

struct VsOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
    @location(1) color_0 : vec4<f32>,
    @location(2) info_0 : vec4<f32>,
    @location(3) shadow_0 : vec3<f32>,
    @location(4) extra_0 : vec4<f32>,
    @location(5) depth_0 : f32,
};

struct vertexInput_0
{
    @location(0) pos_1 : vec3<f32>,
    @location(1) uv_1 : vec2<f32>,
    @location(2) color_1 : vec4<f32>,
    @location(3) info_1 : vec4<f32>,
    @location(4) shadow_1 : vec3<f32>,
    @location(5) extra_1 : vec4<f32>,
};

@vertex
fn vsParticle( _S1 : vertexInput_0) -> VsOut_0
{
    var o_0 : VsOut_0;
    var _S2 : vec4<f32> = (((vec4<f32>(_S1.pos_1, 1.0f)) * (mat4x4<f32>(pcb_0.viewProj_0.data_0[i32(0)][i32(0)], pcb_0.viewProj_0.data_0[i32(1)][i32(0)], pcb_0.viewProj_0.data_0[i32(2)][i32(0)], pcb_0.viewProj_0.data_0[i32(3)][i32(0)], pcb_0.viewProj_0.data_0[i32(0)][i32(1)], pcb_0.viewProj_0.data_0[i32(1)][i32(1)], pcb_0.viewProj_0.data_0[i32(2)][i32(1)], pcb_0.viewProj_0.data_0[i32(3)][i32(1)], pcb_0.viewProj_0.data_0[i32(0)][i32(2)], pcb_0.viewProj_0.data_0[i32(1)][i32(2)], pcb_0.viewProj_0.data_0[i32(2)][i32(2)], pcb_0.viewProj_0.data_0[i32(3)][i32(2)], pcb_0.viewProj_0.data_0[i32(0)][i32(3)], pcb_0.viewProj_0.data_0[i32(1)][i32(3)], pcb_0.viewProj_0.data_0[i32(2)][i32(3)], pcb_0.viewProj_0.data_0[i32(3)][i32(3)]))));
    o_0.pos_0 = _S2;
    o_0.uv_0 = _S1.uv_1;
    o_0.color_0 = _S1.color_1;
    o_0.info_0 = _S1.info_1;
    o_0.shadow_0 = _S1.shadow_1;
    o_0.extra_0 = _S1.extra_1;
    o_0.depth_0 = _S2.w;
    return o_0;
}

fn hardEdge_0( r_0 : f32) -> f32
{
    return saturate((1.0f - r_0) / max((fwidth((r_0))), 9.9999997473787516e-05f) + 0.5f);
}

fn sprite_0( i_0 : VsOut_0) -> vec4<f32>
{
    var gx_0 : vec2<f32> = dpdx(i_0.uv_0);
    var gy_0 : vec2<f32> = dpdy(i_0.uv_0);
    var r_1 : f32 = length(i_0.uv_0 * vec2<f32>(2.0f) - vec2<f32>(1.0f, 1.0f));
    var hard_0 : f32 = hardEdge_0(r_1);
    var _S3 : f32 = i_0.extra_0.x;
    if((_S3 >= 0.0f))
    {
        var _S4 : vec3<f32> = vec3<f32>(i_0.uv_0, _S3);
        return (textureSampleGrad((vfxColor_0), (vfxSampler_0), ((_S4)).xy, i32(((_S4)).z), (gx_0), (gy_0)));
    }
    var _S5 : f32 = i_0.info_0.x;
    if((_S5 > 1.5f))
    {
        return vec4<f32>(1.0f, 1.0f, 1.0f, hard_0);
    }
    if((_S5 > 0.5f))
    {
        return vec4<f32>(1.0f, 1.0f, 1.0f, saturate((1.0f - r_1) * 3.0f));
    }
    var a_0 : f32 = saturate(1.0f - r_1);
    return vec4<f32>(1.0f, 1.0f, 1.0f, a_0 * a_0);
}

fn decodeSceneDepth_0( p_0 : vec2<i32>) -> f32
{
    var c_0 : vec4<f32> = (textureLoad((sceneDepth_0), (p_0), (i32(0))));
    return f32((((((u32(c_0.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(c_0.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(c_0.z * 255.0f + 0.5f)))) / 1.6777215e+07f * pcb_0.params_0.x;
}

fn softFade_0( i_1 : VsOut_0) -> f32
{
    var _S6 : f32 = i_1.extra_0.y;
    if(!(_S6 > 0.0f))
    {
        return 1.0f;
    }
    return saturate((decodeSceneDepth_0(vec2<i32>(i_1.pos_0.xy)) - i_1.depth_0) / _S6);
}

fn baseColor_0( i_2 : VsOut_0) -> vec3<f32>
{
    var lit_0 : f32 = hardEdge_0(length(i_2.uv_0 * vec2<f32>(2.0f) - vec2<f32>(1.0f, 1.0f) - vec2<f32>(i_2.info_0.z, - i_2.info_0.w)));
    if(((i_2.info_0.y) < 0.5f))
    {
        return i_2.color_0.xyz;
    }
    return mix(i_2.shadow_0, i_2.color_0.xyz, vec3<f32>(lit_0));
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) uv_2 : vec2<f32>,
    @location(1) color_2 : vec4<f32>,
    @location(2) info_2 : vec4<f32>,
    @location(3) shadow_2 : vec3<f32>,
    @location(4) extra_2 : vec4<f32>,
    @location(5) depth_1 : f32,
};

@fragment
fn fsAdditive( _S7 : pixelInput_0, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_0
{
    var _S8 : VsOut_0 = VsOut_0( pos_2, _S7.uv_2, _S7.color_2, _S7.info_2, _S7.shadow_2, _S7.extra_2, _S7.depth_1 );
    var s_0 : vec4<f32> = sprite_0(_S8);
    var a_1 : f32 = _S7.color_2.w * s_0.w * softFade_0(_S8);
    var _S9 : pixelOutput_0 = pixelOutput_0( vec4<f32>(baseColor_0(_S8) * s_0.xyz * vec3<f32>(a_1) * vec3<f32>(pcb_0.params_0.y), a_1) );
    return _S9;
}

fn neutralShoulder_0( c_1 : vec3<f32>) -> vec3<f32>
{
    var _S10 : f32 = max(c_1.x, max(c_1.y, c_1.z));
    if((_S10 <= 0.89999997615814209f))
    {
        return c_1;
    }
    return c_1 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S10 + 0.10000002384185791f - 0.89999997615814209f)) / _S10));
}

fn storeHdr_0( c_2 : vec3<f32>,  scale_0 : f32,  pixel_0 : vec2<f32>) -> vec3<f32>
{
    if((scale_0 >= 1.0f))
    {
        return c_2;
    }
    const _S11 : vec2<f32> = vec2<f32>(0.067110560834407806f, 0.0058371499180793762f);
    var n0_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0, _S11)));
    var n1_0 : f32 = fract(52.982917785644531f * fract(dot(pixel_0 + vec2<f32>(17.0f, 31.0f), _S11)));
    return neutralShoulder_0(c_2 * vec3<f32>(scale_0)) + vec3<f32>(n0_0 + n1_0 - 1.0f, n1_0 + n0_0 - 1.0f, n0_0 - n1_0) * vec3<f32>(0.00097751710563898087f);
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) uv_3 : vec2<f32>,
    @location(1) color_3 : vec4<f32>,
    @location(2) info_3 : vec4<f32>,
    @location(3) shadow_3 : vec3<f32>,
    @location(4) extra_3 : vec4<f32>,
    @location(5) depth_2 : f32,
};

@fragment
fn fsAlpha( _S12 : pixelInput_1, @builtin(position) pos_3 : vec4<f32>) -> pixelOutput_1
{
    var _S13 : VsOut_0 = VsOut_0( pos_3, _S12.uv_3, _S12.color_3, _S12.info_3, _S12.shadow_3, _S12.extra_3, _S12.depth_2 );
    var s_1 : vec4<f32> = sprite_0(_S13);
    var _S14 : pixelOutput_1 = pixelOutput_1( vec4<f32>(storeHdr_0(baseColor_0(_S13) * s_1.xyz, pcb_0.params_0.y, pos_3.xy), saturate(_S12.color_3.w * s_1.w * softFade_0(_S13))) );
    return _S14;
}

fn packLinearDepth_0( viewDepth_0 : f32) -> vec4<f32>
{
    var v_0 : u32 = u32(saturate(viewDepth_0 / max(pcb_0.params_0.x, 0.0010000000474974513f)) * 1.6777215e+07f + 0.5f);
    return vec4<f32>(f32((((v_0 >> (u32(16)))) & (u32(255)))), f32((((v_0 >> (u32(8)))) & (u32(255)))), f32((v_0 & (u32(255)))), 255.0f) / vec4<f32>(255.0f);
}

struct OpaqueOut_0
{
    @location(0) color_4 : vec4<f32>,
    @location(1) normal_0 : vec4<f32>,
    @location(2) depth_3 : vec4<f32>,
};

struct pixelInput_2
{
    @location(0) uv_4 : vec2<f32>,
    @location(1) color_5 : vec4<f32>,
    @location(2) info_4 : vec4<f32>,
    @location(3) shadow_4 : vec3<f32>,
    @location(4) extra_4 : vec4<f32>,
    @location(5) depth_4 : f32,
};

@fragment
fn fsOpaque( _S15 : pixelInput_2, @builtin(position) pos_4 : vec4<f32>) -> OpaqueOut_0
{
    var _S16 : VsOut_0 = VsOut_0( pos_4, _S15.uv_4, _S15.color_5, _S15.info_4, _S15.shadow_4, _S15.extra_4, _S15.depth_4 );
    var s_2 : vec4<f32> = sprite_0(_S16);
    var o_1 : OpaqueOut_0;
    o_1.color_4 = vec4<f32>(storeHdr_0(baseColor_0(_S16) * s_2.xyz, pcb_0.params_0.y, pos_4.xy), s_2.w);
    o_1.normal_0 = vec4<f32>(1.0f, 1.0f, 1.0f, 0.12156862765550613f);
    o_1.depth_3 = packLinearDepth_0(_S15.depth_4);
    return o_1;
}

struct pixelInput_3
{
    @location(0) uv_5 : vec2<f32>,
    @location(1) color_6 : vec4<f32>,
    @location(2) info_5 : vec4<f32>,
    @location(3) shadow_5 : vec3<f32>,
    @location(4) extra_5 : vec4<f32>,
    @location(5) depth_5 : f32,
};

@fragment
fn fsOpaqueCut( _S17 : pixelInput_3, @builtin(position) pos_5 : vec4<f32>) -> OpaqueOut_0
{
    var _S18 : VsOut_0 = VsOut_0( pos_5, _S17.uv_5, _S17.color_6, _S17.info_5, _S17.shadow_5, _S17.extra_5, _S17.depth_5 );
    var s_3 : vec4<f32> = sprite_0(_S18);
    if(((s_3.w) < 0.5f))
    {
        discard;
    }
    var o_2 : OpaqueOut_0;
    o_2.color_4 = vec4<f32>(storeHdr_0(baseColor_0(_S18) * s_3.xyz, pcb_0.params_0.y, pos_5.xy), 1.0f);
    o_2.normal_0 = vec4<f32>(1.0f, 1.0f, 1.0f, 0.12156862765550613f);
    o_2.depth_3 = packLinearDepth_0(_S17.depth_5);
    return o_2;
}

