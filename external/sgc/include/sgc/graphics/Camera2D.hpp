#pragma once

/// @file Camera2D.hpp
/// @brief 2Dカメラ（ビューポート変換・追従・シェイク）
///
/// ワールド座標とスクリーン座標の相互変換、スムーズ追従、
/// スクリーンシェイクなどの2Dカメラ機能を提供する。
///
/// @code
/// sgc::graphics::Camera2DConfig cfg{
///     .center = {0.0f, 0.0f},
///     .zoom = 1.0f,
///     .rotation = 0.0f,
///     .screenSize = {800.0f, 600.0f}
/// };
/// sgc::graphics::Camera2D cam(cfg);
/// auto screenPos = cam.worldToScreen({100.0f, 50.0f});
/// @endcode

#include <cmath>
#include <algorithm>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc::graphics
{

/// @brief 2Dカメラの設定
struct Camera2DConfig
{
	sgc::Vec2f center{0.0f, 0.0f};       ///< カメラ中心（ワールド座標）
	float zoom = 1.0f;                     ///< ズーム倍率（>1で拡大）
	float rotation = 0.0f;                 ///< 回転角度（ラジアン）
	sgc::Vec2f screenSize{800.0f, 600.0f}; ///< スクリーンサイズ（ピクセル）
};

/// @brief 可視領域の矩形
struct VisibleRect
{
	sgc::Vec2f min{}; ///< 左上（ワールド座標）
	sgc::Vec2f max{}; ///< 右下（ワールド座標）
};

/// @brief 2Dカメラ
///
/// ワールド座標系とスクリーン座標系の変換を管理する。
/// ズーム・回転・スムーズ追従・スクリーンシェイクをサポート。
class Camera2D
{
public:
	/// @brief コンストラクタ
	/// @param config カメラ設定
	explicit Camera2D(const Camera2DConfig& config = {}) noexcept
		: m_center(config.center)
		, m_zoom(config.zoom)
		, m_rotation(config.rotation)
		, m_screenSize(config.screenSize)
	{}

	// ── アクセサ ──────────────────────────────────────────

	/// @brief カメラ中心を設定する
	/// @param center 新しいカメラ中心（ワールド座標）
	void setCenter(sgc::Vec2f center) noexcept { m_center = center; }

	/// @brief カメラ中心を取得する
	/// @return 現在のカメラ中心（ワールド座標）
	[[nodiscard]] sgc::Vec2f getCenter() const noexcept { return m_center; }

	/// @brief ズーム倍率を設定する
	/// @param zoom ズーム倍率（>1で拡大、<1で縮小）
	void setZoom(float zoom) noexcept { m_zoom = zoom; }

	/// @brief ズーム倍率を取得する
	/// @return 現在のズーム倍率
	[[nodiscard]] float getZoom() const noexcept { return m_zoom; }

	/// @brief 回転角度を設定する
	/// @param rotation 回転角度（ラジアン）
	void setRotation(float rotation) noexcept { m_rotation = rotation; }

	/// @brief 回転角度を取得する
	/// @return 現在の回転角度（ラジアン）
	[[nodiscard]] float getRotation() const noexcept { return m_rotation; }

	/// @brief スクリーンサイズを設定する
	/// @param size スクリーンサイズ（ピクセル）
	void setScreenSize(sgc::Vec2f size) noexcept { m_screenSize = size; }

	/// @brief スクリーンサイズを取得する
	/// @return 現在のスクリーンサイズ
	[[nodiscard]] sgc::Vec2f getScreenSize() const noexcept { return m_screenSize; }

	// ── 座標変換 ──────────────────────────────────────────

	/// @brief ワールド座標をスクリーン座標に変換する
	/// @param worldPos ワールド座標
	/// @return スクリーン座標（ピクセル）
	///
	/// 変換順序: 平行移動（カメラ中心をオフセット）→回転→ズーム→スクリーン中心へ移動
	[[nodiscard]] sgc::Vec2f worldToScreen(sgc::Vec2f worldPos) const noexcept
	{
		const auto effectiveCenter = m_center + m_shakeOffset;

		// カメラ中心基準に移動
		auto p = worldPos - effectiveCenter;

		// 回転（カメラの回転の逆＝-rotation）
		if (m_rotation != 0.0f)
		{
			const float c = std::cos(-m_rotation);
			const float s = std::sin(-m_rotation);
			p = sgc::Vec2f{p.x * c - p.y * s, p.x * s + p.y * c};
		}

		// ズーム適用
		p = p * m_zoom;

		// スクリーン中心へオフセット
		const auto halfScreen = m_screenSize * 0.5f;
		return p + halfScreen;
	}

	/// @brief スクリーン座標をワールド座標に変換する
	/// @param screenPos スクリーン座標（ピクセル）
	/// @return ワールド座標
	///
	/// worldToScreenの逆変換
	[[nodiscard]] sgc::Vec2f screenToWorld(sgc::Vec2f screenPos) const noexcept
	{
		const auto effectiveCenter = m_center + m_shakeOffset;
		const auto halfScreen = m_screenSize * 0.5f;

		// スクリーン中心からのオフセット
		auto p = screenPos - halfScreen;

		// ズーム逆適用
		if (m_zoom != 0.0f)
		{
			p = p / m_zoom;
		}

		// 回転逆変換（+rotation）
		if (m_rotation != 0.0f)
		{
			const float c = std::cos(m_rotation);
			const float s = std::sin(m_rotation);
			p = sgc::Vec2f{p.x * c - p.y * s, p.x * s + p.y * c};
		}

		// カメラ中心を加算してワールド座標に戻す
		return p + effectiveCenter;
	}

	/// @brief 2Dビュー行列を取得する
	/// @return 4x4ビュー行列（平行移動＋回転＋スケール）
	///
	/// 行優先Mat4fとして、2D変換を表現する。
	/// 列: スクリーン座標 = ViewMatrix * ワールド座標(x,y,0,1)
	[[nodiscard]] sgc::Mat4f getViewMatrix() const noexcept
	{
		const auto effectiveCenter = m_center + m_shakeOffset;
		const auto halfScreen = m_screenSize * 0.5f;

		// T(halfScreen) * S(zoom) * R(-rotation) * T(-center)
		const float c = std::cos(-m_rotation);
		const float s = std::sin(-m_rotation);

		// R * T(-center) を合成
		// R(-rot) = [c -s; s c]
		// 平行移動成分: R * (-center) = (-center.x*c + center.y*s, -center.x*s - center.y*c)
		const float tx = -effectiveCenter.x * c + effectiveCenter.y * s;
		const float ty = -effectiveCenter.x * s - effectiveCenter.y * c;

		// S(zoom) * 上記
		const float stx = tx * m_zoom;
		const float sty = ty * m_zoom;

		// T(halfScreen) を加算
		return sgc::Mat4f{
			m_zoom * c,  -m_zoom * s, 0.0f, stx + halfScreen.x,
			m_zoom * s,   m_zoom * c, 0.0f, sty + halfScreen.y,
			0.0f,         0.0f,       1.0f, 0.0f,
			0.0f,         0.0f,       0.0f, 1.0f
		};
	}

	/// @brief 可視領域（ワールド座標）を取得する
	/// @return 可視矩形
	///
	/// 回転なしの場合はカメラ中心を基準にした矩形を返す。
	/// 回転ありの場合はAABBバウンディングボックスを返す。
	[[nodiscard]] VisibleRect getVisibleRect() const noexcept
	{
		const auto effectiveCenter = m_center + m_shakeOffset;
		const auto halfWorld = m_screenSize * 0.5f / m_zoom;

		if (m_rotation == 0.0f)
		{
			return {
				effectiveCenter - halfWorld,
				effectiveCenter + halfWorld
			};
		}

		// 回転がある場合: 四隅を逆変換してAABBを計算
		const sgc::Vec2f corners[4] = {
			screenToWorld({0.0f, 0.0f}),
			screenToWorld({m_screenSize.x, 0.0f}),
			screenToWorld({0.0f, m_screenSize.y}),
			screenToWorld(m_screenSize)
		};

		auto minP = corners[0];
		auto maxP = corners[0];
		for (int i = 1; i < 4; ++i)
		{
			minP = sgc::Vec2f::min(minP, corners[i]);
			maxP = sgc::Vec2f::max(maxP, corners[i]);
		}
		return {minP, maxP};
	}

	// ── カメラ制御 ──────────────────────────────────────────

	/// @brief ターゲットをスムーズに追従する
	/// @param target 追従対象のワールド座標
	/// @param smoothing スムージング係数（0=即座に追従、値が大きいほど滑らか）
	/// @param dt デルタタイム（秒）
	void follow(sgc::Vec2f target, float smoothing, float dt) noexcept
	{
		if (smoothing <= 0.0f)
		{
			m_center = target;
			return;
		}

		// 指数減衰による追従
		const float factor = 1.0f - std::exp(-smoothing * dt);
		m_center = m_center.lerp(target, factor);
	}

	/// @brief スクリーンシェイクを開始する
	/// @param intensity シェイクの強度（ピクセル単位のワールド座標オフセット）
	/// @param duration シェイクの持続時間（秒）
	void shake(float intensity, float duration) noexcept
	{
		m_shakeIntensity = intensity;
		m_shakeDuration = duration;
		m_shakeTimer = duration;
	}

	/// @brief カメラ状態を更新する（シェイクの減衰など）
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		if (m_shakeTimer > 0.0f)
		{
			m_shakeTimer -= dt;
			if (m_shakeTimer <= 0.0f)
			{
				m_shakeTimer = 0.0f;
				m_shakeOffset = sgc::Vec2f::zero();
				return;
			}

			// 残り時間に比例してシェイク強度を減衰
			const float ratio = m_shakeTimer / m_shakeDuration;
			const float currentIntensity = m_shakeIntensity * ratio;

			// 簡易的な疑似乱数オフセット（sin関数ベース）
			const float t = m_shakeTimer * 73.0f;
			m_shakeOffset = sgc::Vec2f{
				std::sin(t * 13.37f) * currentIntensity,
				std::cos(t * 17.31f) * currentIntensity
			};
		}
		else
		{
			m_shakeOffset = sgc::Vec2f::zero();
		}
	}

	/// @brief 現在のシェイクオフセットを取得する（テスト用）
	/// @return シェイクによるオフセット
	[[nodiscard]] sgc::Vec2f getShakeOffset() const noexcept { return m_shakeOffset; }

private:
	sgc::Vec2f m_center{};         ///< カメラ中心（ワールド座標）
	float m_zoom = 1.0f;           ///< ズーム倍率
	float m_rotation = 0.0f;       ///< 回転角度（ラジアン）
	sgc::Vec2f m_screenSize{};     ///< スクリーンサイズ

	// シェイク関連
	float m_shakeIntensity = 0.0f; ///< シェイク強度
	float m_shakeDuration = 0.0f;  ///< シェイク持続時間
	float m_shakeTimer = 0.0f;     ///< シェイク残り時間
	sgc::Vec2f m_shakeOffset{};    ///< 現在のシェイクオフセット
};

} // namespace sgc::graphics
