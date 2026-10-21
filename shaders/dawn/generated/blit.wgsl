@binding(0) @group(0) var blitSource_0 : texture_2d<f32>;

struct BlitOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
};

@vertex
fn vsBlit(@builtin(vertex_index) vid_0 : u32) -> BlitOut_0
{
    var o_0 : BlitOut_0;
    o_0.pos_0 = vec4<f32>(vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    return o_0;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

@fragment
fn fsBlit(@builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S1 : vec3<i32> = vec3<i32>(vec2<i32>(pos_1.xy), i32(0));
    var _S2 : pixelOutput_0 = pixelOutput_0( (textureLoad((blitSource_0), ((_S1)).xy, ((_S1)).z)) );
    return _S2;
}

