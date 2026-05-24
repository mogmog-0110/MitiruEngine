#pragma once

/// @file RenderPipeline3D.hpp
/// @brief 3Dレンダリングパイプライン管理
///
/// カメラ・ライト・レンダーキューを統合し、
/// beginFrame/endFrame/submitパターンで描画コマンドを処理する。
///
/// @code
/// sgc::graphics3d::RenderPipeline3D pipeline;
/// pipeline.setCamera(camera);
/// pipeline.addLight(directionalLight);
/// pipeline.beginFrame();
/// pipeline.submit(command);
/// pipeline.endFrame();
/// for (const auto& cmd : pipeline.queue().commands()) { /* 描画 */ }
/// @endcode

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "sgc/graphics3d/Camera3D.hpp"
#include "sgc/graphics3d/IRenderer3D.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

/// @brief 3Dライト情報
///
/// 平行光源・点光源・スポットライトの情報を保持する。
struct Light3D
{
	LightType type{LightType::Directional};  ///< ライト種別
	Vec3f position{0.0f, 10.0f, 0.0f};      ///< 位置（Point/Spot用）
	Vec3f direction{0.0f, -1.0f, 0.0f};     ///< 方向（Directional/Spot用）
	Colorf color{1.0f, 1.0f, 1.0f, 1.0f};   ///< ライト色
	float intensity = 1.0f;                   ///< 強度
	float range = 100.0f;                     ///< 到達距離（Point/Spot用）
	float spotAngle = 30.0f;                  ///< スポット角度（度、Spot用）

	/// @brief LightDataへの変換
	/// @return IRenderer3D互換のLightData
	[[nodiscard]] LightData toLightData() const noexcept
	{
		return LightData{type, position, direction, color, intensity, range, spotAngle};
	}
};

/// @brief 深度バッファの状態
enum class DepthState : int32_t
{
	ReadWrite,  ///< 読み書き有効（デフォルト）
	ReadOnly,   ///< 読み取りのみ（透明オブジェクト用）
	Disabled    ///< 深度テスト無効
};

/// @brief レンダーコマンド（描画命令1件）
///
/// メッシュ参照・ワールド変換・マテリアルIDをまとめた描画指示。
struct RenderCommand3D
{
	std::string meshId;       ///< メッシュ識別子
	Mat4f transform;          ///< ワールド変換行列
	std::string materialId;   ///< マテリアル識別子
	float sortKey = 0.0f;     ///< ソートキー（深度値等）
	bool transparent = false; ///< 半透明フラグ

	/// @brief 比較演算子（ソートキーで比較）
	[[nodiscard]] bool operator==(const RenderCommand3D&) const noexcept = default;
};

/// @brief レンダーキュー（描画コマンドの管理・ソート）
///
/// 不透明・半透明のコマンドを蓄積し、深度値でソートする。
/// 不透明オブジェクトは手前から奥へ（Early-Z最適化）、
/// 半透明オブジェクトは奥から手前へソートする。
class RenderQueue3D
{
public:
	/// @brief コマンドを追加する
	/// @param cmd 描画コマンド
	void addCommand(const RenderCommand3D& cmd)
	{
		if (cmd.transparent)
		{
			m_transparentCommands.push_back(cmd);
		}
		else
		{
			m_opaqueCommands.push_back(cmd);
		}
	}

	/// @brief 深度値でソートする
	///
	/// 不透明: sortKey昇順（手前→奥、Early-Z最適化）
	/// 半透明: sortKey降順（奥→手前、正しいブレンディング）
	void sort()
	{
		std::sort(m_opaqueCommands.begin(), m_opaqueCommands.end(),
			[](const RenderCommand3D& a, const RenderCommand3D& b)
			{
				return a.sortKey < b.sortKey;
			});
		std::sort(m_transparentCommands.begin(), m_transparentCommands.end(),
			[](const RenderCommand3D& a, const RenderCommand3D& b)
			{
				return a.sortKey > b.sortKey;
			});
	}

	/// @brief 不透明コマンド一覧を取得する
	/// @return 不透明コマンドの参照
	[[nodiscard]] const std::vector<RenderCommand3D>& opaqueCommands() const noexcept
	{
		return m_opaqueCommands;
	}

