struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct LitCB_std140_0
{
    @align(16) ortho_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) mode_0 : vec4<f32>,
    @align(16) map_0 : vec4<f32>,
    @align(16) ambient_0 : vec4<f32>,
    @align(16) lights_0 : array<vec4<f32>, i32(48)>,
};

@binding(0) @group(0) var<uniform> lit_0 : LitCB_std140_0;
@binding(1) @group(0) var colorTex_0 : texture_2d<f32>;

@binding(3) @group(0) var linearSampler_0 : sampler;

@binding(4) @group(0) var pointSampler_0 : sampler;

@binding(2) @group(0) var normalTex_0 : texture_2d<f32>;

struct LitOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
    @location(1) color_0 : vec4<f32>,
};

struct vertexInput_0
{
    @location(0) pos_1 : vec2<f32>,
    @location(1) uv_1 : vec2<f32>,
    @location(2) color_1 : vec4<f32>,
};

@vertex
fn vsLit( _S1 : vertexInput_0) -> LitOut_0
{
    var o_0 : LitOut_0;
    o_0.pos_0 = (((vec4<f32>(_S1.pos_1, 0.0f, 1.0f)) * (mat4x4<f32>(lit_0.ortho_0.data_0[i32(0)][i32(0)], lit_0.ortho_0.data_0[i32(1)][i32(0)], lit_0.ortho_0.data_0[i32(2)][i32(0)], lit_0.ortho_0.data_0[i32(3)][i32(0)], lit_0.ortho_0.data_0[i32(0)][i32(1)], lit_0.ortho_0.data_0[i32(1)][i32(1)], lit_0.ortho_0.data_0[i32(2)][i32(1)], lit_0.ortho_0.data_0[i32(3)][i32(1)], lit_0.ortho_0.data_0[i32(0)][i32(2)], lit_0.ortho_0.data_0[i32(1)][i32(2)], lit_0.ortho_0.data_0[i32(2)][i32(2)], lit_0.ortho_0.data_0[i32(3)][i32(2)], lit_0.ortho_0.data_0[i32(0)][i32(3)], lit_0.ortho_0.data_0[i32(1)][i32(3)], lit_0.ortho_0.data_0[i32(2)][i32(3)], lit_0.ortho_0.data_0[i32(3)][i32(3)]))));
    o_0.uv_0 = _S1.uv_1;
    o_0.color_0 = _S1.color_1;
    return o_0;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) uv_2 : vec2<f32>,
    @location(1) color_2 : vec4<f32>,
};

