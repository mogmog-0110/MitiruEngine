#pragma once

/// @file DeferredPipeline.hpp
/// @brief ディファードレンダリングパイプライン
/// @details シャドウマップ + Gバッファを使った3パスのソフトウェアレンダリング。
///
///   パス1 (シャドウ深度パス): ShadowMapにメッシュ深度を記録する
///   パス2 (ジオメトリパス):   GBufferに位置・法線・アルベドを書き込む
///   パス3 (ライティングパス): GBufferとShadowMapを参照してRenderTextureに出力する

#include <algorithm>
#include <cmath>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/GBuffer.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/RenderTexture.hpp>
#include <mitiru/render/Scene3D.hpp>
#include <mitiru/render/ShadowMap.hpp>
#include <mitiru/debug/Log.hpp>

namespace mitiru::render
{

/// @brief ディファードレンダリングパイプライン（ソフトウェア実装）
/// @details Scene3DとCamera3Dを受け取り、シャドウマップ付きのディファードシェーディングを
///          ソフトウェアラスタライザで実行してRenderTextureに出力する。
///
/// @code
/// mitiru::render::DeferredPipeline pipeline;
/// pipeline.initialize(1280, 720, {});
///
/// mitiru::render::RenderTexture output(1280, 720);
/// pipeline.render(scene, camera, output);
/// @endcode
class DeferredPipeline
{
public:
	/// @brief デフォルトコンストラクタ
	DeferredPipeline() noexcept = default;

	/// @brief パイプラインを初期化する
	/// @param width 出力幅（ピクセル）
	/// @param height 出力高さ（ピクセル）
	/// @param shadowConfig シャドウマップ設定
	void initialize(int width, int height,
	                const ShadowMapConfig& shadowConfig = {})
	{
		m_gBuffer.initialize(width, height);
		m_shadowMap.initialize(shadowConfig);
		m_width = width;
		m_height = height;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_gBuffer.isInitialized() && m_shadowMap.isInitialized();
	}

	/// @brief シャドウマップを取得する（テスト・デバッグ用）
	[[nodiscard]] const ShadowMap& shadowMap() const noexcept
	{
		return m_shadowMap;
	}

	/// @brief Gバッファを取得する（テスト・デバッグ用）
	[[nodiscard]] const GBuffer& gBuffer() const noexcept
	{
		return m_gBuffer;
	}

	/// @brief シーンをディファードパイプラインで描画する
	/// @param scene 描画するシーン
	/// @param camera 使用するカメラ
	/// @param output 出力先RenderTexture
	/// @details
	///   1. シャドウ深度パス: 最初のディレクショナルライトでシャドウマップを生成
	///   2. ジオメトリパス:   全メッシュをGバッファに書き込む
	///   3. ライティングパス: Gバッファ + シャドウマップからピクセル色を計算して出力
	void render(const Scene3D& scene,
	            const Camera3D& camera,
	            RenderTexture& output)
	{
		MITIRU_LOG_TRACE("DeferredPipeline",
			"render enter, w=" + std::to_string(m_width) + " h=" + std::to_string(m_height));
		output.clear(m_clearColor);
		m_gBuffer.clear();

		/// カメラのビュー射影行列を計算する
		const sgc::Mat4f vp = camera.projectionMatrix() * camera.viewMatrix();

		/// パス1: シャドウ深度パス
		const Light* dirLight = findFirstDirectionalLight(scene);
		MITIRU_LOG_TRACE("DeferredPipeline",
			std::string("dirLight=") + (dirLight ? "found" : "null"));
		if (dirLight)
		{
			m_shadowMap.beginShadowPass(*dirLight);
			for (const auto& obj : scene.objects())
			{
				if (obj.mesh && obj.mesh->vertexCount() > 0)
				{
					const sgc::Mat4f world = buildWorldMatrix(obj);
					m_shadowMap.recordMesh(*obj.mesh, world);
				}
			}
			m_shadowMap.endShadowPass();
		}

		/// パス2: ジオメトリパス（GBufferへの書き込み）
		for (const auto& obj : scene.objects())
		{
			if (!obj.mesh || obj.mesh->vertexCount() == 0)
			{
				continue;
			}

			const sgc::Mat4f world = buildWorldMatrix(obj);
			geometryPass(*obj.mesh, world, obj.material, vp);
		}

		/// パス3: ライティングパス（GBuffer→RenderTexture）
		lightingPass(scene, camera, output, dirLight);
		MITIRU_LOG_TRACE("DeferredPipeline", "render complete");
	}

private:
	/// @brief シーン内の最初のディレクショナルライトを検索する
	/// @param scene シーン
	/// @return ディレクショナルライトへのポインタ（なければnullptr）
	[[nodiscard]] static const Light* findFirstDirectionalLight(
		const Scene3D& scene) noexcept
	{
		for (const auto& light : scene.lights())
		{
			if (light.type == LightType::Directional)
			{
				return &light;
			}
		}
		return nullptr;
	}

