// clod_engine.hlsl。MitiruEngine 組み込み版 clod (cluster-LOD) シェーダ。
// 原本: cluster-lod-renderer/shaders/clod.hlsl。engine 差分:
//   - CB 末尾に engineLightDir / engineLightColor (s.light3D と共有)
//   - ResolveCS は linear のまま出力 (ガンマ無し。後段 ACES tonemap 前提)、
//     背景 pixel には書かない (inject パスが visbuffer==0 を discard する)
// 再生成: python tools/clod_shaders/generate_blobs.py (DXC SM6.6 必須)
//
// GPU 駆動 visibility buffer パイプライン:
//   CullCS (LOD カット+錐台+two-pass HZB 遮蔽 → HW / SW 可視クラスタリスト)
//   → PrepArgsCS (間接引数) → HW: ExecuteIndirect DispatchMesh (MS+PS) /
//     SW: ExecuteIndirect Dispatch (SwRasterCS が固定小数ラスタ)。双方 64bit
//   visbuffer へ InterlockedMax → HzbBuild (visbuffer 深度 → max ピラミッド)
//   → ResolveCS (visbuffer → 法線再構成 → シェーディング → offscreen HDR)
//   CullCS (LOD カット+錐台+two-pass HZB 遮蔽 → HW / SW 可視クラスタリスト)
//   → PrepArgsCS (間接引数) → HW: ExecuteIndirect DispatchMesh (MS+PS) /
//     SW: ExecuteIndirect Dispatch (SwRasterCS が固定小数ラスタ)。双方 64bit
//   visbuffer へ InterlockedMax → HzbBuild (visbuffer 深度 → max ピラミッド)
//   → ResolveCS (visbuffer → 面法線再構成 → シェーディング → RT)
//
//   visbuffer packing (64bit): depth30 (1-z、近いほど大) | src 1 (0=HW 1=SW)
//                              | listIdx 26 | tri 7
//   list entry (32bit): bit31 = pass1 復活 | item (= inst * clusterCount + cluster)

// 画面寸法は実行時指定 (--size)。misc.z / swParams.w に uint で入る
#define SCREEN_W asuint(misc.z)
#define SCREEN_H asuint(swParams.w)
#define LIST_CAP (1u << 20)

cbuffer CB : register(b0)
{
    float4x4 viewProj;        // column-major
    float4 camPosTau;         // xyz = カメラ, w = τ (スクリーン誤差 0..1)
    float4 misc;              // x=projScale y=znear z=asuint(screenW) w=debugMode
    float4 counts;            // x=instanceCount y=itemCount z=dispatchX w=passIndex
    float4 modelCtr;          // xyz = モデル中心 (回転軸) w = モデル半径
    float4 frustum[6];        // world 錐台平面
    float4 viewRow[3];        // 現 view 行 (world→view)
    float4 prevViewRow[3];    // 前フレーム view 行
    float4 projParams;        // P00, P11, proj.m22, proj.m23
    float4 hzbParams;         // hzbW, hzbH, mipCount, occlusionOn
    float4 swParams;          // x = SW ルーティング閾値 (投影直径 px、0 = SW 無効)
                              // y = 最大 LOD 深さ  z = asuint(instanceCount)  w = asuint(screenH)
    float4 engineLightDir;    // xyz = 平行光の向き (シーンへ向かう)
    float4 engineLightColor;  // rgb = 光色、w = ambient 強度
}
// item 空間はメッシュごとに密に連続 (mesh-major)。counts.x = メッシュ数。
// item ∈ [itemBase_m, itemBase_m+1) → inst = instBase + rel / clusterCount,
//                                      cluster = clusterBase + rel % clusterCount

// 汎用 root constants (b1): HzbBuild = x=op y=dstMip z/w=dst 寸法。PrepArgs = x=pass
cbuffer HzbCB : register(b1) { uint4 hzbOp; }

struct Group   { float3 center; float radius; float error; };
struct Cluster
{
    int  ownGroup; int refined;
    float4 cull;
    uint vertOffset; uint vertCount;
    uint triOffset;  uint triCount;
    uint lodDepth;
    uint materialId;
};
struct Mat
{
    float4 baseColor;
    uint texIndex;      // 0xFFFFFFFF = テクスチャ無し。descriptor heap [1+mipCount+i]
    uint flags;         // bit0 = masked (アルファテスト)
    uint normalTex;     // 接空間法線マップ (0xFFFFFFFF = 無し)
    uint pad;
};
struct Inst      // メッシュはロード時に原点中心へ baked 済み
{
    float3 ofs; float rot;
    float scale;
    uint clusterBase;   // 連結 Clusters[] 内の先頭
    uint clusterCount;
    uint pad;
};
struct MeshRec   // メッシュ表 (mesh-major item 空間の分解 + BVH 根)
{
    uint itemBase, instBase, clusterCount, clusterBase;
    uint bvhRoot;
    uint radiusBits;   // asfloat = メッシュ包含球半径 (engine はモデル毎スケール任意)
    uint pad1, pad2;
};
struct BvhNode   // group 上の 8 分木。childCount==0 = leaf (groupId が有効)
{
    float4 sphere;      // object 空間 (中心 xyz、半径 w)
    float maxErr;       // 部分木の最大 group 誤差 (FLT_MAX = 終端含む)
    uint firstChild;
    uint childCount;
    uint groupId;       // 連結 group index (leaf のみ)
};

