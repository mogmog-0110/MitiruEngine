struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct ShadowDrawCB_std140_0
{
    @align(16) mvp_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
};

@binding(0) @group(0) var<uniform> shadowDraw_0 : ShadowDrawCB_std140_0;
struct vertexOutput_0
{
    @builtin(position) output_0 : vec4<f32>,
};

struct vertexInput_0
{
    @location(0) pos_0 : vec3<f32>,
};

@vertex
fn vsShadow( _S1 : vertexInput_0) -> vertexOutput_0
{
    var _S2 : vertexOutput_0 = vertexOutput_0( (((vec4<f32>(_S1.pos_0, 1.0f)) * (mat4x4<f32>(shadowDraw_0.mvp_0.data_0[i32(0)][i32(0)], shadowDraw_0.mvp_0.data_0[i32(1)][i32(0)], shadowDraw_0.mvp_0.data_0[i32(2)][i32(0)], shadowDraw_0.mvp_0.data_0[i32(3)][i32(0)], shadowDraw_0.mvp_0.data_0[i32(0)][i32(1)], shadowDraw_0.mvp_0.data_0[i32(1)][i32(1)], shadowDraw_0.mvp_0.data_0[i32(2)][i32(1)], shadowDraw_0.mvp_0.data_0[i32(3)][i32(1)], shadowDraw_0.mvp_0.data_0[i32(0)][i32(2)], shadowDraw_0.mvp_0.data_0[i32(1)][i32(2)], shadowDraw_0.mvp_0.data_0[i32(2)][i32(2)], shadowDraw_0.mvp_0.data_0[i32(3)][i32(2)], shadowDraw_0.mvp_0.data_0[i32(0)][i32(3)], shadowDraw_0.mvp_0.data_0[i32(1)][i32(3)], shadowDraw_0.mvp_0.data_0[i32(2)][i32(3)], shadowDraw_0.mvp_0.data_0[i32(3)][i32(3)])))) );
    return _S2;
}

