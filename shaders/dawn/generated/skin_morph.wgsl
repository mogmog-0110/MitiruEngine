struct SkinMorphParams_std140_0
{
    @align(16) vertexCount_0 : u32,
    @align(4) jointCount_0 : u32,
    @align(8) firstInstance_0 : u32,
    @align(4) rowsOffset_0 : u32,
    @align(16) entriesOffset_0 : u32,
    @align(4) pad0_0 : u32,
    @align(8) pad1_0 : u32,
    @align(4) pad2_0 : u32,
};

@binding(0) @group(0) var<uniform> params_0 : SkinMorphParams_std140_0;
@binding(3) @group(0) var<storage, read> instances_0 : array<vec2<u32>>;

@binding(1) @group(0) var<storage, read> source_0 : array<u32>;

@binding(2) @group(0) var<storage, read> palette_0 : array<vec4<f32>>;

@binding(4) @group(0) var<storage, read_write> outVertices_0 : array<vec4<f32>>;

fn sourceF4_0( word_0 : u32) -> vec4<f32>
{
    return vec4<f32>((bitcast<f32>((source_0[word_0]))), (bitcast<f32>((source_0[word_0 + u32(1)]))), (bitcast<f32>((source_0[word_0 + u32(2)]))), (bitcast<f32>((source_0[word_0 + u32(3)]))));
}

