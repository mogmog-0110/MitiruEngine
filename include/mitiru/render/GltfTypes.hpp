#pragma once

/// @file GltfTypes.hpp
/// @brief glTF中間データ型
/// @details cgltf内部型とエンジン型の間の橋渡しデータ構造。

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/CpuTexture.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief 頂点ごとのスキン束縛 (JOINTS_0 / WEIGHTS_0)。最大 4 ボーン影響 (glTF 標準)。
/// @details joints はスキンの joints 配列内インデックス (= palette / inverseBind の添字)。
///          weights は対応する重み。CPU スキニング (`Skinning.hpp::skinVertices`) の入力 (#23a)。
struct SkinVertexBinding
{
	std::uint32_t joints[4] = {0, 0, 0, 0};
	float         weights[4] = {0, 0, 0, 0};
};

/// @brief glTF モーフターゲット (blend shape) の頂点デルタ (#24)。
/// @details base 頂点に Σ(weight × delta) を足すと表情等が出る。適用は呼び出し側 (研究側) が行う。
///          positionDelta は base.vertices と同数。normalDelta は無ければ空。
struct GltfMorphTarget
{
	std::vector<sgc::Vec3f> positionDelta;   ///< POSITION デルタ (base 頂点数と同数)
	std::vector<sgc::Vec3f> normalDelta;     ///< NORMAL デルタ (任意・空可)
};

/// @brief glTF プリミティブデータ
struct GltfMeshPrimitive
{
	std::vector<Vertex3D> vertices;    ///< 頂点データ
	std::vector<uint32_t> indices;     ///< インデックスデータ
	int materialIndex = -1;            ///< マテリアルインデックス (-1=デフォルト)
	std::vector<SkinVertexBinding> skin;  ///< 頂点ごとのスキン束縛 (空=非スキン)。vertices と同数 (#23a)
	std::vector<GltfMorphTarget> morphTargets;  ///< モーフターゲット (空=無し)。順序は targetNames と対応 (#24)
};

/// @brief glTF メッシュデータ
struct GltfMeshData
{
	std::string name;                              ///< メッシュ名
	std::vector<GltfMeshPrimitive> primitives;     ///< プリミティブ一覧
	std::vector<std::string> morphTargetNames;     ///< モーフ名 (VRM は日本語モーフ名)。primitive.morphTargets と同順 (#24)
};

/// @brief glTF の不透明度の扱い (alphaMode)
enum class GltfAlphaMode
{
	Opaque,   ///< アルファを無視して不透明で描く
	Mask,     ///< alphaCutoff 未満の画素を捨てる (葉・柵・格子)
	Blend,    ///< 半透明として合成する
};

/// @brief glTF マテリアルデータ (PBR metallic-roughness)
struct GltfMaterialData
{
	std::string name;                              ///< マテリアル名
	sgc::Colorf baseColor{1, 1, 1, 1};            ///< ベースカラー
	float metallic = 0.0f;                         ///< メタリック [0,1]
	float roughness = 1.0f;                        ///< ラフネス [0,1]
	std::string baseColorTexturePath;              ///< ベースカラーテクスチャパス（外部 URI 等）
	std::string normalTexturePath;                 ///< 法線マップパス
	CpuTexture  baseColorTexture;                  ///< デコード済みベースカラー（埋め込み glb のみ。#17）
	GltfAlphaMode alphaMode = GltfAlphaMode::Opaque;  ///< 不透明度の扱い
	float alphaCutoff = 0.5f;                      ///< Mask のしきい値
	bool doubleSided = false;                      ///< 真なら背面カリングを切る
	/// 基本色テクスチャの拡大フィルタが NEAREST か。ドット絵資産はここが真になる。
	bool nearestFilter = false;
};

/// @brief glTF ノード (ボーン階層 / TRS)。VRM はノード名がボーン名 (UTF-8) (#23a)。
/// @details parent はノード配列内の親インデックス (-1=ルート)。TRS から局所行列を組める。
///          研究側 (VMD 再生・ボーン名マップ) がワールドポーズ palette を組むのに使う。
struct GltfNode
{
	std::string  name;                       ///< ノード名 (VRM ではボーン名)
	int          parent = -1;                ///< 親ノードindex (-1=ルート)
	int          mesh = -1;                  ///< 参照する mesh index (-1=無し)。mesh→skin 対応に使う (#25)
	int          skin = -1;                  ///< 参照する skin index (-1=無し) (#25)
	sgc::Vec3f   translation{0, 0, 0};       ///< 局所平行移動
	sgc::Vec4f   rotation{0, 0, 0, 1};       ///< 局所回転 (quaternion xyzw)
	sgc::Vec3f   scale{1, 1, 1};             ///< 局所スケール
	std::vector<int> children;               ///< 子ノードindex
};

/// @brief glTF スキン (joints + inverseBindMatrices) (#23a)。
struct GltfSkinData
{
	std::string                   name;                  ///< スキン名
	std::vector<int>              joints;                ///< joint ノードindex (palette の添字順)
	std::vector<sgc::Mat4f>       inverseBindMatrices;   ///< joint ごとの逆バインド行列 (row-major)
	int                           skeletonRoot = -1;     ///< skeleton ルートノード (任意, -1=未指定)
};

/// @brief アニメーションチャンネルが動かすノードプロパティ (ADR 0028)。
enum class GltfAnimPath : std::uint8_t
{
	Translation,
	Rotation,
	Scale,
};

/// @brief キーフレーム補間方式 (ADR 0028)。CUBICSPLINE はロード時に Linear へ縮退する。
enum class GltfAnimInterp : std::uint8_t
{
	Linear,
	Step,
};

/// @brief 1 ノード × 1 プロパティのキーフレーム列 (ADR 0028)。
/// @details times は昇順の秒。values は T/S なら xyz (w=0)、R なら quaternion xyzw。
///          times と values は同数 (ローダーが不整合チャンネルを捨てる)。
struct GltfAnimationChannel
{
	int            nodeIndex = -1;                       ///< 対象ノード index (nodes 配列基準)
	GltfAnimPath   path = GltfAnimPath::Translation;     ///< 動かすプロパティ
	GltfAnimInterp interpolation = GltfAnimInterp::Linear; ///< 補間方式
	std::vector<float>      times;                       ///< キー時刻 (秒、昇順)
	std::vector<sgc::Vec4f> values;                      ///< キー値 (T/S: xyz, R: quat xyzw)
};

/// @brief 名前付きアニメーションクリップ (ADR 0028)。Blender の Action がこれになる。
struct GltfAnimationClip
{
	std::string name;                                    ///< クリップ名 ("Walk" 等)
	float       durationSec = 0.0f;                      ///< 全チャンネル最大の終端時刻
	std::vector<GltfAnimationChannel> channels;          ///< チャンネル一覧
};

/// @brief glTF シーンデータ
struct GltfSceneData
{
	std::vector<GltfMeshData> meshes;              ///< メッシュ一覧
	std::vector<GltfMaterialData> materials;       ///< マテリアル一覧
	std::vector<GltfNode>     nodes;               ///< ノード階層 (#23a, ボーン)
	std::vector<GltfSkinData> skins;               ///< スキン一覧 (#23a)
	std::vector<GltfAnimationClip> animations;     ///< アニメーションクリップ (ADR 0028)
};

} // namespace mitiru::render