	/// @brief Scene3D::RenderObjectからワールド行列を構築する
	/// @param obj 描画オブジェクト
	/// @return ワールド変換行列
	[[nodiscard]] static sgc::Mat4f buildWorldMatrix(
		const Scene3D::RenderObject& obj) noexcept
	{
		const sgc::Mat4f T = sgc::Mat4f::translation(obj.position);
		const sgc::Mat4f Rx = sgc::Mat4f::rotationX(obj.rotation.x);
		const sgc::Mat4f Ry = sgc::Mat4f::rotationY(obj.rotation.y);
		const sgc::Mat4f Rz = sgc::Mat4f::rotationZ(obj.rotation.z);
		const sgc::Mat4f S = sgc::Mat4f::scaling(obj.scale);
		return T * Ry * Rx * Rz * S;
	}

	/// @brief ジオメトリパス: メッシュをGBufferに書き込む
	/// @param mesh メッシュデータ
	/// @param world ワールド行列
	/// @param material マテリアル
	/// @param vp ビュー射影行列
	void geometryPass(const Mesh& mesh,
	                  const sgc::Mat4f& world,
	                  const Material& material,
	                  const sgc::Mat4f& vp)
	{
		const sgc::Mat4f mvp = vp * world;
		const auto& verts = mesh.vertices();
		const auto& indices = mesh.indices();

		if (!indices.empty())
		{
			for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				rasterizeGeometryTriangle(
					verts[indices[i]],
					verts[indices[i + 1]],
					verts[indices[i + 2]],
					world, mvp, material);
			}
		}
		else
		{
			for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
			{
				rasterizeGeometryTriangle(
					verts[i],
					verts[i + 1],
					verts[i + 2],
					world, mvp, material);
			}
		}
	}

