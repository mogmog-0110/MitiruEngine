#pragma once

/// @file Camera3D.hpp
/// @brief 3Dカメラ
/// @details 3D描画用の透視投影カメラ。ビュー行列・射影行列を生成する。

#include <cmath>

#include <sgc/math/Vec3.hpp>
#include <sgc/math/Mat4.hpp>

namespace mitiru::render
{

/// @brief 3Dカメラ
/// @details 位置・注視点・上方向からビュー行列を、
///          FOV・ニア/ファーから射影行列を生成する。
///
/// @code
/// mitiru::render::Camera3D camera;
/// camera.setPosition({0, 5, -10});
/// camera.setTarget({0, 0, 0});
/// auto vp = camera.projectionMatrix() * camera.viewMatrix();
/// @endcode
class Camera3D
{
public:
	/// @brief デフォルトコンストラクタ
	Camera3D() noexcept = default;

	/// @brief パラメータ指定コンストラクタ
	/// @param position カメラ位置
	/// @param target 注視点
	/// @param up 上方向ベクトル
	/// @param fov 垂直視野角（ラジアン）
	/// @param aspectRatio アスペクト比（幅/高さ）
	/// @param nearClip ニアクリップ面
	/// @param farClip ファークリップ面
	Camera3D(const sgc::Vec3f& position,
	         const sgc::Vec3f& target,
	         const sgc::Vec3f& up,
	         float fov,
	         float aspectRatio,
	         float nearClip,
	         float farClip) noexcept
		: m_position(position)
		, m_target(target)
		, m_up(up)
		, m_fov(fov)
		, m_aspectRatio(aspectRatio)
		, m_nearClip(nearClip)
		, m_farClip(farClip)
	{
	}

	/// @brief カメラ位置を設定する
	void setPosition(const sgc::Vec3f& position) noexcept
	{
		m_position = position;
	}

	/// @brief カメラ位置を取得する
	[[nodiscard]] const sgc::Vec3f& position() const noexcept
	{
		return m_position;
	}

	/// @brief 注視点を設定する
	void setTarget(const sgc::Vec3f& target) noexcept
	{
		m_target = target;
	}

	/// @brief 注視点を取得する
	[[nodiscard]] const sgc::Vec3f& target() const noexcept
	{
		return m_target;
	}

	/// @brief 上方向ベクトルを設定する
	void setUp(const sgc::Vec3f& up) noexcept
	{
		m_up = up;
	}

	/// @brief 上方向ベクトルを取得する
	[[nodiscard]] const sgc::Vec3f& up() const noexcept
	{
		return m_up;
	}

	/// @brief 垂直視野角を設定する（ラジアン）
	void setFov(float fov) noexcept
	{
		m_fov = fov;
	}

	/// @brief 垂直視野角を取得する
	[[nodiscard]] float fov() const noexcept
	{
		return m_fov;
	}

	/// @brief アスペクト比を設定する
	void setAspectRatio(float ratio) noexcept
	{
		m_aspectRatio = ratio;
	}

	/// @brief アスペクト比を取得する
	[[nodiscard]] float aspectRatio() const noexcept
	{
		return m_aspectRatio;
	}

	/// @brief ニアクリップ面を設定する
	void setNearClip(float nearClip) noexcept
	{
		m_nearClip = nearClip;
	}

	/// @brief ニアクリップ面を取得する
	[[nodiscard]] float nearClip() const noexcept
	{
		return m_nearClip;
	}

	/// @brief ファークリップ面を設定する
	void setFarClip(float farClip) noexcept
	{
		m_farClip = farClip;
	}

	/// @brief ファークリップ面を取得する
	[[nodiscard]] float farClip() const noexcept
	{
		return m_farClip;
	}

	/// @brief 正射影フラグを設定する
	void setOrthographic(bool ortho) noexcept
	{
		m_isOrthographic = ortho;
	}

	/// @brief 正射影フラグを取得する
	[[nodiscard]] bool isOrthographic() const noexcept
	{
		return m_isOrthographic;
	}

