struct SmaaCB_std140_0
{
    @align(16) metrics_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> smaaCb_0 : SmaaCB_std140_0;
@binding(7) @group(0) var pointSampler_0 : sampler;

@binding(1) @group(0) var colorTex_0 : texture_2d<f32>;

@binding(6) @group(0) var linearSampler_0 : sampler;

@binding(2) @group(0) var edgesTex_0 : texture_2d<f32>;

@binding(4) @group(0) var areaTex_0 : texture_2d<f32>;

@binding(5) @group(0) var searchTex_0 : texture_2d<f32>;

@binding(3) @group(0) var blendTex_0 : texture_2d<f32>;

struct SmaaOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
};

@vertex
fn vsSmaa(@builtin(vertex_index) vid_0 : u32) -> SmaaOut_0
{
    var t_0 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2)))));
    var o_0 : SmaaOut_0;
    o_0.pos_0 = vec4<f32>(t_0 * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    o_0.uv_0 = t_0;
    return o_0;
}

fn SMAAEdgeDetectionVS_0( texcoord_0 : vec2<f32>,  offset_0 : ptr<function, array<vec4<f32>, i32(3)>>)
{
    var _S1 : vec4<f32> = texcoord_0.xyxy;
    (*offset_0)[i32(0)] = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(-1.0f, 0.0f, 0.0f, -1.0f), _S1);
    (*offset_0)[i32(1)] = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(1.0f, 0.0f, 0.0f, 1.0f), _S1);
    (*offset_0)[i32(2)] = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(-2.0f, 0.0f, 0.0f, -2.0f), _S1);
    return;
}

fn SMAALumaEdgeDetectionPS_0( texcoord_1 : vec2<f32>,  offset_1 : array<vec4<f32>, i32(3)>,  colorTex_1 : texture_2d<f32>) -> vec2<f32>
{
    const threshold_0 : vec2<f32> = vec2<f32>(0.10000000149011612f, 0.10000000149011612f);
    const weights_0 : vec3<f32> = vec3<f32>(0.2125999927520752f, 0.71520000696182251f, 0.072200000286102295f);
    var _S2 : vec2<f32> = vec2<f32>(dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(0)].xy), (0.0f))).xyz, weights_0), dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(0)].zw), (0.0f))).xyz, weights_0));
    var _S3 : vec2<f32> = vec2<f32>(dot((textureSampleLevel((colorTex_1), (pointSampler_0), (texcoord_1), (0.0f))).xyz, weights_0));
    var _S4 : vec2<f32> = abs(_S3 - _S2);
    var delta_0 : vec4<f32>;
    delta_0.x = _S4.x;
    delta_0.y = _S4.y;
    var _S5 : vec2<f32> = step(threshold_0, delta_0.xy);
    var edges_0 : vec2<f32> = _S5;
    if((dot(_S5, vec2<f32>(1.0f, 1.0f))) == 0.0f)
    {
        discard;
    }
    var _S6 : vec2<f32> = abs(_S3 - vec2<f32>(dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(1)].xy), (0.0f))).xyz, weights_0), dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(1)].zw), (0.0f))).xyz, weights_0)));
    delta_0.z = _S6.x;
    delta_0.w = _S6.y;
    var _S7 : vec2<f32> = max(delta_0.xy, delta_0.zw);
    var _S8 : vec2<f32> = abs(_S2 - vec2<f32>(dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(2)].xy), (0.0f))).xyz, weights_0), dot((textureSampleLevel((colorTex_1), (pointSampler_0), (offset_1[i32(2)].zw), (0.0f))).xyz, weights_0)));
    delta_0.z = _S8.x;
    delta_0.w = _S8.y;
    var _S9 : vec2<f32> = max(_S7.xy, delta_0.zw);
    var _S10 : vec2<f32> = edges_0.xy * step(vec2<f32>(max(_S9.x, _S9.y)), vec2<f32>(2.0f) * delta_0.xy);
    edges_0.x = _S10.x;
    edges_0.y = _S10.y;
    return edges_0;
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
fn fsSmaaEdge( _S11 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var offset_2 : array<vec4<f32>, i32(3)>;
    SMAAEdgeDetectionVS_0(_S11.uv_1, &(offset_2));
    var _S12 : vec2<f32> = SMAALumaEdgeDetectionPS_0(_S11.uv_1, offset_2, colorTex_0);
    var _S13 : pixelOutput_0 = pixelOutput_0( vec4<f32>(_S12, 0.0f, 0.0f) );
    return _S13;
}