StructuredBuffer<Group>   Groups       : register(t0);
StructuredBuffer<Cluster> Clusters     : register(t1);
StructuredBuffer<float3>  Positions    : register(t2);
StructuredBuffer<uint>    ClusterVerts : register(t3);
ByteAddressBuffer         ClusterTris  : register(t4);
StructuredBuffer<Inst>    Instances    : register(t5);
StructuredBuffer<MeshRec> MeshTable    : register(t6);
StructuredBuffer<uint2>   GroupRanges  : register(t7);   // group → (先頭 cluster, 数)
StructuredBuffer<BvhNode> BvhNodes     : register(t8);
StructuredBuffer<float3>  Normals      : register(t9);   // 頂点法線 (object 空間、LOD0 由来)
StructuredBuffer<float2>  Uvs          : register(t10);
StructuredBuffer<Mat>     Materials    : register(t11);
SamplerState              Samp         : register(s0);   // trilinear wrap (static)
RWStructuredBuffer<uint>  Stats        : register(u0);   // [0]=可視クラスタ [1]=可視三角形 [2]=occluded
                                                         // [3..6]=pass1 復活理由 (near/clip/背景/深度)
                                                         // [7]=リスト溢れ [8]=HW クラスタ [9]=SW クラスタ
RWStructuredBuffer<uint>  MarkedList   : register(u1);   // pass0 で遮蔽と判定した item (pass1 が再テスト)
RWStructuredBuffer<uint64_t> VisBuf    : register(u2);   // visibility buffer (SCREEN_W×SCREEN_H)
RWStructuredBuffer<uint>  VisListHw    : register(u3);   // HW 経路の可視クラスタリスト (両 pass 追記)
RWStructuredBuffer<uint>  Counters     : register(u4);   // [0]=HW カーソル [1]=HW pass0 終端
                                                         // [2]=SW カーソル [3]=SW pass0 終端 [4]=marked 数
RWStructuredBuffer<uint>  IndArgs      : register(u5);   // [0..1]=HW DispatchMesh [2..3]=SW Dispatch [4]=pass1 cull
RWStructuredBuffer<uint>  VisListSw    : register(u6);   // SW 経路の可視クラスタリスト
RWStructuredBuffer<uint>  Overdraw     : register(u7);   // pixel ごとの書き込み回数 (debug 5)
RWStructuredBuffer<uint>  InstVis      : register(u8);   // インスタンス錐台ビット (事前カリング)
RWStructuredBuffer<uint2> QueueA       : register(u9);   // BVH 走査キュー (inst, node) ping
RWStructuredBuffer<uint2> QueueB       : register(u10);  // 同 pong。数は Counters[5]/[6]
#define QUEUE_CAP (1u << 22)
// ResourceDescriptorHeap: [0]=RT UAV (resolve 出力), [1+mip]=HZB mip UAV (R32_FLOAT)

float3 rotY(float3 p, float a)
{
    float s = sin(a), c = cos(a);
    return float3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}
float3 instXform(float3 p, Inst I)
{
    return rotY(p, I.rot) * I.scale + I.ofs;
}

float projectError(Group g, Inst I)
{
    if (g.error >= 1e30) { return 1e30; }
    float3 c = instXform(g.center, I);
    float d = distance(c, camPosTau.xyz);
    // 誤差・半径は object 空間量なので instance scale が乗る
    return g.error * I.scale / max(d - g.radius * I.scale, misc.y) * misc.x;
}

bool lodCut(Cluster c, Inst I)
{
    float parentErr = projectError(Groups[c.ownGroup], I);
    float childErr  = (c.refined < 0) ? 0.0 : projectError(Groups[c.refined], I);
    return parentErr > camPosTau.w && childErr <= camPosTau.w;
}

// item → (inst, 連結配列上の cluster index)
void decodeIdx(uint item, out uint inst, out uint ci)
{
    uint meshCount = asuint(counts.x);
    uint m = 0;
    [unroll] for (uint k = 1; k < 16; ++k)
        if (k < meshCount && item >= MeshTable[k].itemBase) { m = k; }
    MeshRec mt = MeshTable[m];
    uint rel = item - mt.itemBase;
    inst = mt.instBase + rel / mt.clusterCount;
    ci   = mt.clusterBase + rel % mt.clusterCount;
}
void decodeItem(uint item, out uint inst, out uint ci, out Inst I)
{
    decodeIdx(item, inst, ci);
    I = Instances[inst];
}
// inst → メッシュ index (≤ 16 なので線形走査で十分)
uint meshOf(uint inst)
{
    uint meshCount = asuint(counts.x);
    uint m = 0;
    [unroll] for (uint k = 1; k < 16; ++k)
        if (k < meshCount && inst >= MeshTable[k].instBase) { m = k; }
    return m;
}

bool frustumTest(float3 c, float r)
{
    [unroll] for (int i = 0; i < 6; ++i)
        if (dot(frustum[i].xyz, c) + frustum[i].w < -r) { return false; }
    return true;
}

