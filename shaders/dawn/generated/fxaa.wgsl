struct FxaaCB_std140_0
{
    @align(16) params_0 : vec4<f32>,
    @align(16) params2_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> fxaa_0 : FxaaCB_std140_0;
@binding(1) @group(0) var sceneTexture_0 : texture_2d<f32>;

@binding(2) @group(0) var linearSampler_0 : sampler;

var<private> kQuality_0 : array<f32, i32(12)> = array<f32, i32(12)>( 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f );
struct FxaaOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
};

@vertex
fn vsFxaa(@builtin(vertex_index) vid_0 : u32) -> FxaaOut_0
{
    var t_0 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2)))));
    var o_0 : FxaaOut_0;
    o_0.pos_0 = vec4<f32>(t_0 * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    o_0.uv_0 = t_0;
    return o_0;
}

fn fxaaTex_0( uv_1 : vec2<f32>) -> vec3<f32>
{
    return (textureSampleLevel((sceneTexture_0), (linearSampler_0), (uv_1), (0.0f))).xyz;
}

fn fxaaLuma_0( rgb_0 : vec3<f32>) -> f32
{
    return dot(rgb_0, vec3<f32>(0.29899999499320984f, 0.58700001239776611f, 0.11400000005960464f));
}

fn fxaaTexOff_0( uv_2 : vec2<f32>,  offset_0 : vec2<f32>) -> vec3<f32>
{
    return (textureSampleLevel((sceneTexture_0), (linearSampler_0), (uv_2 + offset_0 * fxaa_0.params_0.xy), (0.0f))).xyz;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) uv_3 : vec2<f32>,
};

