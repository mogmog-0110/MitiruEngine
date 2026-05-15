#pragma once

/// @file ScenePlacer.hpp
/// @brief シーン配置ヘルパー — 1行でモデルを面の上に配置

#include <mitiru/render/Scene3D.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/MeshNormalizer.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

/// @brief シーン配置ユーティリティ
/// @details 正規化済みメッシュをシーンに簡潔に配置するためのヘルパー。
///
/// @code
/// auto mesh = mitiru::render::Mesh::createCube(1.0f);
/// mitiru::render::ScenePlacer::placeOnSurface(scene, mesh, 0.0f, {0, 0}, 1.0f, mat);
/// @endcode
class ScenePlacer
{
public:
	/// @brief メッシュを指定した面の上に配置する
	/// @param scene シーン
	/// @param mesh メッシュ（底面y=0に正規化済み前提）
	/// @param surfaceY 面のY座標
	/// @param xz XZ平面上の位置
	/// @param scale スケール
	/// @param material マテリアル
	static void placeOnSurface(Scene3D& scene, const Mesh& mesh,
	                            float surfaceY, sgc::Vec2f xz, float scale,
	                            const Material& material)
	{
		Scene3D::RenderObject obj;
		obj.mesh = &mesh;
		obj.position = {xz.x, surfaceY, xz.y};
		obj.scale = {scale, scale, scale};
		obj.material = material;
		scene.addObject(obj);
	}

	/// @brief メッシュを指定位置にスケール付きで配置する
	/// @param scene シーン
	/// @param mesh メッシュ
	/// @param position ワールド位置
	/// @param scale スケール
	/// @param material マテリアル
	static void place(Scene3D& scene, const Mesh& mesh,
	                  sgc::Vec3f position, float scale,
	                  const Material& material)
	{
		Scene3D::RenderObject obj;
		obj.mesh = &mesh;
		obj.position = position;
		obj.scale = {scale, scale, scale};
		obj.material = material;
		scene.addObject(obj);
	}

	/// @brief メッシュを別のメッシュの上に配置する（参照メッシュのバウンディングボックス上面を使用）
	/// @param scene シーン
	/// @param itemMesh 上に置くメッシュ
	/// @param baseMesh 土台メッシュ
	/// @param basePosition 土台のワールド位置
	/// @param baseScale 土台のスケール
	/// @param offsetXZ XZ平面のオフセット
	/// @param itemScale アイテムのスケール
	/// @param material マテリアル
	static void placeOnTop(Scene3D& scene, const Mesh& itemMesh, const Mesh& baseMesh,
	                        sgc::Vec3f basePosition, float baseScale,
	                        sgc::Vec2f offsetXZ, float itemScale,
	                        const Material& material)
	{
		const auto baseBounds = MeshNormalizer::computeBounds(baseMesh);
		const float topY = basePosition.y + baseBounds.max.y * baseScale;
		place(scene, itemMesh,
		      {basePosition.x + offsetXZ.x, topY, basePosition.z + offsetXZ.y},
		      itemScale, material);
	}

	/// @brief 等間隔に複数のアイテムを横に並べる
	/// @param scene シーン
	/// @param mesh メッシュ
	/// @param startPos 開始位置
	/// @param spacing 間隔
	/// @param count 個数
	/// @param scale スケール
	/// @param material マテリアル
	static void placeRow(Scene3D& scene, const Mesh& mesh,
	                     sgc::Vec3f startPos, float spacing, int count,
	                     float scale, const Material& material)
	{
		for (int i = 0; i < count; ++i)
		{
			sgc::Vec3f pos = startPos;
			pos.x += static_cast<float>(i) * spacing;
			place(scene, mesh, pos, scale, material);
		}
	}
};

} // namespace mitiru::render