fn SMAABlendingWeightCalculationVS_0( texcoord_2 : vec2<f32>,  pixcoord_0 : ptr<function, vec2<f32>>,  offset_3 : ptr<function, array<vec4<f32>, i32(3)>>)
{
    (*pixcoord_0) = texcoord_2 * smaaCb_0.metrics_0.zw;
    var _S14 : vec4<f32> = texcoord_2.xyxy;
    var _S15 : vec4<f32> = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(-0.25f, -0.125f, 1.25f, -0.125f), _S14);
    (*offset_3)[i32(0)] = _S15;
    var _S16 : vec4<f32> = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(-0.125f, -0.25f, -0.125f, 1.25f), _S14);
    (*offset_3)[i32(1)] = _S16;
    (*offset_3)[i32(2)] = fma(smaaCb_0.metrics_0.xxyy, vec4<f32>(-2.0f, 2.0f, -2.0f, 2.0f) * vec4<f32>(16.0f), vec4<f32>(_S15.xz, _S16.yw));
    return;
}

fn SMAASearchDiag1_0( edgesTex_1 : texture_2d<f32>,  texcoord_3 : vec2<f32>,  dir_0 : vec2<f32>,  e_0 : ptr<function, vec2<f32>>) -> vec2<f32>
{
    var coord_0 : vec4<f32> = vec4<f32>(texcoord_3, -1.0f, 1.0f);
    var _S17 : vec3<f32> = vec3<f32>(smaaCb_0.metrics_0.xy, 1.0f);
    loop
    {
        var _S18 : bool;
        if(((coord_0.z) < 7.0f))
        {
            _S18 = ((coord_0.w) > 0.89999997615814209f);
        }
        else
        {
            _S18 = false;
        }
        if(_S18)
        {
        }
        else
        {
            break;
        }
        var _S19 : vec3<f32> = fma(_S17, vec3<f32>(dir_0, 1.0f), coord_0.xyz);
        coord_0.x = _S19.x;
        coord_0.y = _S19.y;
        coord_0.z = _S19.z;
        var _S20 : vec2<f32> = (textureSampleLevel((edgesTex_1), (linearSampler_0), (coord_0.xy), (0.0f))).xy;
        (*e_0) = _S20;
        coord_0[i32(3)] = dot(_S20, vec2<f32>(0.5f, 0.5f));
    }
    return coord_0.zw;
}

fn SMAADecodeDiagBilinearAccess_0( e_1 : vec4<f32>) -> vec4<f32>
{
    var _S21 : vec4<f32> = e_1;
    var _S22 : vec2<f32> = e_1.xz;
    var _S23 : vec2<f32> = _S22 * abs(vec2<f32>(5.0f) * _S22 - vec2<f32>(3.75f));
    _S21.x = _S23.x;
    _S21.z = _S23.y;
    return round(_S21);
}

fn SMAAMovc_0( cond_0 : vec2<bool>,  variable_0 : ptr<function, vec2<f32>>,  value_0 : vec2<f32>)
{
    if(cond_0.x)
    {
        (*variable_0)[i32(0)] = value_0.x;
    }
    if(cond_0.y)
    {
        (*variable_0)[i32(1)] = value_0.y;
    }
    return;
}

fn SMAAMovc_1( cond_1 : vec2<bool>,  variable_1 : ptr<function, vec2<f32>>,  value_1 : vec2<f32>)
{
    if(cond_1.x)
    {
        (*variable_1)[i32(0)] = value_1.x;
    }
    if(cond_1.y)
    {
        (*variable_1)[i32(1)] = value_1.y;
    }
    return;
}

fn SMAAMovc_2( cond_2 : vec2<bool>,  variable_2 : ptr<function, vec2<f32>>,  value_2 : vec2<f32>)
{
    if(cond_2.x)
    {
        (*variable_2)[i32(0)] = value_2.x;
    }
    if(cond_2.y)
    {
        (*variable_2)[i32(1)] = value_2.y;
    }
    return;
}

fn SMAAAreaDiag_0( areaTex_1 : texture_2d<f32>,  dist_0 : vec2<f32>,  e_2 : vec2<f32>,  offset_4 : f32) -> vec2<f32>
{
    const _S24 : vec2<f32> = vec2<f32>(160.0f, 560.0f);
    var _S25 : vec2<f32> = vec2<f32>(1.0f);
    var texcoord_4 : vec2<f32> = fma(_S25 / _S24, fma(vec2<f32>(20.0f, 20.0f), e_2, dist_0), vec2<f32>(0.5f) * (_S25 / _S24));
    texcoord_4[i32(0)] = texcoord_4[i32(0)] + 0.5f;
    texcoord_4[i32(1)] = texcoord_4[i32(1)] + 0.1428571492433548f * offset_4;
    return (textureSampleLevel((areaTex_1), (linearSampler_0), (texcoord_4), (0.0f))).xy;
}

