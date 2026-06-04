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
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/CpuTexture.hpp>
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
		m_prevGBuffer.initialize(width, height);   // #6: 前フレーム GBuffer
		m_shadowMap.initialize(shadowConfig);
		m_width = width;
		m_height = height;
		m_hasPrevFrame = false;
		m_prevWorld.clear();
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

	/// @brief 前フレームの Gバッファを取得する（#6: temporal coherence 用）
	/// @details depth / normal / objectId / velocity を前フレーム分そのまま読める。
	///          初回フレームは空（clear 済み）。輪郭線の reproject 先で
	///          `objectId_{t-1}` 等を参照して correspondence を検証するのに使う。
	[[nodiscard]] const GBuffer& previousGBuffer() const noexcept
	{
		return m_prevGBuffer;
	}

	/// @brief シーンをディファードパイプラインで描画する
	/// @param scene 描画するシーン
	/// @param camera 使用するカメラ
	/// @param output 出力先RenderTexture
	/// @details
	///   1. シャドウ深度パス: 最初のディレクショナルライトでシャドウマップを生成
	///   2. ジオメトリパス:   全メッシュをGバッファに書き込む
	///   3. ライティングパス: Gバッファ + シャドウマップからピクセル色を計算して出力
	/// @param culledNodeIds 任意。ここに含まれる nodeId のオブジェクトは描画スキップ（#5b LOD 配線）。
	///        `LODManager::update()` → `collectCulledNodeIds()` の結果を渡すと遠方が省ける。
	void render(const Scene3D& scene,
	            const Camera3D& camera,
	            RenderTexture& output,
	            const std::unordered_set<int>* culledNodeIds = nullptr)
	{
		MITIRU_LOG_TRACE("DeferredPipeline",
			"render enter, w=" + std::to_string(m_width) + " h=" + std::to_string(m_height));
		output.clear(m_clearColor);

		/// #6: 現フレームを書く前に、前回の結果を prev へ退避する（ping-pong）。
		///     これで描画中も previousGBuffer() から前フレーム属性を読める。
		std::swap(m_gBuffer, m_prevGBuffer);
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
				if (isCulled(obj, culledNodeIds)) { continue; }
				if (obj.mesh && obj.mesh->vertexCount() > 0)
				{
					const sgc::Mat4f world = buildWorldMatrix(obj);
					m_shadowMap.recordMesh(*obj.mesh, world);
				}
			}
			m_shadowMap.endShadowPass();
		}

		/// パス2: ジオメトリパス（GBufferへの書き込み）
		std::unordered_map<int, sgc::Mat4f> curWorld;   // #7: 次フレームの prev 用
		for (const auto& obj : scene.objects())
		{
			if (!obj.mesh || obj.mesh->vertexCount() == 0)
			{
				continue;
			}
			if (isCulled(obj, culledNodeIds)) { continue; }   // #5b: LOD カリング

			const sgc::Mat4f world = buildWorldMatrix(obj);
			// nodeId(-1=未設定) → objectId(0=背景)。0 と区別するため +1 して焼き込む。
			const std::uint32_t objectId =
				obj.nodeId < 0 ? 0u : static_cast<std::uint32_t>(obj.nodeId) + 1u;

			// #7: velocity 用に前フレームの MVP を用意する。nodeId で前フレーム world を
			//     引けた時だけ velocity を書く（初回 / 未マッチ / nodeId 未設定は 0 のまま）。
			sgc::Mat4f prevMvp;
			const sgc::Mat4f* prevMvpPtr = nullptr;
			if (m_hasPrevFrame && obj.nodeId >= 0)
			{
				const auto it = m_prevWorld.find(obj.nodeId);
				if (it != m_prevWorld.end())
				{
					prevMvp = m_prevVP * it->second;
					prevMvpPtr = &prevMvp;
				}
			}
			if (obj.nodeId >= 0) { curWorld[obj.nodeId] = world; }

			geometryPass(*obj.mesh, world, obj.material, vp, objectId, prevMvpPtr, obj.prevMesh,
			             obj.albedoTexture);
		}

		/// パス3: ライティングパス（GBuffer→RenderTexture）
		lightingPass(scene, camera, output, dirLight);

		/// #7: 今フレームの VP / world を次フレームの「前フレーム」として保存する。
		m_prevVP = vp;
		m_prevWorld = std::move(curWorld);
		m_hasPrevFrame = true;
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

	/// @brief このオブジェクトが LOD カリング集合に含まれるか（#5b）。
	[[nodiscard]] static bool isCulled(const Scene3D::RenderObject& obj,
	                                   const std::unordered_set<int>* culledNodeIds) noexcept
	{
		return culledNodeIds != nullptr && obj.nodeId >= 0 &&
		       culledNodeIds->count(obj.nodeId) > 0;
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
	                  const sgc::Mat4f& vp,
	                  std::uint32_t objectId = 0,
	                  const sgc::Mat4f* prevMvp = nullptr,
	                  const Mesh* prevMesh = nullptr,
	                  const CpuTexture* albedoTex = nullptr)
	{
		const sgc::Mat4f mvp = vp * world;
		const auto& verts = mesh.vertices();
		const auto& indices = mesh.indices();

		// #18: 変形メッシュ（クロス/スキニング/モーフ）は同 nodeId でも頂点が変わるので、
		//      前フレームの頂点位置から velocity を出す。同サイズの prevMesh があれば使う。
		const std::vector<Vertex3D>* prevVerts =
			(prevMesh != nullptr && prevMesh->vertices().size() == verts.size())
				? &prevMesh->vertices() : nullptr;
		auto prevPos = [&](std::size_t idx) -> const sgc::Vec3f* {
			return prevVerts ? &(*prevVerts)[idx].position : nullptr;
		};

		if (!indices.empty())
		{
			for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				rasterizeGeometryTriangle(
					verts[indices[i]], verts[indices[i + 1]], verts[indices[i + 2]],
					world, mvp, material, objectId, prevMvp,
					prevPos(indices[i]), prevPos(indices[i + 1]), prevPos(indices[i + 2]),
					albedoTex);
			}
		}
		else
		{
			for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
			{
				rasterizeGeometryTriangle(
					verts[i], verts[i + 1], verts[i + 2],
					world, mvp, material, objectId, prevMvp,
					prevPos(i), prevPos(i + 1), prevPos(i + 2),
					albedoTex);
			}
		}
	}

	/// @brief 頂点を MVP でピクセル座標へ射影する（velocity 計算の補助）。
	/// @return w がほぼ 0（カメラ背後）の場合 ok=false。
	struct ProjectedPoint { float x, y; bool ok; };
	[[nodiscard]] ProjectedPoint projectToPixel(
		const sgc::Mat4f& mvp, const sgc::Vec3f& p) const noexcept
	{
		const sgc::Vec4f c = mvp * sgc::Vec4f{p.x, p.y, p.z, 1.0f};
		if (std::abs(c.w) < 1e-6f) { return {0.0f, 0.0f, false}; }
		const float ndcX = c.x / c.w, ndcY = c.y / c.w;
		return {(ndcX + 1.0f) * 0.5f * static_cast<float>(m_width),
		        (1.0f - ndcY) * 0.5f * static_cast<float>(m_height), true};
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
		const Material& material,
		std::uint32_t objectId = 0,
		const sgc::Mat4f* prevMvp = nullptr,
		const sgc::Vec3f* prevPosA = nullptr,
		const sgc::Vec3f* prevPosB = nullptr,
		const sgc::Vec3f* prevPosC = nullptr,
		const CpuTexture* albedoTex = nullptr)
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

		/// #7: 前フレームのスクリーン座標（velocity 用）。prevMvp が無い / カメラ背後なら無効。
		/// #18: prevPos* があれば前フレームの頂点位置を使う（変形メッシュ対応）。無ければ現頂点位置
		///      （剛体: 同じ頂点が prev 変換で動いた分だけが velocity）。
		ProjectedPoint prevA{}, prevB{}, prevC{};
		bool hasVelocity = false;
		if (prevMvp != nullptr)
		{
			prevA = projectToPixel(*prevMvp, prevPosA ? *prevPosA : va.position);
			prevB = projectToPixel(*prevMvp, prevPosB ? *prevPosB : vb.position);
			prevC = projectToPixel(*prevMvp, prevPosC ? *prevPosC : vc.position);
			hasVelocity = prevA.ok && prevB.ok && prevC.ok;
		}

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
				// #17: テクスチャがあれば頂点 UV を補間してサンプル、無ければ diffuse ベタ塗り。
				if (albedoTex != nullptr && albedoTex->valid())
				{
					const float u = wa * va.texCoord.x + wb * vb.texCoord.x + wc * vc.texCoord.x;
					const float v = wa * va.texCoord.y + wb * vb.texCoord.y + wc * vc.texCoord.y;
					pixel.albedo = albedoTex->sampleNearest(u, v);
				}
				else
				{
					pixel.albedo = material.diffuse;
				}
				pixel.objectId = objectId;

				/// #7: velocity = 現ピクセル − 同一表面点の前フレームスクリーン座標（ピクセル単位）。
				///     前フレームの頂点スクリーン座標を現フレームの重心座標で補間する。
				if (hasVelocity)
				{
					const float prevX = wa * prevA.x + wb * prevB.x + wc * prevC.x;
					const float prevY = wa * prevA.y + wb * prevB.y + wc * prevC.y;
					pixel.velocity = {(static_cast<float>(x) + 0.5f) - prevX,
					                  (static_cast<float>(y) + 0.5f) - prevY};
				}

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
	/// @brief 前フレーム Gバッファ（#6: ping-pong）
	GBuffer m_prevGBuffer;
	/// @brief 前フレームの VP 行列（#7: velocity 計算用）
	sgc::Mat4f m_prevVP;
	/// @brief 前フレームの nodeId → world 行列（#7: velocity 計算用）
	std::unordered_map<int, sgc::Mat4f> m_prevWorld;
	/// @brief 前フレームの情報が揃っているか（初回は false → velocity 0）
	bool m_hasPrevFrame = false;
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