// world 球 → HZB 遮蔽テスト。0 = 遮蔽。非 0 = 可視 (理由コード):
//   1 = near 跨ぎ  2 = 近点が near より手前  3 = footprint に背景 (far=1)  4 = 深度差で可視
// usePrev: 前フレームの view で判定 (pass0 は前フレーム末に built した HZB を使うため)
uint hzbTest(float3 wc, float r, bool usePrev)
{
    float4 v0 = usePrev ? prevViewRow[0] : viewRow[0];
    float4 v1 = usePrev ? prevViewRow[1] : viewRow[1];
    float4 v2 = usePrev ? prevViewRow[2] : viewRow[2];
    float3 cv = float3(dot(v0.xyz, wc) + v0.w, dot(v1.xyz, wc) + v1.w, dot(v2.xyz, wc) + v2.w);
    float cz = -cv.z;                        // 前方を正に
    float znear = misc.y;
    if (cz < r + znear) { return 1; }        // near 跨ぎ = 常に可視
    float zvNear = cv.z + r;                 // RH: 前方負。近点
    if (zvNear > -znear) { return 2; }

    // 球 → UV AABB (回転円錐の接線、Mara-McGuire 系)
    float p00 = projParams.x, p11 = projParams.y;
    float2 cx = float2(cv.x, cz);
    float2 vx = float2(sqrt(dot(cx, cx) - r * r), r);
    float2 minx = float2(vx.x * cx.x - vx.y * cx.y, vx.y * cx.x + vx.x * cx.y);
    float2 maxx = float2(vx.x * cx.x + vx.y * cx.y, -vx.y * cx.x + vx.x * cx.y);
    float2 cy = float2(cv.y, cz);
    float2 vy = float2(sqrt(dot(cy, cy) - r * r), r);
    float2 miny = float2(vy.x * cy.x - vy.y * cy.y, vy.y * cy.x + vy.x * cy.y);
    float2 maxy = float2(vy.x * cy.x + vy.y * cy.y, -vy.y * cy.x + vy.x * cy.y);
    float4 aabb = float4(minx.x / minx.y * p00, miny.x / miny.y * p11,
                         maxx.x / maxx.y * p00, maxy.x / maxy.y * p11);
    aabb = saturate(aabb.xwzy * float4(0.5, -0.5, 0.5, -0.5) + 0.5);   // clip → uv

    // rect が 4x4 texel に収まる mip (Karis p77)。粗い mip だと texel が広域 max になり
    // シルエット際で背景 (far=1) を拾って可視クラスタまで遮蔽マーク→pass1 で全復活が振動する
    float2 sizePx = (aabb.zw - aabb.xy) * hzbParams.xy;
    uint mip = (uint)clamp(ceil(log2(max(max(sizePx.x, sizePx.y) * 0.25, 1.0))),
                           0.0, hzbParams.z - 1.0);
    uint2 dim = max(uint2((uint)hzbParams.x >> mip, (uint)hzbParams.y >> mip), uint2(1, 1));
    uint2 c0 = min(uint2(aabb.xy * float2(dim)), dim - 1);
    uint2 c1 = min(uint2(aabb.zw * float2(dim)), dim - 1);
    // footprint 全 texel の max (span ≤ 5。4 隅だけだと中間 texel が抜けて過剰カリングし得る)
    RWTexture2D<float> hzb = ResourceDescriptorHeap[1 + mip];
    float farthest = 0.0;
    for (uint ty = c0.y; ty <= c1.y; ++ty)
        for (uint tx = c0.x; tx <= c1.x; ++tx)
            farthest = max(farthest, hzb[uint2(tx, ty)]);

    float ndcNear = (projParams.z * zvNear + projParams.w) / (-zvNear);
    if (ndcNear > farthest + 1e-6) { return 0; }
    return (farthest >= 1.0) ? 3 : 4;
}

// ── 共通: 可視クラスタの HW / SW ルーティングとリスト追記 ───────────────────
// 球の投影直径が閾値未満 (micropoly) かつ near 非跨ぎなら SW ラスタへ
void appendVisible(uint entry, Cluster c, float3 cc, float cr)
{
    float cz = -(dot(viewRow[2].xyz, cc) + viewRow[2].w);
    float diaPx = 2.0 * cr / max(cz - cr, misc.y) * misc.x * SCREEN_H;
    bool useSw = swParams.x > 0.0 && cz > cr + misc.y && diaPx < swParams.x;

    uint slot; bool stored = false;
    if (useSw)
    {
        InterlockedAdd(Counters[2], 1, slot);
        if (slot < LIST_CAP) { VisListSw[slot] = entry; stored = true; InterlockedAdd(Stats[9], 1); }
    }
    else
    {
        InterlockedAdd(Counters[0], 1, slot);
        if (slot < LIST_CAP) { VisListHw[slot] = entry; stored = true; InterlockedAdd(Stats[8], 1); }
    }
    if (stored)
    {
        InterlockedAdd(Stats[0], 1);
        InterlockedAdd(Stats[1], c.triCount);
    }
    else { InterlockedAdd(Stats[7], 1); }   // 溢れは黙殺せず数える
}

// ── 共通: pass0 のクラスタ 1 個分のテスト (LOD + 錐台 + 遮蔽マーク) ─────────
void testClusterPass0(uint item, uint ci, Inst I, bool occOn)
{
    Cluster c = Clusters[ci];
    float3 cc = instXform(c.cull.xyz, I);
    float cr  = c.cull.w * I.scale;
    bool visible = frustumTest(cc, cr) && lodCut(c, I);
    if (visible && occOn && hzbTest(cc, cr, true) == 0)
    {
        uint slot;
        InterlockedAdd(Counters[4], 1, slot);
        if (slot < LIST_CAP)
        {
            MarkedList[slot] = item;
            InterlockedAdd(Stats[2], 1);
            visible = false;
        }
        // リスト溢れ時は保守的にそのまま描く (visible のまま)
    }
    if (visible) { appendVisible(item, c, cc, cr); }
}

// ── Cull (compute): pass0 = 全 item 総当たり (--nobvh)、pass1 = MarkedList 再テスト ──
[numthreads(64, 1, 1)]
void CullCS(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint itemCount    = asuint(counts.y);
    uint dispatchX    = asuint(counts.z);
    uint passIndex    = asuint(counts.w);
    bool occOn        = hzbParams.w > 0.5;
    // pass1 は間接 dispatch (stride 32768 固定)
    uint flat = ((gid.y * (passIndex ? 32768u : dispatchX)) + gid.x) * 64 + gtid;

    if (passIndex == 1)
    {
        if (flat >= min(Counters[4], LIST_CAP)) { return; }
        uint item = MarkedList[flat];
        uint inst, ci;
        decodeIdx(item, inst, ci);
        Inst I = Instances[inst];
        Cluster c = Clusters[ci];
        float3 cc = instXform(c.cull.xyz, I);
        float cr  = c.cull.w * I.scale;
        uint code = hzbTest(cc, cr, false);   // 現 HZB で再テスト
        if (code != 0)
        {
            InterlockedAdd(Stats[2 + code], 1);   // 復活理由の内訳
            appendVisible(item | 0x80000000u, c, cc, cr);
        }
        return;
    }

    if (flat >= itemCount) { return; }
    // インスタンス事前カリング bit で錐台外を安価に棄却 (Instances/Clusters 読み前)
    uint inst, ci;
    decodeIdx(flat, inst, ci);
    if ((InstVis[inst >> 5] & (1u << (inst & 31))) == 0) { return; }
    testClusterPass0(flat, ci, Instances[inst], occOn);
}

