struct SkinParams_std140_0
{
    @align(16) vertexCount_0 : u32,
    @align(4) jointCount_0 : u32,
    @align(8) firstInstance_0 : u32,
    @align(4) pad_0 : u32,
};

@binding(0) @group(0) var<uniform> params_0 : SkinParams_std140_0;
@binding(3) @group(0) var<storage, read> instances_0 : array<vec2<u32>>;

struct SkinSource_std430_0
{
    @align(16) v0_0 : vec4<f32>,
    @align(16) v1_0 : vec4<f32>,
    @align(16) v2_0 : vec4<f32>,
    @align(16) joints_0 : vec4<u32>,
    @align(16) weights_0 : vec4<f32>,
};

@binding(1) @group(0) var<storage, read> source_0 : array<SkinSource_std430_0>;

@binding(2) @group(0) var<storage, read> palette_0 : array<vec4<f32>>;

@binding(4) @group(0) var<storage, read_write> outVertices_0 : array<vec4<f32>>;

@compute
@workgroup_size(64, 1, 1)
fn csSkin(@builtin(global_invocation_id) id_0 : vec3<u32>)
{
    var i_0 : u32 = id_0.x;
    if((i_0 >= (params_0.vertexCount_0)))
    {
        return;
    }
    var inst_0 : vec2<u32> = instances_0[params_0.firstInstance_0 + id_0.y];
    var s_0 : SkinSource_std430_0 = source_0[i_0];
    var wsum_0 : f32 = s_0.weights_0.x + s_0.weights_0.y + s_0.weights_0.z + s_0.weights_0.w;
    var o0_0 : vec4<f32>;
    var o1_0 : vec4<f32>;
    if((wsum_0 > 9.99999993922529029e-09f))
    {
        var _S1 : vec4<f32> = vec4<f32>(0.0f);
        var k_0 : u32 = u32(0);
        o0_0 = _S1;
        o1_0 = _S1;
        var r2_0 : vec4<f32> = _S1;
        var r3_0 : vec4<f32> = _S1;
        loop
        {
            if((k_0 < u32(4)))
            {
            }
            else
            {
                break;
            }
            var _S2 : u32 = k_0;
            var _S3 : u32 = k_0;
            var _S4 : bool;
            if((s_0.weights_0[k_0]) == 0.0f)
            {
                _S4 = true;
            }
            else
            {
                _S4 = ((s_0.joints_0[_S3]) >= (params_0.jointCount_0));
            }
            if(_S4)
            {
                k_0 = k_0 + u32(1);
                continue;
            }
            var b_0 : u32 = (inst_0.x + s_0.joints_0[_S3]) * u32(4);
            var _S5 : vec4<f32> = vec4<f32>((s_0.weights_0[_S2] / wsum_0));
            var r1_0 : vec4<f32> = o1_0 + palette_0[b_0 + u32(1)] * _S5;
            var r2_1 : vec4<f32> = r2_0 + palette_0[b_0 + u32(2)] * _S5;
            var r3_1 : vec4<f32> = r3_0 + palette_0[b_0 + u32(3)] * _S5;
            o0_0 = o0_0 + palette_0[b_0] * _S5;
            o1_0 = r1_0;
            r2_0 = r2_1;
            r3_0 = r3_1;
            k_0 = k_0 + u32(1);
        }
        var p_0 : vec4<f32> = vec4<f32>(s_0.v0_0.xyz, 1.0f);
        var pw_0 : f32 = dot(r3_0, p_0);
        var invW_0 : f32;
        if(pw_0 == 0.0f)
        {
            invW_0 = 0.0f;
        }
        else
        {
            invW_0 = 1.0f / pw_0;
        }
        var pos_0 : vec3<f32> = vec3<f32>(dot(o0_0, p_0), dot(o1_0, p_0), dot(r2_0, p_0)) * vec3<f32>(invW_0);
        var nIn_0 : vec3<f32> = vec3<f32>(s_0.v0_0.w, s_0.v1_0.x, s_0.v1_0.y);
        var n_0 : vec3<f32> = vec3<f32>(dot(o0_0.xyz, nIn_0), dot(o1_0.xyz, nIn_0), dot(r2_0.xyz, nIn_0));
        var len_0 : f32 = length(n_0);
        var n_1 : vec3<f32>;
        if((len_0 > 1.00000001168609742e-07f))
        {
            n_1 = n_0 / vec3<f32>(len_0);
        }
        else
        {
            n_1 = n_0;
        }
        var _S6 : vec4<f32> = vec4<f32>(n_1.y, n_1.z, s_0.v1_0.z, s_0.v1_0.w);
        o0_0 = vec4<f32>(pos_0, n_1.x);
        o1_0 = _S6;
    }
    else
    {
        o0_0 = s_0.v0_0;
        o1_0 = s_0.v1_0;
    }
    var o_0 : u32 = (inst_0.y + i_0) * u32(3);
    outVertices_0[o_0] = o0_0;
    outVertices_0[o_0 + u32(1)] = o1_0;
    outVertices_0[o_0 + u32(2)] = s_0.v2_0;
    return;
}

