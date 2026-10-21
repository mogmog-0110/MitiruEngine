struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct Draw_std140_0
{
    @align(16) transform_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) colorMatrix_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
    @align(16) stopColors_0 : array<vec4<f32>, i32(16)>,
    @align(16) stopPositions_0 : array<vec4<f32>, i32(4)>,
    @align(16) color_0 : vec4<f32>,
    @align(16) weights_0 : vec4<f32>,
    @align(16) a_0 : vec4<f32>,
    @align(16) b_0 : vec4<f32>,
    @align(16) c_0 : vec4<f32>,
    @align(16) d_0 : vec4<f32>,
    @align(16) f_0 : vec4<f32>,
    @align(16) ints_0 : vec4<i32>,
};

@binding(0) @group(0) var<uniform> draw_0 : Draw_std140_0;
@binding(1) @group(0) var uTex_0 : texture_2d<f32>;

@binding(3) @group(0) var sWrap_0 : sampler;

@binding(4) @group(0) var sClamp_0 : sampler;

@binding(2) @group(0) var uMask_0 : texture_2d<f32>;

struct GeoOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) color_1 : vec4<f32>,
    @location(1) uv_0 : vec2<f32>,
};

struct vertexInput_0
{
    @location(0) pos_1 : vec2<f32>,
    @location(1) color_2 : vec4<f32>,
    @location(2) uv_1 : vec2<f32>,
};

@vertex
fn vsGeometry( _S1 : vertexInput_0) -> GeoOut_0
{
    var o_0 : GeoOut_0;
    o_0.pos_0 = (((vec4<f32>(_S1.pos_1 + draw_0.a_0.xy, 0.0f, 1.0f)) * (mat4x4<f32>(draw_0.transform_0.data_0[i32(0)][i32(0)], draw_0.transform_0.data_0[i32(1)][i32(0)], draw_0.transform_0.data_0[i32(2)][i32(0)], draw_0.transform_0.data_0[i32(3)][i32(0)], draw_0.transform_0.data_0[i32(0)][i32(1)], draw_0.transform_0.data_0[i32(1)][i32(1)], draw_0.transform_0.data_0[i32(2)][i32(1)], draw_0.transform_0.data_0[i32(3)][i32(1)], draw_0.transform_0.data_0[i32(0)][i32(2)], draw_0.transform_0.data_0[i32(1)][i32(2)], draw_0.transform_0.data_0[i32(2)][i32(2)], draw_0.transform_0.data_0[i32(3)][i32(2)], draw_0.transform_0.data_0[i32(0)][i32(3)], draw_0.transform_0.data_0[i32(1)][i32(3)], draw_0.transform_0.data_0[i32(2)][i32(3)], draw_0.transform_0.data_0[i32(3)][i32(3)]))));
    o_0.color_1 = _S1.color_2;
    o_0.uv_0 = _S1.uv_1;
    return o_0;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) color_3 : vec4<f32>,
    @location(1) uv_2 : vec2<f32>,
};

@fragment
fn fsColor( _S2 : pixelInput_0, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_0
{
    var _S3 : pixelOutput_0 = pixelOutput_0( _S2.color_3 );
    return _S3;
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) color_4 : vec4<f32>,
    @location(1) uv_3 : vec2<f32>,
};

@fragment
fn fsTexture( _S4 : pixelInput_1, @builtin(position) pos_3 : vec4<f32>) -> pixelOutput_1
{
    var _S5 : pixelOutput_1 = pixelOutput_1( _S4.color_4 * (textureSample((uTex_0), (sWrap_0), (_S4.uv_3))) );
    return _S5;
}

fn stopPosition_0( k_0 : i32) -> f32
{
    return draw_0.stopPositions_0[k_0 / i32(4)][k_0 % i32(4)];
}

fn glslMod_0( x_0 : f32,  y_0 : f32) -> f32
{
    return x_0 - y_0 * floor(x_0 / y_0);
}

fn mixStops_0( t_0 : f32) -> vec4<f32>
{
    var c_1 : vec4<f32> = draw_0.stopColors_0[i32(0)];
    var k_1 : i32 = i32(1);
    loop
    {
        if((k_1 < (draw_0.ints_0.y)))
        {
        }
        else
        {
            break;
        }
        var _S6 : vec4<f32> = mix(c_1, draw_0.stopColors_0[k_1], vec4<f32>(smoothstep(stopPosition_0(k_1 - i32(1)), stopPosition_0(k_1), t_0)));
        var k_2 : i32 = k_1 + i32(1);
        c_1 = _S6;
        k_1 = k_2;
    }
    return c_1;
}

