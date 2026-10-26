struct _MatrixStorage_float4x4_ColMajorstd140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct BatchCB_std140_0
{
    @align(16) ortho_0 : _MatrixStorage_float4x4_ColMajorstd140_0,
};

@binding(0) @group(0) var<uniform> batch_0 : BatchCB_std140_0;
@binding(0) @group(1) var imageTex_0 : texture_2d<f32>;

@binding(1) @group(1) var imageSamp_0 : sampler;

struct BatchOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) color_0 : vec4<f32>,
};

struct vertexInput_0
{
    @location(0) pos_1 : vec2<f32>,
    @location(1) uv_0 : vec2<f32>,
    @location(2) color_1 : vec4<f32>,
};

@vertex
fn vsBatch( _S1 : vertexInput_0) -> BatchOut_0
{
    var o_0 : BatchOut_0;
    o_0.pos_0 = (((vec4<f32>(_S1.pos_1, 0.0f, 1.0f)) * (mat4x4<f32>(batch_0.ortho_0.data_0[i32(0)][i32(0)], batch_0.ortho_0.data_0[i32(1)][i32(0)], batch_0.ortho_0.data_0[i32(2)][i32(0)], batch_0.ortho_0.data_0[i32(3)][i32(0)], batch_0.ortho_0.data_0[i32(0)][i32(1)], batch_0.ortho_0.data_0[i32(1)][i32(1)], batch_0.ortho_0.data_0[i32(2)][i32(1)], batch_0.ortho_0.data_0[i32(3)][i32(1)], batch_0.ortho_0.data_0[i32(0)][i32(2)], batch_0.ortho_0.data_0[i32(1)][i32(2)], batch_0.ortho_0.data_0[i32(2)][i32(2)], batch_0.ortho_0.data_0[i32(3)][i32(2)], batch_0.ortho_0.data_0[i32(0)][i32(3)], batch_0.ortho_0.data_0[i32(1)][i32(3)], batch_0.ortho_0.data_0[i32(2)][i32(3)], batch_0.ortho_0.data_0[i32(3)][i32(3)]))));
    o_0.color_0 = _S1.color_1;
    return o_0;
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) color_2 : vec4<f32>,
};

@fragment
fn fsBatch( _S2 : pixelInput_0, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_0
{
    var _S3 : pixelOutput_0 = pixelOutput_0( _S2.color_2 );
    return _S3;
}

struct ImageOut_0
{
    @builtin(position) pos_3 : vec4<f32>,
    @location(0) color_3 : vec4<f32>,
    @location(1) uv_1 : vec2<f32>,
};

struct vertexInput_1
{
    @location(0) pos_4 : vec2<f32>,
    @location(1) uv_2 : vec2<f32>,
    @location(2) color_4 : vec4<f32>,
};

@vertex
fn vsImage( _S4 : vertexInput_1) -> ImageOut_0
{
    var o_1 : ImageOut_0;
    o_1.pos_3 = (((vec4<f32>(_S4.pos_4, 0.0f, 1.0f)) * (mat4x4<f32>(batch_0.ortho_0.data_0[i32(0)][i32(0)], batch_0.ortho_0.data_0[i32(1)][i32(0)], batch_0.ortho_0.data_0[i32(2)][i32(0)], batch_0.ortho_0.data_0[i32(3)][i32(0)], batch_0.ortho_0.data_0[i32(0)][i32(1)], batch_0.ortho_0.data_0[i32(1)][i32(1)], batch_0.ortho_0.data_0[i32(2)][i32(1)], batch_0.ortho_0.data_0[i32(3)][i32(1)], batch_0.ortho_0.data_0[i32(0)][i32(2)], batch_0.ortho_0.data_0[i32(1)][i32(2)], batch_0.ortho_0.data_0[i32(2)][i32(2)], batch_0.ortho_0.data_0[i32(3)][i32(2)], batch_0.ortho_0.data_0[i32(0)][i32(3)], batch_0.ortho_0.data_0[i32(1)][i32(3)], batch_0.ortho_0.data_0[i32(2)][i32(3)], batch_0.ortho_0.data_0[i32(3)][i32(3)]))));
    o_1.color_3 = _S4.color_4;
    o_1.uv_1 = _S4.uv_2;
    return o_1;
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) color_5 : vec4<f32>,
    @location(1) uv_3 : vec2<f32>,
};

@fragment
fn fsImage( _S5 : pixelInput_1, @builtin(position) pos_5 : vec4<f32>) -> pixelOutput_1
{
    var _S6 : pixelOutput_1 = pixelOutput_1( (textureSample((imageTex_0), (imageSamp_0), (_S5.uv_3))) * _S5.color_5 );
    return _S6;
}