fn SMAADecodeDiagBilinearAccess_1( e_3 : vec2<f32>) -> vec2<f32>
{
    var _S26 : vec2<f32> = e_3;
    var _S27 : f32 = e_3.x;
    _S26[i32(0)] = _S27 * abs(5.0f * _S27 - 3.75f);
    return round(_S26);
}

fn SMAASearchDiag2_0( edgesTex_2 : texture_2d<f32>,  texcoord_5 : vec2<f32>,  dir_1 : vec2<f32>,  e_4 : ptr<function, vec2<f32>>) -> vec2<f32>
{
    var coord_1 : vec4<f32> = vec4<f32>(texcoord_5, -1.0f, 1.0f);
    coord_1[i32(0)] = coord_1[i32(0)] + 0.25f * smaaCb_0.metrics_0.x;
    var _S28 : vec3<f32> = vec3<f32>(smaaCb_0.metrics_0.xy, 1.0f);
    loop
    {
        var _S29 : bool;
        if(((coord_1.z) < 7.0f))
        {
            _S29 = ((coord_1.w) > 0.89999997615814209f);
        }
        else
        {
            _S29 = false;
        }
        if(_S29)
        {
        }
        else
        {
            break;
        }
        var _S30 : vec3<f32> = fma(_S28, vec3<f32>(dir_1, 1.0f), coord_1.xyz);
        coord_1.x = _S30.x;
        coord_1.y = _S30.y;
        coord_1.z = _S30.z;
        var _S31 : vec2<f32> = SMAADecodeDiagBilinearAccess_1((textureSampleLevel((edgesTex_2), (linearSampler_0), (coord_1.xy), (0.0f))).xy);
        (*e_4) = _S31;
        coord_1[i32(3)] = dot(_S31, vec2<f32>(0.5f, 0.5f));
    }
    return coord_1.zw;
}

fn SMAACalculateDiagWeights_0( edgesTex_3 : texture_2d<f32>,  areaTex_2 : texture_2d<f32>,  texcoord_6 : vec2<f32>,  e_5 : vec2<f32>,  subsampleIndices_0 : vec4<f32>) -> vec2<f32>
{
    const weights_1 : vec2<f32> = vec2<f32>(0.0f, 0.0f);
    var d_0 : vec4<f32>;
    var end_0 : vec2<f32>;
    if(((e_5.x) > 0.0f))
    {
        var _S32 : vec2<f32> = SMAASearchDiag1_0(edgesTex_3, texcoord_6, vec2<f32>(-1.0f, 1.0f), &(end_0));
        d_0.x = _S32.x;
        d_0.z = _S32.y;
        d_0[i32(0)] = d_0[i32(0)] + f32(((end_0.y) > 0.89999997615814209f));
    }
    else
    {
        d_0.x = weights_1.x;
        d_0.z = weights_1.y;
    }
    var _S33 : vec2<f32> = SMAASearchDiag1_0(edgesTex_3, texcoord_6, vec2<f32>(1.0f, -1.0f), &(end_0));
    d_0.y = _S33.x;
    d_0.w = _S33.y;
    var weights_2 : vec2<f32>;
    if(((d_0.x + d_0.y) > 2.0f))
    {
        var coords_0 : vec4<f32> = fma(vec4<f32>(- d_0.x + 0.25f, d_0.x, d_0.y, - d_0.y - 0.25f), smaaCb_0.metrics_0.xyxy, texcoord_6.xyxy);
        var _S34 : vec2<f32> = (textureSampleLevel((edgesTex_3), (linearSampler_0), (coords_0.xy), (0.0f), (vec2<i32>(i32(-1), i32(0))))).xy;
        var c_0 : vec4<f32>;
        c_0.x = _S34.x;
        c_0.y = _S34.y;
        var _S35 : vec2<f32> = (textureSampleLevel((edgesTex_3), (linearSampler_0), (coords_0.zw), (0.0f), (vec2<i32>(i32(1), i32(0))))).xy;
        c_0.z = _S35.x;
        c_0.w = _S35.y;
        var _S36 : vec4<f32> = SMAADecodeDiagBilinearAccess_0(c_0.xyzw);
        c_0.y = _S36.x;
        c_0.x = _S36.y;
        c_0.w = _S36.z;
        c_0.z = _S36.w;
        var cc_0 : vec2<f32> = fma(vec2<f32>(2.0f, 2.0f), c_0.xz, c_0.yw);
        SMAAMovc_0((step(vec2<f32>(0.89999997615814209f), d_0.zw)) != vec2<f32>(0.0f), &(cc_0), weights_1);
        weights_2 = SMAAAreaDiag_0(areaTex_2, d_0.xy, cc_0, subsampleIndices_0.z);
    }
    else
    {
        weights_2 = weights_1;
    }
    var _S37 : vec2<f32> = SMAASearchDiag2_0(edgesTex_3, texcoord_6, vec2<f32>(-1.0f, -1.0f), &(end_0));
    d_0.x = _S37.x;
    d_0.z = _S37.y;
    const _S38 : vec2<i32> = vec2<i32>(i32(1), i32(0));
    if((((textureSampleLevel((edgesTex_3), (linearSampler_0), (texcoord_6), (0.0f), (_S38))).x) > 0.0f))
    {
        var _S39 : vec2<f32> = SMAASearchDiag2_0(edgesTex_3, texcoord_6, vec2<f32>(1.0f, 1.0f), &(end_0));
        d_0.y = _S39.x;
        d_0.w = _S39.y;
        d_0[i32(1)] = d_0[i32(1)] + f32(((end_0.y) > 0.89999997615814209f));
    }
    else
    {
        d_0.y = weights_1.x;
        d_0.w = weights_1.y;
    }
    if(((d_0.x + d_0.y) > 2.0f))
    {
        var coords_1 : vec4<f32> = fma(vec4<f32>(- d_0.x, - d_0.x, d_0.y, d_0.y), smaaCb_0.metrics_0.xyxy, texcoord_6.xyxy);
        var c_1 : vec4<f32>;
        var _S40 : vec2<f32> = coords_1.xy;
        c_1[i32(0)] = (textureSampleLevel((edgesTex_3), (linearSampler_0), (_S40), (0.0f), (vec2<i32>(i32(-1), i32(0))))).y;
        c_1[i32(1)] = (textureSampleLevel((edgesTex_3), (linearSampler_0), (_S40), (0.0f), (vec2<i32>(i32(0), i32(-1))))).x;
        var _S41 : vec2<f32> = (textureSampleLevel((edgesTex_3), (linearSampler_0), (coords_1.zw), (0.0f), (_S38))).yx;
        c_1.z = _S41.x;
        c_1.w = _S41.y;
        var cc_1 : vec2<f32> = fma(vec2<f32>(2.0f, 2.0f), c_1.xz, c_1.yw);
        SMAAMovc_0((step(vec2<f32>(0.89999997615814209f), d_0.zw)) != vec2<f32>(0.0f), &(cc_1), weights_1);
        weights_2 = weights_2 + SMAAAreaDiag_0(areaTex_2, d_0.xy, cc_1, subsampleIndices_0.w).yx;
    }
    return weights_2;
}