// ── BVH 走査 (pass0 の既定経路): Seed → レベル毎 Traverse ──────────────────
[numthreads(64, 1, 1)]
void SeedCS(uint3 dt : SV_DispatchThreadID)
{
    uint instanceCount = asuint(swParams.z);
    uint i = dt.x;
    if (i >= instanceCount) { return; }
    if ((InstVis[i >> 5] & (1u << (i & 31))) == 0) { return; }
    uint slot;
    InterlockedAdd(Counters[5], 1, slot);
    if (slot < QUEUE_CAP) { QueueA[slot] = uint2(i, MeshTable[meshOf(i)].bvhRoot); }
    else { InterlockedAdd(Stats[7], 1); }
}

// hzbOp.x = 入力キュー (0=A 1=B)。leaf = 1 group → クラスタ総当たり
[numthreads(64, 1, 1)]
void TraverseCS(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    bool inA = hzbOp.x == 0;
    uint count = min(Counters[inA ? 5 : 6], QUEUE_CAP);
    uint flat = ((gid.y * 32768u) + gid.x) * 64 + gtid;
    if (flat >= count) { return; }
    uint2 e = inA ? QueueA[flat] : QueueB[flat];
    uint inst = e.x;
    Inst I = Instances[inst];
    BvhNode n = BvhNodes[e.y];
    float3 nc = instXform(n.sphere.xyz, I);
    float nr  = n.sphere.w * I.scale;
    if (!frustumTest(nc, nr)) { return; }
    // LOD 剪定: 部分木の最粗 parent 誤差が既に τ 以下なら全クラスタがカット外
    // (node 球は group 球を包含 → この射影は部分木内の全 projErr の上界)
    if (n.maxErr < 1e30)
    {
        float d = distance(nc, camPosTau.xyz);
        if (n.maxErr * I.scale / max(d - nr, misc.y) * misc.x <= camPosTau.w) { return; }
    }
    if (n.childCount == 0)
    {
        bool occOn = hzbParams.w > 0.5;
        uint2 gr = GroupRanges[n.groupId];
        MeshRec mr = MeshTable[meshOf(inst)];
        uint itemBase = mr.itemBase + (inst - mr.instBase) * mr.clusterCount - mr.clusterBase;
        for (uint k = 0; k < gr.y; ++k)
        {
            uint ci = gr.x + k;
            testClusterPass0(itemBase + ci, ci, I, occOn);
        }
    }
    else
    {
        uint dst = inA ? 6 : 5;
        uint slot;
        InterlockedAdd(Counters[dst], n.childCount, slot);
        for (uint k = 0; k < n.childCount; ++k)
        {
            if (slot + k < QUEUE_CAP)
            {
                uint2 child = uint2(inst, n.firstChild + k);
                if (inA) { QueueB[slot + k] = child; } else { QueueA[slot + k] = child; }
            }
            else { InterlockedAdd(Stats[7], 1); }   // 溢れ = 黙殺しない
        }
    }
}

// hzbOp.x = 入力キューの Counters index (5/6)、hzbOp.y = 出力側 (0 に戻す)
[numthreads(1, 1, 1)]
void PrepQueueCS()
{
    uint g = (min(Counters[hzbOp.x], QUEUE_CAP) + 63u) / 64u;
    IndArgs[15] = min(g, 32768u);
    IndArgs[16] = (g + 32767u) / 32768u;
    IndArgs[17] = 1;
    Counters[hzbOp.y] = 0;
}

// ── PrepArgs (compute, 1 thread): 可視数 → 間接引数 (HW/SW × pass) ─────────
// pass0 実行後に呼び、hzbOp.x = これから描く pass。pass0 なら終端を snapshot し
// pass1 cull (MarkedList 再テスト) の dispatch 引数も書く
[numthreads(1, 1, 1)]
void PrepArgsCS()
{
    uint pass = hzbOp.x;
    // HW (DispatchMesh)
    uint end   = min(Counters[0], LIST_CAP);
    uint start = pass ? Counters[1] : 0;
    if (pass == 0) { Counters[1] = end; }
    uint n = end - start;
    IndArgs[pass * 3 + 0] = min(n, 32768u);
    IndArgs[pass * 3 + 1] = (n + 32767u) / 32768u;
    IndArgs[pass * 3 + 2] = 1;
    // SW (Dispatch)
    end   = min(Counters[2], LIST_CAP);
    start = pass ? Counters[3] : 0;
    if (pass == 0) { Counters[3] = end; }
    n = end - start;
    IndArgs[6 + pass * 3 + 0] = min(n, 32768u);
    IndArgs[6 + pass * 3 + 1] = (n + 32767u) / 32768u;
    IndArgs[6 + pass * 3 + 2] = 1;
    if (pass == 0)
    {
        uint g = (min(Counters[4], LIST_CAP) + 63u) / 64u;   // 64 thread groups
        IndArgs[12] = min(g, 32768u);
        IndArgs[13] = (g + 32767u) / 32768u;
        IndArgs[14] = 1;
    }
}

// ── VisClear (compute): visbuffer / overdraw / inst bits / stats / counters を 0 に ──
// hzbOp.y = instance bits の word 数 (MarkedList はカーソルのみで実体クリア不要)。
// engine 版は Stats/Counters もここで潰す (zero バッファのコピー配管を持たない)
[numthreads(256, 1, 1)]
void VisClear(uint3 dt : SV_DispatchThreadID)
{
    if (dt.x < SCREEN_W * SCREEN_H) { VisBuf[dt.x] = 0; Overdraw[dt.x] = 0; }
    if (dt.x < hzbOp.y) { InstVis[dt.x] = 0; }
    if (dt.x < 12) { Stats[dt.x] = 0; }
    if (dt.x < 8)  { Counters[dt.x] = 0; }
}

