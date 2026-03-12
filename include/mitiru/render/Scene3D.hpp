#pragma once

/// @file Scene3D.hpp
/// @brief 3Dシーンコンテナ
/// @details メッシュ、マテリアル、ライトを管理する3Dシーン。

#include <vector>

#include <sgc/math/Vec3.hpp>

#include "Light.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

namespace mitiru::render
{

/// @brief 3Dシーン
/// @details 描画オブジェクトとライトを保持するコンテナ。
///          レンダラーに渡してシーン全体を描画する。
///
/// @code
/// mitiru::render::Scene3D scene;
/// auto cube = mitiru::render::Mesh::createCube(1.0f);
/// scene.addObject({&cube, {}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}});
/// scene.addLight(mitiru::render::Light::directional({0, -1, 0}));
/// @endcode
class Scene3D
{
public:
	/// @brief 描画オブジェクト
	/// @details メッシュへのポインタ・マテリアル・トランスフォームを保持する。
	struct RenderObject
	{
		const Mesh* mesh = nullptr;              ///< メッシュデータ（非所有）
		Material material;                        ///< マテリアル
		sgc::Vec3f position{};                   ///< ワールド位置
		sgc::Vec3f rotation{};                   ///< オイラー角回転（ラジアン）
		sgc::Vec3f scale{1.0f, 1.0f, 1.0f};     ///< スケール
	};

	/// @brief デフォルトコンストラクタ
	Scene3D() noexcept = default;

	/// @brief 描画オブジェクトを追加する
	/// @param obj 描画オブジェクト
	void addObject(RenderObject obj)
	{
		m_objects.push_back(std::move(obj));
	}

	/// @brief ライトを追加する
	/// @param light ライト
	void addLight(Light light)
	{
		m_lights.push_back(std::move(light));
	}

	/// @brief シーンをクリアする（全オブジェクト・ライトを削除）
	void clear()
	{
		m_objects.clear();
		m_lights.clear();
	}

	/// @brief 描画オブジェクト一覧を取得する
	/// @return オブジェクト配列の定数参照
	[[nodiscard]] const std::vector<RenderObject>& objects() const noexcept
	{
		return m_objects;
	}

	/// @brief ライト一覧を取得する
	/// @return ライト配列の定数参照
	[[nodiscard]] const std::vector<Light>& lights() const noexcept
	{
		return m_lights;
	}

	/// @brief オブジェクト数を取得する
	/// @return オブジェクト数
	[[nodiscard]] std::size_t objectCount() const noexcept
	{
		return m_objects.size();
	}

	/// @brief ライト数を取得する
	/// @return ライト数
	[[nodiscard]] std::size_t lightCount() const noexcept
	{
		return m_lights.size();
	}

private:
	std::vector<RenderObject> m_objects;  ///< 描画オブジェクト一覧
	std::vector<Light> m_lights;          ///< ライト一覧
};

} // namespace mitiru::render
