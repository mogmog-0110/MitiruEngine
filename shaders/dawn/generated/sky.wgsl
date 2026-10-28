struct SkyCB_std140_0
{
    @align(16) cameraKm_0 : vec4<f32>,
    @align(16) sunDir_0 : vec4<f32>,
    @align(16) rayleigh_0 : vec4<f32>,
    @align(16) mie_0 : vec4<f32>,
    @align(16) ozone_0 : vec4<f32>,
    @align(16) ground_0 : vec4<f32>,
    @align(16) radii_0 : vec4<f32>,
    @align(16) camForward_0 : vec4<f32>,
    @align(16) camRight_0 : vec4<f32>,
    @align(16) camUp_0 : vec4<f32>,
    @align(16) cloud_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> sky_0 : SkyCB_std140_0;
@binding(2) @group(0) var transmittanceTex_0 : texture_2d<f32>;

@binding(1) @group(0) var linearClamp_0 : sampler;

@binding(3) @group(0) var multiScatterTex_0 : texture_2d<f32>;

@binding(4) @group(0) var skyViewTex_0 : texture_2d<f32>;

@binding(5) @group(0) var lineDepthMs_0 : texture_multisampled_2d<f32>;

struct FullOut_0
{
    @builtin(position) pos_0 : vec4<f32>,
    @location(0) ndc_0 : vec2<f32>,
};

@vertex
fn vsFull(@builtin(vertex_index) vid_0 : u32) -> FullOut_0
{
    var o_0 : FullOut_0;
    var _S1 : vec2<f32> = vec2<f32>(f32((((vid_0 << (u32(1)))) & (u32(2)))), f32((vid_0 & (u32(2))))) * vec2<f32>(2.0f, -2.0f) + vec2<f32>(-1.0f, 1.0f);
    o_0.ndc_0 = _S1;
    o_0.pos_0 = vec4<f32>(_S1, 0.0f, 1.0f);
    return o_0;
}

fn transmittanceRMu_0( uv_0 : vec2<f32>,  r_0 : ptr<function, f32>,  mu_0 : ptr<function, f32>)
{
    var H_0 : f32 = sqrt(sky_0.radii_0.y * sky_0.radii_0.y - sky_0.radii_0.x * sky_0.radii_0.x);
    var rho_0 : f32 = H_0 * uv_0.y;
    var _S2 : f32 = rho_0 * rho_0;
    var _S3 : f32 = sqrt(_S2 + sky_0.radii_0.x * sky_0.radii_0.x);
    (*r_0) = _S3;
    var dMin_0 : f32 = sky_0.radii_0.y - _S3;
    var d_0 : f32 = dMin_0 + uv_0.x * (rho_0 + H_0 - dMin_0);
    var _S4 : f32;
    if(d_0 == 0.0f)
    {
        _S4 = 1.0f;
    }
    else
    {
        _S4 = (H_0 * H_0 - _S2 - d_0 * d_0) / (2.0f * (*r_0) * d_0);
    }
    (*mu_0) = clamp(_S4, -1.0f, 1.0f);
    return;
}

fn distanceToSphere_0( r_1 : f32,  mu_1 : f32,  R_0 : f32) -> f32
{
    var disc_0 : f32 = r_1 * r_1 * (mu_1 * mu_1 - 1.0f) + R_0 * R_0;
    if((disc_0 < 0.0f))
    {
        return -1.0f;
    }
    var s_0 : f32 = sqrt(disc_0);
    var _S5 : f32 = - r_1 * mu_1;
    var nearT_0 : f32 = _S5 - s_0;
    var _S6 : f32;
    if((nearT_0 > 0.0f))
    {
        _S6 = nearT_0;
    }
    else
    {
        _S6 = _S5 + s_0;
    }
    return _S6;
}

fn extinctionAt_0( h_0 : f32) -> vec3<f32>
{
    var _S7 : f32 = - h_0;
    var m_0 : f32 = exp(_S7 / sky_0.mie_0.z);
    return sky_0.rayleigh_0.xyz * vec3<f32>(exp(_S7 / sky_0.rayleigh_0.w)) + vec3<f32>(sky_0.mie_0.y * m_0, sky_0.mie_0.y * m_0, sky_0.mie_0.y * m_0) + sky_0.ozone_0.xyz * vec3<f32>(max(0.0f, 1.0f - abs(h_0 - sky_0.ozone_0.w) / sky_0.ground_0.w));
}

struct pixelOutput_0
{
    @location(0) output_0 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) ndc_1 : vec2<f32>,
};