	/// @brief 正射影パラメータを設定する
	/// @param left 左端
	/// @param right 右端
	/// @param bottom 下端
	/// @param top 上端
	void setOrthoParams(float left, float right, float bottom, float top) noexcept
	{
		m_orthoLeft = left;
		m_orthoRight = right;
		m_orthoBottom = bottom;
		m_orthoTop = top;
	}

	/// @brief ビュー行列を計算する
	/// @return ビュー変換行列
	[[nodiscard]] sgc::Mat4f viewMatrix() const noexcept
	{
		return sgc::Mat4f::lookAt(m_position, m_target, m_up);
	}

	/// @brief 射影行列を計算する
	/// @return 透視投影行列または正射影行列
	[[nodiscard]] sgc::Mat4f projectionMatrix() const noexcept
	{
		if (m_isOrthographic)
		{
			return sgc::Mat4f::orthographic(
				m_orthoLeft, m_orthoRight,
				m_orthoBottom, m_orthoTop,
				m_nearClip, m_farClip);
		}
		return sgc::Mat4f::perspective(m_fov, m_aspectRatio, m_nearClip, m_farClip);
	}

	/// @brief ビュー×射影行列を計算する
	/// @return ビュー射影合成行列
	[[nodiscard]] sgc::Mat4f viewProjectionMatrix() const noexcept
	{
		return projectionMatrix() * viewMatrix();
	}

	// ── スクリーン座標変換 ─────────────────────────────────

	/// @brief ワールド座標をスクリーン座標に射影する
	/// @param worldPos ワールド座標
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @return スクリーン座標 (x, y) — 左上原点。z はNDC深度値。
	///         カメラ背面の点は (-1, -1, -1) を返す。
	[[nodiscard]] sgc::Vec3f worldToScreen(const sgc::Vec3f& worldPos,
	                                       float screenWidth,
	                                       float screenHeight) const noexcept
	{
		const auto vp = viewProjectionMatrix();

		/// 4次元同次座標に変換する（行優先行列 * 列ベクトル）
		const float x = worldPos.x, y = worldPos.y, z = worldPos.z;
		const float cx = vp.m[0][0]*x + vp.m[0][1]*y + vp.m[0][2]*z + vp.m[0][3];
		const float cy = vp.m[1][0]*x + vp.m[1][1]*y + vp.m[1][2]*z + vp.m[1][3];
		const float cz = vp.m[2][0]*x + vp.m[2][1]*y + vp.m[2][2]*z + vp.m[2][3];
		const float cw = vp.m[3][0]*x + vp.m[3][1]*y + vp.m[3][2]*z + vp.m[3][3];

		/// カメラ背面チェック
		if (std::abs(cw) < 0.0001f)
		{
			return {-1.0f, -1.0f, -1.0f};
		}

		/// 透視除算 → NDC [-1, 1]
		const float ndcX = cx / cw;
		const float ndcY = cy / cw;
		const float ndcZ = cz / cw;

		/// NDC からスクリーン座標へ変換する（左上原点）
		const float sx = (ndcX * 0.5f + 0.5f) * screenWidth;
		const float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * screenHeight;

		return {sx, sy, ndcZ};
	}

	/// @brief ワールド座標がカメラの前にあるかを判定する
	/// @param worldPos ワールド座標
	/// @return カメラの前方にある場合 true
	[[nodiscard]] bool isInFront(const sgc::Vec3f& worldPos) const noexcept
	{
		const auto vp = viewProjectionMatrix();
		const float cw = vp.m[3][0]*worldPos.x
		               + vp.m[3][1]*worldPos.y
		               + vp.m[3][2]*worldPos.z
		               + vp.m[3][3];
		return cw > 0.0f;
	}

	// ── 方向ベクトル取得 ──────────────────────────────────

	/// @brief カメラの前方向ベクトルを取得する（正規化済み）
	/// @return 注視点方向の単位ベクトル
	[[nodiscard]] sgc::Vec3f forwardDirection() const noexcept
	{
		return (m_target - m_position).normalized();
	}

	/// @brief カメラの右方向ベクトルを取得する（正規化済み）
	/// @return カメラのローカル右方向の単位ベクトル
	[[nodiscard]] sgc::Vec3f rightDirection() const noexcept
	{
		return forwardDirection().cross(m_up).normalized();
	}

