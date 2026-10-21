@binding(0) @group(0) var oitAccum_0 : texture_2d<f32>;

@binding(1) @group(0) var oitReveal_0 : texture_2d<f32>;

struct OitCompositeOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
};

@vertex
fn vsOitComposite(@builtin(vertex_index) vid_0 : u32) -> OitCompositeOut_0
{
    var o_0 : OitCompositeOut_0;
    o_0.pos_0 = vec4<f32>(vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f), 0.0f, 1.0f);
    return o_0;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

@fragment
fn fsOitComposite(@builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var p_0 : vec3<i32> = vec3<i32>(vec2<i32>(pos_1.xy), i32(0));
    var ac_0 : vec4<f32> = (textureLoad((oitAccum_0), ((p_0)).xy, ((p_0)).z));
    var _S1 : pixelOutput_0 = pixelOutput_0( vec4<f32>(ac_0.xyz / vec3<f32>(max(ac_0.w, 9.9999997473787516e-06f)), 1.0f - (textureLoad((oitReveal_0), ((p_0)).xy, ((p_0)).z)).x) );
    return _S1;
}

