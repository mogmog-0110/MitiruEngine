@binding(1) @group(0) var hdr_0 : texture_2d<f32>;

struct PostCB_std140_0
{
    @align(16) tone_0 : vec4<f32>,
    @align(16) size_0 : vec4<f32>,
    @align(16) up_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> post_0 : PostCB_std140_0;
@binding(3) @group(0) var bloomTex_0 : texture_2d<f32>;

@binding(2) @group(0) var hdrSampler_0 : sampler;

struct PostOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
};

@vertex
fn vsPost(@builtin(vertex_index) vid_0 : u32) -> PostOut_0
{
    var o_0 : PostOut_0;
    o_0.pos_0 = vec4<f32>(vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    return o_0;
}

fn neutralShoulder_0( c_0 : vec3<f32>) -> vec3<f32>
{
    var _S1 : f32 = max(c_0.x, max(c_0.y, c_0.z));
    if((_S1 <= 0.89999997615814209f))
    {
        return c_0;
    }
    return c_0 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S1 + 0.10000002384185791f - 0.89999997615814209f)) / _S1));
}

fn linearToSrgb_0( x_0 : vec3<f32>) -> vec3<f32>
{
    var lo_0 : vec3<f32> = x_0 * vec3<f32>(12.920000076293945f);
    var hi_0 : vec3<f32> = vec3<f32>(1.0549999475479126f) * pow(max(x_0, vec3<f32>(0.0031308000907301903f, 0.0031308000907301903f, 0.0031308000907301903f)), vec3<f32>(0.4166666567325592f, 0.4166666567325592f, 0.4166666567325592f)) - vec3<f32>(0.054999999701976776f);
    var _S2 : f32;
    if(((x_0.x) <= 0.0031308000907301903f))
    {
        _S2 = lo_0.x;
    }
    else
    {
        _S2 = hi_0.x;
    }
    var _S3 : f32;
    if(((x_0.y) <= 0.0031308000907301903f))
    {
        _S3 = lo_0.y;
    }
    else
    {
        _S3 = hi_0.y;
    }
    var _S4 : f32;
    if(((x_0.z) <= 0.0031308000907301903f))
    {
        _S4 = lo_0.z;
    }
    else
    {
        _S4 = hi_0.z;
    }
    return vec3<f32>(_S2, _S3, _S4);
}