@fragment
fn fsTransmittance( _S8 : pixelInput_0, @builtin(position) pos_1 : vec4<f32>) -> pixelOutput_0
{
    var r_2 : f32;
    var mu_2 : f32;
    transmittanceRMu_0((floor(pos_1.xy) + vec2<f32>(0.5f)) / vec2<f32>(256.0f, 64.0f), &(r_2), &(mu_2));
    var _S9 : f32 = max(distanceToSphere_0(r_2, mu_2, sky_0.radii_0.y), 0.0f) / 40.0f;
    const _S10 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var k_0 : i32 = i32(0);
    var optical_0 : vec3<f32> = _S10;
    loop
    {
        if((k_0 < i32(40)))
        {
        }
        else
        {
            break;
        }
        var s_1 : f32 = (f32(k_0) + 0.5f) * _S9;
        var optical_1 : vec3<f32> = optical_0 + extinctionAt_0(sqrt(r_2 * r_2 + s_1 * s_1 + 2.0f * r_2 * mu_2 * s_1) - sky_0.radii_0.x) * vec3<f32>(_S9);
        k_0 = k_0 + i32(1);
        optical_0 = optical_1;
    }
    var _S11 : pixelOutput_0 = pixelOutput_0( vec4<f32>(exp((vec3<f32>(0) - optical_0)), 1.0f) );
    return _S11;
}

fn hitsGround_0( r_3 : f32,  mu_3 : f32) -> bool
{
    var _S12 : bool;
    if((mu_3 < 0.0f))
    {
        _S12 = ((r_3 * r_3 * (mu_3 * mu_3 - 1.0f) + sky_0.radii_0.x * sky_0.radii_0.x) >= 0.0f);
    }
    else
    {
        _S12 = false;
    }
    return _S12;
}

fn transmittanceUv_0( r_4 : f32,  mu_4 : f32) -> vec2<f32>
{
    var H_1 : f32 = sqrt(sky_0.radii_0.y * sky_0.radii_0.y - sky_0.radii_0.x * sky_0.radii_0.x);
    var _S13 : f32 = r_4 * r_4;
    var rho_1 : f32 = sqrt(max(_S13 - sky_0.radii_0.x * sky_0.radii_0.x, 0.0f));
    var dMin_1 : f32 = sky_0.radii_0.y - r_4;
    return vec2<f32>((max(0.0f, - r_4 * mu_4 + sqrt(max(_S13 * (mu_4 * mu_4 - 1.0f) + sky_0.radii_0.y * sky_0.radii_0.y, 0.0f))) - dMin_1) / max(rho_1 + H_1 - dMin_1, 9.99999997475242708e-07f), rho_1 / H_1);
}

fn sampleTransmittance_0( r_5 : f32,  mu_5 : f32) -> vec3<f32>
{
    return (textureSampleLevel((transmittanceTex_0), (linearClamp_0), (transmittanceUv_0(r_5, mu_5)), (0.0f))).xyz;
}

fn scatteringMie_0( h_1 : f32) -> f32
{
    return sky_0.mie_0.x * exp(- h_1 / sky_0.mie_0.z);
}

fn scatteringRayleigh_0( h_2 : f32) -> vec3<f32>
{
    return sky_0.rayleigh_0.xyz * vec3<f32>(exp(- h_2 / sky_0.rayleigh_0.w));
}

struct pixelOutput_1
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_1
{
    @location(0) ndc_2 : vec2<f32>,
};