@fragment
fn fsFxaa( _S1 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var rcpFrame_0 : vec2<f32> = fxaa_0.params_0.xy;
    var rgbM_0 : vec3<f32> = fxaaTex_0(_S1.uv_3);
    var lumaM_0 : f32 = fxaaLuma_0(rgbM_0);
    var lumaN_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(0.0f, -1.0f)));
    var lumaS_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(0.0f, 1.0f)));
    var lumaE_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(1.0f, 0.0f)));
    var lumaW_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(-1.0f, 0.0f)));
    var _S2 : f32 = max(lumaM_0, max(max(lumaN_0, lumaS_0), max(lumaE_0, lumaW_0)));
    var lumaRange_0 : f32 = _S2 - min(lumaM_0, min(min(lumaN_0, lumaS_0), min(lumaE_0, lumaW_0)));
    if((lumaRange_0 < (max(fxaa_0.params2_0.x, _S2 * fxaa_0.params_0.w))))
    {
        var _S3 : pixelOutput_0 = pixelOutput_0( vec4<f32>(rgbM_0, 1.0f) );
        return _S3;
    }
    var lumaNW_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(-1.0f, -1.0f)));
    var lumaNE_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(1.0f, -1.0f)));
    var lumaSW_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(-1.0f, 1.0f)));
    var lumaSE_0 : f32 = fxaaLuma_0(fxaaTexOff_0(_S1.uv_3, vec2<f32>(1.0f, 1.0f)));
    var lumaNS_0 : f32 = lumaN_0 + lumaS_0;
    var lumaEW_0 : f32 = lumaE_0 + lumaW_0;
    var lumaNWSW_0 : f32 = lumaNW_0 + lumaSW_0;
    var lumaNESE_0 : f32 = lumaNE_0 + lumaSE_0;
    var _S4 : f32 = 2.0f * lumaM_0;
    var isHorizontal_0 : bool = ((abs(lumaNWSW_0 - 2.0f * lumaW_0) + abs(lumaNS_0 - _S4) * 2.0f + abs(lumaNESE_0 - 2.0f * lumaE_0)) >= (abs(lumaNW_0 + lumaNE_0 - 2.0f * lumaN_0) + abs(lumaEW_0 - _S4) * 2.0f + abs(lumaSW_0 + lumaSE_0 - 2.0f * lumaS_0)));
    var stepLength_0 : f32;
    if(isHorizontal_0)
    {
        stepLength_0 = rcpFrame_0.y;
    }
    else
    {
        stepLength_0 = rcpFrame_0.x;
    }
    var luma1_0 : f32;
    if(isHorizontal_0)
    {
        luma1_0 = lumaN_0;
    }
    else
    {
        luma1_0 = lumaW_0;
    }
    var luma2_0 : f32;
    if(isHorizontal_0)
    {
        luma2_0 = lumaS_0;
    }
    else
    {
        luma2_0 = lumaE_0;
    }
    var _S5 : f32 = abs(luma1_0 - lumaM_0);
    var _S6 : f32 = abs(luma2_0 - lumaM_0);
    var is1Steepest_0 : bool = (_S5 >= _S6);
    var gradientScaled_0 : f32 = 0.25f * max(_S5, _S6);
    if(!is1Steepest_0)
    {
        stepLength_0 = - stepLength_0;
    }
    var lumaLocalAvg_0 : f32;
    if(is1Steepest_0)
    {
        lumaLocalAvg_0 = 0.5f * (luma1_0 + lumaM_0);
    }
    else
    {
        lumaLocalAvg_0 = 0.5f * (luma2_0 + lumaM_0);
    }
    var currentUV_0 : vec2<f32> = _S1.uv_3;
    if(isHorizontal_0)
    {
        currentUV_0[i32(1)] = currentUV_0[i32(1)] + stepLength_0 * 0.5f;
    }
    else
    {
        currentUV_0[i32(0)] = currentUV_0[i32(0)] + stepLength_0 * 0.5f;
    }
    var offset2_0 : vec2<f32>;
    if(isHorizontal_0)
    {
        offset2_0 = vec2<f32>(rcpFrame_0.x, 0.0f);
    }
    else
    {
        offset2_0 = vec2<f32>(0.0f, rcpFrame_0.y);
    }
    var uv1_0 : vec2<f32> = currentUV_0 - offset2_0;
    var uv2_0 : vec2<f32> = currentUV_0 + offset2_0;
    var lumaEnd1_0 : f32 = fxaaLuma_0(fxaaTex_0(uv1_0)) - lumaLocalAvg_0;
    var lumaEnd2_0 : f32 = fxaaLuma_0(fxaaTex_0(uv2_0)) - lumaLocalAvg_0;
    var reached1_0 : bool = ((abs(lumaEnd1_0)) >= gradientScaled_0);
    var reached2_0 : bool = ((abs(lumaEnd2_0)) >= gradientScaled_0);
    var reachedBoth_0 : bool;
    if(reached1_0)
    {
        reachedBoth_0 = reached2_0;
    }
    else
    {
        reachedBoth_0 = false;
    }
    var uv1_1 : vec2<f32>;
    if(!reached1_0)
    {
        uv1_1 = uv1_0 - offset2_0;
    }
    else
    {
        uv1_1 = uv1_0;
    }
    var uv2_1 : vec2<f32>;
    if(!reached2_0)
    {
        uv2_1 = uv2_0 + offset2_0;
    }
    else
    {
        uv2_1 = uv2_0;
    }
    var reached1_1 : bool = reached1_0;
    var reached2_1 : bool = reached2_0;
    var lumaEnd1_1 : f32 = lumaEnd1_0;
    var lumaEnd2_1 : f32 = lumaEnd2_0;
    var i_0 : i32 = i32(0);
    loop
    {
        if((i_0 < i32(12)))
        {
        }
        else
        {
            break;
        }
        if(reachedBoth_0)
        {
            break;
        }
        if(!reached1_1)
        {
            lumaEnd1_1 = fxaaLuma_0(fxaaTex_0(uv1_1)) - lumaLocalAvg_0;
        }
        if(!reached2_1)
        {
            lumaEnd2_1 = fxaaLuma_0(fxaaTex_0(uv2_1)) - lumaLocalAvg_0;
        }
        var reached1_2 : bool = ((abs(lumaEnd1_1)) >= gradientScaled_0);
        var reached2_2 : bool = ((abs(lumaEnd2_1)) >= gradientScaled_0);
        if(reached1_2)
        {
            reachedBoth_0 = reached2_2;
        }
        else
        {
            reachedBoth_0 = false;
        }
        if(!reached1_2)
        {
            uv1_1 = uv1_1 - offset2_0 * vec2<f32>(kQuality_0[i_0]);
        }
        if(!reached2_2)
        {
            uv2_1 = uv2_1 + offset2_0 * vec2<f32>(kQuality_0[i_0]);
        }
        var _S7 : i32 = i_0 + i32(1);
        reached1_1 = reached1_2;
        reached2_1 = reached2_2;
        i_0 = _S7;
    }
    var dist1_0 : f32;
    if(isHorizontal_0)
    {
        dist1_0 = _S1.uv_3.x - uv1_1.x;
    }
    else
    {
        dist1_0 = _S1.uv_3.y - uv1_1.y;
    }
    var dist2_0 : f32;
    if(isHorizontal_0)
    {
        dist2_0 = uv2_1.x - _S1.uv_3.x;
    }
    else
    {
        dist2_0 = uv2_1.y - _S1.uv_3.y;
    }
    var pixelOffset_0 : f32 = - min(dist1_0, dist2_0) / max(dist1_0 + dist2_0, 9.9999997473787516e-06f) + 0.5f;
    var isLumaCenterSmaller_0 : bool = (lumaM_0 < lumaLocalAvg_0);
    if((dist1_0 < dist2_0))
    {
        luma1_0 = lumaEnd1_1;
    }
    else
    {
        luma1_0 = lumaEnd2_1;
    }
    var finalOffset_0 : f32;
    if((luma1_0 < 0.0f) != isLumaCenterSmaller_0)
    {
        finalOffset_0 = pixelOffset_0;
    }
    else
    {
        finalOffset_0 = 0.0f;
    }
    var subpixOffset1_0 : f32 = saturate(abs(0.083333335816860199f * (2.0f * lumaNS_0 + 2.0f * lumaEW_0 + lumaNWSW_0 + lumaNESE_0) - lumaM_0) / max(lumaRange_0, 9.9999997473787516e-06f));
    var subpixOffset2_0 : f32 = (-2.0f * subpixOffset1_0 + 3.0f) * subpixOffset1_0 * subpixOffset1_0;
    var _S8 : f32 = max(finalOffset_0, subpixOffset2_0 * subpixOffset2_0 * fxaa_0.params_0.z);
    var finalUV_0 : vec2<f32> = _S1.uv_3;
    if(isHorizontal_0)
    {
        finalUV_0[i32(1)] = finalUV_0[i32(1)] + _S8 * stepLength_0;
    }
    else
    {
        finalUV_0[i32(0)] = finalUV_0[i32(0)] + _S8 * stepLength_0;
    }
    var _S9 : pixelOutput_0 = pixelOutput_0( vec4<f32>(fxaaTex_0(finalUV_0), 1.0f) );
    return _S9;
}

