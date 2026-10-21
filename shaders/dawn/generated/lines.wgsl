struct LineCB_std140_0
{
    @align(16) screen_0 : vec4<f32>,
    @align(16) depthParams_0 : vec4<f32>,
    @align(16) extra_0 : vec4<f32>,
    @align(16) slotColor_0 : array<vec4<f32>, i32(8)>,
    @align(16) slotWidth_0 : array<vec4<f32>, i32(8)>,
    @align(16) slotFade_0 : array<vec4<f32>, i32(8)>,
};

@binding(0) @group(0) var<uniform> lineCb_0 : LineCB_std140_0;
@binding(2) @group(0) var normalMs_0 : texture_multisampled_2d<f32>;

@binding(1) @group(0) var depthMs_0 : texture_multisampled_2d<f32>;

@binding(3) @group(0) var seedTex_0 : texture_2d<f32>;

struct LineOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
};

@vertex
fn vsLine(@builtin(vertex_index) vid_0 : u32) -> LineOut_0
{
    var o_0 : LineOut_0;
    o_0.pos_0 = vec4<f32>(vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    return o_0;
}

fn decodeDepth_0( c_0 : vec4<f32>) -> f32
{
    return f32((((((u32(c_0.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(c_0.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(c_0.z * 255.0f + 0.5f)))) / 1.6777215e+07f * lineCb_0.depthParams_0.x;
}

fn slotOfNormalA_0( a_0 : f32) -> u32
{
    return (u32(round(a_0 * 255.0f)) >> (u32(5)));
}

struct LinePixel_0
{
     d_0 : f32,
     n_0 : vec3<f32>,
     slot_0 : u32,
     excluded_0 : bool,
};

fn loadLinePixel_0( p_0 : vec2<i32>) -> LinePixel_0
{
    var _S1 : vec2<i32> = clamp(p_0, vec2<i32>(i32(0), i32(0)), vec2<i32>(lineCb_0.screen_0.xy) - vec2<i32>(i32(1), i32(1)));
    var e_0 : vec4<f32> = (textureLoad((normalMs_0), (_S1), (i32(0))));
    var o_1 : LinePixel_0;
    o_1.d_0 = decodeDepth_0((textureLoad((depthMs_0), (_S1), (i32(0)))));
    o_1.n_0 = e_0.xyz * vec3<f32>(2.0f) - vec3<f32>(1.0f);
    o_1.slot_0 = slotOfNormalA_0(e_0.w);
    var _S2 : bool;
    if(((e_0.x) > 0.99000000953674316f))
    {
        _S2 = ((e_0.y) > 0.99000000953674316f);
    }
    else
    {
        _S2 = false;
    }
    if(_S2)
    {
        _S2 = ((e_0.z) > 0.99000000953674316f);
    }
    else
    {
        _S2 = false;
    }
    o_1.excluded_0 = _S2;
    return o_1;
}

fn ownsEdge_0( c_1 : LinePixel_0,  q_0 : LinePixel_0,  opp_0 : LinePixel_0) -> bool
{
    var _S3 : bool;
    if(q_0.excluded_0)
    {
        _S3 = true;
    }
    else
    {
        _S3 = ((q_0.d_0) < (c_1.d_0));
    }
    if(_S3)
    {
        return false;
    }
    if((q_0.slot_0) != (c_1.slot_0))
    {
        return true;
    }
    var iOpp_0 : f32;
    if(opp_0.excluded_0)
    {
        iOpp_0 = 1.0f / c_1.d_0;
    }
    else
    {
        iOpp_0 = 1.0f / opp_0.d_0;
    }
    if(((1.0f / q_0.d_0) < ((2.0f / c_1.d_0 - iOpp_0) * (1.0f - lineCb_0.depthParams_0.z))))
    {
        return true;
    }
    var _S4 : vec3<f32> = c_1.n_0;
    if(((dot(_S4, _S4)) < 0.25f))
    {
        _S3 = true;
    }
    else
    {
        var _S5 : vec3<f32> = q_0.n_0;
        _S3 = ((dot(_S5, _S5)) < 0.25f);
    }
    if(_S3)
    {
        return false;
    }
    return ((dot(normalize(c_1.n_0), normalize(q_0.n_0))) < (lineCb_0.depthParams_0.w));
}

struct pixelOutput_0
{
    @location(0) output_0 : vec2<f32>,
};

@fragment
fn fsSeed(@builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S6 : vec2<i32> = vec2<i32>(pos_1.xy);
    var c_2 : LinePixel_0 = loadLinePixel_0(_S6);
    if(c_2.excluded_0)
    {
        var _S7 : pixelOutput_0 = pixelOutput_0( vec2<f32>(0.0f, 0.0f) );
        return _S7;
    }
    var l_0 : LinePixel_0 = loadLinePixel_0(_S6 + vec2<i32>(i32(-1), i32(0)));
    var r_0 : LinePixel_0 = loadLinePixel_0(_S6 + vec2<i32>(i32(1), i32(0)));
    var u_0 : LinePixel_0 = loadLinePixel_0(_S6 + vec2<i32>(i32(0), i32(-1)));
    var d_1 : LinePixel_0 = loadLinePixel_0(_S6 + vec2<i32>(i32(0), i32(1)));
    var own_0 : bool;
    if(ownsEdge_0(c_2, l_0, r_0))
    {
        own_0 = true;
    }
    else
    {
        own_0 = ownsEdge_0(c_2, r_0, l_0);
    }
    if(own_0)
    {
        own_0 = true;
    }
    else
    {
        own_0 = ownsEdge_0(c_2, u_0, d_1);
    }
    if(own_0)
    {
        own_0 = true;
    }
    else
    {
        own_0 = ownsEdge_0(c_2, d_1, u_0);
    }
    var _S8 : vec2<f32>;
    if(own_0)
    {
        _S8 = vec2<f32>(c_2.d_0, f32(c_2.slot_0));
    }
    else
    {
        _S8 = vec2<f32>(0.0f, 0.0f);
    }
    var _S9 : pixelOutput_0 = pixelOutput_0( _S8 );
    return _S9;
}

fn scenePixel_0( pos_2 : vec2<f32>) -> vec2<i32>
{
    return vec2<i32>(pos_2 * lineCb_0.screen_0.xy / lineCb_0.screen_0.zw);
}

fn lineWidthAt_0( s_0 : u32,  d_2 : f32) -> f32
{
    var w_0 : vec4<f32> = lineCb_0.slotWidth_0[s_0];
    var _S10 : f32 = lineCb_0.slotWidth_0[s_0].z;
    var _S11 : bool;
    if((_S10 > 0.0f))
    {
        _S11 = (d_2 > _S10);
    }
    else
    {
        _S11 = false;
    }
    var _S12 : f32;
    if(_S11)
    {
        _S12 = w_0.y;
    }
    else
    {
        _S12 = w_0.x;
    }
    return _S12;
}

fn lineAlphaAt_0( s_1 : u32,  d_3 : f32) -> f32
{
    var f_0 : vec4<f32> = lineCb_0.slotFade_0[s_1];
    var _S13 : f32 = lineCb_0.slotFade_0[s_1].y;
    var _S14 : f32 = lineCb_0.slotFade_0[s_1].x;
    if(!(_S13 > _S14))
    {
        return 1.0f;
    }
    return mix(1.0f, f_0.z, saturate((d_3 - _S14) / (_S13 - _S14)));
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

@fragment
fn fsLine(@builtin(position) pos_3 : vec4<f32>) -> pixelOutput_1
{
    var p_1 : vec2<i32> = scenePixel_0(pos_3.xy);
    var _S15 : f32 = decodeDepth_0((textureLoad((depthMs_0), (p_1), (i32(0)))));
    var radius_0 : i32 = i32(lineCb_0.extra_0.x);
    const _S16 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var _S17 : vec2<i32> = vec2<i32>(lineCb_0.screen_0.xy) - vec2<i32>(i32(1), i32(1));
    var _S18 : i32 = - radius_0;
    var bestA_0 : f32 = 0.0f;
    var bestD_0 : f32 = 1.00000001504746622e+30f;
    var bestC_0 : vec3<f32> = _S16;
    var y_0 : i32 = _S18;
    loop
    {
        if((y_0 <= radius_0))
        {
        }
        else
        {
            break;
        }
        var bestA_1 : f32 = bestA_0;
        var bestD_1 : f32 = bestD_0;
        var bestC_1 : vec3<f32> = bestC_0;
        var x_0 : i32 = _S18;
        loop
        {
            if((x_0 <= radius_0))
            {
            }
            else
            {
                break;
            }
            var _S19 : vec3<i32> = vec3<i32>(clamp(p_1 + vec2<i32>(x_0, y_0), vec2<i32>(i32(0), i32(0)), _S17), i32(0));
            var s_2 : vec2<f32> = (textureLoad((seedTex_0), ((_S19)).xy, ((_S19)).z).xy).xy;
            var _S20 : f32 = s_2.x;
            if((_S20 <= 0.0f))
            {
                x_0 = x_0 + i32(1);
                continue;
            }
            var _S21 : u32 = min(u32(s_2.y + 0.5f), u32(7));
            var w_1 : f32 = lineWidthAt_0(_S21, _S20);
            var _S22 : bool;
            if((w_1 <= 0.0f))
            {
                _S22 = true;
            }
            else
            {
                _S22 = (_S15 < (_S20 * 0.97000002861022949f - 0.019999999552965164f));
            }
            if(_S22)
            {
                x_0 = x_0 + i32(1);
                continue;
            }
            var t_0 : f32 = length(vec2<f32>(f32(x_0), f32(y_0)));
            var a_1 : f32 = saturate(min(t_0 + 0.5f, w_1 * 0.5f) - max(t_0 - 0.5f, - w_1 * 0.5f)) * lineAlphaAt_0(_S21, _S20) * lineCb_0.slotColor_0[_S21].w;
            var _S23 : bool;
            if((a_1 > (bestA_1 + 0.0010000000474974513f)))
            {
                _S23 = true;
            }
            else
            {
                if((a_1 > (bestA_1 - 0.0010000000474974513f)))
                {
                    _S23 = (a_1 > 0.0f);
                }
                else
                {
                    _S23 = false;
                }
                var _S24 : bool;
                if(_S23)
                {
                    _S24 = (_S20 < bestD_1);
                }
                else
                {
                    _S24 = false;
                }
                _S23 = _S24;
            }
            var bestA_2 : f32;
            var bestD_2 : f32;
            var bestC_2 : vec3<f32>;
            if(_S23)
            {
                var _S25 : vec3<f32> = lineCb_0.slotColor_0[_S21].xyz;
                bestA_2 = a_1;
                bestD_2 = _S20;
                bestC_2 = _S25;
            }
            else
            {
                bestA_2 = bestA_1;
                bestD_2 = bestD_1;
                bestC_2 = bestC_1;
            }
            bestA_1 = bestA_2;
            bestD_1 = bestD_2;
            bestC_1 = bestC_2;
            x_0 = x_0 + i32(1);
        }
        var y_1 : i32 = y_0 + i32(1);
        bestA_0 = bestA_1;
        bestD_0 = bestD_1;
        bestC_0 = bestC_1;
        y_0 = y_1;
    }
    if((bestA_0 <= 0.0040000001899898052f))
    {
        discard;
    }
    var _S26 : pixelOutput_1 = pixelOutput_1( vec4<f32>(bestC_0, bestA_0) );
    return _S26;
}