@fragment
fn fsMultiScatter( _S14 : pixelInput_1, @builtin(position) pos_2 : vec4<f32>) -> pixelOutput_1
{
    var sunT_0 : vec3<f32>;
    var uv_1 : vec2<f32> = (floor(pos_2.xy) + vec2<f32>(0.5f)) / vec2<f32>(32.0f, 32.0f);
    var cosSun_0 : f32 = uv_1.x * 2.0f - 1.0f;
    var _S15 : vec3<f32> = vec3<f32>(0.0f, max(sky_0.radii_0.x + uv_1.y * (sky_0.radii_0.y - sky_0.radii_0.x), sky_0.radii_0.x + 0.0099999997764825821f), 0.0f);
    var _S16 : vec3<f32> = vec3<f32>(sqrt(saturate(1.0f - cosSun_0 * cosSun_0)), cosSun_0, 0.0f);
    const _S17 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    var a_0 : i32 = i32(0);
    var L2_0 : vec3<f32> = _S17;
    var fms_0 : vec3<f32> = _S17;
    loop
    {
        if((a_0 < i32(8)))
        {
        }
        else
        {
            break;
        }
        var b_0 : i32 = i32(0);
        loop
        {
            if((b_0 < i32(8)))
            {
            }
            else
            {
                break;
            }
            var cosT_0 : f32 = 1.0f - 2.0f * ((f32(a_0) + 0.5f) / 8.0f);
            var sinT_0 : f32 = sqrt(saturate(1.0f - cosT_0 * cosT_0));
            var phi_0 : f32 = 6.2831854820251465f * ((f32(b_0) + 0.5f) / 8.0f);
            var dir_0 : vec3<f32> = vec3<f32>(sinT_0 * cos(phi_0), cosT_0, sinT_0 * sin(phi_0));
            var rr_0 : f32 = length(_S15);
            var mu_6 : f32 = dot(_S15, dir_0) / rr_0;
            var tGround_0 : f32;
            if(hitsGround_0(rr_0, mu_6))
            {
                tGround_0 = distanceToSphere_0(rr_0, mu_6, sky_0.radii_0.x);
            }
            else
            {
                tGround_0 = -1.0f;
            }
            var _S18 : bool = (tGround_0 > 0.0f);
            var tMax_0 : f32;
            if(_S18)
            {
                tMax_0 = tGround_0;
            }
            else
            {
                tMax_0 = distanceToSphere_0(rr_0, mu_6, sky_0.radii_0.y);
            }
            var _S19 : f32 = max(tMax_0, 0.0f) / 20.0f;
            const _S20 : vec3<f32> = vec3<f32>(1.0f, 1.0f, 1.0f);
            var k_1 : i32 = i32(0);
            var T_0 : vec3<f32> = _S20;
            var L_0 : vec3<f32> = _S17;
            var F_0 : vec3<f32> = _S17;
            loop
            {
                if((k_1 < i32(20)))
                {
                }
                else
                {
                    break;
                }
                var p_0 : vec3<f32> = _S15 + dir_0 * vec3<f32>(((f32(k_1) + 0.5f) * _S19));
                var pr2_0 : f32 = length(p_0);
                var h_3 : f32 = pr2_0 - sky_0.radii_0.x;
                var sunMu_0 : f32 = dot(p_0 / vec3<f32>(pr2_0), _S16);
                if(hitsGround_0(pr2_0, sunMu_0))
                {
                    sunT_0 = _S17;
                }
                else
                {
                    sunT_0 = sampleTransmittance_0(pr2_0, sunMu_0);
                }
                var sM_0 : f32 = scatteringMie_0(h_3);
                var scat_0 : vec3<f32> = scatteringRayleigh_0(h_3) + vec3<f32>(sM_0, sM_0, sM_0);
                var _S21 : vec3<f32> = max(extinctionAt_0(h_3), vec3<f32>(1.00000001168609742e-07f, 1.00000001168609742e-07f, 1.00000001168609742e-07f));
                var segT_0 : vec3<f32> = exp((vec3<f32>(0) - _S21) * vec3<f32>(_S19));
                var S_0 : vec3<f32> = scat_0 * sunT_0 / vec3<f32>(12.566370964050293f);
                var L_1 : vec3<f32> = L_0 + T_0 * (S_0 - S_0 * segT_0) / _S21;
                var F_1 : vec3<f32> = F_0 + T_0 * (scat_0 - scat_0 * segT_0) / _S21;
                var T_1 : vec3<f32> = T_0 * segT_0;
                k_1 = k_1 + i32(1);
                T_0 = T_1;
                L_0 = L_1;
                F_0 = F_1;
            }
            if(_S18)
            {
                var gp_0 : vec3<f32> = _S15 + dir_0 * vec3<f32>(tGround_0);
                var gr_0 : f32 = length(gp_0);
                var gmu_0 : f32 = dot(gp_0 / vec3<f32>(gr_0), _S16);
                sunT_0 = L_0 + T_0 * sampleTransmittance_0(gr_0, max(gmu_0, 0.0f)) * vec3<f32>(saturate(gmu_0)) * sky_0.ground_0.xyz / vec3<f32>(3.1415927410125732f);
            }
            else
            {
                sunT_0 = L_0;
            }
            var L2_1 : vec3<f32> = L2_0 + sunT_0;
            var fms_1 : vec3<f32> = fms_0 + F_0;
            b_0 = b_0 + i32(1);
            L2_0 = L2_1;
            fms_0 = fms_1;
        }
        a_0 = a_0 + i32(1);
    }
    var _S22 : vec3<f32> = vec3<f32>(64.0f);
    var _S23 : pixelOutput_1 = pixelOutput_1( vec4<f32>(L2_0 / _S22 / max(vec3<f32>(1.0f, 1.0f, 1.0f) - fms_0 / _S22, vec3<f32>(0.0010000000474974513f, 0.0010000000474974513f, 0.0010000000474974513f)), 1.0f) );
    return _S23;
}