// ── InstCull (compute): インスタンス球の錐台テスト → InstVis bit ────────────
// メッシュは原点中心へ bake 済み + 大きさ正規化済みなので球 = (ofs, modelCtr.w)
[numthreads(64, 1, 1)]
void InstCullCS(uint3 dt : SV_DispatchThreadID)
{
    uint instanceCount = asuint(swParams.z);
    if (dt.x >= instanceCount) { return; }
    Inst I = Instances[dt.x];
    // 半径 = メッシュ包含球 × インスタンス scale (メッシュはロード時に原点中心へ bake 済)
    float r = asfloat(MeshTable[meshOf(dt.x)].radiusBits) * I.scale;
    if (frustumTest(I.ofs, r))
        InterlockedOr(InstVis[dt.x >> 5], 1u << (dt.x & 31));
}

// ── Mesh: 可視リストの 1 エントリ = 1 クラスタ ─────────────────────────────
struct VOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;   // masked マテリアルのアルファテスト用 (HW 補間)
};
struct POut
{
    uint listIdx : TEXCOORD1;
    uint tri     : TEXCOORD2;
    uint matId   : TEXCOORD3;
};

uint3 loadTri(uint byteOffset, uint tri)
{
    uint o = byteOffset + tri * 3;
    uint w0 = ClusterTris.Load(o & ~3u);
    uint w1 = ClusterTris.Load((o & ~3u) + 4);
    uint sh = (o & 3u) * 8;
    uint packed = (sh == 0) ? w0 : (w0 >> sh) | (w1 << (32 - sh));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID,
            out vertices VOut verts[128],
            out primitives POut prims[128],
            out indices uint3 tris[128])
{
    uint passIndex = asuint(counts.w);
    uint base = passIndex ? Counters[1] : 0;
    uint end  = min(Counters[0], LIST_CAP);
    uint idx  = base + gid.y * 32768 + gid.x;
    // 端数グループは (0,0) 出力。SetMeshOutputCounts は単一呼び出しのみ許可される
    // ため早期 return を使わず、無効時は出力数 0 で空振りさせる
    uint safeIdx = min(idx, end - 1);
    uint item = VisListHw[safeIdx] & 0x7FFFFFFFu;
    uint inst, ci; Inst I;
    decodeItem(item, inst, ci, I);
    Cluster c = Clusters[ci];
    uint vc = idx < end ? c.vertCount : 0;
    uint tc = idx < end ? c.triCount : 0;
    SetMeshOutputCounts(vc, tc);

    if (gtid < vc)
    {
        uint vi = ClusterVerts[c.vertOffset + gtid];
        float3 p = instXform(Positions[vi], I);
        verts[gtid].pos = mul(viewProj, float4(p, 1.0));
        verts[gtid].uv  = Uvs[vi];
    }
    if (gtid < tc)
    {
        tris[gtid] = loadTri(c.triOffset, gtid);
        prims[gtid].listIdx = safeIdx;
        prims[gtid].tri = gtid;
        prims[gtid].matId = c.materialId;
    }
}

// ── Pixel: visbuffer へ atomic 書き (RT / depth なし)。src=0 (HW) ──────────
void PSMain(VOut v, POut p)
{
    Mat m = Materials[p.matId];
    if ((m.flags & 1) && m.texIndex != 0xFFFFFFFFu)   // masked: アルファテスト
    {
        Texture2D<float4> tex = ResourceDescriptorHeap[1 + (uint)hzbParams.z + m.texIndex];
        if (tex.Sample(Samp, v.uv).a < 0.5) { return; }
    }
    uint2 pix = uint2(v.pos.xy);
    // 近いほど大きい値が勝つよう 1-z を 30bit 量子化 (切り捨て = 遠回り = 保守的)
    uint depth30 = (uint)(saturate(1.0 - v.pos.z) * 1073741823.0);
    uint64_t val = (uint64_t(depth30) << 34)
                 | (uint64_t(p.listIdx & 0x3FFFFFFu) << 7)
                 | uint64_t(p.tri & 0x7Fu);
    InterlockedMax(VisBuf[pix.y * SCREEN_W + pix.x], val);
    if (asuint(misc.w) == 5) { InterlockedAdd(Overdraw[pix.y * SCREEN_W + pix.x], 1); }
}

// ── SW ラスタ (compute): 1 group = SW リスト 1 クラスタ、128 thread ─────────
// 頂点変換 → 8bit subpixel 固定小数 → thread/tri で edge functions + bbox 走査。
// D3D ラスタ規則準拠: 頂点 1/256 スナップ、pixel 中心 +0.5、top-left 規則。
// 向き: NDC CCW front は y 反転後の screen 座標で area2 > 0。
groupshared int2   sFx[128];   // 固定小数頂点 (subpixel 8bit)
groupshared float  sZ[128];    // ndc z
groupshared float  sInvW[128]; // 1/w (masked の遠近補正 UV 用)
groupshared float2 sUv[128];

