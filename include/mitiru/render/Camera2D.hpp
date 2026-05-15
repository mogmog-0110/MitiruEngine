#pragma once

/// @file Camera2D.hpp
/// @brief 2Dカメラ
/// @details 2D描画用のカメラ。位置・ズーム・回転・スクリーンシェイクを管理する。

#include <algorithm>
#include <cmath>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Mat4.hpp>

namespace mitiru::render
{

/// @brief 2Dカメラ
/// @details ビュー変換行列を生成し、スクリーン座標とワールド座標の変換を行う。
///
/// @code
/// mitiru::render::Camera2D camera;
/// camera.setPosition({400.0f, 300.0f});
/// camera.setZoom(2.0f);
/// auto worldPos = camera.screenToWorld({mouseX, mouseY});
/// @endcode
class Camera2D
{
public:
	/// @brief デフォルトコンストラクタ
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	explicit Camera2D(float screenWidth = 1280.0f,
	                  float screenHeight = 720.0f) noexcept
		: m_screenWidth(screenWidth)
		, m_screenHeight(screenHeight)
	{
	}

	/// @brief カメラ位置を設定する
	/// @param position ワールド座標でのカメラ位置
	void setPosition(const sgc::Vec2f& position) noexcept
	{
		m_position = position;
	}

	/// @brief カメラ位置を取得する
	[[nodiscard]] const sgc::Vec2f& position() const noexcept
	{
		return m_position;
	}

	/// @brief ズーム倍率を設定する
	/// @param zoom ズーム倍率（1.0で等倍）
	void setZoom(float zoom) noexcept
	{
		m_zoom = std::max(zoom, 0.001f);
	}

	/// @brief ズーム倍率を取得する
	[[nodiscard]] float zoom() const noexcept
	{
		return m_zoom;
	}

	/// @brief 回転角度を設定する
	/// @param radians 回転角度（ラジアン）
	void setRotation(float radians) noexcept
	{
		m_rotation = radians;
	}

	/// @brief 回転角度を取得する
	[[nodiscard]] float rotation() const noexcept
	{
		return m_rotation;
	}

	/// @brief ビュー行列を計算する
	/// @return 2D用のビュー変換行列（4x4）
	[[nodiscard]] sgc::Mat4f viewMatrix() const noexcept
	{
		/// シェイクオフセットを加味した実効位置
		const auto effectivePos = m_position + m_shakeOffset;

		/// カメラ変換: 平行移動 → 回転 → スケール → スクリーン中心移動
		const auto translate = sgc::Mat4f::translation(
			{-effectivePos.x, -effectivePos.y, 0.0f});
		const auto rotate = sgc::Mat4f::rotationZ(-m_rotation);
		const auto scale = sgc::Mat4f::scaling(
			{m_zoom, m_zoom, 1.0f});
		const auto center = sgc::Mat4f::translation(
			{m_screenWidth * 0.5f, m_screenHeight * 0.5f, 0.0f});

		return center * scale * rotate * translate;
	}

	/// @brief スクリーン座標をワールド座標に変換する
	/// @param screenPos スクリーン座標
	/// @return ワールド座標
	[[nodiscard]] sgc::Vec2f screenToWorld(const sgc::Vec2f& screenPos) const noexcept
	{
		/// ビュー行列の逆行列で変換する
		const auto invView = viewMatrix().inversed();
		const auto result = invView.transformPoint(
			{screenPos.x, screenPos.y, 0.0f});
		return {result.x, result.y};
	}

	/// @brief ワールド座標をスクリーン座標に変換する
	/// @param worldPos ワールド座標
	/// @return スクリーン座標
	[[nodiscard]] sgc::Vec2f worldToScreen(const sgc::Vec2f& worldPos) const noexcept
	{
		const auto result = viewMatrix().transformPoint(
			{worldPos.x, worldPos.y, 0.0f});
		return {result.x, result.y};
	}

	/// @brief スクリーンシェイクを開始する
	/// @param intensity シェイクの強度（ピクセル）
	/// @param duration 持続時間（秒）
	void shake(float intensity, float duration) noexcept
	{
		m_shakeIntensity = intensity;
		m_shakeDuration = duration;
		m_shakeTimer = duration;
	}

	/// @brief シェイク状態を更新する
	/// @param deltaTime 経過時間（秒）
	void updateShake(float deltaTime) noexcept
	{
		if (m_shakeTimer <= 0.0f)
		{
			m_shakeOffset = {};
			return;
		}

		m_shakeTimer -= deltaTime;
		const float ratio = std::max(m_shakeTimer / m_shakeDuration, 0.0f);
		const float currentIntensity = m_shakeIntensity * ratio;

		/// 簡易的な疑似ランダムシェイク（三角関数ベース）
		const float t = m_shakeDuration - m_shakeTimer;
		m_shakeOffset =
		{
			currentIntensity * std::sin(t * 37.0f),
			currentIntensity * std::cos(t * 53.0f)
		};
	}

	/// @brief スクリーンサイズを設定する
	/// @param width スクリーン幅
	/// @param height スクリーン高さ
	void setScreenSize(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

private:
	sgc::Vec2f m_position{};     ///< カメラ位置（ワールド座標）
	float m_zoom = 1.0f;         ///< ズーム倍率
	float m_rotation = 0.0f;     ///< 回転角度（ラジアン）
	float m_screenWidth;         ///< スクリーン幅
	float m_screenHeight;        ///< スクリーン高さ

	/// @brief シェイク関連パラメータ
	float m_shakeIntensity = 0.0f;  ///< シェイク強度
	float m_shakeDuration = 0.0f;   ///< シェイク持続時間
	float m_shakeTimer = 0.0f;      ///< シェイク残り時間
	sgc::Vec2f m_shakeOffset{};     ///< 現在のシェイクオフセット
};

} // namespace mitiru::render
