// glass — 面の手前で 1 回だけ屈折させた向きの変わりぶんを、画面の右・下の画素のずれに直して背景を読む。法線から離れた縁ほど、背景が中心の側へ寄って見える。
// 画面の右と下の向きは、視線 (i.view) の 1 画素ぶんの変化から取る。視線はカメラの光線そのものなので、球の面の分割に左右されず、ずれがなめらかに変わる
void material(SurfaceIn i, inout SurfaceOut o)
{
    float3 n = normalize(i.normal);
    float3 incident = -i.view;
    float3 bend = refract(incident, n, 1.0 / param_ior) - incident;
    float3 right = -normalize(ddx(i.view));
    float3 down = -normalize(ddy(i.view));
    float2 pixels = float2(dot(bend, right), dot(bend, down)) * param_reach;
    float3 seen = mitiruBackground(i.pixel + pixels);
    float rim = pow(1.0 - saturate(dot(n, i.view)), 3.0);
    o.baseColor = seen * float3(param_tint_r, param_tint_g, param_tint_b) + rim * param_rim;
}