	/// @brief カメラの上方向ベクトルを取得する（正規化済み）
	/// @return カメラのローカル上方向の単位ベクトル
	[[nodiscard]] sgc::Vec3f upDirection() const noexcept
	{
		const auto fwd = forwardDirection();
		const auto right = fwd.cross(m_up).normalized();
		return right.cross(fwd).normalized();
	}

	// ── カメラ操作ヘルパー ────────────────────────────────

	/// @brief 注視点を中心としたオービットカメラを設定する
	/// @param orbitTarget オービット中心点
	/// @param yawRadians ヨー角（Y軸回転、ラジアン）
	/// @param pitchRadians ピッチ角（X軸回転、ラジアン、-PI/2〜PI/2）
	/// @param distance 中心点からの距離
	/// @details 球面座標系でカメラ位置を計算し、注視点を設定する。
	void orbitAround(const sgc::Vec3f& orbitTarget,
	                 float yawRadians,
	                 float pitchRadians,
	                 float distance) noexcept
	{
		/// ピッチを-89度〜89度にクランプ（ジンバルロック防止）
		constexpr float MAX_PITCH = 1.553343f;  ///< ~89度
		if (pitchRadians > MAX_PITCH) pitchRadians = MAX_PITCH;
		if (pitchRadians < -MAX_PITCH) pitchRadians = -MAX_PITCH;

		const float cosPitch = std::cos(pitchRadians);
		const float sinPitch = std::sin(pitchRadians);
		const float cosYaw = std::cos(yawRadians);
		const float sinYaw = std::sin(yawRadians);

		/// 球面座標から直交座標へ変換する
		const sgc::Vec3f offset{
			distance * cosPitch * sinYaw,
			distance * sinPitch,
			distance * cosPitch * cosYaw
		};

		m_position = orbitTarget + offset;
		m_target = orbitTarget;
	}

	/// @brief FPSカメラのようにヨー・ピッチから注視方向を設定する
	/// @param yawRadians ヨー角（Y軸回転、ラジアン）
	/// @param pitchRadians ピッチ角（X軸回転、ラジアン、-PI/2〜PI/2）
	/// @details カメラ位置はそのまま、注視点のみを更新する。
	void lookDirection(float yawRadians, float pitchRadians) noexcept
	{
		/// ピッチを-89度〜89度にクランプする
		constexpr float MAX_PITCH = 1.553343f;  ///< ~89度
		if (pitchRadians > MAX_PITCH) pitchRadians = MAX_PITCH;
		if (pitchRadians < -MAX_PITCH) pitchRadians = -MAX_PITCH;

		const float cosPitch = std::cos(pitchRadians);
		const float sinPitch = std::sin(pitchRadians);
		const float cosYaw = std::cos(yawRadians);
		const float sinYaw = std::sin(yawRadians);

		/// 方向ベクトルを計算する
		const sgc::Vec3f direction{
			cosPitch * sinYaw,
			sinPitch,
			cosPitch * cosYaw
		};

		m_target = m_position + direction;
	}

private:
	sgc::Vec3f m_position{0.0f, 0.0f, -5.0f};  ///< カメラ位置
	sgc::Vec3f m_target{0.0f, 0.0f, 0.0f};      ///< 注視点
	sgc::Vec3f m_up{0.0f, 1.0f, 0.0f};          ///< 上方向
	float m_fov = 1.0471975512f;                 ///< 垂直視野角（デフォルト60度）
	float m_aspectRatio = 16.0f / 9.0f;          ///< アスペクト比
	float m_nearClip = 0.1f;                     ///< ニアクリップ面
	float m_farClip = 1000.0f;                   ///< ファークリップ面

	/// 正射影パラメータ
	bool m_isOrthographic = false;               ///< 正射影フラグ
	float m_orthoLeft = -1.0f;                   ///< 正射影左端
	float m_orthoRight = 1.0f;                   ///< 正射影右端
	float m_orthoBottom = -1.0f;                 ///< 正射影下端
	float m_orthoTop = 1.0f;                     ///< 正射影上端
};

} // namespace mitiru::render