fn rayleighPhase_0( c_0 : f32) -> f32
{
    return 0.059683103114366531f * (1.0f + c_0 * c_0);
}

fn miePhase_0( g_0 : f32,  c_1 : f32) -> f32
{
    var g2_0 : f32 = g_0 * g_0;
    return 3.0f * (1.0f - g2_0) * (1.0f + c_1 * c_1) / (25.132741928100586f * (2.0f + g2_0) * pow(max(1.0f + g2_0 - 2.0f * g_0 * c_1, 9.9999997473787516e-05f), 1.5f));
}

fn multiScatterUv_0( r_6 : f32,  cosSun_1 : f32) -> vec2<f32>
{
    return vec2<f32>(saturate(cosSun_1 * 0.5f + 0.5f), saturate((r_6 - sky_0.radii_0.x) / (sky_0.radii_0.y - sky_0.radii_0.x)));
}

struct ScatterResult_0
{
     inscatter_0 : vec3<f32>,
     transmittance_0 : vec3<f32>,
};

fn integrateScattering_0( pos_3 : vec3<f32>,  dir_1 : vec3<f32>,  maxKm_0 : f32,  steps_0 : i32,  multi_0 : bool,  stopAtGround_0 : bool) -> ScatterResult_0
{
    var res_0 : ScatterResult_0;
    const _S24 : vec3<f32> = vec3<f32>(0.0f, 0.0f, 0.0f);
    res_0.inscatter_0 = _S24;
    res_0.transmittance_0 = vec3<f32>(1.0f, 1.0f, 1.0f);
    var r_7 : f32 = length(pos_3);
    var mu_7 : f32 = dot(pos_3, dir_1) / r_7;
    var tAtmo_0 : f32 = distanceToSphere_0(r_7, mu_7, sky_0.radii_0.y);
    var _S25 : bool;
    if(stopAtGround_0)
    {
        _S25 = hitsGround_0(r_7, mu_7);
    }
    else
    {
        _S25 = false;
    }
    var tGround_1 : f32;
    if(_S25)
    {
        tGround_1 = distanceToSphere_0(r_7, mu_7, sky_0.radii_0.x);
    }
    else
    {
        tGround_1 = -1.0f;
    }
    var tMax_1 : f32;
    if((tGround_1 > 0.0f))
    {
        tMax_1 = tGround_1;
    }
    else
    {
        tMax_1 = tAtmo_0;
    }
    var _S26 : f32 = min(tMax_1, maxKm_0);
    if(!(_S26 > 0.0f))
    {
        return res_0;
    }
    var c_2 : f32 = dot(dir_1, sky_0.sunDir_0.xyz);
    var _S27 : f32 = rayleighPhase_0(c_2);
    var _S28 : f32 = miePhase_0(sky_0.mie_0.w, c_2);
    var _S29 : f32 = _S26 / f32(steps_0);
    var i_0 : i32 = i32(0);
    loop
    {
        if((i_0 < steps_0))
        {
        }
        else
        {
            break;
        }
        var p_1 : vec3<f32> = pos_3 + dir_1 * vec3<f32>(((f32(i_0) + 0.5f) * _S29));
        var _S30 : f32 = max(length(p_1), sky_0.radii_0.x + 0.0010000000474974513f);
        var h_4 : f32 = _S30 - sky_0.radii_0.x;
        var sunMu_1 : f32 = dot(normalize(p_1), sky_0.sunDir_0.xyz);
        var sunT_1 : vec3<f32>;
        if(hitsGround_0(_S30, sunMu_1))
        {
            sunT_1 = _S24;
        }
        else
        {
            sunT_1 = sampleTransmittance_0(_S30, sunMu_1);
        }
        var sR_0 : vec3<f32> = scatteringRayleigh_0(h_4);
        var sM_1 : f32 = scatteringMie_0(h_4);
        var _S31 : f32 = sM_1 * _S28;
        var S_1 : vec3<f32> = sunT_1 * (sR_0 * vec3<f32>(_S27) + vec3<f32>(_S31, _S31, _S31));
        var S_2 : vec3<f32>;
        if(multi_0)
        {
            S_2 = S_1 + (sR_0 + vec3<f32>(sM_1, sM_1, sM_1)) * (textureSampleLevel((multiScatterTex_0), (linearClamp_0), (multiScatterUv_0(_S30, sunMu_1)), (0.0f))).xyz;
        }
        else
        {
            S_2 = S_1;
        }
        var _S32 : vec3<f32> = max(extinctionAt_0(h_4), vec3<f32>(1.00000001168609742e-07f, 1.00000001168609742e-07f, 1.00000001168609742e-07f));
        var segT_1 : vec3<f32> = exp((vec3<f32>(0) - _S32) * vec3<f32>(_S29));
        res_0.inscatter_0 = res_0.inscatter_0 + res_0.transmittance_0 * (S_2 - S_2 * segT_1) / _S32;
        res_0.transmittance_0 = res_0.transmittance_0 * segT_1;
        i_0 = i_0 + i32(1);
    }
    return res_0;
}