@compute
@workgroup_size(64, 1, 1)
fn csSkinMorph(@builtin(global_invocation_id) id_0 : vec3<u32>)
{
    var i_0 : u32 = id_0.x;
    if((i_0 >= (params_0.vertexCount_0)))
    {
        return;
    }
    var at_0 : u32 = params_0.firstInstance_0 + id_0.y * u32(2);
    var inst_0 : vec2<u32> = instances_0[at_0];
    var _S1 : vec2<u32> = instances_0[at_0 + u32(1)];
    var b_0 : u32 = i_0 * u32(20);
    var v0_0 : vec4<f32> = sourceF4_0(b_0);
    var v1_0 : vec4<f32> = sourceF4_0(b_0 + u32(4));
    var v2_0 : vec4<f32> = sourceF4_0(b_0 + u32(8));
    var _S2 : vec4<u32> = vec4<u32>(source_0[b_0 + u32(12)], source_0[b_0 + u32(13)], source_0[b_0 + u32(14)], source_0[b_0 + u32(15)]);
    var weights_0 : vec4<f32> = sourceF4_0(b_0 + u32(16));
    var _S3 : vec3<f32> = v0_0.xyz;
    var _S4 : vec3<f32> = vec3<f32>(v0_0.w, v1_0.x, v1_0.y);
    var rowBegin_0 : u32 = source_0[params_0.rowsOffset_0 + i_0];
    var _S5 : u32 = source_0[params_0.rowsOffset_0 + i_0 + u32(1)];
    var touched_0 : bool = false;
    var e_0 : u32 = rowBegin_0;
    var mpos_0 : vec3<f32> = _S3;
    var mnrm_0 : vec3<f32> = _S4;
    loop
    {
        if((e_0 < _S5))
        {
        }
        else
        {
            break;
        }
        var eb_0 : u32 = params_0.entriesOffset_0 + e_0 * u32(8);
        var slot_0 : u32 = source_0[eb_0];
        if((slot_0 >= (_S1.y)))
        {
            e_0 = e_0 + u32(1);
            continue;
        }
        var wi_0 : u32 = _S1.x * u32(16) + slot_0;
        var _S6 : vec4<f32> = palette_0[wi_0 / u32(4)];
        var _S7 : u32 = wi_0 % u32(4);
        if((_S6[_S7]) == 0.0f)
        {
            e_0 = e_0 + u32(1);
            continue;
        }
        var _S8 : vec3<f32> = vec3<f32>(_S6[_S7]);
        var mpos_1 : vec3<f32> = mpos_0 + vec3<f32>((bitcast<f32>((source_0[eb_0 + u32(1)]))), (bitcast<f32>((source_0[eb_0 + u32(2)]))), (bitcast<f32>((source_0[eb_0 + u32(3)])))) * _S8;
        var mnrm_1 : vec3<f32> = mnrm_0 + vec3<f32>((bitcast<f32>((source_0[eb_0 + u32(4)]))), (bitcast<f32>((source_0[eb_0 + u32(5)]))), (bitcast<f32>((source_0[eb_0 + u32(6)])))) * _S8;
        touched_0 = true;
        mpos_0 = mpos_1;
        mnrm_0 = mnrm_1;
        e_0 = e_0 + u32(1);
    }
    var _S9 : vec4<f32> = vec4<f32>(mpos_0, mnrm_0.x);
    var _S10 : f32 = v1_0.z;
    var _S11 : f32 = v1_0.w;
    var _S12 : vec4<f32> = vec4<f32>(mnrm_0.y, mnrm_0.z, _S10, _S11);
    var wsum_0 : f32 = weights_0.x + weights_0.y + weights_0.z + weights_0.w;
    var o0_0 : vec4<f32>;
    var o1_0 : vec4<f32>;
    if((wsum_0 > 9.99999993922529029e-09f))
    {
        var _S13 : vec4<f32> = vec4<f32>(0.0f);
        var k_0 : u32 = u32(0);
        o0_0 = _S13;
        o1_0 = _S13;
        var r2_0 : vec4<f32> = _S13;
        var r3_0 : vec4<f32> = _S13;
        loop
        {
            if((k_0 < u32(4)))
            {
            }
            else
            {
                break;
            }
            var _S14 : u32 = k_0;
            var _S15 : u32 = k_0;
            if((weights_0[k_0]) == 0.0f)
            {
                touched_0 = true;
            }
            else
            {
                touched_0 = ((_S2[_S15]) >= (params_0.jointCount_0));
            }
            if(touched_0)
            {
                k_0 = k_0 + u32(1);
                continue;
            }
            var pb_0 : u32 = (inst_0.x + _S2[_S15]) * u32(4);
            var _S16 : vec4<f32> = vec4<f32>((weights_0[_S14] / wsum_0));
            var r1_0 : vec4<f32> = o1_0 + palette_0[pb_0 + u32(1)] * _S16;
            var r2_1 : vec4<f32> = r2_0 + palette_0[pb_0 + u32(2)] * _S16;
            var r3_1 : vec4<f32> = r3_0 + palette_0[pb_0 + u32(3)] * _S16;
            o0_0 = o0_0 + palette_0[pb_0] * _S16;
            o1_0 = r1_0;
            r2_0 = r2_1;
            r3_0 = r3_1;
            k_0 = k_0 + u32(1);
        }
        var p_0 : vec4<f32> = vec4<f32>(mpos_0, 1.0f);
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
        var n_0 : vec3<f32> = vec3<f32>(dot(o0_0.xyz, mnrm_0), dot(o1_0.xyz, mnrm_0), dot(r2_0.xyz, mnrm_0));
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
        var _S17 : vec4<f32> = vec4<f32>(n_1.y, n_1.z, _S10, _S11);
        o0_0 = vec4<f32>(pos_0, n_1.x);
        o1_0 = _S17;
    }
    else
    {
        if(touched_0)
        {
            var len_1 : f32 = length(mnrm_0);
            if((len_1 > 1.00000001168609742e-07f))
            {
                var n_2 : vec3<f32> = mnrm_0 / vec3<f32>(len_1);
                var _S18 : vec4<f32> = vec4<f32>(n_2.y, n_2.z, _S10, _S11);
                o0_0 = vec4<f32>(mpos_0, n_2.x);
                o1_0 = _S18;
            }
            else
            {
                o0_0 = _S9;
                o1_0 = _S12;
            }
        }
        else
        {
            o0_0 = _S9;
            o1_0 = _S12;
        }
    }
    var o_0 : u32 = (inst_0.y + i_0) * u32(3);
    outVertices_0[o_0] = o0_0;
    outVertices_0[o_0 + u32(1)] = o1_0;
    outVertices_0[o_0 + u32(2)] = v2_0;
    return;
}

