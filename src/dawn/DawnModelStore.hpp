#pragma once

// glTF / GLB / FBX モデルを CPU に読み込む置き場の宣言。
// 骨格とクリップには、DX12 とゲーム DLL と同じ animation::AnimAsset と姿勢評価関数を使う。
// 材質の画像 (基本色・法線・自発光) は DawnMaterialPool が GPU へ上げる。読み込みはその場で終える。

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <span>
#include <memory>
#include <string>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Skinning.hpp>
#include <mitiru/render/SkinnedLod.hpp>
#include <mitiru/render/SkinnedLodLook.hpp>

#include "DawnMaterialMaps.hpp"
#include "DawnSkinTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// スキンの prim を簡略化した 1 段 (段 1..3)。頂点は元の頂点のうち使う物だけを詰めたもの
struct DawnSkinLod
{
	std::uint64_t id = nextSkinGeometryId();
	std::vector<Vertex3D> baseVertices;
	std::vector<SkinVertexBinding> binding;
	std::vector<std::uint32_t> indices;
};

/// 1 プリミティブ。skinIndex が負なら剛体として mesh をノードの姿勢で動かし、0 以上なら baseVertices を毎回変形する
struct DawnModelPrim
{
	int nodeIndex = -1;
	int skinIndex = -1;
	std::uint64_t skinId = 0;                    ///< スキンのみ。段 0 の頂点列の識別 (DawnSkinGeometry::id)
	SkinBoundsSource bounds;                     ///< スキンのみ。姿勢から包含箱を出す元 (全段で共通)
	Mesh mesh;                                   ///< 剛体: 描くメッシュ。スキン: バインドポーズ (箱の元)
	std::vector<Vertex3D> baseVertices;          ///< スキンのみ
	std::vector<std::uint32_t> indices;          ///< スキンのみ
	std::vector<SkinVertexBinding> binding;      ///< スキンのみ
	std::vector<DawnSkinLod> lods;               ///< スキンのみ。段 1..3 (減らせない形なら少ない)
	Material material;
	std::shared_ptr<const DawnMaterialMaps> maps;   ///< 画像を持たなければ nullptr
};

struct DawnModel
{
	animation::AnimAsset anim;
	std::deque<DawnModelPrim> prims;   ///< アドレスは変わらない (GPU の cache が Mesh のアドレスで引く)
	std::vector<sgc::Mat4f> restModel; ///< レストポーズのノードのモデル行列
	sgc::Vec3f boundsCenter{0.0f, 0.0f, 0.0f};   ///< レストポーズのスキンの外接球 (モデル空間)。段を選ぶ画面の大きさに使う
	float boundsRadius = 0.0f;
	int lodLevels = 1;                           ///< 段の数 (段 0 を含む)
	// 段の選び方に前のフレームの段を使う (境目でちらつかない)。描いた順 (ordinal) ごとに持つ
	std::uint64_t historyFrame = 0;
	std::uint32_t historyOrdinal = 0;
	std::vector<std::uint8_t> levelHistory;
};

/// 描画側へ渡す 1 メッシュぶん。スキンは mesh を持たず、頂点列と骨の行列 (DawnModelStore::palette() の中の位置) で渡す
struct DawnDrawItem
{
	const Mesh* mesh = nullptr;
	sgc::Mat4f world;
	const Material* material = nullptr;
	const DawnMaterialMaps* maps = nullptr;
	DawnSkinGeometry skin;
	Mesh::AABB skinBounds;
	std::uint32_t paletteFirst = 0;
	std::uint32_t jointCount = 0;

	[[nodiscard]] bool skinned() const noexcept { return skin.base != nullptr; }
};

class DawnModelStore
{
public:
	explicit DawnModelStore(DawnMaterialPool& materials) : m_materials(materials) {}

	/// 読めなければ nullptr。失敗を記録し、同じ path は再度読み込まない
	[[nodiscard]] DawnModel* find(const char* path);

	/// path のファイルから読み込んだモデルを手放す。手放した数を返す
	int forget(const std::filesystem::path& changed);

	/// フレームの最初に呼ぶ
	void beginFrame() noexcept
	{
		m_retired.clear();
		++m_frame;
	}

	/// 直前に返した items のスキンが指す骨の行列 (姿勢 * 逆バインド)。次の入口を呼ぶまで有効
	[[nodiscard]] std::span<const sgc::Mat4f> palette() const noexcept { return m_palette; }

	/// スキンの段を選ぶカメラ。フレームの中で 1 度、描く前に渡す
	void setView(const sgc::Vec3f& eye, float fovY) noexcept
	{
		m_eye = eye;
		m_fovY = fovY;
	}
	/// ゲームの LOD の選び方。0 の欄は既定に戻す
	void setLodLook(const SkinnedLodLook& look) noexcept;

	/// 姿勢の渡し方ごとの入口。描画する物を 1 件ずつ並べて返し、次の呼び出しで上書きする。モデルを読めなければ空
	/// AnimPoseParams を評価して IK を掛ける。nullptr ならレストポーズ。同じ関数をゲーム DLL の当たり判定にも使う
	[[nodiscard]] const std::vector<DawnDrawItem>& animPose(const char* path, const sgc::Mat4f& world,
	                                                        const animation::AnimPoseParams* pose,
	                                                        const animation::AnimIkRequest* ik, int ikCount);
	/// クリップ名と時刻。B があれば blend で混ぜる
	[[nodiscard]] const std::vector<DawnDrawItem>& clips(const char* path, const sgc::Mat4f& world, const ModelPose& pose);
	/// ノードごとのモデル行列。AnimPose::model と同じ並び。数がノードと違えば空
	[[nodiscard]] const std::vector<DawnDrawItem>& nodes(const char* path, const sgc::Mat4f& world,
	                                                     const sgc::Mat4f* nodeModel, int count);

private:
	/// nodeModel の姿勢で全 prim を m_items に並べる。glTF の決まりに従い、スキンの prim にはノードの変換を掛けず、world だけを使う
	void collect(DawnModel& model, const sgc::Mat4f& world, std::span<const sgc::Mat4f> nodeModel);
	/// このフレームの描画に使う段。描いた順ごとに前の段を覚える
	[[nodiscard]] int pickLevel(DawnModel& model, const sgc::Mat4f& world);
	[[nodiscard]] bool load(const std::string& path, DawnModel& out) const;

	DawnMaterialPool& m_materials;
	std::map<std::string, std::unique_ptr<DawnModel>, std::less<>> m_models;   // nullptr は読めなかった印
	std::vector<std::unique_ptr<DawnModel>> m_retired;   ///< forget した物。次の beginFrame まで生かす
	std::uint64_t m_frame = 0;
	sgc::Vec3f m_eye{0.0f, 0.0f, 0.0f};
	float m_fovY = 0.9f;
	SkinnedLodThresholds m_lodThresholds{};
	float m_lodScale = 1.0f;
	int m_lodForced = -1;
	animation::AnimPose m_pose;
	std::vector<DawnDrawItem> m_items;
	std::vector<sgc::Mat4f> m_jointScratch;
	std::vector<sgc::Mat4f> m_palette;
};

} // namespace mitiru::render::dawn_detail
