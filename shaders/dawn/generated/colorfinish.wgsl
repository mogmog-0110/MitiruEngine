@binding(1) @group(0) var sceneCopy_0 : texture_2d<f32>;

struct ColorFinishCB_std140_0
{
    @align(16) LutSize_0 : f32,
    @align(4) LutStrength_0 : f32,
    @align(8) VignetteStrength_0 : f32,
    @align(4) VignetteRadius_0 : f32,
    @align(16) GrainStrength_0 : f32,
    @align(4) GrainCell_0 : u32,
    @align(8) Seed_0 : u32,
    @align(4) pad0_0 : f32,
    @align(16) OutSize_0 : vec2<f32>,
    @align(8) pad1_0 : vec2<f32>,
};

@binding(0) @group(0) var<uniform> cf_0 : ColorFinishCB_std140_0;
@binding(2) @group(0) var lutTex_0 : texture_2d<f32>;

struct FinishOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
};

@vertex
fn vsColorFinish(@builtin(vertex_index) vid_0 : u32) -> FinishOut_0
{
    var o_0 : FinishOut_0;
    o_0.pos_0 = vec4<f32>(vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    return o_0;
}

fn lutTexel_0( n_0 : i32,  r_0 : i32,  g_0 : i32,  b_0 : i32) -> vec3<f32>
{
    var _S1 : vec3<i32> = vec3<i32>(b_0 * n_0 + r_0, g_0, i32(0));
    return (textureLoad((lutTex_0), ((_S1)).xy, ((_S1)).z)).xyz;
}

fn sampleLut_0( c_0 : vec3<f32>) -> vec3<f32>
{
    var n_1 : i32 = i32(cf_0.LutSize_0);
    var s_0 : vec3<f32> = saturate(c_0) * vec3<f32>((cf_0.LutSize_0 - 1.0f));
    var fl_0 : vec3<f32> = floor(s_0);
    var _S2 : vec3<i32> = vec3<i32>(fl_0);
    var _S3 : i32 = n_1 - i32(1);
    var _S4 : vec3<i32> = min(_S2 + vec3<i32>(i32(1), i32(1), i32(1)), vec3<i32>(_S3, _S3, _S3));
    var _S5 : vec3<f32> = s_0 - fl_0;
    const _S6 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var corner_0 : i32 = i32(0);
    var sum_0 : vec3<f32> = _S6;
    loop
    {
        if((corner_0 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var hr_0 : bool = ((corner_0 & (i32(1)))) != i32(0);
        var hg_0 : bool = ((corner_0 & (i32(2)))) != i32(0);
        var hb_0 : bool = ((corner_0 & (i32(4)))) != i32(0);
        var _S7 : f32;
        if(hr_0)
        {
            _S7 = _S5.x;
        }
        else
        {
            _S7 = 1.0f - _S5.x;
        }
        var _S8 : f32;
        if(hg_0)
        {
            _S8 = _S5.y;
        }
        else
        {
            _S8 = 1.0f - _S5.y;
        }
        var _S9 : f32 = _S7 * _S8;
        var _S10 : f32;
        if(hb_0)
        {
            _S10 = _S5.z;
        }
        else
        {
            _S10 = 1.0f - _S5.z;
        }
        var w_0 : f32 = _S9 * _S10;
        var _S11 : i32;
        if(hr_0)
        {
            _S11 = _S4.x;
        }
        else
        {
            _S11 = _S2.x;
        }
        var _S12 : i32;
        if(hg_0)
        {
            _S12 = _S4.y;
        }
        else
        {
            _S12 = _S2.y;
        }
        var _S13 : i32;
        if(hb_0)
        {
            _S13 = _S4.z;
        }
        else
        {
            _S13 = _S2.z;
        }
        var sum_1 : vec3<f32> = sum_0 + lutTexel_0(n_1, _S11, _S12, _S13) * vec3<f32>(w_0);
        corner_0 = corner_0 + i32(1);
        sum_0 = sum_1;
    }
    return sum_0;
}

fn grainHash_0( x_0 : u32,  y_0 : u32,  seed_0 : u32) -> u32
{
    var h_0 : u32 = ((((x_0 * u32(2376512323)) ^ ((y_0 * u32(3625334849))))) ^ ((seed_0 * u32(3407524639))));
    var h_1 : u32 = ((h_0 ^ (((h_0 >> (u32(16))))))) * u32(2146121005);
    var h_2 : u32 = ((h_1 ^ (((h_1 >> (u32(15))))))) * u32(2221713035);
    return (h_2 ^ (((h_2 >> (u32(16))))));
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

@fragment
fn fsColorFinish(@builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S14 : vec2<u32> = vec2<u32>(pos_1.xy);
    var _S15 : vec3<i32> = vec3<i32>(vec2<i32>(_S14), i32(0));
    var src_0 : vec4<f32> = (textureLoad((sceneCopy_0), ((_S15)).xy, ((_S15)).z));
    var c_1 : vec3<f32> = src_0.xyz;
    var c_2 : vec3<f32>;
    if(((cf_0.LutSize_0) >= 2.0f))
    {
        c_2 = c_1 + (sampleLut_0(c_1) - c_1) * vec3<f32>(cf_0.LutStrength_0);
    }
    else
    {
        c_2 = c_1;
    }
    if(((cf_0.VignetteStrength_0) > 0.0f))
    {
        var halfSize_0 : vec2<f32> = cf_0.OutSize_0 * vec2<f32>(0.5f);
        var d_0 : vec2<f32> = vec2<f32>(_S14) + vec2<f32>(0.5f, 0.5f) - halfSize_0;
        c_2 = c_2 * vec3<f32>((1.0f - cf_0.VignetteStrength_0 * smoothstep(cf_0.VignetteRadius_0, 1.0f, sqrt(dot(d_0, d_0)) / sqrt(dot(halfSize_0, halfSize_0)))));
    }
    else
    {
    }
    if(((cf_0.GrainStrength_0) > 0.0f))
    {
        var luma_0 : f32 = dot(c_2, vec3<f32>(0.2125999927520752f, 0.71520000696182251f, 0.072200000286102295f));
        var midtone_0 : f32 = 0.5f + 0.5f * (4.0f * luma_0 * (1.0f - luma_0));
        var _S16 : u32 = max(cf_0.GrainCell_0, u32(1));
        var _S17 : u32 = _S14.x / _S16;
        var _S18 : u32 = _S14.y / _S16;
        c_2 = c_2 + vec3<f32>(((f32(((grainHash_0(_S17, _S18, cf_0.Seed_0)) >> (u32(8)))) * 1.1920928955078125e-07f - 1.0f) * cf_0.GrainStrength_0 * midtone_0));
    }
    else
    {
    }
    var _S19 : pixelOutput_0 = pixelOutput_0( vec4<f32>(saturate(c_2), src_0.w) );
    return _S19;
}

