#pragma once

/// @file ClodFormat.hpp
/// @brief .clod v7 フォーマット定義と GPU 側 POD (clod 仮想ジオメトリパス)
/// @details 変換は drawModel の import (ClodImport) か clod_build (PLY / OBJ+MTL → .clod v5)。
///          v6 は v5 と同じ並びで、マテリアルのテクスチャ名が import の作った圧縮済み
///          `<画像>.dds` を指しうる点だけが違う。v7 は v6 の末尾にマテリアルごとの金属度と粗さ
///          (ClodFileMaterialPbr) を足したもの。v5 と v6 も読むが、import cache としては作り直す。

#include <cstdint>

namespace mitiru::render::clod
{

/// @brief .clod ファイル magic ('CLD7' little endian)。import が書くのはこれ
inline constexpr uint32_t kClodMagic = 0x37444C43u;
/// @brief 旧 magic ('CLD6')。金属度と粗さを持たない import cache
inline constexpr uint32_t kClodMagicV6 = 0x36444C43u;
/// @brief 旧 magic ('CLD5')。clod_build の出力と、圧縮導入前の import cache
inline constexpr uint32_t kClodMagicV5 = 0x35444C43u;

[[nodiscard]] constexpr bool isClodMagic(uint32_t magic) noexcept
{
	return magic == kClodMagic || magic == kClodMagicV6 || magic == kClodMagicV5;
}

/// @brief マテリアルの表の後ろに ClodFileMaterialPbr の表が続く版か
[[nodiscard]] constexpr bool clodHasPbrTable(uint32_t magic) noexcept
{
	return magic == kClodMagic;
}

/// @brief .clod ヘッダ (56B、ファイル先頭)
struct ClodFileHeader
{
	uint32_t magic;
	uint32_t vertexCount;
	uint32_t clusterCount;
	uint32_t groupCount;
	uint32_t vertIdxCount;
	uint32_t triIdxByteCount;
	uint32_t materialCount;
	uint32_t pad;
	float boundsMin[3];
	float boundsMax[3];
};
static_assert(sizeof(ClodFileHeader) == 56);

/// @brief LOD group (簡略化境界。誤差 FLT_MAX = 終端)
struct ClodGroup
{
	float center[3];
	float radius;
	float error;
};
static_assert(sizeof(ClodGroup) == 20);

/// @brief クラスタ (≤128 tri / ≤128 vert)。ownGroup/refined が LOD DAG カットの親子
struct ClodCluster
{
	int32_t ownGroup;
	int32_t refined;
	float cull[4];   ///< カリング球 (center xyz, radius)
	uint32_t vertOffset, vertCount;
	uint32_t triOffset, triCount;   ///< triOffset は byte、triCount は三角形数
	uint32_t lodDepth;
	uint32_t materialId;
};
static_assert(sizeof(ClodCluster) == 48);

/// @brief ファイル内マテリアル (パスは .clod からの相対)
struct ClodFileMaterial
{
	float baseColor[4];
	char albedo[120];
	char normal[120];
};
static_assert(sizeof(ClodFileMaterial) == 256);

/// @brief v7 のマテリアルの表の後ろに、同じ順で並ぶ金属度と粗さ (glTF の係数)。
///        この表が無い v5 / v6 は前方の描画の既定 (金属度 0、粗さ 1) で読む
struct ClodFileMaterialPbr
{
	float metallic = 0.0f;
	float roughness = 1.0f;
	float pad[2] = {};
};
static_assert(sizeof(ClodFileMaterialPbr) == 16);

// ── 以下は GPU 側 POD (shader 構造体と 1:1。clod_engine.hlsl 参照) ──

/// @brief インスタンス (Y 回転 + 一様スケール。メッシュはロード時原点中心)
struct GpuInstance
{
	float ofs[3];
	float rotY;
	float scale;
	uint32_t clusterBase;
	uint32_t clusterCount;
	uint32_t pad;
};
static_assert(sizeof(GpuInstance) == 32);

/// @brief メッシュ表 (mesh-major item 空間の分解 + BVH 根 + 包含球半径)
struct GpuMeshRec
{
	uint32_t itemBase, instBase, clusterCount, clusterBase;
	uint32_t bvhRoot;
	uint32_t radiusBits;   ///< asfloat = メッシュ包含球半径
	uint32_t pad1, pad2;
};
static_assert(sizeof(GpuMeshRec) == 32);

/// @brief BVH ノード (group 上の 8 分木。childCount==0 = leaf)
struct GpuBvhNode
{
	float sphere[4];
	float maxErr;
	uint32_t firstChild;
	uint32_t childCount;
	uint32_t groupId;
};
static_assert(sizeof(GpuBvhNode) == 32);

/// @brief GPU マテリアル (テクスチャは descriptor heap の slot index)
struct GpuMaterial
{
	float baseColor[4];
	uint32_t texIndex;    ///< 0xFFFFFFFF = 無し
	uint32_t flags;       ///< kClodMaterial* の OR
	uint32_t normalTex;   ///< 0xFFFFFFFF = 無し
	uint32_t metalRough;  ///< packClodMetalRough の値
};
static_assert(sizeof(GpuMaterial) == 32);

/// @brief 金属度 (下位 16 bit) と粗さ (上位 16 bit) を 0..1 の 16 bit 固定小数で 1 語にする。clod_engine.hlsl が同じ式で戻す
[[nodiscard]] constexpr uint32_t packClodMetalRough(float metallic, float roughness) noexcept
{
	const auto unorm = [](float v) {
		const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
		return static_cast<uint32_t>(c * 65535.0f + 0.5f);
	};
	return unorm(metallic) | (unorm(roughness) << 16);
}

/// @brief アルファテストで抜く
inline constexpr uint32_t kClodMaterialMasked = 1u;
/// @brief 法線マップが XY だけ (BC5)。Z はシェーダが長さ 1 から復元する
inline constexpr uint32_t kClodMaterialNormalXY = 2u;

/// @brief 描画 CB (shader の cbuffer CB と一致。100 dwords)
struct ClodDrawCB
{
	float viewProj[16];
	float camPosTau[4];
	float misc[4];         ///< projScale, znear, asuint(screenW), debugMode
	float counts[4];       ///< asuint(meshCount), asuint(itemCount), asuint(dispatchX), asuint(passIndex)
	float shading[4];      ///< x = 陰影 (0 トゥーン、1 Phong、2 PBR。Renderer3D_DX12::worldShadeIndex と同じ番号)
	float frustum[6][4];
	float viewRow[3][4];
	float prevViewRow[3][4];
	float projParams[4];   ///< P00, P11, m22, m23
	float hzbParams[4];    ///< hzbW, hzbH, mipCount, occlusionOn
	float swParams[4];     ///< SW 閾値 px, 最大 LOD 深さ, asuint(instanceCount), asuint(screenH)
	float engineLightDir[4];
	float engineLightColor[4];
};
static_assert(sizeof(ClodDrawCB) == 100 * 4);

/// @brief 可視リスト容量 (shader の LIST_CAP と一致)
inline constexpr uint32_t kClodListCap = 1u << 20;
/// @brief BVH 走査キュー容量 (shader の QUEUE_CAP と一致)
inline constexpr uint32_t kClodQueueCap = 1u << 22;
/// @brief 同時メッシュ上限 (shader の meshOf/decodeIdx unroll と一致)
inline constexpr uint32_t kClodMaxMeshes = 16;

} // namespace mitiru::render::clod