fn SMAASearchLength_0( searchTex_1 : texture_2d<f32>,  e_6 : vec2<f32>,  offset_5 : f32) -> f32
{
    const _S42 : vec2<f32> = vec2<f32>(66.0f, 33.0f);
    const _S43 : vec2<f32> = vec2<f32>(64.0f, 16.0f);
    var _S44 : vec2<f32> = vec2<f32>(1.0f);
    return (textureSampleLevel((searchTex_1), (linearSampler_0), (fma((_S42 * vec2<f32>(0.5f, -1.0f) + vec2<f32>(-1.0f, 1.0f)) * (_S44 / _S43), e_6, (_S42 * vec2<f32>(offset_5, 1.0f) + vec2<f32>(0.5f, -0.5f)) * (_S44 / _S43))), (0.0f))).x;
}

fn SMAASearchXLeft_0( edgesTex_4 : texture_2d<f32>,  searchTex_2 : texture_2d<f32>,  texcoord_7 : vec2<f32>,  end_1 : f32) -> f32
{
    var _S45 : f32;
    const _S46 : vec2<f32> = vec2<f32>(0.0f, 1.0f);
    var _S47 : vec2<f32> = texcoord_7;
    var e_7 : vec2<f32> = _S46;
    loop
    {
        var _S48 : f32 = _S47.x;
        _S45 = _S48;
        var _S49 : bool;
        if((_S48 > end_1))
        {
            _S49 = ((e_7.y) > 0.82810002565383911f);
        }
        else
        {
            _S49 = false;
        }
        var _S50 : bool;
        if(_S49)
        {
            _S50 = (e_7.x) == 0.0f;
        }
        else
        {
            _S50 = false;
        }
        if(_S50)
        {
        }
        else
        {
            break;
        }
        var _S51 : vec2<f32> = (textureSampleLevel((edgesTex_4), (linearSampler_0), (_S47), (0.0f))).xy;
        _S47 = fma((vec2<f32>(0) - vec2<f32>(2.0f, 0.0f)), smaaCb_0.metrics_0.xy, _S47);
        e_7 = _S51;
    }
    return fma(smaaCb_0.metrics_0.x, fma(-2.0078740119934082f, SMAASearchLength_0(searchTex_2, e_7, 0.0f), 3.25f), _S45);
}

