#pragma once

/// @file GltfTypes.hpp
/// @brief glTF 中間データ型
/// @details cgltf 内部型とエンジン型の間をつなぐデータ構造。

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
///          weights は対応する重み。スキニング (`Skinning.hpp::skinVertices` / DX12 の compute) の入力 (#23a)。
struct SkinVertexBinding
{
	std::uint32_t joints[4] = {0, 0, 0, 0};
	float         weights[4] = {0, 0, 0, 0};
};

/// @brief モーフの差分 1 つ。target は prim の何番目のモーフか
struct GltfMorphEntry
{
	std::uint32_t target = 0;
	sgc::Vec3f positionDelta{0, 0, 0};
	sgc::Vec3f normalDelta{0, 0, 0};
};

/// @brief prim のモーフターゲットの差分。頂点ごとの CSR (行 = 頂点、中身 = 動かすモーフだけ)。
/// @details 表情のモデルは 50 個ほどのモーフを持つが、1 つの頂点を動かすのは数個なので、モーフ数 × 頂点数の密な配列にしない。
///          行の中は target の昇順で、GPU も同じ順に足す。差分が全部 0 の (頂点, モーフ) は持たない。
///          スパースなアクセサの差分は読み込みの時に展開済みで、ここには出てこない。
struct GltfMorphSet
{
	std::uint32_t targetCount = 0;
	std::vector<std::uint32_t> rowStart;      ///< 頂点数 + 1 個。頂点 v の差分は entries[rowStart[v] .. rowStart[v+1])
	std::vector<GltfMorphEntry> entries;
	std::vector<sgc::Vec3f> maxAbsPosition;   ///< モーフごとの |位置の差分| の軸ごとの最大 (包含箱を広げる量)

	[[nodiscard]] bool empty() const noexcept { return entries.empty(); }
};

/// @brief glTF プリミティブデータ
struct GltfMeshPrimitive
{
	std::vector<Vertex3D> vertices;    ///< 頂点データ
	std::vector<uint32_t> indices;     ///< インデックスデータ
	int materialIndex = -1;            ///< マテリアルインデックス (-1=デフォルト)
	std::vector<SkinVertexBinding> skin;  ///< 頂点ごとのスキン束縛 (空=非スキン)。vertices と同数 (#23a)
	GltfMorphSet morph;                         ///< モーフターゲットの差分 (差分が無ければ空)。モーフの順序は targetNames と対応 (#24)
};

/// @brief glTF メッシュデータ
struct GltfMeshData
{
	std::string name;                              ///< メッシュ名
	std::vector<GltfMeshPrimitive> primitives;     ///< プリミティブ一覧
	std::vector<std::string> morphTargetNames;     ///< モーフ名 (VRM は日本語モーフ名)。primitive.morph のモーフと同順 (#24)
};

/// @brief 頂点を読まずに取れる mesh のモーフの情報。ゲーム DLL がスロット表を組む元 (animation/MorphSlots.hpp)。
/// @details GltfSceneData::meshMorphs の添字は glTF の mesh の番号そのまま (GltfSceneData::meshes は描けない mesh を詰めるのでずれる)。
struct GltfMeshMorph
{
	std::uint32_t targetCount = 0;
	std::vector<std::string> names;          ///< extras.targetNames。無ければ空 (あっても targetCount より短いことがある)
	std::vector<float> defaultWeights;       ///< mesh.weights。無ければ空 (= 全部 0)
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
	CpuTexture  normalTexture;                     ///< デコード済み法線マップ (接空間、緑は v が減る向き)
	float       normalScale = 1.0f;                ///< 法線マップの xy に掛ける強さ
	std::string metallicRoughnessTexturePath;
	CpuTexture  metallicRoughnessTexture;          ///< デコード済み金属・粗さ (G = 粗さ、B = 金属)
	/// occlusion テクスチャが金属・粗さと同じ画像 (ORM) のとき、その R を遮蔽に使う強さ。別の画像なら 0
	float       occlusionStrength = 0.0f;
	std::string emissiveTexturePath;
	CpuTexture  emissiveTexture;                   ///< デコード済み自発光
	float       emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
	/// KHR_materials_clearcoat / anisotropy / sheen の係数 (画像は読まない)。PBR の陰影だけが使う。0 は層なし
	float       clearcoat = 0.0f;                  ///< クリアコートの強さ [0,1]
	float       clearcoatRoughness = 0.0f;         ///< クリアコートの粗さ [0,1]
	float       anisotropyStrength = 0.0f;         ///< 異方性の強さ [0,1]
	float       anisotropyRotation = 0.0f;         ///< 異方性の向き (ラジアン、接線から)
	float       sheenColor[3] = {0.0f, 0.0f, 0.0f};  ///< シーン (布の縁の光) の色。黒は層なし
	float       sheenRoughness = 0.0f;             ///< シーンの粗さ [0,1]
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
	std::vector<float> morphWeights;         ///< node.weights。あれば mesh.weights の既定を上書きする (空 = 上書きしない)
};