struct pixelOutput_2
{
    @location(0) output_2 : vec4<f32>,
};

struct pixelInput_2
{
    @location(0) color_5 : vec4<f32>,
    @location(1) uv_4 : vec2<f32>,
};

@fragment
fn fsGradient( _S7 : pixelInput_2, @builtin(position) pos_4 : vec4<f32>) -> pixelOutput_2
{
    var f_1 : i32 = draw_0.ints_0.x % i32(3);
    var d_1 : vec2<f32> = _S7.uv_4 - draw_0.a_0.zw;
    var v_0 : vec2<f32> = draw_0.b_0.xy;
    var t_1 : f32;
    if(f_1 == i32(0))
    {
        t_1 = dot(v_0, d_1) / dot(v_0, v_0);
    }
    else
    {
        if(f_1 == i32(1))
        {
            t_1 = length(v_0 * d_1);
        }
        else
        {
            var _S8 : f32 = v_0.x;
            var _S9 : f32 = d_1.x;
            var _S10 : f32 = v_0.y;
            var _S11 : f32 = d_1.y;
            t_1 = 0.5f + atan2(- (_S8 * _S9 + _S10 * _S11), - _S10 * _S9 + _S8 * _S11) / 6.2831854820251465f;
        }
    }
    if(((draw_0.ints_0.x) >= i32(3)))
    {
        var t0_0 : f32 = stopPosition_0(i32(0));
        t_1 = t0_0 + glslMod_0(t_1 - t0_0, stopPosition_0(draw_0.ints_0.y - i32(1)) - t0_0);
    }
    else
    {
    }
    var _S12 : pixelOutput_2 = pixelOutput_2( _S7.color_5 * mixStops_0(t_1) );
    return _S12;
}

struct FullOut_0
{
    @builtin(position) pos_5 : vec4<f32>,
    @location(0) uv_5 : vec2<f32>,
};

@vertex
fn vsFullscreen(@builtin(vertex_index) id_0 : u32) -> FullOut_0
{
    var o_1 : FullOut_0;
    var _S13 : f32;
    if(id_0 == u32(2))
    {
        _S13 = 3.0f;
    }
    else
    {
        _S13 = -1.0f;
    }
    var _S14 : f32;
    if(id_0 == u32(1))
    {
        _S14 = 3.0f;
    }
    else
    {
        _S14 = -1.0f;
    }
    o_1.pos_5 = vec4<f32>(vec2<f32>(_S13, _S14), 0.0f, 1.0f);
    o_1.uv_5 = vec2<f32>(_S13 * 0.5f + 0.5f, 0.5f - _S14 * 0.5f) * draw_0.d_0.zw + draw_0.d_0.xy;
    return o_1;
}

struct pixelOutput_3
{
    @location(0) output_3 : vec4<f32>,
};

struct pixelInput_3
{
    @location(0) uv_6 : vec2<f32>,
};

@fragment
fn fsCopy( _S15 : pixelInput_3, @builtin(position) pos_6 : vec4<f32>) -> pixelOutput_3
{
    var _S16 : vec3<i32> = vec3<i32>(vec2<i32>(pos_6.xy), i32(0));
    var _S17 : pixelOutput_3 = pixelOutput_3( (textureLoad((uTex_0), ((_S16)).xy, ((_S16)).z)) * vec4<f32>(draw_0.f_0.x) );
    return _S17;
}

struct pixelOutput_4
{
    @location(0) output_4 : vec4<f32>,
};

struct pixelInput_4
{
    @location(0) uv_7 : vec2<f32>,
};

@fragment
fn fsSample( _S18 : pixelInput_4, @builtin(position) pos_7 : vec4<f32>) -> pixelOutput_4
{
    var _S19 : pixelOutput_4 = pixelOutput_4( (textureSample((uTex_0), (sClamp_0), (_S18.uv_7))) );
    return _S19;
}

struct pixelOutput_5
{
    @location(0) output_5 : vec4<f32>,
};

struct pixelInput_5
{
    @location(0) uv_8 : vec2<f32>,
};

