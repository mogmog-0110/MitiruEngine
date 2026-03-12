#pragma once

/// @file Pipeline3D.hpp
/// @brief 3Dレンダリングパイプライン
/// @details Scene3Dのメッシュ・ライトデータをGPUに送信する3D描画パイプライン。
///          DX11環境では実GPU描画を行い、NullDevice環境ではドローコールのカウントのみ行う。

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief 3D描画コマンド
/// @details メッシュ・トランスフォーム・マテリアルを1つの描画単位として保持する。
struct DrawCommand3D
{
	const Mesh* mesh = nullptr;     ///< メッシュデータ（非所有）
	sgc::Mat4f transform;           ///< ワールド変換行列
	Material material;              ///< マテリアル
};

/// @brief 3Dレンダリングパイプライン
/// @details begin/submitMesh/submitLight/endの流れでフレーム描画を行う。
///          NullDevice時はドローコール数をカウントするのみ。
///
/// @code
/// mitiru::render::Pipeline3D pipeline;
/// pipeline.setDevice(device);
///
/// pipeline.begin(camera);
/// pipeline.submitMesh(cubeMesh, worldMatrix, material);
/// pipeline.submitLight(Light::directional({0, -1, 0}));
/// pipeline.end();
///
/// int draws = pipeline.drawCallCount();
/// @endcode
class Pipeline3D
{
public:
	/// @brief デフォルトコンストラクタ
	Pipeline3D() noexcept = default;

	/// @brief GPUデバイスを設定する
	/// @param device GPUデバイスへのポインタ（nullptrでヘッドレスモード）
	void setDevice(gfx::IDevice* device) noexcept
	{
		m_device = device;
	}

	/// @brief 接続中のGPUデバイスを取得する
	/// @return GPUデバイスへのポインタ（未接続時はnullptr）
	[[nodiscard]] gfx::IDevice* device() const noexcept
	{
		return m_device;
	}

	/// @brief フレーム描画を開始する
	/// @param camera 3Dカメラ（ビュー行列・射影行列の生成に使用）
	void begin(const Camera3D& camera)
	{
		m_drawCommands.clear();
		m_lights.clear();
		m_drawCallCount = 0;

		m_viewMatrix = camera.viewMatrix();
		m_projMatrix = camera.projectionMatrix();
		m_vpMatrix = m_projMatrix * m_viewMatrix;
		m_cameraPosition = camera.position();
	}

	/// @brief メッシュを描画キューに追加する
	/// @param mesh メッシュデータへの参照
	/// @param transform ワールド変換行列
	/// @param material マテリアル
	void submitMesh(const Mesh& mesh, const sgc::Mat4f& transform,
	                const Material& material)
	{
		DrawCommand3D cmd;
		cmd.mesh = &mesh;
		cmd.transform = transform;
		cmd.material = material;
		m_drawCommands.push_back(std::move(cmd));
	}

	/// @brief ライトを追加する
	/// @param light ライトデータ
	void submitLight(const Light& light)
	{
		m_lights.push_back(light);
	}

	/// @brief フレーム描画を終了し、GPUにフラッシュする
	/// @details 蓄積された描画コマンドをデバイス経由でGPU送信する。
	///          NullDevice時はドローコールのカウントのみ行う。
	void end()
	{
		if (!m_device)
		{
			/// デバイス未接続時はカウントのみ
			m_drawCallCount = static_cast<int>(m_drawCommands.size());
			return;
		}

		if (m_device->backend() == gfx::Backend::Null)
		{
			/// NullDeviceの場合はカウントのみ
			m_drawCallCount = static_cast<int>(m_drawCommands.size());
			return;
		}

#ifdef _WIN32
		flushToGpu();
#else
		/// 非Windows環境でもカウントは記録する
		m_drawCallCount = static_cast<int>(m_drawCommands.size());
#endif
	}

	/// @brief 今フレームのドローコール数を取得する
	/// @return ドローコール数
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 送信済みの描画コマンド一覧を取得する
	/// @return 描画コマンド配列の定数参照
	[[nodiscard]] const std::vector<DrawCommand3D>& drawCommands() const noexcept
	{
		return m_drawCommands;
	}

	/// @brief 送信済みのライト一覧を取得する
	/// @return ライト配列の定数参照
	[[nodiscard]] const std::vector<Light>& lights() const noexcept
	{
		return m_lights;
	}

	/// @brief ビュー行列を取得する
	/// @return ビュー変換行列
	[[nodiscard]] const sgc::Mat4f& viewMatrix() const noexcept
	{
		return m_viewMatrix;
	}

	/// @brief 射影行列を取得する
	/// @return 透視投影行列
	[[nodiscard]] const sgc::Mat4f& projectionMatrix() const noexcept
	{
		return m_projMatrix;
	}

	/// @brief ビュー×射影行列を取得する
	/// @return 合成行列
	[[nodiscard]] const sgc::Mat4f& viewProjectionMatrix() const noexcept
	{
		return m_vpMatrix;
	}

private:
#ifdef _WIN32
	/// @brief 蓄積された描画コマンドをDX11経由でGPU送信する
	void flushToGpu()
	{
		if (m_drawCommands.empty())
		{
			return;
		}

		/// コマンドリストを生成する
		auto cmdList = m_device->createCommandList();
		if (!cmdList)
		{
			m_drawCallCount = static_cast<int>(m_drawCommands.size());
			return;
		}

		cmdList->begin();

		for (const auto& cmd : m_drawCommands)
		{
			if (!cmd.mesh || cmd.mesh->vertexCount() == 0)
			{
				continue;
			}

			/// 頂点バッファを生成する
			const auto& verts = cmd.mesh->vertices();
			const auto vbSize = static_cast<std::uint32_t>(
				verts.size() * sizeof(Vertex3D));

			auto vb = m_device->createBuffer(
				gfx::BufferType::Vertex,
				vbSize,
				false,
				verts.data());

			if (!vb)
			{
				continue;
			}

			/// インデックスバッファを生成する（インデックスがある場合）
			std::unique_ptr<gfx::IBuffer> ib;
			const auto& indices = cmd.mesh->indices();

			if (!indices.empty())
			{
				const auto ibSize = static_cast<std::uint32_t>(
					indices.size() * sizeof(std::uint32_t));

				ib = m_device->createBuffer(
					gfx::BufferType::Index,
					ibSize,
					false,
					indices.data());
			}

			/// 頂点バッファを設定する
			cmdList->setVertexBuffer(vb.get());

			if (ib)
			{
				/// インデックス付き描画を実行する
				cmdList->setIndexBuffer(ib.get());
				cmdList->drawIndexed(
					static_cast<std::uint32_t>(indices.size()), 0, 0);
			}
			else
			{
				/// 頂点のみで描画する
				cmdList->draw(
					static_cast<std::uint32_t>(verts.size()), 0);
			}

			++m_drawCallCount;
		}

		cmdList->end();
	}
#endif

	/// @brief GPUデバイス（非所有）
	gfx::IDevice* m_device = nullptr;

	/// @brief 描画コマンドキュー
	std::vector<DrawCommand3D> m_drawCommands;

	/// @brief ライトリスト
	std::vector<Light> m_lights;

	/// @brief ビュー行列
	sgc::Mat4f m_viewMatrix;

	/// @brief 射影行列
	sgc::Mat4f m_projMatrix;

	/// @brief ビュー×射影合成行列
	sgc::Mat4f m_vpMatrix;

	/// @brief カメラ位置
	sgc::Vec3f m_cameraPosition;

	/// @brief 今フレームのドローコール数
	int m_drawCallCount = 0;
};

} // namespace mitiru::render