fn SMAASearchXRight_0( edgesTex_5 : texture_2d<f32>,  searchTex_3 : texture_2d<f32>,  texcoord_8 : vec2<f32>,  end_2 : f32) -> f32
{
    var _S52 : f32;
    const _S53 : vec2<f32> = vec2<f32>(0.0f, 1.0f);
    var _S54 : vec2<f32> = texcoord_8;
    var e_8 : vec2<f32> = _S53;
    loop
    {
        var _S55 : f32 = _S54.x;
        _S52 = _S55;
        var _S56 : bool;
        if((_S55 < end_2))
        {
            _S56 = ((e_8.y) > 0.82810002565383911f);
        }
        else
        {
            _S56 = false;
        }
        var _S57 : bool;
        if(_S56)
        {
            _S57 = (e_8.x) == 0.0f;
        }
        else
        {
            _S57 = false;
        }
        if(_S57)
        {
        }
        else
        {
            break;
        }
        var _S58 : vec2<f32> = (textureSampleLevel((edgesTex_5), (linearSampler_0), (_S54), (0.0f))).xy;
        _S54 = fma(vec2<f32>(2.0f, 0.0f), smaaCb_0.metrics_0.xy, _S54);
        e_8 = _S58;
    }
    return fma(- smaaCb_0.metrics_0.x, fma(-2.0078740119934082f, SMAASearchLength_0(searchTex_3, e_8, 0.5f), 3.25f), _S52);
}

fn SMAAArea_0( areaTex_3 : texture_2d<f32>,  dist_1 : vec2<f32>,  e1_0 : f32,  e2_0 : f32,  offset_6 : f32) -> vec2<f32>
{
    const _S59 : vec2<f32> = vec2<f32>(160.0f, 560.0f);
    var _S60 : vec2<f32> = vec2<f32>(1.0f);
    var _S61 : vec2<f32> = fma(_S60 / _S59, fma(vec2<f32>(16.0f, 16.0f), round(vec2<f32>(4.0f) * vec2<f32>(e1_0, e2_0)), dist_1), vec2<f32>(0.5f) * (_S60 / _S59));
    var texcoord_9 : vec2<f32> = _S61;
    texcoord_9[i32(1)] = fma(0.1428571492433548f, offset_6, _S61.y);
    return (textureSampleLevel((areaTex_3), (linearSampler_0), (texcoord_9), (0.0f))).xy;
}

fn SMAADetectHorizontalCornerPattern_0( edgesTex_6 : texture_2d<f32>,  weights_3 : ptr<function, vec2<f32>>,  texcoord_10 : vec4<f32>,  d_1 : vec2<f32>)
{
    var leftRight_0 : vec2<f32> = step(d_1.xy, d_1.yx);
    var rounding_0 : vec2<f32> = vec2<f32>(0.75f) * leftRight_0 / vec2<f32>((leftRight_0.x + leftRight_0.y));
    var factor_0 : vec2<f32> = vec2<f32>(1.0f, 1.0f);
    var _S62 : f32 = rounding_0.x;
    var _S63 : vec2<f32> = texcoord_10.xy;
    var _S64 : f32 = rounding_0.y;
    var _S65 : vec2<f32> = texcoord_10.zw;
    factor_0[i32(0)] = factor_0[i32(0)] - _S62 * (textureSampleLevel((edgesTex_6), (linearSampler_0), (_S63), (0.0f), (vec2<i32>(i32(0), i32(1))))).x - _S64 * (textureSampleLevel((edgesTex_6), (linearSampler_0), (_S65), (0.0f), (vec2<i32>(i32(1), i32(1))))).x;
    factor_0[i32(1)] = factor_0[i32(1)] - _S62 * (textureSampleLevel((edgesTex_6), (linearSampler_0), (_S63), (0.0f), (vec2<i32>(i32(0), i32(-2))))).x - _S64 * (textureSampleLevel((edgesTex_6), (linearSampler_0), (_S65), (0.0f), (vec2<i32>(i32(1), i32(-2))))).x;
    (*weights_3) = (*weights_3) * saturate(factor_0);
    return;
}