struct pixelOutput_2
{
    @location(0) output_2 : vec4<f32>,
};

struct pixelInput_2
{
    @location(0) ndc_3 : vec2<f32>,
};

@fragment
fn fsSkyView( _S33 : pixelInput_2, @builtin(position) pos_4 : vec4<f32>) -> pixelOutput_2
{
    var uv_2 : vec2<f32> = (floor(pos_4.xy) + vec2<f32>(0.5f)) / vec2<f32>(192.0f, 108.0f);
    var r_8 : f32 = length(sky_0.cameraKm_0.xyz);
    var beta_0 : f32 = acos(clamp(sqrt(max(r_8 * r_8 - sky_0.radii_0.x * sky_0.radii_0.x, 0.0f)) / r_8, -1.0f, 1.0f));
    var zenithHorizon_0 : f32 = 3.1415927410125732f - beta_0;
    var _S34 : f32 = uv_2.y;
    var viewZenith_0 : f32;
    if((_S34 < 0.5f))
    {
        var c_3 : f32 = 1.0f - _S34 * 2.0f;
        viewZenith_0 = zenithHorizon_0 * (1.0f - c_3 * c_3);
    }
    else
    {
        var c_4 : f32 = _S34 * 2.0f - 1.0f;
        viewZenith_0 = zenithHorizon_0 + beta_0 * c_4 * c_4;
    }
    var _S35 : f32 = uv_2.x;
    var az_0 : f32 = _S35 * _S35 * 3.1415927410125732f;
    var sh_0 : vec2<f32> = sky_0.sunDir_0.xz;
    var sh_1 : vec2<f32>;
    if(((dot(sh_0, sh_0)) > 9.99999993922529029e-09f))
    {
        sh_1 = normalize(sh_0);
    }
    else
    {
        sh_1 = vec2<f32>(1.0f, 0.0f);
    }
    var _S36 : f32 = sh_1.x;
    var _S37 : f32 = cos(az_0);
    var _S38 : f32 = sh_1.y;
    var _S39 : f32 = sin(az_0);
    var sz_0 : f32 = sin(viewZenith_0);
    var _S40 : pixelOutput_2 = pixelOutput_2( vec4<f32>(integrateScattering_0(vec3<f32>(0.0f, r_8, 0.0f), vec3<f32>((_S36 * _S37 - _S38 * _S39) * sz_0, cos(viewZenith_0), (_S36 * _S39 + _S38 * _S37) * sz_0), 1.0e+09f, i32(30), true, true).inscatter_0, 1.0f) );
    return _S40;
}

fn cameraUp_0() -> vec3<f32>
{
    return normalize(sky_0.cameraKm_0.xyz);
}

fn skyViewUv_0( r_9 : f32,  dir_2 : vec3<f32>) -> vec2<f32>
{
    var beta_1 : f32 = acos(clamp(sqrt(max(r_9 * r_9 - sky_0.radii_0.x * sky_0.radii_0.x, 0.0f)) / r_9, -1.0f, 1.0f));
    var zenithHorizon_1 : f32 = 3.1415927410125732f - beta_1;
    var viewZenith_1 : f32 = acos(clamp(dot(dir_2, cameraUp_0()), -1.0f, 1.0f));
    var v_0 : f32;
    if((viewZenith_1 < zenithHorizon_1))
    {
        v_0 = (1.0f - sqrt(1.0f - viewZenith_1 / zenithHorizon_1)) * 0.5f;
    }
    else
    {
        v_0 = sqrt(saturate((viewZenith_1 - zenithHorizon_1) / beta_1)) * 0.5f + 0.5f;
    }
    var dh_0 : vec2<f32> = dir_2.xz;
    var sh_2 : vec2<f32> = sky_0.sunDir_0.xz;
    var _S41 : bool;
    if(((dot(dh_0, dh_0)) > 9.99999993922529029e-09f))
    {
        _S41 = ((dot(sh_2, sh_2)) > 9.99999993922529029e-09f);
    }
    else
    {
        _S41 = false;
    }
    var az_1 : f32;
    if(_S41)
    {
        az_1 = acos(clamp(dot(normalize(dh_0), normalize(sh_2)), -1.0f, 1.0f));
    }
    else
    {
        az_1 = 0.0f;
    }
    return vec2<f32>(sqrt(az_1 / 3.1415927410125732f), v_0);
}