fn tonemap_0( c_1 : vec4<f32>,  uv_0 : vec2<f32>) -> vec4<f32>
{
    var _S5 : vec4<f32> = c_1;
    if(((post_0.size_0.z) > 0.0f))
    {
        var _S6 : vec3<f32> = _S5.xyz + (textureSampleLevel((bloomTex_0), (hdrSampler_0), (uv_0), (0.0f))).xyz * vec3<f32>(post_0.size_0.z);
        _S5.x = _S6.x;
        _S5.y = _S6.y;
        _S5.z = _S6.z;
    }
    var _S7 : vec3<f32> = _S5.xyz * vec3<f32>(post_0.size_0.w);
    _S5.x = _S7.x;
    _S5.y = _S7.y;
    _S5.z = _S7.z;
    var mapped_0 : vec3<f32> = neutralShoulder_0(max(_S5.xyz * vec3<f32>(post_0.tone_0.x), vec3<f32>(0.0f, 0.0f, 0.0f)));
    var luma_0 : f32 = dot(mapped_0, vec3<f32>(0.2125999927520752f, 0.71520000696182251f, 0.072200000286102295f));
    var _S8 : vec3<f32> = vec3<f32>(0.18000000715255737f);
    return vec4<f32>(linearToSrgb_0(pow(saturate(_S8 + (mix(vec3<f32>(luma_0, luma_0, luma_0), mapped_0, vec3<f32>(post_0.tone_0.z)) - _S8) * vec3<f32>(post_0.tone_0.w)), vec3<f32>(2.2000000476837158f / max(post_0.tone_0.y, 9.9999997473787516e-05f), 2.2000000476837158f / max(post_0.tone_0.y, 9.9999997473787516e-05f), 2.2000000476837158f / max(post_0.tone_0.y, 9.9999997473787516e-05f)))), _S5.w);
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

@fragment
fn fsTonemap(@builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S9 : vec2<f32> = pos_1.xy;
    var _S10 : vec3<i32> = vec3<i32>(vec2<i32>(_S9), i32(0));
    var _S11 : pixelOutput_0 = pixelOutput_0( tonemap_0((textureLoad((hdr_0), ((_S10)).xy, ((_S10)).z)), _S9 / post_0.size_0.xy) );
    return _S11;
}

fn upscaleCatmullRom_0( uv_1 : vec2<f32>) -> vec3<f32>
{
    var dim_0 : vec2<u32>;
    var _S12 : u32 = dim_0[i32(0)];
    var _S13 : u32 = dim_0[i32(1)];
    {var dim = textureDimensions((hdr_0));((_S12)) = dim.x;((_S13)) = dim.y;};
    dim_0[i32(0)] = _S12;
    dim_0[i32(1)] = _S13;
    var _S14 : vec2<f32> = vec2<f32>(dim_0);
    var _S15 : vec2<f32> = vec2<f32>(1.0f);
    var texel_0 : vec2<f32> = _S15 / _S14;
    var samplePos_0 : vec2<f32> = uv_1 * _S14;
    var _S16 : vec2<f32> = vec2<f32>(0.5f);
    var tc1_0 : vec2<f32> = floor(samplePos_0 - _S16) + _S16;
    var f_0 : vec2<f32> = samplePos_0 - tc1_0;
    var _S17 : vec2<f32> = _S16 * f_0;
    var _S18 : vec2<f32> = vec2<f32>(-0.5f);
    var w0_0 : vec2<f32> = f_0 * (_S18 + f_0 * (_S15 - _S17));
    var _S19 : vec2<f32> = f_0 * f_0;
    var _S20 : vec2<f32> = vec2<f32>(1.5f) * f_0;
    var _S21 : vec2<f32> = vec2<f32>(2.0f);
    var w2_0 : vec2<f32> = f_0 * (_S16 + f_0 * (_S21 - _S20));
    var w3_0 : vec2<f32> = _S19 * (_S18 + _S17);
    var w12_0 : vec2<f32> = _S15 + _S19 * (vec2<f32>(-2.5f) + _S20) + w2_0;
    var tc0_0 : vec2<f32> = (tc1_0 - _S15) * texel_0;
    var tc3_0 : vec2<f32> = (tc1_0 + _S21) * texel_0;
    var tc12_0 : vec2<f32> = (tc1_0 + w2_0 / w12_0) * texel_0;
    var _S22 : f32 = tc12_0.x;
    var _S23 : f32 = w12_0.x;
    var _S24 : f32 = _S23 * w0_0.y;
    var _S25 : f32 = tc12_0.y;
    var _S26 : f32 = w12_0.y;
    var _S27 : f32 = w0_0.x * _S26;
    var _S28 : f32 = _S23 * _S26;
    var _S29 : f32 = w3_0.x * _S26;
    var _S30 : f32 = _S23 * w3_0.y;
    var hi_1 : vec2<i32> = vec2<i32>(dim_0) - vec2<i32>(i32(1));
    const _S31 : vec2<i32> = vec2<i32>(i32(0), i32(0));
    var p_0 : vec2<i32> = clamp(vec2<i32>(tc1_0 - _S16), _S31, hi_1);
    var q_0 : vec2<i32> = clamp(p_0 + vec2<i32>(i32(1), i32(1)), _S31, hi_1);
    var _S32 : vec3<i32> = vec3<i32>(p_0, i32(0));
    var a_0 : vec3<f32> = (textureLoad((hdr_0), ((_S32)).xy, ((_S32)).z)).xyz;
    var _S33 : vec3<i32> = vec3<i32>(vec2<i32>(q_0.x, p_0.y), i32(0));
    var b_0 : vec3<f32> = (textureLoad((hdr_0), ((_S33)).xy, ((_S33)).z)).xyz;
    var _S34 : vec3<i32> = vec3<i32>(vec2<i32>(p_0.x, q_0.y), i32(0));
    var c_2 : vec3<f32> = (textureLoad((hdr_0), ((_S34)).xy, ((_S34)).z)).xyz;
    var _S35 : vec3<i32> = vec3<i32>(q_0, i32(0));
    var d_0 : vec3<f32> = (textureLoad((hdr_0), ((_S35)).xy, ((_S35)).z)).xyz;
    return clamp(max(((textureSampleLevel((hdr_0), (hdrSampler_0), (vec2<f32>(_S22, tc0_0.y)), (0.0f))).xyz * vec3<f32>(_S24) + (textureSampleLevel((hdr_0), (hdrSampler_0), (vec2<f32>(tc0_0.x, _S25)), (0.0f))).xyz * vec3<f32>(_S27) + (textureSampleLevel((hdr_0), (hdrSampler_0), (tc12_0), (0.0f))).xyz * vec3<f32>(_S28) + (textureSampleLevel((hdr_0), (hdrSampler_0), (vec2<f32>(tc3_0.x, _S25)), (0.0f))).xyz * vec3<f32>(_S29) + (textureSampleLevel((hdr_0), (hdrSampler_0), (vec2<f32>(_S22, tc3_0.y)), (0.0f))).xyz * vec3<f32>(_S30)) / vec3<f32>((_S24 + _S27 + _S28 + _S29 + _S30)), vec3<f32>(0.0f, 0.0f, 0.0f)), min(min(a_0, b_0), min(c_2, d_0)), max(max(a_0, b_0), max(c_2, d_0)));
}

fn upscaledLdr_0( uv_2 : vec2<f32>) -> vec4<f32>
{
    return tonemap_0(vec4<f32>(upscaleCatmullRom_0(uv_2), (textureSampleLevel((hdr_0), (hdrSampler_0), (uv_2), (0.0f))).w), uv_2);
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

@fragment
fn fsTonemapScaled(@builtin(position) pos_2 : vec4<f32>) -> pixelOutput_1
{
    var uv_3 : vec2<f32> = pos_2.xy / post_0.size_0.xy;
    if(((post_0.up_0.x) < 0.5f))
    {
        var _S36 : pixelOutput_1 = pixelOutput_1( tonemap_0((textureSampleLevel((hdr_0), (hdrSampler_0), (uv_3), (0.0f))), uv_3) );
        return _S36;
    }
    var _S37 : pixelOutput_1 = pixelOutput_1( upscaledLdr_0(uv_3) );
    return _S37;
}

