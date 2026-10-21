@binding(0) @group(0) var viewImage_0 : texture_2d<f32>;

@binding(1) @group(0) var viewSampler_0 : sampler;

struct ViewOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) uv_0 : vec2<f32>,
};

@vertex
fn vsView(@builtin(vertex_index) vid_0 : u32) -> ViewOut_0
{
    var t_0 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2)))));
    var o_0 : ViewOut_0;
    o_0.uv_0 = t_0;
    o_0.pos_0 = vec4<f32>(t_0 * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
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
fn fsView( _S1 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var _S2 : pixelOutput_0 = pixelOutput_0( vec4<f32>((textureSample((viewImage_0), (viewSampler_0), (_S1.uv_1))).xyz, 1.0f) );
    return _S2;
}