/// @brief glTF スキン (joints + inverseBindMatrices) (#23a)。
struct GltfSkinData
{
	std::string                   name;                  ///< スキン名
	std::vector<int>              joints;                ///< joint ノードindex (palette の添字順)
	std::vector<sgc::Mat4f>       inverseBindMatrices;   ///< joint ごとの逆バインド行列 (row-major)
	int                           skeletonRoot = -1;     ///< skeleton ルートノード (任意, -1=未指定)
};

/// @brief アニメーションチャンネルが動かすノードプロパティ。
enum class GltfAnimPath : std::uint8_t
{
	Translation,
	Rotation,
	Scale,
};

/// @brief キーフレーム補間方式。
enum class GltfAnimInterp : std::uint8_t
{
	Linear,
	Step,
	CubicSpline,
};

/// @brief 1 ノード × 1 プロパティのキーフレーム列。
/// @details times は昇順の秒。values は T/S なら xyz (w=0)、R なら quaternion xyzw。
///          times と values は同数 (ローダーが不整合チャンネルを捨てる)。
///          CubicSpline のときのみ inTangents/outTangents が values と同数で埋まる
///          (glTF 仕様どおり区間長でスケール済み)。それ以外の補間方式では空。
struct GltfAnimationChannel
{
	int            nodeIndex = -1;                       ///< 対象ノード index (nodes 配列基準)
	GltfAnimPath   path = GltfAnimPath::Translation;     ///< 動かすプロパティ
	GltfAnimInterp interpolation = GltfAnimInterp::Linear; ///< 補間方式
	std::vector<float>      times;                       ///< キー時刻 (秒、昇順)
	std::vector<sgc::Vec4f> values;                      ///< キー値 (T/S: xyz, R: quat xyzw)
	std::vector<sgc::Vec4f> inTangents;                  ///< CubicSpline のみ: 各キーの in-tangent
	std::vector<sgc::Vec4f> outTangents;                 ///< CubicSpline のみ: 各キーの out-tangent
};

/// @brief weights のチャンネル。1 キーに targetCount 個の float が並ぶ (glTF の path "weights")。
/// @details values は key 番号の順に targetCount 個ずつ。CubicSpline は in / out の接線も同じ並びで持つ。
///          T/R/S の channels に混ぜない: 姿勢の評価は Vec4 のキーを前提にしていて、クリップの長さも T/R/S だけから決まる。
struct GltfMorphChannel
{
	int            nodeIndex = -1;
	GltfAnimInterp interpolation = GltfAnimInterp::Linear;
	std::uint32_t  targetCount = 0;
	std::vector<float> times;
	std::vector<float> values;
	std::vector<float> inTangents;
	std::vector<float> outTangents;
};

/// @brief 名前付きアニメーションクリップ。Blender の Action がこれになる。
struct GltfAnimationClip
{
	std::string name;                                    ///< クリップ名 ("Walk" 等)
	float       durationSec = 0.0f;                      ///< T/R/S チャンネルの最大の終端時刻 (weights だけのクリップは weights の終端)
	std::vector<GltfAnimationChannel> channels;          ///< チャンネル一覧
	std::vector<GltfMorphChannel> morphChannels;         ///< weights のチャンネル (モーフを動かす)
};

/// @brief glTF シーンデータ
struct GltfSceneData
{
	std::vector<GltfMeshData> meshes;              ///< メッシュ一覧
	std::vector<GltfMaterialData> materials;       ///< マテリアル一覧
	std::vector<GltfNode>     nodes;               ///< ノード階層 (#23a, ボーン)
	std::vector<GltfSkinData> skins;               ///< スキン一覧 (#23a)
	std::vector<GltfAnimationClip> animations;     ///< アニメーションクリップ
	std::vector<GltfMeshMorph> meshMorphs;         ///< glTF の mesh の番号ごとのモーフの情報 (モーフを持たない mesh は targetCount 0)
};

} // namespace mitiru::render