	/// @brief 半透明コマンド一覧を取得する
	/// @return 半透明コマンドの参照
	[[nodiscard]] const std::vector<RenderCommand3D>& transparentCommands() const noexcept
	{
		return m_transparentCommands;
	}

	/// @brief 全コマンド数を返す
	/// @return 不透明＋半透明のコマンド総数
	[[nodiscard]] std::size_t totalCommandCount() const noexcept
	{
		return m_opaqueCommands.size() + m_transparentCommands.size();
	}

	/// @brief 全コマンドをクリアする
	void clear()
	{
		m_opaqueCommands.clear();
		m_transparentCommands.clear();
	}

private:
	std::vector<RenderCommand3D> m_opaqueCommands;       ///< 不透明コマンド
	std::vector<RenderCommand3D> m_transparentCommands;   ///< 半透明コマンド
};

/// @brief 3Dレンダリングパイプライン
///
/// カメラ・ライト・レンダーキュー・深度状態を統合管理する。
/// beginFrame()→submit()→endFrame()の順で使用する。
class RenderPipeline3D
{
public:
	/// @brief フレーム開始処理
	///
	/// レンダーキューとライトをクリアし、フレーム状態を初期化する。
	void beginFrame()
	{
		m_queue.clear();
		m_lights.clear();
		m_frameActive = true;
	}

	/// @brief 描画コマンドを送信する
	/// @param cmd 描画コマンド
	void submit(const RenderCommand3D& cmd)
	{
		m_queue.addCommand(cmd);
	}

	/// @brief フレーム終了処理
	///
	/// レンダーキューをソートし、フレームを完了する。
	void endFrame()
	{
		m_queue.sort();
		m_frameActive = false;
	}

	/// @brief カメラを設定する
	/// @param camera 3Dカメラ
	void setCamera(const Camera3D& camera) { m_camera = camera; }

	/// @brief カメラを取得する
	/// @return 現在のカメラ
	[[nodiscard]] const Camera3D& camera() const noexcept { return m_camera; }

	/// @brief ライトを追加する
	/// @param light 3Dライト
	void addLight(const Light3D& light) { m_lights.push_back(light); }

	/// @brief ライト一覧を取得する
	/// @return ライト配列の参照
	[[nodiscard]] const std::vector<Light3D>& lights() const noexcept { return m_lights; }

	/// @brief 深度バッファ状態を設定する
	/// @param state 深度状態
	void setDepthState(DepthState state) noexcept { m_depthState = state; }

	/// @brief 深度バッファ状態を取得する
	/// @return 現在の深度状態
	[[nodiscard]] DepthState depthState() const noexcept { return m_depthState; }

	/// @brief レンダーキューを取得する
	/// @return レンダーキューの参照
	[[nodiscard]] const RenderQueue3D& queue() const noexcept { return m_queue; }

	/// @brief フレームがアクティブかどうかを返す
	/// @return beginFrame後かつendFrame前ならtrue
	[[nodiscard]] bool isFrameActive() const noexcept { return m_frameActive; }

	/// @brief ビュー投影行列を取得する
	/// @return カメラのビュー投影行列
	[[nodiscard]] Mat4f viewProjectionMatrix() const
	{
		return m_camera.viewProjectionMatrix();
	}

	/// @brief コマンドのソートキーをカメラ距離で計算する
	/// @param worldPos オブジェクトのワールド位置
	/// @return カメラとの距離の二乗（ソートキーとして使用）
	[[nodiscard]] float calculateSortKey(const Vec3f& worldPos) const noexcept
	{
		return m_camera.position.distanceSquaredTo(worldPos);
	}

private:
	Camera3D m_camera;                         ///< カメラ
	std::vector<Light3D> m_lights;             ///< ライト配列
	RenderQueue3D m_queue;                     ///< レンダーキュー
	DepthState m_depthState{DepthState::ReadWrite}; ///< 深度バッファ状態
	bool m_frameActive = false;                ///< フレームアクティブフラグ
};

} // namespace sgc::graphics3d
