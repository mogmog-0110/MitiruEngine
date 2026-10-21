@binding(1) @group(0) var sceneCopy_0 : texture_2d<f32>;

struct ImpactCB_std140_0
{
    @align(16) lightColor_0 : vec4<f32>,
    @align(16) darkColor_0 : vec4<f32>,
    @align(16) params_0 : vec4<f32>,
    @align(16) keep_0 : array<vec4<f32>, i32(2)>,
};

@binding(0) @group(0) var<uniform> impact_0 : ImpactCB_std140_0;
@binding(2) @group(0) var normalMs_0 : texture_multisampled_2d<f32>;

struct ImpactOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
};

@vertex
fn vsImpact(@builtin(vertex_index) vid_0 : u32) -> ImpactOut_0
{
    var t_0 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2)))));
    var o_0 : ImpactOut_0;
    o_0.pos_0 = vec4<f32>(t_0 * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    o_0.uv_0 = t_0;
    return o_0;
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
fn fsImpact( _S1 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S2 : vec3<i32> = vec3<i32>(vec2<i32>(pos_1.xy), i32(0));
    var c_0 : vec3<f32> = (textureLoad((sceneCopy_0), ((_S2)).xy, ((_S2)).z)).xyz;
    var slot_0 : u32 = (u32(round((textureLoad((normalMs_0), (min(vec2<i32>(_S1.uv_1 * impact_0.params_0.yz), vec2<i32>(impact_0.params_0.yz) - vec2<i32>(i32(1), i32(1)))), (i32(0)))).w * 255.0f)) >> (u32(5)));
    var keep_1 : f32;
    if((slot_0 < u32(4)))
    {
        keep_1 = impact_0.keep_0[i32(0)][slot_0];
    }
    else
    {
        keep_1 = impact_0.keep_0[i32(1)][slot_0 - u32(4)];
    }
    if((keep_1 > 0.5f))
    {
        var _S3 : pixelOutput_0 = pixelOutput_0( vec4<f32>(c_0, 1.0f) );
        return _S3;
    }
    var lum_0 : f32 = dot(c_0, vec3<f32>(0.2125999927520752f, 0.71520000696182251f, 0.072200000286102295f));
    var o_1 : vec3<f32>;
    if(((impact_0.params_0.x) < 1.5f))
    {
        if((lum_0 >= (impact_0.lightColor_0.w)))
        {
            o_1 = impact_0.lightColor_0.xyz;
        }
        else
        {
            o_1 = impact_0.darkColor_0.xyz;
        }
    }
    else
    {
        o_1 = vec3<f32>(1.0f, 1.0f, 1.0f) - c_0;
    }
    var _S4 : pixelOutput_0 = pixelOutput_0( vec4<f32>(mix(c_0, o_1, vec3<f32>(impact_0.darkColor_0.w)), 1.0f) );
    return _S4;
}

