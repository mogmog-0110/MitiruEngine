#pragma once

// Dawn のフォワード描画で使う GPU 定数と描画記録の型。forward.slang の FrameCB / DrawCB と同じ並びにする

#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <cstdint>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/ToonStyleGpu.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render::dawn_detail
{

constexpr std::uint32_t kSamples = 4;
constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth32Float;
constexpr std::uint64_t kDrawStride = 512;   // DrawCB (400 byte) を uniform の dynamic offset の境 (256) に揃えた大きさ
constexpr std::uint64_t kMeshIdleFrames = 240;
// 中間の色が 1 で頭打ちの形式のとき、場面の色をこの倍率で縮めて持つ (2 まで入る)。10 bit の刻みは粗くなる
constexpr float kClampedHdrScale = 0.5f;

// forward.slang の FrameCB / DrawCB と同じ並び。float4 と列優先の float4x4 だけで構成し、パディングは不要
struct FrameCB
{
	float viewProj[16];
	float cameraPos[4];
	float lightDir[4];
	float lightColor[4];
	float ambientSky[4];
	float ambientGround[4];
	float shadowTint[4];
	float toonMidTint[4];
	float toonParams[4];
	float rimParams[4];
	float fogColor[4];
	float fogParams[4];
	float skyZenith[4];
	float skyNadir[4];
	float camRight[4];
	float camUp[4];
	float camForward[4];
	float shadowVP[3][16];   // 太陽のカスケード 0, 1, 2 の 光の view * proj
	float shadowC[3][4];     // x = 影マップ 1 texel の世界の幅 y = 光の深さの 1 世界単位あたりの変化
	float shadowSplit[4];    // x = 1 → 2 の距離 y = 2 → 3 の距離 z = PCF の広がり (texel) w = 余白
	float shadowTexel[4];    // x = 1 / カスケード 0 の一辺 y = 1 / アトラスの 1 列の一辺 z = カスケードの数 (0 は影なし)
	float viewInfo[4];       // x = 線形の深度を詰める範囲
	float giOrigin[4];       // xyz = 焼いた格子の原点 w = 焼いた光を使うか
	float giSpacing[4];      // xyz = プローブの間隔 w = 強さ
	float giDims[4];         // xyz = 各軸のプローブの数 w = 距離の地図の 1 行のタイルの数
	float giParams[4];       // x = 面から浮かせる距離 y = トゥーンの段数 zw = 距離の地図の 1 画素 (uv)
	float fogA[4];           // x = 基準の密度 y = 高さの減り方 z = 基準の高さ w = 一様に足す密度
	float fogB[4];           // rgb = 散乱の割合 w = HG の g
	float fogC[4];           // rgb = 主光源の色 * 強さ * 倍率 w = 影を見るか
	float fogD[4];           // rgb = 環境光の平均 * 倍率 w = 奥の端 (世界単位)
	float ambientFlat[4];    // rgb = 半球を使わない環境光 (線形)。半透明の Phong が使う
	float decalInfo[4];      // x = デカールの数
	float lightInfo[4];      // x = 局所光の数 y = 中間の色に掛ける倍率 (頭打ちの形式で明るさを 1 より上へ持つため。1 は掛けない)
	float fadeCamera[4];     // 透かし (OccluderFadeGpu と同じ並び): xyz = 目 w = 的の数 (主ビューだけ。0 は透かさない)
	float fadeParams[4];     // x = 中心で抜く割合 y = 縁の幅 z = 閾値のずらし
	float fadeTargets[4][4]; // xyz = 的 w = 半径
	float fadeCones[4][4];   // xyz = 目から的への単位ベクトル w = 円錐の半角の tan
	float fadeSilColor[4];   // 影絵の色 (線形) と濃さ (a。0 は描かない)
	float fadeSilParams[4];  // x = 影絵のために的を目の側へ寄せる距離
	float lightTiles[4];     // 局所光の格子: x = 格子の一辺 (画素、0 は格子を使わず全部の光を当てる) y = 格子の欄の行数 z = ループの回数 (最大の光の数)
};

struct DrawCB
{
	float world[16];
	float diffuse[4];
	float tintAdd[4];
	float toonMaterial[4];
	ToonRampGpu ramp;       // params, thresholds, tints[4]
	ToonAccentGpu accent;   // rim, gloss, rimLimit
	ToonMetalGpu metal;     // a, b, sky, horizon, ground
	float misc[4];          // x = 1 なら輪郭線の検出から外す
	float phong[4];         // rgb = 鏡面の色 (線形) w = 光沢度。半透明の Phong が使う
	float mapFlags[4];      // x = 基本色の画像 y = 法線の画像 z = 自発光の画像 w = 抜きのしきい値
	float mapParams[4];     // x = 法線の強さ y = 1 なら最近傍で読む
	float emissive[4];      // rgb = 自発光の係数
	float userParams[4][4]; // 自作の材質の float (forward.slang の DrawCB と同じ並び)。材質を使わない描画では 0
	float userInfo[4];      // x = 秒
};

// まとめ描き (連続して同じ見た目の不透明な描画) の 1 個ぶん。forward.slang の vsMainInst が頂点属性 (instance 単位) で読む
struct InstanceData
{
	float world[16];     // DrawCB::world と同じ列優先
	float diffuse[4];    // DrawCB::diffuse と同じ値 (線形、a は不透明度)
};
static_assert(sizeof(InstanceData) == 80);
static_assert(offsetof(DrawCB, world) == 0 && offsetof(DrawCB, diffuse) == sizeof(float) * 16 && offsetof(DrawCB, tintAdd) == sizeof(float) * 20,
              "まとめ描きは DrawCB の world と diffuse を除いた残り (tintAdd から後ろ) が同じかで同じ見た目かを決める");

static_assert(sizeof(Vertex3D) == 48, "頂点バッファの stride と属性の offset はこの大きさを前提にしている");
static_assert(sizeof(DrawCB) <= kDrawStride);

struct ToneCB
{
	float tone[4];   // 露出、ガンマ、彩度、コントラスト
	float size[4];   // xy = 出力の大きさ (画素) z = bloom の強さ
};

// 影の深度パスの 1 描画ぶん (光の view * proj * world)。uniform の dynamic offset の境 (256) に 1 つずつ置く
constexpr std::uint64_t kShadowDrawStride = 256;
struct ShadowDrawCB
{
	float mvp[16];
};

constexpr int kToonLineSlots = 8;
struct LineCB
{
	float screen[4];                   // xy = 場面の大きさ zw = 出力の大きさ
	float depthParams[4];              // x = 距離を詰めた範囲 z = 線にする深さの段差 w = 折れ目とみなす面の向きの差 (cos)
	float extra[4];                    // x = 線の画素を探す半径
	float slotColor[kToonLineSlots][4];
	float slotWidth[kToonLineSlots][4];
	float slotFade[kToonLineSlots][4];
};

struct BloomCB
{
	float params[4];   // xy = 読む側の 1 texel (uv) z = しきい値 w = 1 なら明部だけ残す
};

// 場面の MRT。色のほかに、輪郭線の下地を 2 枚の RGBA8 に持つ (法線と印、線形の距離)
constexpr wgpu::TextureFormat kLineGBufferFormat = wgpu::TextureFormat::RGBA8Unorm;

// 半透明の WBOIT の蓄積先 (DX12 の WeightedBlendedOIT と同じ形式)。accum は重みつきの色と重み、reveal は (1 - alpha) の積
constexpr wgpu::TextureFormat kOitAccumFormat = wgpu::TextureFormat::RGBA16Float;
constexpr wgpu::TextureFormat kOitRevealFormat = wgpu::TextureFormat::R16Float;


// 行優先の sgc::Mat4f を、シェーダーが読む列優先の 16 float に変換する
inline void toColumnMajor(const sgc::Mat4f& m, float out[16]) noexcept
{
	for (int c = 0; c < 4; ++c)
	{
		for (int r = 0; r < 4; ++r) { out[c * 4 + r] = m.m[r][c]; }
	}
}

inline void setRgb(float out[4], const sgc::Colorf& c, float w = 0.0f) noexcept
{
	out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = w;
}

struct GpuMesh
{
	wgpu::Buffer vertexBuffer;
	wgpu::Buffer indexBuffer;
	std::uint64_t vertexOffset = 0;   ///< 頂点バッファの中の先頭。スキンの出力は 1 本の大きなバッファを分け合う
	std::uint64_t vertexBytes = 0;
	std::uint64_t indexBytes = 0;
	std::uint64_t revision = 0;
	std::uint32_t indexCount = 0;
	std::uint32_t vertexCount = 0;
	std::uint64_t lastUsed = 0;
};

struct DawnMaterialMaps;

struct DrawRecord
{
	GpuMesh* mesh = nullptr;
	const DawnMaterialMaps* maps = nullptr;   // 画像を持つ材質の束。nullptr は既定の束
	std::uint32_t offset = 0;
	bool blend = false;
	bool cullNone = false;
	bool fadeTarget = false;   ///< 透かしの的として描いた不透明の物 (主ビューだけ)。影絵のパスが描き直す
	float viewDepth = 0.0f;
	std::uint32_t instanceFirst = 0;   ///< まとめた描画 (instanceCount >= 2) のとき、InstanceData の列の先頭。offset の定数は共通の欄だけを使う
	std::uint32_t instanceCount = 1;
	std::uint32_t templateId = 0;      ///< 定数の共通の欄 (world と diffuse 以外) が同じだった間の番号。同じ番号なら中身が同じ
	std::uint16_t userMaterial = 0;    ///< 自作の材質の番号 (0 は場面の陰影)。材質つきの描画はまとめず、その材質のパイプラインで描く
};

// 影を落とす 1 描画。world は光の行列と掛けるために持ち、bounds は光の錐台の外を描かないために使う
struct ShadowCaster
{
	GpuMesh* mesh = nullptr;
	sgc::Mat4f world;
	Mesh::AABB bounds;
};

} // namespace mitiru::render::dawn_detail