[numthreads(128, 1, 1)]
void SwRasterCS(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint passIndex = asuint(counts.w);
    uint base = passIndex ? Counters[3] : 0;
    uint end  = min(Counters[2], LIST_CAP);
    uint idx  = base + gid.y * 32768 + gid.x;
    if (idx >= end) { return; }   // group 一様分岐 (barrier より手前で安全)

    uint item = VisListSw[idx] & 0x7FFFFFFFu;
    uint inst, ci; Inst I;
    decodeItem(item, inst, ci, I);
    Cluster c = Clusters[ci];

    if (gtid < c.vertCount)
    {
        uint vi = ClusterVerts[c.vertOffset + gtid];
        float3 p = instXform(Positions[vi], I);
        float4 h = mul(viewProj, float4(p, 1.0));       // ルーティングが near 跨ぎを除外済 (w > 0)
        float2 s = float2((h.x / h.w * 0.5 + 0.5) * SCREEN_W,
                          (0.5 - h.y / h.w * 0.5) * SCREEN_H);
        sFx[gtid] = int2(round(s * 256.0));
        sZ[gtid]  = h.z / h.w;
        sInvW[gtid] = 1.0 / h.w;
        sUv[gtid] = Uvs[vi];
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid >= c.triCount) { return; }

    Mat mat = Materials[c.materialId];
    bool masked = (mat.flags & 1) && mat.texIndex != 0xFFFFFFFFu;

    uint3 t = loadTri(c.triOffset, gtid);
    int2 p0 = sFx[t.x], p1 = sFx[t.y], p2 = sFx[t.z];
    int64_t area2 = (int64_t)(p1.x - p0.x) * (p2.y - p0.y)
                  - (int64_t)(p1.y - p0.y) * (p2.x - p0.x);
    // front = render target 空間 (y 下向き) で CCW = area2 < 0 (HW FrontCCW+CULL_BACK と同値)。
    // 裏面/退化を捨て、p1↔p2 交換で正の向きに正規化 (E ≥ 0 = 内側になる)
    if (area2 >= 0) { return; }
    { int2 tmp = p1; p1 = p2; p2 = tmp; }
    { uint tmp = t.y; t.y = t.z; t.z = tmp; }
    area2 = -area2;

    // bbox → pixel 範囲 (中心 = px*256+128)
    int minX = min(p0.x, min(p1.x, p2.x)), maxX = max(p0.x, max(p1.x, p2.x));
    int minY = min(p0.y, min(p1.y, p2.y)), maxY = max(p0.y, max(p1.y, p2.y));
    // SCREEN_W/H は uint (CB 読み)。負になり得る左辺と混ぜる前に int へ落とす
    int px0 = max((minX - 128 + 255) >> 8, 0), px1 = min((maxX - 128) >> 8, (int)SCREEN_W - 1);
    int py0 = max((minY - 128 + 255) >> 8, 0), py1 = min((maxY - 128) >> 8, (int)SCREEN_H - 1);

    // edge functions (E ≥ 0 = 内側)。top-left 規則: top ⟺ dy==0 && dx>0、left ⟺ dy<0
    int2 e01 = p1 - p0, e12 = p2 - p1, e20 = p0 - p2;
    int64_t b01 = (e01.y == 0 && e01.x > 0) || e01.y < 0 ? 0 : -1;
    int64_t b12 = (e12.y == 0 && e12.x > 0) || e12.y < 0 ? 0 : -1;
    int64_t b20 = (e20.y == 0 && e20.x > 0) || e20.y < 0 ? 0 : -1;

    float z0 = sZ[t.x], z1 = sZ[t.y], z2 = sZ[t.z];
    float invArea = 1.0 / (float)area2;

    // masked: tri 単位の mip 推定 (texel 数 / pixel 数)。SW は micropoly 前提なので十分
    float triLod = 0.0;
    float iw0 = sInvW[t.x], iw1 = sInvW[t.y], iw2 = sInvW[t.z];
    float2 uv0 = sUv[t.x], uv1 = sUv[t.y], uv2 = sUv[t.z];
    if (masked)
    {
        Texture2D<float4> tex = ResourceDescriptorHeap[1 + (uint)hzbParams.z + mat.texIndex];
        float tw, th;
        tex.GetDimensions(tw, th);
        float2 du = (uv1 - uv0) * float2(tw, th), dv = (uv2 - uv0) * float2(tw, th);
        float texels = abs(du.x * dv.y - du.y * dv.x);
        float pixels = (float)area2 / (2.0 * 65536.0);
        triLod = 0.5 * log2(max(texels / max(pixels, 1e-3), 1.0));
    }

    // 増分評価: bbox 左上 pixel 中心で 1 回だけ乗算し、行/列は定数加算で進める
    int cx0 = px0 * 256 + 128, cy0 = py0 * 256 + 128;
    int64_t r2 = (int64_t)e01.x * (cy0 - p0.y) - (int64_t)e01.y * (cx0 - p0.x) + b01;
    int64_t r0 = (int64_t)e12.x * (cy0 - p1.y) - (int64_t)e12.y * (cx0 - p1.x) + b12;
    int64_t r1 = (int64_t)e20.x * (cy0 - p2.y) - (int64_t)e20.y * (cx0 - p2.x) + b20;
    int64_t sx2 = -(int64_t)e01.y * 256, sy2 = (int64_t)e01.x * 256;
    int64_t sx0 = -(int64_t)e12.y * 256, sy0 = (int64_t)e12.x * 256;
    int64_t sx1 = -(int64_t)e20.y * 256, sy1 = (int64_t)e20.x * 256;
    for (int py = py0; py <= py1; ++py)
    {
        int64_t w0 = r0, w1 = r1, w2 = r2;
        for (int px = px0; px <= px1; ++px)
        {
            if ((w0 | w1 | w2) >= 0)   // 3 つとも非負 (bias 込み) = 被覆
            {
                if (masked)
                {
                    // 遠近補正バリセントリックで UV → アルファテスト
                    float pw0 = (float)(w0 - b12) * iw0, pw1 = (float)(w1 - b20) * iw1,
                          pw2 = (float)(w2 - b01) * iw2;
                    float psum = pw0 + pw1 + pw2;
                    float2 uvp = (uv0 * pw0 + uv1 * pw1 + uv2 * pw2) / max(psum, 1e-20);
                    Texture2D<float4> tex = ResourceDescriptorHeap[1 + (uint)hzbParams.z + mat.texIndex];
                    if (tex.SampleLevel(Samp, uvp, triLod).a < 0.5)
                    { w0 += sx0; w1 += sx1; w2 += sx2; continue; }
                }
                float z = ((float)(w0 - b12) * z0 + (float)(w1 - b20) * z1
                         + (float)(w2 - b01) * z2) * invArea;
                uint depth30 = (uint)(saturate(1.0 - z) * 1073741823.0);
                uint64_t val = (uint64_t(depth30) << 34) | (1ull << 33)
                             | (uint64_t(idx & 0x3FFFFFFu) << 7) | uint64_t(gtid);
                InterlockedMax(VisBuf[py * SCREEN_W + px], val);
                if (asuint(misc.w) == 5) { InterlockedAdd(Overdraw[py * SCREEN_W + px], 1); }
            }
            w0 += sx0; w1 += sx1; w2 += sx2;
        }
        r0 += sy0; r1 += sy1; r2 += sy2;
    }
}