fn SMAASearchYUp_0( edgesTex_7 : texture_2d<f32>,  searchTex_4 : texture_2d<f32>,  texcoord_11 : vec2<f32>,  end_3 : f32) -> f32
{
    var _S66 : f32;
    const _S67 : vec2<f32> = vec2<f32>(1.0f, 0.0f);
    var _S68 : vec2<f32> = texcoord_11;
    var e_9 : vec2<f32> = _S67;
    loop
    {
        var _S69 : f32 = _S68.y;
        _S66 = _S69;
        var _S70 : bool;
        if((_S69 > end_3))
        {
            _S70 = ((e_9.x) > 0.82810002565383911f);
        }
        else
        {
            _S70 = false;
        }
        var _S71 : bool;
        if(_S70)
        {
            _S71 = (e_9.y) == 0.0f;
        }
        else
        {
            _S71 = false;
        }
        if(_S71)
        {
        }
        else
        {
            break;
        }
        var _S72 : vec2<f32> = (textureSampleLevel((edgesTex_7), (linearSampler_0), (_S68), (0.0f))).xy;
        _S68 = fma((vec2<f32>(0) - vec2<f32>(0.0f, 2.0f)), smaaCb_0.metrics_0.xy, _S68);
        e_9 = _S72;
    }
    return fma(smaaCb_0.metrics_0.y, fma(-2.0078740119934082f, SMAASearchLength_0(searchTex_4, e_9.yx, 0.0f), 3.25f), _S66);
}

fn SMAASearchYDown_0( edgesTex_8 : texture_2d<f32>,  searchTex_5 : texture_2d<f32>,  texcoord_12 : vec2<f32>,  end_4 : f32) -> f32
{
    var _S73 : f32;
    const _S74 : vec2<f32> = vec2<f32>(1.0f, 0.0f);
    var _S75 : vec2<f32> = texcoord_12;
    var e_10 : vec2<f32> = _S74;
    loop
    {
        var _S76 : f32 = _S75.y;
        _S73 = _S76;
        var _S77 : bool;
        if((_S76 < end_4))
        {
            _S77 = ((e_10.x) > 0.82810002565383911f);
        }
        else
        {
            _S77 = false;
        }
        var _S78 : bool;
        if(_S77)
        {
            _S78 = (e_10.y) == 0.0f;
        }
        else
        {
            _S78 = false;
        }
        if(_S78)
        {
        }
        else
        {
            break;
        }
        var _S79 : vec2<f32> = (textureSampleLevel((edgesTex_8), (linearSampler_0), (_S75), (0.0f))).xy;
        _S75 = fma(vec2<f32>(0.0f, 2.0f), smaaCb_0.metrics_0.xy, _S75);
        e_10 = _S79;
    }
    return fma(- smaaCb_0.metrics_0.y, fma(-2.0078740119934082f, SMAASearchLength_0(searchTex_5, e_10.yx, 0.5f), 3.25f), _S73);
}

fn SMAADetectVerticalCornerPattern_0( edgesTex_9 : texture_2d<f32>,  weights_4 : ptr<function, vec2<f32>>,  texcoord_13 : vec4<f32>,  d_2 : vec2<f32>)
{
    var leftRight_1 : vec2<f32> = step(d_2.xy, d_2.yx);
    var rounding_1 : vec2<f32> = vec2<f32>(0.75f) * leftRight_1 / vec2<f32>((leftRight_1.x + leftRight_1.y));
    var factor_1 : vec2<f32> = vec2<f32>(1.0f, 1.0f);
    var _S80 : f32 = rounding_1.x;
    var _S81 : vec2<f32> = texcoord_13.xy;
    var _S82 : f32 = rounding_1.y;
    var _S83 : vec2<f32> = texcoord_13.zw;
    factor_1[i32(0)] = factor_1[i32(0)] - _S80 * (textureSampleLevel((edgesTex_9), (linearSampler_0), (_S81), (0.0f), (vec2<i32>(i32(1), i32(0))))).y - _S82 * (textureSampleLevel((edgesTex_9), (linearSampler_0), (_S83), (0.0f), (vec2<i32>(i32(1), i32(1))))).y;
    factor_1[i32(1)] = factor_1[i32(1)] - _S80 * (textureSampleLevel((edgesTex_9), (linearSampler_0), (_S81), (0.0f), (vec2<i32>(i32(-2), i32(0))))).y - _S82 * (textureSampleLevel((edgesTex_9), (linearSampler_0), (_S83), (0.0f), (vec2<i32>(i32(-2), i32(1))))).y;
    (*weights_4) = (*weights_4) * saturate(factor_1);
    return;
}