fn cloudHash_0( p_2 : vec2<i32>) -> f32
{
    var h_5 : u32 = u32(p_2.x) * u32(374761393) + u32(p_2.y) * u32(668265263);
    var h_6 : u32 = ((h_5 ^ (((h_5 >> (u32(13))))))) * u32(1274126177);
    return f32((h_6 ^ (((h_6 >> (u32(16))))))) * 2.32830643653869629e-10f;
}

fn cloudNoise_0( p_3 : vec2<f32>) -> f32
{
    var f_0 : vec2<f32> = fract(p_3);
    var _S42 : vec2<i32> = vec2<i32>(floor(p_3));
    var u_0 : vec2<f32> = f_0 * f_0 * (vec2<f32>(3.0f) - vec2<f32>(2.0f) * f_0);
    var _S43 : f32 = u_0.x;
    return mix(mix(cloudHash_0(_S42), cloudHash_0(_S42 + vec2<i32>(i32(1), i32(0))), _S43), mix(cloudHash_0(_S42 + vec2<i32>(i32(0), i32(1))), cloudHash_0(_S42 + vec2<i32>(i32(1), i32(1))), _S43), u_0.y);
}

fn cloudFbm_0( p_4 : vec2<f32>) -> f32
{
    var _S44 : vec2<f32> = p_4;
    var k_2 : i32 = i32(0);
    var a_1 : f32 = 0.5f;
    var v_1 : f32 = 0.0f;
    loop
    {
        if((k_2 < i32(4)))
        {
        }
        else
        {
            break;
        }
        var v_2 : f32 = v_1 + a_1 * cloudNoise_0(_S44);
        var _S45 : vec2<f32> = _S44 * vec2<f32>(2.0299999713897705f) + vec2<f32>(17.0f);
        var a_2 : f32 = a_1 * 0.5f;
        var k_3 : i32 = k_2 + i32(1);
        _S44 = _S45;
        k_2 = k_3;
        a_1 = a_2;
        v_1 = v_2;
    }
    return v_1 / 0.9375f;
}

fn applyCloudLayer_0( c_5 : vec3<f32>,  dir_3 : vec3<f32>,  r_10 : f32,  bands_0 : f32) -> vec3<f32>
{
    var cover_0 : f32 = sky_0.cloud_0.x;
    var _S46 : f32 = dir_3.y;
    var fade_0 : f32 = smoothstep(0.019999999552965164f, 0.2199999988079071f, _S46);
    var _S47 : bool;
    if((cover_0 <= 0.0f))
    {
        _S47 = true;
    }
    else
    {
        _S47 = (fade_0 <= 0.0f);
    }
    if(_S47)
    {
        return c_5;
    }
    var uv_3 : vec2<f32> = dir_3.xz / vec2<f32>((_S46 + 0.15000000596046448f)) * vec2<f32>(1.6000000238418579f) + vec2<f32>(sky_0.cloud_0.y * 0.012000000104308128f, sky_0.cloud_0.y * 0.004999999888241291f);
    var n_0 : f32 = cloudFbm_0(uv_3);
    var thr_0 : f32 = 1.0f - cover_0;
    var d_1 : f32 = smoothstep(thr_0 - 0.019999999552965164f, thr_0 + 0.2800000011920929f, n_0);
    var lit_0 : f32 = saturate(0.55000001192092896f + (n_0 - cloudFbm_0(uv_3 + normalize(sky_0.sunDir_0.xz + vec2<f32>(9.9999997473787516e-05f, 0.0f)) * vec2<f32>(0.070000000298023224f))) * 4.0f);
    var d_2 : f32;
    var lit_1 : f32;
    if((bands_0 > 1.5f))
    {
        var _S48 : f32 = round(lit_0 * bands_0) / bands_0;
        d_2 = round(d_1 * bands_0) / bands_0;
        lit_1 = _S48;
    }
    else
    {
        d_2 = d_1;
        lit_1 = lit_0;
    }
    var sunMu_2 : f32 = dot(cameraUp_0(), sky_0.sunDir_0.xyz);
    var sunT_2 : vec3<f32>;
    if(hitsGround_0(r_10, sunMu_2))
    {
        sunT_2 = vec3<f32>(0.0f, 0.0f, 0.0f);
    }
    else
    {
        sunT_2 = sampleTransmittance_0(r_10, sunMu_2);
    }
    var rim_0 : f32 = pow(saturate(dot(dir_3, sky_0.sunDir_0.xyz)), 8.0f) * (1.0f - d_2);
    return mix(c_5, (sunT_2 * vec3<f32>(sky_0.sunDir_0.w) * vec3<f32>((0.11999999731779099f + 0.62000000476837158f * lit_1 + 0.5f * rim_0)) + (textureSampleLevel((skyViewTex_0), (linearClamp_0), (skyViewUv_0(r_10, vec3<f32>(0.0f, 1.0f, 0.0f))), (0.0f))).xyz * vec3<f32>(sky_0.sunDir_0.w) * vec3<f32>(0.69999998807907104f)) * vec3<f32>((1.0f - 0.40000000596046448f * smoothstep(thr_0 + 0.10000000149011612f, thr_0 + 0.44999998807907104f, n_0) * (1.0f - rim_0))), vec3<f32>((d_2 * fade_0 * 0.95999997854232788f)));
}