// ── Resolve (compute): visbuffer → 面法線再構成 → シェーディング → RT ──────
float3 clusterColor(uint id)
{
    uint h = id * 2654435761u;
    return float3((h & 255), ((h >> 8) & 255), ((h >> 16) & 255)) / 255.0 * 0.75 + 0.25;
}

[numthreads(8, 8, 1)]
void ResolveCS(uint3 dt : SV_DispatchThreadID)
{
    if (dt.x >= SCREEN_W || dt.y >= SCREEN_H) { return; }
    RWTexture2D<float4> outTex = ResourceDescriptorHeap[0];
    uint64_t v = VisBuf[dt.y * SCREEN_W + dt.x];
    if (v == 0) { return; }   // 背景は書かない (inject が visbuffer==0 を discard する)

    uint tri     = uint(v) & 0x7Fu;
    uint listIdx = uint(v >> 7) & 0x3FFFFFFu;
    uint src     = uint(v >> 33) & 1u;
    uint entry   = src ? VisListSw[listIdx] : VisListHw[listIdx];
    uint item    = entry & 0x7FFFFFFFu;
    uint fromPost = entry >> 31;
    uint inst, ci; Inst I;
    decodeItem(item, inst, ci, I);
    Cluster c = Clusters[ci];

    uint3 t = loadTri(c.triOffset, tri);
    uint v0 = ClusterVerts[c.vertOffset + t.x];
    uint v1 = ClusterVerts[c.vertOffset + t.y];
    uint v2 = ClusterVerts[c.vertOffset + t.z];
    float3 p0 = instXform(Positions[v0], I);
    float3 p1 = instXform(Positions[v1], I);
    float3 p2 = instXform(Positions[v2], I);

    // pixel 中心のバリセントリック (screen 空間 + 遠近補正) → 頂点法線を補間。
    // 退化時は面法線へフォールバック
    float4 h0 = mul(viewProj, float4(p0, 1.0));
    float4 h1 = mul(viewProj, float4(p1, 1.0));
    float4 h2 = mul(viewProj, float4(p2, 1.0));
    float2 s0 = float2((h0.x / h0.w * 0.5 + 0.5) * SCREEN_W, (0.5 - h0.y / h0.w * 0.5) * SCREEN_H);
    float2 s1 = float2((h1.x / h1.w * 0.5 + 0.5) * SCREEN_W, (0.5 - h1.y / h1.w * 0.5) * SCREEN_H);
    float2 s2 = float2((h2.x / h2.w * 0.5 + 0.5) * SCREEN_W, (0.5 - h2.y / h2.w * 0.5) * SCREEN_H);
    float2 pc = float2(dt.xy) + 0.5;
    float e0 = (s2.x - s1.x) * (pc.y - s1.y) - (s2.y - s1.y) * (pc.x - s1.x);
    float e1 = (s0.x - s2.x) * (pc.y - s2.y) - (s0.y - s2.y) * (pc.x - s2.x);
    float e2 = (s1.x - s0.x) * (pc.y - s0.y) - (s1.y - s0.y) * (pc.x - s0.x);
    float w0 = e0 / h0.w, w1 = e1 / h1.w, w2 = e2 / h2.w;   // 遠近補正
    float wsum = w0 + w1 + w2;
    float3 n;
    if (abs(wsum) > 1e-12)
    {
        float3 nO = (Normals[v0] * w0 + Normals[v1] * w1 + Normals[v2] * w2) / wsum;
        n = normalize(rotY(nO, I.rot));   // 一様 scale は向きを変えない
    }
    else { n = normalize(cross(p1 - p0, p2 - p0)); }

    // マテリアル: baseColor × (あれば) albedo テクスチャ。mip は隣接 pixel の
    // バリセントリック再評価による UV 勾配から (edge 関数は screen 座標に対し affine)
    Mat mat = Materials[c.materialId];
    float3 albedo = mat.baseColor.rgb;
    if ((mat.texIndex != 0xFFFFFFFFu || mat.normalTex != 0xFFFFFFFFu) && abs(wsum) > 1e-12)
    {
        float2 uv0 = Uvs[v0], uv1 = Uvs[v1], uv2 = Uvs[v2];
        float2 uv = (uv0 * w0 + uv1 * w1 + uv2 * w2) / wsum;
        float w0x = (e0 - (s2.y - s1.y)) / h0.w, w1x = (e1 - (s0.y - s2.y)) / h1.w,
              w2x = (e2 - (s1.y - s0.y)) / h2.w;
        float w0y = (e0 + (s2.x - s1.x)) / h0.w, w1y = (e1 + (s0.x - s2.x)) / h1.w,
              w2y = (e2 + (s1.x - s0.x)) / h2.w;
        float sx = w0x + w1x + w2x, sy = w0y + w1y + w2y;
        float2 uvx = abs(sx) > 1e-12 ? (uv0 * w0x + uv1 * w1x + uv2 * w2x) / sx : uv;
        float2 uvy = abs(sy) > 1e-12 ? (uv0 * w0y + uv1 * w1y + uv2 * w2y) / sy : uv;
        float2 guv = uvx - uv, gvv = uvy - uv;

        if (mat.texIndex != 0xFFFFFFFFu)
        {
            Texture2D<float4> tex = ResourceDescriptorHeap[1 + (uint)hzbParams.z + mat.texIndex];
            float tw, th;
            tex.GetDimensions(tw, th);
            float2 gx = guv * float2(tw, th), gy = gvv * float2(tw, th);
            float lod = 0.5 * log2(max(max(dot(gx, gx), dot(gy, gy)), 1.0));
            albedo *= tex.SampleLevel(Samp, uv, lod).rgb;
        }
        if (mat.normalTex != 0xFFFFFFFFu)
        {
            // 接空間を三角形の (位置, UV) 勾配から解析導出 (事前タンジェント不要)
            float2 d1 = uv1 - uv0, d2 = uv2 - uv0;
            float det = d1.x * d2.y - d1.y * d2.x;
            if (abs(det) > 1e-12)
            {
                float r = 1.0 / det;
                float3 e1w = p1 - p0, e2w = p2 - p0;
                float3 T = normalize((e1w * d2.y - e2w * d1.y) * r - n * dot(n, (e1w * d2.y - e2w * d1.y) * r));
                float3 B0 = (e2w * d1.x - e1w * d2.x) * r;
                float3 B = cross(n, T) * (dot(cross(n, T), B0) < 0.0 ? -1.0 : 1.0);
                Texture2D<float4> ntex = ResourceDescriptorHeap[1 + (uint)hzbParams.z + mat.normalTex];
                float tw, th;
                ntex.GetDimensions(tw, th);
                float2 gx = guv * float2(tw, th), gy = gvv * float2(tw, th);
                float lod = 0.5 * log2(max(max(dot(gx, gx), dot(gy, gy)), 1.0));
                float3 tn = ntex.SampleLevel(Samp, uv, lod).rgb * 2.0 - 1.0;
                n = normalize(T * tn.x + B * tn.y + n * max(tn.z, 0.2));
            }
        }
    }

    uint debugMode = asuint(misc.w);
    float3 base = albedo;
    if (debugMode == 1) { base = clusterColor(ci); }
    if (debugMode == 2) { base = clusterColor(ci) * 0.6 + 0.3; }
    if (debugMode == 3) { base = fromPost ? float3(1.0, 0.1, 0.1) : float3(0.4, 0.4, 0.45); }
    if (debugMode == 4) { base = src ? float3(0.3, 0.85, 0.35) : float3(0.35, 0.45, 0.9); }   // SW=緑 HW=青
    if (debugMode == 5)
    {
        // overdraw ヒート: 1=青 → 4=緑 → 8=黄 → 16+=赤 (log2 スケール)
        float h = saturate(log2((float)max(Overdraw[dt.y * SCREEN_W + dt.x], 1u)) / 4.0);
        outTex[dt.xy] = float4(saturate(h * 2.0), saturate(2.0 - abs(h * 4.0 - 2.0) * 0.5) * 0.8,
                               saturate(1.0 - h * 2.0), 1.0);
        return;
    }
    if (debugMode == 6)
    {
        // LOD ヒート: 深い (粗い) ほど赤、浅い (細かい) ほど青
        float h = swParams.y > 0.0 ? saturate((float)c.lodDepth / swParams.y) : 0.0;
        base = lerp(float3(0.2, 0.35, 0.9), float3(0.95, 0.25, 0.15), h);
    }
    // engine の平行光 (s.light3D) で lambert + 半球 ambient。linear のまま出す
    // (後段の forward と同じ MSAA HDR に合成され、ACES tonemap が均す)
    float3 l = -normalize(engineLightDir.xyz);
    float  ndl = saturate(dot(n, l));
    float  hemi = 0.5 + 0.5 * n.y;
    float  amb = engineLightColor.w;
    float3 col = base * engineLightColor.rgb * (amb + (1.0 - amb) * ndl)
               + base * 0.06 * hemi;
    outTex[dt.xy] = float4(saturate(col), 1.0);
}