fn SMAABlendingWeightCalculationPS_0( texcoord_14 : vec2<f32>,  pixcoord_1 : vec2<f32>,  offset_7 : array<vec4<f32>, i32(3)>,  edgesTex_10 : texture_2d<f32>,  areaTex_4 : texture_2d<f32>,  searchTex_6 : texture_2d<f32>,  subsampleIndices_1 : vec4<f32>) -> vec4<f32>
{
    var weights_5 : vec4<f32> = vec4<f32>(0.0f, 0.0f, 0.0f, 0.0f);
    var _S84 : vec2<f32> = (textureSampleLevel((edgesTex_10), (linearSampler_0), (texcoord_14), (0.0f))).xy;
    var e_11 : vec2<f32> = _S84;
    if(((_S84.y) > 0.0f))
    {
        var _S85 : vec2<f32> = SMAACalculateDiagWeights_0(edgesTex_10, areaTex_4, texcoord_14, e_11, subsampleIndices_1);
        weights_5.x = _S85.x;
        weights_5.y = _S85.y;
        if((weights_5.x) == (- weights_5.y))
        {
            var coords_2 : vec3<f32>;
            coords_2[i32(0)] = SMAASearchXLeft_0(edgesTex_10, searchTex_6, offset_7[i32(0)].xy, offset_7[i32(2)].x);
            coords_2[i32(1)] = offset_7[i32(1)].y;
            var d_3 : vec2<f32>;
            d_3[i32(0)] = coords_2.x;
            var e1_1 : f32 = (textureSampleLevel((edgesTex_10), (linearSampler_0), (coords_2.xy), (0.0f))).x;
            coords_2[i32(2)] = SMAASearchXRight_0(edgesTex_10, searchTex_6, offset_7[i32(0)].zw, offset_7[i32(2)].y);
            d_3[i32(1)] = coords_2.z;
            var _S86 : vec2<f32> = abs(round(fma(smaaCb_0.metrics_0.zz, d_3, (vec2<f32>(0) - pixcoord_1.xx))));
            d_3 = _S86;
            var _S87 : vec2<f32> = SMAAArea_0(areaTex_4, sqrt(_S86), e1_1, (textureSampleLevel((edgesTex_10), (linearSampler_0), (coords_2.zy), (0.0f), (vec2<i32>(i32(1), i32(0))))).x, subsampleIndices_1.y);
            weights_5.x = _S87.x;
            weights_5.y = _S87.y;
            coords_2[i32(1)] = texcoord_14.y;
            var _S88 : vec2<f32> = weights_5.xy;
            SMAADetectHorizontalCornerPattern_0(edgesTex_10, &(_S88), coords_2.xyzy, _S86);
            var _S89 : vec2<f32> = _S88;
            weights_5.x = _S89.x;
            weights_5.y = _S89.y;
        }
        else
        {
            e_11[i32(0)] = 0.0f;
        }
    }
    if(((e_11.x) > 0.0f))
    {
        var coords_3 : vec3<f32>;
        coords_3[i32(1)] = SMAASearchYUp_0(edgesTex_10, searchTex_6, offset_7[i32(1)].xy, offset_7[i32(2)].z);
        coords_3[i32(0)] = offset_7[i32(0)].x;
        var d_4 : vec2<f32>;
        d_4[i32(0)] = coords_3.y;
        var e1_2 : f32 = (textureSampleLevel((edgesTex_10), (linearSampler_0), (coords_3.xy), (0.0f))).y;
        coords_3[i32(2)] = SMAASearchYDown_0(edgesTex_10, searchTex_6, offset_7[i32(1)].zw, offset_7[i32(2)].w);
        d_4[i32(1)] = coords_3.z;
        var _S90 : vec2<f32> = abs(round(fma(smaaCb_0.metrics_0.ww, d_4, (vec2<f32>(0) - pixcoord_1.yy))));
        d_4 = _S90;
        var _S91 : vec2<f32> = SMAAArea_0(areaTex_4, sqrt(_S90), e1_2, (textureSampleLevel((edgesTex_10), (linearSampler_0), (coords_3.xz), (0.0f), (vec2<i32>(i32(0), i32(1))))).y, subsampleIndices_1.x);
        weights_5.z = _S91.x;
        weights_5.w = _S91.y;
        coords_3[i32(0)] = texcoord_14.x;
        var _S92 : vec2<f32> = weights_5.zw;
        SMAADetectVerticalCornerPattern_0(edgesTex_10, &(_S92), coords_3.xyxz, _S90);
        var _S93 : vec2<f32> = _S92;
        weights_5.z = _S93.x;
        weights_5.w = _S93.y;
    }
    return weights_5;
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
fn fsSmaaWeight( _S94 : pixelInput_1, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_1
{
    var offset_8 : array<vec4<f32>, i32(3)>;
    var pixcoord_2 : vec2<f32>;
    SMAABlendingWeightCalculationVS_0(_S94.uv_2, &(pixcoord_2), &(offset_8));
    var _S95 : pixelOutput_1 = pixelOutput_1( SMAABlendingWeightCalculationPS_0(_S94.uv_2, pixcoord_2, offset_8, edgesTex_0, areaTex_0, searchTex_0, vec4<f32>(0.0f, 0.0f, 0.0f, 0.0f)) );
    return _S95;
}

fn SMAANeighborhoodBlendingVS_0( texcoord_15 : vec2<f32>,  offset_9 : ptr<function, vec4<f32>>)
{
    (*offset_9) = fma(smaaCb_0.metrics_0.xyxy, vec4<f32>(1.0f, 0.0f, 0.0f, 1.0f), texcoord_15.xyxy);
    return;
}

fn SMAAMovc_3( cond_3 : vec4<bool>,  variable_3 : ptr<function, vec4<f32>>,  value_3 : vec4<f32>)
{
    var _S96 : vec2<bool> = cond_3.xy;
    var _S97 : vec2<f32> = (*variable_3).xy;
    SMAAMovc_2(_S96, &(_S97), value_3.xy);
    var _S98 : vec2<f32> = _S97;
    (*variable_3).x = _S98.x;
    (*variable_3).y = _S98.y;
    var _S99 : vec2<bool> = cond_3.zw;
    var _S100 : vec2<f32> = (*variable_3).zw;
    SMAAMovc_2(_S99, &(_S100), value_3.zw);
    var _S101 : vec2<f32> = _S100;
    (*variable_3).z = _S101.x;
    (*variable_3).w = _S101.y;
    return;
}

fn SMAANeighborhoodBlendingPS_0( texcoord_16 : vec2<f32>,  offset_10 : vec4<f32>,  colorTex_2 : texture_2d<f32>,  blendTex_1 : texture_2d<f32>) -> vec4<f32>
{
    var a_0 : vec4<f32>;
    a_0[i32(0)] = (textureSampleLevel((blendTex_1), (linearSampler_0), (offset_10.xy), (0.0f))).w;
    a_0[i32(1)] = (textureSampleLevel((blendTex_1), (linearSampler_0), (offset_10.zw), (0.0f))).y;
    var _S102 : vec2<f32> = (textureSampleLevel((blendTex_1), (linearSampler_0), (texcoord_16), (0.0f))).xz;
    a_0.w = _S102.x;
    a_0.z = _S102.y;
    if(((dot(a_0, vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f))) < 9.9999997473787516e-06f))
    {
        return (textureSampleLevel((colorTex_2), (linearSampler_0), (texcoord_16), (0.0f)));
    }
    else
    {
        var h_0 : bool = ((max(a_0.x, a_0.z)) > (max(a_0.y, a_0.w)));
        var blendingOffset_0 : vec4<f32> = vec4<f32>(0.0f, a_0.y, 0.0f, a_0.w);
        var blendingWeight_0 : vec2<f32> = a_0.yw;
        SMAAMovc_3(vec4<bool>(h_0, h_0, h_0, h_0), &(blendingOffset_0), vec4<f32>(a_0.x, 0.0f, a_0.z, 0.0f));
        SMAAMovc_1(vec2<bool>(h_0, h_0), &(blendingWeight_0), a_0.xz);
        var _S103 : vec2<f32> = blendingWeight_0 / vec2<f32>(dot(blendingWeight_0, vec2<f32>(1.0f, 1.0f)));
        blendingWeight_0 = _S103;
        var blendingCoord_0 : vec4<f32> = fma(blendingOffset_0, vec4<f32>(smaaCb_0.metrics_0.xy, (vec2<f32>(0) - smaaCb_0.metrics_0.xy)), texcoord_16.xyxy);
        return vec4<f32>(_S103.x) * (textureSampleLevel((colorTex_2), (linearSampler_0), (blendingCoord_0.xy), (0.0f))) + vec4<f32>(_S103.y) * (textureSampleLevel((colorTex_2), (linearSampler_0), (blendingCoord_0.zw), (0.0f)));
    }
}

struct pixelOutput_2
{
    @location(0) output_2 : vec4<f32>,
};

struct pixelInput_2
{
    @location(0) uv_3 : vec2<f32>,
};

@fragment
fn fsSmaaBlend( _S104 : pixelInput_2, @builtin(position) pos_3 : vec4<f32>) -> pixelOutput_2
{
    var offset_11 : vec4<f32>;
    SMAANeighborhoodBlendingVS_0(_S104.uv_3, &(offset_11));
    var _S105 : pixelOutput_2 = pixelOutput_2( SMAANeighborhoodBlendingPS_0(_S104.uv_3, offset_11, colorTex_0, blendTex_0) );
    return _S105;
}