fn toonBand_0( c_6 : vec3<f32>,  bands_1 : f32,  refL_0 : f32) -> vec3<f32>
{
    var l_0 : f32 = dot(c_6, vec3<f32>(0.2125999927520752f, 0.71520000696182251f, 0.072200000286102295f));
    var _S49 : bool;
    if((bands_1 < 1.5f))
    {
        _S49 = true;
    }
    else
    {
        _S49 = (l_0 <= 9.99999997475242708e-07f);
    }
    if(_S49)
    {
        return c_6;
    }
    var _S50 : f32 = min(round(l_0 / (l_0 + refL_0) * bands_1) / bands_1, 0.99900001287460327f);
    return c_6 * vec3<f32>((refL_0 * _S50 / (1.0f - _S50) / l_0));
}

fn neutralShoulder_0( c_7 : vec3<f32>) -> vec3<f32>
{
    var _S51 : f32 = max(c_7.x, max(c_7.y, c_7.z));
    if((_S51 <= 0.89999997615814209f))
    {
        return c_7;
    }
    return c_7 * vec3<f32>(((1.0f - 0.010000004433095455f / (_S51 + 0.10000002384185791f - 0.89999997615814209f)) / _S51));
}

struct SkyOut_0
{
    @location(0) color_0 : vec4<f32>,
    @location(1) normal_0 : vec4<f32>,
    @location(2) depth_0 : vec4<f32>,
};

struct pixelInput_3
{
    @location(0) ndc_4 : vec2<f32>,
};

