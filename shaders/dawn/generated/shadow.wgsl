struct vertexOutput_0
{
    @builtin(position) output_0 : vec4<f32>,
};

struct vertexInput_0
{
    @location(0) pos_0 : vec3<f32>,
    @location(1) c0_0 : vec4<f32>,
    @location(2) c1_0 : vec4<f32>,
    @location(3) c2_0 : vec4<f32>,
    @location(4) c3_0 : vec4<f32>,
};

@vertex
fn vsShadow( _S1 : vertexInput_0) -> vertexOutput_0
{
    var _S2 : vertexOutput_0 = vertexOutput_0( (((vec4<f32>(_S1.pos_0, 1.0f)) * (mat4x4<f32>(vec4<f32>(_S1.c0_0.x, _S1.c1_0.x, _S1.c2_0.x, _S1.c3_0.x), vec4<f32>(_S1.c0_0.y, _S1.c1_0.y, _S1.c2_0.y, _S1.c3_0.y), vec4<f32>(_S1.c0_0.z, _S1.c1_0.z, _S1.c2_0.z, _S1.c3_0.z), vec4<f32>(_S1.c0_0.w, _S1.c1_0.w, _S1.c2_0.w, _S1.c3_0.w))))) );
    return _S2;
}