// ── HZB build (compute): max 縮小 (標準 Z、far=1 が「遠い」) ────────────────
float loadVisZ(uint2 p)
{
    uint64_t v = VisBuf[p.y * SCREEN_W + p.x];
    return v == 0 ? 1.0 : 1.0 - float(uint(v >> 34)) / 1073741823.0;
}

[numthreads(8, 8, 1)]
void HzbBuild(uint3 t : SV_DispatchThreadID)
{
    if (t.x >= hzbOp.z || t.y >= hzbOp.w) { return; }
    RWTexture2D<float> dst = ResourceDescriptorHeap[1 + hzbOp.y];
    float d;
    if (hzbOp.x == 0)
    {
        // visbuffer 深度 → mip0。HZB は画面より大きい (2048x1024 ≥ 1280x720) ので
        // 2x2 load で footprint を保守的に被覆できる
        uint2 s0 = uint2(float2(t.xy) * float2(SCREEN_W, SCREEN_H) / float2(hzbOp.zw));
        uint2 s1 = min(s0 + 1, uint2(SCREEN_W - 1, SCREEN_H - 1));
        d = max(max(loadVisZ(s0), loadVisZ(uint2(s1.x, s0.y))),
                max(loadVisZ(uint2(s0.x, s1.y)), loadVisZ(s1)));
    }
    else
    {
        RWTexture2D<float> src = ResourceDescriptorHeap[hzbOp.y];   // 1+(dstMip-1)
        uint2 s = t.xy * 2;
        d = max(max(src[s], src[s + uint2(1, 0)]),
                max(src[s + uint2(0, 1)], src[s + uint2(1, 1)]));
    }
    dst[t.xy] = d;
}