	/// @brief ジオメトリパス用三角形ラスタライザ
	/// @param va 頂点A
	/// @param vb 頂点B
	/// @param vc 頂点C
	/// @param world ワールド行列
	/// @param mvp モデルビュー射影行列
	/// @param material マテリアル
	void rasterizeGeometryTriangle(
		const Vertex3D& va,
		const Vertex3D& vb,
		const Vertex3D& vc,
		const sgc::Mat4f& world,
		const sgc::Mat4f& mvp,
		const Material& material)
	{
		/// クリップ座標に変換する
		const sgc::Vec4f ca = mvp * sgc::Vec4f{va.position.x, va.position.y, va.position.z, 1.0f};
		const sgc::Vec4f cb = mvp * sgc::Vec4f{vb.position.x, vb.position.y, vb.position.z, 1.0f};
		const sgc::Vec4f cc = mvp * sgc::Vec4f{vc.position.x, vc.position.y, vc.position.z, 1.0f};

		if (std::abs(ca.w) < 1e-6f || std::abs(cb.w) < 1e-6f || std::abs(cc.w) < 1e-6f)
		{
			return;
		}

		/// NDCに変換する
		const float ax = ca.x / ca.w, ay = ca.y / ca.w, az = (ca.z / ca.w + 1.0f) * 0.5f;
		const float bx = cb.x / cb.w, by = cb.y / cb.w, bz = (cb.z / cb.w + 1.0f) * 0.5f;
		const float cx = cc.x / cc.w, cy = cc.y / cc.w, cz = (cc.z / cc.w + 1.0f) * 0.5f;

		/// NDC→ピクセル座標へ変換する
		const float fw = static_cast<float>(m_width);
		const float fh = static_cast<float>(m_height);

		const int pax = static_cast<int>((ax + 1.0f) * 0.5f * fw);
		const int pay = static_cast<int>((1.0f - ay) * 0.5f * fh);
		const int pbx = static_cast<int>((bx + 1.0f) * 0.5f * fw);
		const int pby = static_cast<int>((1.0f - by) * 0.5f * fh);
		const int pcx = static_cast<int>((cx + 1.0f) * 0.5f * fw);
		const int pcy = static_cast<int>((1.0f - cy) * 0.5f * fh);

		/// ワールド空間の位置と法線を計算する
		const sgc::Vec3f worldA = world.transformPoint(va.position);
		const sgc::Vec3f worldB = world.transformPoint(vb.position);
		const sgc::Vec3f worldC = world.transformPoint(vc.position);
		const sgc::Vec3f normA = world.transformVector(va.normal).normalized();
		const sgc::Vec3f normB = world.transformVector(vb.normal).normalized();
		const sgc::Vec3f normC = world.transformVector(vc.normal).normalized();

		/// バウンディングボックスを計算する
		const int minX = std::max(0, std::min({pax, pbx, pcx}));
		const int maxX = std::min(m_width - 1, std::max({pax, pbx, pcx}));
		const int minY = std::max(0, std::min({pay, pby, pcy}));
		const int maxY = std::min(m_height - 1, std::max({pay, pby, pcy}));

		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;

				const float denom =
					static_cast<float>((pby - pcy) * (pax - pcx) +
					                   (pcx - pbx) * (pay - pcy));

				if (std::abs(denom) < 1e-6f) continue;

				const float wa =
					static_cast<float>((pby - pcy) * (px - static_cast<float>(pcx)) +
					                   (pcx - pbx) * (py - static_cast<float>(pcy))) / denom;
				const float wb =
					static_cast<float>((pcy - pay) * (px - static_cast<float>(pcx)) +
					                   (pax - pcx) * (py - static_cast<float>(pcy))) / denom;
				const float wc = 1.0f - wa - wb;

				if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;

				/// 深度・位置・法線を補間する
				const float depth = wa * az + wb * bz + wc * cz;

				GBufferPixel pixel;
				pixel.depth = depth;
				pixel.position = worldA * wa + worldB * wb + worldC * wc;
				pixel.normal = (normA * wa + normB * wb + normC * wc).normalized();
				pixel.albedo = material.diffuse;

				/// 深度テストを行ってGBufferに書き込む
				const auto& existing = m_gBuffer.readPixel(x, y);
				if (depth < existing.depth)
				{
					m_gBuffer.writePixel(x, y, pixel);
				}
			}
		}
	}

	/// @brief ライティングパス: GBuffer + ShadowMapから最終色を計算してRenderTextureに出力する
	/// @param scene シーン
	/// @param camera カメラ（鏡面反射計算用）
	/// @param output 出力先RenderTexture
	/// @param dirLight ディレクショナルライト（nullptrならアンビエントのみ）
	void lightingPass(const Scene3D& scene,
	                  const Camera3D& camera,
	                  RenderTexture& output,
	                  const Light* dirLight)
	{
		static constexpr float kAmbient = 0.15f;

		for (int y = 0; y < m_height; ++y)
		{
			for (int x = 0; x < m_width; ++x)
			{
				const auto& pixel = m_gBuffer.readPixel(x, y);

				/// 書き込みがないピクセルはスキップする（depth=1.0のまま）
				if (pixel.depth >= 1.0f)
				{
					continue;
				}

				/// アンビエント成分
				sgc::Colorf finalColor{
					pixel.albedo.r * kAmbient,
					pixel.albedo.g * kAmbient,
					pixel.albedo.b * kAmbient,
					pixel.albedo.a
				};

				if (dirLight)
				{
					/// ディフューズ成分を計算する
					const sgc::Vec3f lightDir =
						(dirLight->direction * -1.0f).normalized();
					const float nDotL =
						std::max(0.0f, pixel.normal.dot(lightDir));

					const float li = dirLight->intensity;
					const float lr = dirLight->color.r;
					const float lg = dirLight->color.g;
					const float lb = dirLight->color.b;

					/// シャドウ判定を行う
					const float shadowFactor =
						m_shadowMap.isInShadow(pixel.position) ? 0.0f : 1.0f;

					finalColor.r += pixel.albedo.r * nDotL * li * lr * shadowFactor;
					finalColor.g += pixel.albedo.g * nDotL * li * lg * shadowFactor;
					finalColor.b += pixel.albedo.b * nDotL * li * lb * shadowFactor;

					/// 鏡面反射成分を計算する
					const sgc::Vec3f viewDir =
						(camera.position() - pixel.position).normalized();
					const sgc::Vec3f halfVec = (lightDir + viewDir).normalized();
					const float nDotH = std::max(0.0f, pixel.normal.dot(halfVec));

					/// 固定の鏡面反射強度を使用する
					constexpr float kSpecPow = 32.0f;
					const float specular = std::pow(nDotH, kSpecPow) * shadowFactor;
					finalColor.r += specular * li * lr;
					finalColor.g += specular * li * lg;
					finalColor.b += specular * li * lb;
				}

				/// 色を [0,1] にクランプしてピクセルに書き込む
				finalColor.r = std::min(1.0f, finalColor.r);
				finalColor.g = std::min(1.0f, finalColor.g);
				finalColor.b = std::min(1.0f, finalColor.b);

				output.setPixel(x, y, finalColor);
			}
		}
	}

	/// @brief シャドウマップ
	ShadowMap m_shadowMap;
	/// @brief Gバッファ
	GBuffer m_gBuffer;
	/// @brief 出力幅
	int m_width = 0;
	/// @brief 出力高さ
	int m_height = 0;
	/// @brief クリア色
	sgc::Colorf m_clearColor{0, 0, 0, 1};

public:
	/// @brief クリア色を設定する
	void setClearColor(const sgc::Colorf& color) noexcept { m_clearColor = color; }
};

} // namespace mitiru::render