@fragment
fn fsColorMatrix( _S20 : pixelInput_5, @builtin(position) pos_8 : vec4<f32>) -> pixelOutput_5
{
    var _S21 : vec3<i32> = vec3<i32>(vec2<i32>(pos_8.xy), i32(0));
    var c_2 : vec4<f32> = (textureLoad((uTex_0), ((_S21)).xy, ((_S21)).z));
    var _S22 : pixelOutput_5 = pixelOutput_5( vec4<f32>((((c_2) * (mat4x4<f32>(draw_0.colorMatrix_0.data_0[i32(0)][i32(0)], draw_0.colorMatrix_0.data_0[i32(1)][i32(0)], draw_0.colorMatrix_0.data_0[i32(2)][i32(0)], draw_0.colorMatrix_0.data_0[i32(3)][i32(0)], draw_0.colorMatrix_0.data_0[i32(0)][i32(1)], draw_0.colorMatrix_0.data_0[i32(1)][i32(1)], draw_0.colorMatrix_0.data_0[i32(2)][i32(1)], draw_0.colorMatrix_0.data_0[i32(3)][i32(1)], draw_0.colorMatrix_0.data_0[i32(0)][i32(2)], draw_0.colorMatrix_0.data_0[i32(1)][i32(2)], draw_0.colorMatrix_0.data_0[i32(2)][i32(2)], draw_0.colorMatrix_0.data_0[i32(3)][i32(2)], draw_0.colorMatrix_0.data_0[i32(0)][i32(3)], draw_0.colorMatrix_0.data_0[i32(1)][i32(3)], draw_0.colorMatrix_0.data_0[i32(2)][i32(3)], draw_0.colorMatrix_0.data_0[i32(3)][i32(3)])))).xyz, c_2.w) );
    return _S22;
}

struct pixelOutput_6
{
    @location(0) output_6 : vec4<f32>,
};

struct pixelInput_6
{
    @location(0) uv_9 : vec2<f32>,
};

@fragment
fn fsBlendMask( _S23 : pixelInput_6, @builtin(position) pos_9 : vec4<f32>) -> pixelOutput_6
{
    var p_0 : vec3<i32> = vec3<i32>(vec2<i32>(pos_9.xy), i32(0));
    var _S24 : pixelOutput_6 = pixelOutput_6( (textureLoad((uTex_0), ((p_0)).xy, ((p_0)).z)) * vec4<f32>((textureLoad((uMask_0), ((p_0)).xy, ((p_0)).z)).w) );
    return _S24;
}

fn inRegion_0( uv_10 : vec2<f32>) -> f32
{
    var s_0 : vec2<f32> = step(draw_0.c_0.xy, uv_10) * step(uv_10, draw_0.c_0.zw);
    return s_0.x * s_0.y;
}

struct pixelOutput_7
{
    @location(0) output_7 : vec4<f32>,
};

struct pixelInput_7
{
    @location(0) uv_11 : vec2<f32>,
};

@fragment
fn fsDropShadow( _S25 : pixelInput_7, @builtin(position) pos_10 : vec4<f32>) -> pixelOutput_7
{
    var _S26 : pixelOutput_7 = pixelOutput_7( vec4<f32>(((textureSample((uTex_0), (sClamp_0), (_S25.uv_11))).w * inRegion_0(_S25.uv_11))) * draw_0.color_0 );
    return _S26;
}

struct pixelOutput_8
{
    @location(0) output_8 : vec4<f32>,
};

struct pixelInput_8
{
    @location(0) uv_12 : vec2<f32>,
};

@fragment
fn fsBlur( _S27 : pixelInput_8, @builtin(position) pos_11 : vec4<f32>) -> pixelOutput_8
{
    const _S28 : vec4<f32> = vec4<f32>(0.0f, 0.0f, 0.0f, 0.0f);
    var k_3 : i32 = i32(0);
    var c_3 : vec4<f32> = _S28;
    loop
    {
        if((k_3 < i32(7)))
        {
        }
        else
        {
            break;
        }
        var _S29 : i32 = k_3 - i32(3);
        var uv_13 : vec2<f32> = _S27.uv_12 - vec2<f32>(f32(_S29)) * draw_0.b_0.zw;
        var c_4 : vec4<f32> = c_3 + (textureSample((uTex_0), (sClamp_0), (uv_13))) * vec4<f32>(inRegion_0(uv_13)) * vec4<f32>(draw_0.weights_0[abs(_S29)]);
        k_3 = k_3 + i32(1);
        c_3 = c_4;
    }
    var _S30 : pixelOutput_8 = pixelOutput_8( c_3 );
    return _S30;
}

struct pixelOutput_9
{
    @location(0) output_9 : vec4<f32>,
};

struct pixelInput_9
{
    @location(0) uv_14 : vec2<f32>,
};

@fragment
fn fsClear( _S31 : pixelInput_9, @builtin(position) pos_12 : vec4<f32>) -> pixelOutput_9
{
    var _S32 : pixelOutput_9 = pixelOutput_9( vec4<f32>(0.0f, 0.0f, 0.0f, 0.0f) );
    return _S32;
}

