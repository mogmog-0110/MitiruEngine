#pragma once

/// @file OffscreenRenderer3D.hpp
/// @brief 3Dシーンをオフスクリーンテクスチャにレンダリングする

#include <mitiru/render/DeferredPipeline.hpp>
#include <mitiru/render/RenderTexture.hpp>
#include <mitiru/render/Scene3D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render {

/// @brief オフスクリーン3Dレンダラー
/// @details 3Dシーンをオフスクリーンのピクセルバッファにレンダリングし、
///          結果をTextureとして2D描画に合成できるようにする。
///
/// @code
/// mitiru::render::OffscreenRenderer3D offscreen(256, 256);
/// offscreen.beginScene();
/// offscreen.addMesh(cubeMesh, {0,0,0});
/// offscreen.addLight(Light::directional({0, -1, 0.5f}));
/// offscreen.render(camera);
///
/// // 2Dに合成
/// screen.drawSprite(offscreen.texture(), {100, 100, 256, 256});
/// @endcode
class OffscreenRenderer3D {
public:
	/// @brief コンストラクタ
	/// @param width レンダリング幅
	/// @param height レンダリング高さ
	OffscreenRenderer3D(int width, int height)
		: m_output(width, height)
		, m_width(width)
		, m_height(height)
	{
		m_pipeline.initialize(width, height, ShadowMapConfig{512});
	}

	/// @brief シーンの構築を開始する
	void beginScene() {
		m_scene.clear();
	}

	/// @brief メッシュをシーンに追加する
	void addMesh(const Mesh& mesh,
	             const sgc::Vec3f& position = {},
	             const sgc::Vec3f& rotation = {},
	             const sgc::Vec3f& scale = {1,1,1},
	             const Material& material = Material::defaultMaterial()) {
		Scene3D::RenderObject obj;
		obj.mesh = &mesh;
		obj.material = material;
		obj.position = position;
		obj.rotation = rotation;
		obj.scale = scale;
		m_scene.addObject(obj);
	}

	/// @brief ライトをシーンに追加する
	void addLight(const Light& light) {
		m_scene.addLight(light);
	}

	/// @brief シーンをレンダリングする
	void render(const Camera3D& camera) {
		m_output.clear(m_clearColor);
		m_pipeline.render(m_scene, camera, m_output);
	}

	/// @brief レンダリング結果をTextureとして取得する
	[[nodiscard]] Texture texture() const {
		return m_output.texture();
	}

	/// @brief レンダリング結果のRenderTextureを取得する
	[[nodiscard]] const RenderTexture& renderTexture() const noexcept {
		return m_output;
	}

	/// @brief レンダリング解像度を変更する
	void resize(int width, int height) {
		m_width = width;
		m_height = height;
		m_output = RenderTexture(width, height);
		m_pipeline.initialize(width, height, ShadowMapConfig{512});
	}

	[[nodiscard]] int width() const noexcept { return m_width; }
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief クリア色を設定する（デフォルト: 黒）
	void setClearColor(const sgc::Colorf& color) noexcept {
		m_clearColor = color;
		m_pipeline.setClearColor(color);
	}

	/// @brief 内部シーンの参照
	[[nodiscard]] Scene3D& scene() noexcept { return m_scene; }
	[[nodiscard]] const Scene3D& scene() const noexcept { return m_scene; }

private:
	Scene3D m_scene;
	DeferredPipeline m_pipeline;
	RenderTexture m_output;
	int m_width;
	int m_height;
	sgc::Colorf m_clearColor{0, 0, 0, 1};
};

} // namespace mitiru::render