@fragment
fn fsSkyDraw( _S52 : pixelInput_3, @builtin(position) pos_5 : vec4<f32>) -> SkyOut_0
{
    var dir_4 : vec3<f32> = normalize(sky_0.camForward_0.xyz + vec3<f32>(_S52.ndc_4.x) * sky_0.camRight_0.xyz + vec3<f32>(_S52.ndc_4.y) * sky_0.camUp_0.xyz);
    var r_11 : f32 = length(sky_0.cameraKm_0.xyz);
    var c_8 : vec3<f32> = (textureSampleLevel((skyViewTex_0), (linearClamp_0), (skyViewUv_0(r_11, dir_4)), (0.0f))).xyz * vec3<f32>(sky_0.sunDir_0.w);
    var bands_2 : f32 = sky_0.radii_0.w;
    var c_9 : vec3<f32>;
    if(((sky_0.radii_0.z) > 0.0f))
    {
        var cs_0 : f32 = dot(dir_4, sky_0.sunDir_0.xyz);
        var edge_0 : f32 = cos(sky_0.radii_0.z);
        var _S53 : f32 = sky_0.radii_0.z;
        var _S54 : bool = (bands_2 > 1.5f);
        var disk_0 : f32;
        if(_S54)
        {
            disk_0 = 1.0f;
        }
        else
        {
            disk_0 = 0.69999998807907104f;
        }
        var inner_0 : f32 = cos(_S53 * disk_0);
        if(_S54)
        {
            disk_0 = step(edge_0, cs_0);
        }
        else
        {
            disk_0 = smoothstep(edge_0, inner_0, cs_0);
        }
        var mu_8 : f32 = dot(dir_4, cameraUp_0());
        if(hitsGround_0(r_11, mu_8))
        {
            c_9 = vec3<f32>(0.0f, 0.0f, 0.0f);
        }
        else
        {
            c_9 = sampleTransmittance_0(r_11, mu_8);
        }
        c_9 = c_8 + c_9 * vec3<f32>(sky_0.sunDir_0.w) * vec3<f32>(4.0f) * vec3<f32>(disk_0);
    }
    else
    {
        c_9 = c_8;
    }
    var c_10 : vec3<f32> = toonBand_0(applyCloudLayer_0(c_9, dir_4, r_11, bands_2), bands_2, sky_0.sunDir_0.w * 0.039999999105930328f);
    if(((sky_0.camForward_0.w) > 0.0f))
    {
        c_9 = neutralShoulder_0(c_10 * vec3<f32>(sky_0.camForward_0.w));
    }
    else
    {
        c_9 = c_10;
    }
    var o_1 : SkyOut_0;
    o_1.color_0 = vec4<f32>(c_9, 1.0f);
    o_1.normal_0 = vec4<f32>(0.5f, 0.5f, 0.5f, 0.0f);
    o_1.depth_0 = vec4<f32>(1.0f, 1.0f, 1.0f, 1.0f);
    return o_1;
}

struct pixelOutput_3
{
    @location(0) output_3 : vec4<f32>,
};

struct pixelInput_4
{
    @location(0) ndc_5 : vec2<f32>,
};

@fragment
fn fsAerial( _S55 : pixelInput_4, @builtin(position) pos_6 : vec4<f32>) -> pixelOutput_3
{
    var e_0 : vec4<f32> = (textureLoad((lineDepthMs_0), (vec2<i32>(pos_6.xy)), (i32(0))));
    var v_3 : u32 = (((((u32(e_0.x * 255.0f + 0.5f) << (u32(16)))) | (((u32(e_0.y * 255.0f + 0.5f) << (u32(8))))))) | (u32(e_0.z * 255.0f + 0.5f)));
    if((v_3 >= u32(16777215)))
    {
        var _S56 : pixelOutput_3 = pixelOutput_3( vec4<f32>(0.0f, 0.0f, 0.0f, 1.0f) );
        return _S56;
    }
    var km_0 : f32 = f32(v_3) / 1.6777215e+07f * sky_0.camUp_0.w * sky_0.cameraKm_0.w * sky_0.camRight_0.w;
    var f_1 : f32 = saturate(sqrt(saturate(km_0 / 32.0f)) * 64.0f);
    var r_12 : ScatterResult_0 = integrateScattering_0(sky_0.cameraKm_0.xyz, normalize(sky_0.camForward_0.xyz + vec3<f32>(_S55.ndc_5.x) * sky_0.camRight_0.xyz + vec3<f32>(_S55.ndc_5.y) * sky_0.camUp_0.xyz), km_0, i32(10), true, false);
    var apT_0 : f32 = mix(1.0f, dot(r_12.transmittance_0, vec3<f32>(0.3333333432674408f, 0.3333333432674408f, 0.3333333432674408f)), f_1);
    var apS_0 : vec3<f32> = r_12.inscatter_0 * vec3<f32>(sky_0.sunDir_0.w) * vec3<f32>(f_1);
    var bands_3 : f32 = sky_0.radii_0.w;
    var apT_1 : f32;
    var apS_1 : vec3<f32>;
    if((bands_3 > 1.5f))
    {
        var q_0 : f32 = ceil(apT_0 * bands_3) / bands_3;
        var apS_2 : vec3<f32> = apS_0 * vec3<f32>(((1.0f - q_0) / max(1.0f - apT_0, 9.9999997473787516e-05f)));
        apT_1 = q_0;
        apS_1 = apS_2;
    }
    else
    {
        apT_1 = apT_0;
        apS_1 = apS_0;
    }
    var _S57 : f32;
    if(((sky_0.camForward_0.w) > 0.0f))
    {
        _S57 = sky_0.camForward_0.w;
    }
    else
    {
        _S57 = 1.0f;
    }
    var _S58 : pixelOutput_3 = pixelOutput_3( vec4<f32>(apS_1 * vec3<f32>(_S57), apT_1) );
    return _S58;
}

