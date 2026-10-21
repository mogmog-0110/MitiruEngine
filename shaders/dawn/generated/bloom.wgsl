struct BloomCB_std140_0
{
    @align(16) params_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> bloom_0 : BloomCB_std140_0;
@binding(1) @group(0) var srcTex_0 : texture_2d<f32>;

@binding(3) @group(0) var linearClamp_0 : sampler;

@binding(2) @group(0) var addTex_0 : texture_2d<f32>;

struct BloomOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
};

@vertex
fn vsBloom(@builtin(vertex_index) vid_0 : u32) -> BloomOut_0
{
    var t_0 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2)))));
    var o_0 : BloomOut_0;
    o_0.pos_0 = vec4<f32>(t_0 * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    o_0.uv_0 = t_0;
    return o_0;
}

fn prefilter_0( c_0 : vec3<f32>) -> vec3<f32>
{
    if(((bloom_0.params_0.w) < 0.5f))
    {
        return c_0;
    }
    var _S1 : f32 = max(c_0.x, max(c_0.y, c_0.z));
    return c_0 * vec3<f32>((max(_S1 - bloom_0.params_0.z, 0.0f) / max(_S1, 9.9999997473787516e-05f)));
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) uv_1 : vec2<f32>,
};

@fragment
fn fsBloomDown( _S2 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var o_1 : vec2<f32> = bloom_0.params_0.xy;
    var _S3 : f32 = o_1.x;
    var _S4 : f32 = - _S3;
    var _S5 : f32 = o_1.y;
    var _S6 : f32 = - _S5;
    var _S7 : pixelOutput_0 = pixelOutput_0( vec4<f32>((prefilter_0((textureSampleLevel((srcTex_0), (linearClamp_0), (_S2.uv_1 + vec2<f32>(_S4, _S6)), (0.0f))).xyz) + prefilter_0((textureSampleLevel((srcTex_0), (linearClamp_0), (_S2.uv_1 + vec2<f32>(_S3, _S6)), (0.0f))).xyz) + prefilter_0((textureSampleLevel((srcTex_0), (linearClamp_0), (_S2.uv_1 + vec2<f32>(_S4, _S5)), (0.0f))).xyz) + prefilter_0((textureSampleLevel((srcTex_0), (linearClamp_0), (_S2.uv_1 + vec2<f32>(_S3, _S5)), (0.0f))).xyz)) * vec3<f32>(0.25f), 1.0f) );
    return _S7;
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) uv_2 : vec2<f32>,
};

@fragment
fn fsBloomUp( _S8 : pixelInput_1, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_1
{
    var o_2 : vec2<f32> = bloom_0.params_0.xy;
    var _S9 : f32 = o_2.x;
    var _S10 : f32 = - _S9;
    var _S11 : f32 = o_2.y;
    var _S12 : f32 = - _S11;
    var _S13 : vec3<f32> = vec3<f32>(2.0f);
    var _S14 : pixelOutput_1 = pixelOutput_1( vec4<f32>(((textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S10, _S12)), (0.0f))).xyz + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(0.0f, _S12)), (0.0f))).xyz * _S13 + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S9, _S12)), (0.0f))).xyz + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S10, 0.0f)), (0.0f))).xyz * _S13 + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2), (0.0f))).xyz * vec3<f32>(4.0f) + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S9, 0.0f)), (0.0f))).xyz * _S13 + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S10, _S11)), (0.0f))).xyz + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(0.0f, _S11)), (0.0f))).xyz * _S13 + (textureSampleLevel((srcTex_0), (linearClamp_0), (_S8.uv_2 + vec2<f32>(_S9, _S11)), (0.0f))).xyz) / vec3<f32>(16.0f) + (textureSampleLevel((addTex_0), (linearClamp_0), (_S8.uv_2), (0.0f))).xyz, 1.0f) );
    return _S14;
}