@fragment
fn fsLit( _S2 : pixelInput_0, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_0
{
    var duvdx_0 : vec2<f32> = dpdx(_S2.uv_2);
    var duvdy_0 : vec2<f32> = dpdy(_S2.uv_2);
    var uvWidth_0 : vec2<f32> = (fwidth((_S2.uv_2)));
    var usePoint_0 : bool = ((lit_0.mode_0.w) > 0.5f);
    var baseColor_0 : vec4<f32>;
    if(((lit_0.mode_0.x) > 1.5f))
    {
        var s_0 : vec4<f32> = (textureSampleLevel((colorTex_0), (linearSampler_0), (_S2.uv_2), (0.0f)));
        var _S3 : f32 = s_0.x;
        var _S4 : f32 = s_0.y;
        var _S5 : f32 = max(min(_S3, _S4), min(max(_S3, _S4), s_0.z));
        var texW_0 : u32;
        var texH_0 : u32;
        {var dim = textureDimensions((colorTex_0));((texW_0)) = dim.x;((texH_0)) = dim.y;};
        baseColor_0 = vec4<f32>(_S2.color_2.xyz, _S2.color_2.w * saturate(max(0.5f * dot(vec2<f32>(lit_0.mode_0.y) / vec2<f32>(f32(texW_0), f32(texH_0)), vec2<f32>(1.0f) / uvWidth_0), 1.0f) * (_S5 - 0.5f) + 0.5f));
    }
    else
    {
        if(((lit_0.mode_0.x) > 0.5f))
        {
            if(usePoint_0)
            {
                baseColor_0 = (textureSampleLevel((colorTex_0), (pointSampler_0), (_S2.uv_2), (0.0f)));
            }
            else
            {
                baseColor_0 = (textureSampleLevel((colorTex_0), (linearSampler_0), (_S2.uv_2), (0.0f)));
            }
            baseColor_0 = baseColor_0 * _S2.color_2;
        }
        else
        {
            baseColor_0 = _S2.color_2;
        }
    }
    var hasNormal_0 : bool = ((lit_0.mode_0.z) > 0.5f);
    const _S6 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 1.0f);
    var total_0 : vec3<f32>;
    var normal_0 : vec3<f32>;
    var weight_0 : f32;
    if(hasNormal_0)
    {
        var raw_0 : vec4<f32>;
        if(usePoint_0)
        {
            raw_0 = (textureSampleLevel((normalTex_0), (pointSampler_0), (_S2.uv_2), (0.0f)));
        }
        else
        {
            raw_0 = (textureSampleLevel((normalTex_0), (linearSampler_0), (_S2.uv_2), (0.0f)));
        }
        var t_0 : vec3<f32> = raw_0.xyz * vec3<f32>(2.0f) - vec3<f32>(1.0f);
        var tl_0 : f32 = length(t_0);
        if((tl_0 < 9.99999997475242708e-07f))
        {
            total_0 = _S6;
        }
        else
        {
            total_0 = t_0 / vec3<f32>(tl_0);
        }
        var _S7 : f32 = duvdx_0.x;
        var _S8 : f32 = duvdy_0.y;
        var _S9 : f32 = duvdy_0.x;
        var _S10 : f32 = duvdx_0.y;
        if(((_S7 * _S8 - _S9 * _S10) < 0.0f))
        {
            weight_0 = -1.0f;
        }
        else
        {
            weight_0 = 1.0f;
        }
        var tDir_0 : vec2<f32> = vec2<f32>(_S8, - _S10);
        var bDir_0 : vec2<f32> = vec2<f32>(- _S9, _S7);
        var tLen_0 : f32 = length(tDir_0);
        var bLen_0 : f32 = length(bDir_0);
        var _S11 : bool;
        if((tLen_0 > 9.999999960041972e-13f))
        {
            _S11 = (bLen_0 > 9.999999960041972e-13f);
        }
        else
        {
            _S11 = false;
        }
        if(_S11)
        {
            normal_0 = vec3<f32>(vec2<f32>(total_0.x) * (tDir_0 / vec2<f32>(tLen_0) * vec2<f32>(weight_0)) + vec2<f32>(total_0.y) * (vec2<f32>(0) - bDir_0 / vec2<f32>(bLen_0) * vec2<f32>(weight_0)), total_0.z);
        }
        else
        {
            normal_0 = _S6;
        }
    }
    else
    {
        normal_0 = _S6;
    }
    var _S12 : vec2<f32> = (pos_2.xy - lit_0.map_0.zw) * lit_0.map_0.xy;
    var _S13 : vec3<f32> = lit_0.ambient_0.xyz;
    var _S14 : i32 = i32(lit_0.ambient_0.w);
    var n_0 : i32 = i32(0);
    total_0 = _S13;
    loop
    {
        if((n_0 < _S14))
        {
        }
        else
        {
            break;
        }
        var _S15 : i32 = n_0 * i32(3);
        var a_0 : vec4<f32> = lit_0.lights_0[_S15];
        var b_0 : vec4<f32> = lit_0.lights_0[_S15 + i32(1)];
        var c_0 : vec4<f32> = lit_0.lights_0[_S15 + i32(2)];
        var d_0 : vec2<f32> = lit_0.lights_0[_S15].xy - _S12;
        var dist_0 : f32 = length(d_0);
        var _S16 : f32 = lit_0.lights_0[_S15].w;
        if((dist_0 >= _S16))
        {
            n_0 = n_0 + i32(1);
            continue;
        }
        var weight_1 : f32 = pow(1.0f - dist_0 / _S16, b_0.w);
        var _S17 : f32 = c_0.z;
        if((_S17 > -1.5f))
        {
            var _S18 : vec2<f32> = c_0.xy;
            var toPixel_0 : vec2<f32>;
            if((dist_0 > 9.9999997473787516e-06f))
            {
                toPixel_0 = (vec2<f32>(0) - d_0) / vec2<f32>(dist_0);
            }
            else
            {
                toPixel_0 = _S18;
            }
            weight_0 = weight_1 * smoothstep(_S17, c_0.w, dot(_S18, toPixel_0));
        }
        else
        {
            weight_0 = weight_1;
        }
        var weight_2 : f32;
        if(hasNormal_0)
        {
            var l_0 : vec3<f32> = vec3<f32>(d_0, a_0.z);
            weight_2 = weight_0 * max(dot(normal_0, l_0 / vec3<f32>(max(length(l_0), 9.99999997475242708e-07f))), 0.0f);
        }
        else
        {
            weight_2 = weight_0;
        }
        total_0 = total_0 + b_0.xyz * vec3<f32>(weight_2);
        n_0 = n_0 + i32(1);
    }
    var _S19 : pixelOutput_0 = pixelOutput_0( vec4<f32>(baseColor_0.xyz * total_0, baseColor_0.w) );
    return _S19;
}

