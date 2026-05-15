#pragma once

/// @file SpatialAudio.hpp
/// @brief 3Dオーディオスペーシャリゼーション
/// @details リスナー位置とソース位置から距離減衰・パンニングを計算する。
///          miniaudioのスペーシャリゼーション機能のラッパー。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::audio
{

/// @brief 3D座標
struct AudioVec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	[[nodiscard]] float distanceTo(const AudioVec3& other) const noexcept
	{
		const float dx = x - other.x;
		const float dy = y - other.y;
		const float dz = z - other.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	[[nodiscard]] AudioVec3 normalized() const noexcept
	{
		const float len = std::sqrt(x * x + y * y + z * z);
		if (len < 1e-6f) { return {0, 0, 0}; }
		return {x / len, y / len, z / len};
	}
};

/// @brief 減衰モデル
enum class AttenuationModel : uint8_t
{
	None,           ///< 減衰なし
	InverseDistance, ///< 1/d 減衰
	LinearDistance,  ///< 線形減衰
	ExponentialDistance, ///< 指数減衰
};

/// @brief 3Dオーディオリスナー
struct AudioListener
{
	AudioVec3 position = {0, 0, 0};
	AudioVec3 forward = {0, 0, -1};
	AudioVec3 up = {0, 1, 0};
};

/// @brief 3Dオーディオソース設定
struct SpatialSourceConfig
{
	float minDistance = 1.0f;    ///< 減衰開始距離
	float maxDistance = 100.0f;  ///< 減衰終了距離（これ以上は無音）
	float rolloffFactor = 1.0f; ///< 減衰係数
	float dopplerFactor = 0.0f; ///< ドップラー効果係数（0で無効）
	AttenuationModel attenuation = AttenuationModel::InverseDistance;
};

/// @brief スペーシャリゼーション計算結果
struct SpatialResult
{
	float volume = 1.0f;   ///< 距離減衰後のボリューム (0-1)
	float pan = 0.0f;      ///< ステレオパン (-1=左, 0=中央, 1=右)
	float doppler = 1.0f;  ///< ドップラーピッチ倍率
};

/// @brief 3Dオーディオスペーシャリゼーション計算器
class SpatialAudio
{
public:
	/// @brief リスナーを設定する
	void setListener(const AudioListener& listener) noexcept
	{
		m_listener = listener;
	}

	/// @brief リスナーを取得する
	[[nodiscard]] const AudioListener& listener() const noexcept { return m_listener; }

	/// @brief ソース位置からスペーシャリゼーション結果を計算する
	/// @param sourcePos ソースのワールド位置
	/// @param config ソース設定
	/// @return 計算結果（ボリューム、パン、ドップラー）
	[[nodiscard]] SpatialResult calculate(
		const AudioVec3& sourcePos,
		const SpatialSourceConfig& config = {}) const noexcept
	{
		SpatialResult result;

		const float dist = m_listener.position.distanceTo(sourcePos);

		// ── 距離減衰 ──
		result.volume = calculateAttenuation(dist, config);

		// ── ステレオパン ──
		result.pan = calculatePan(sourcePos);

		// ── ドップラー効果（将来拡張: 前フレームの位置が必要）──
		result.doppler = 1.0f;

		return result;
	}

	/// @brief カメラのトランスフォームからリスナーを更新する
	/// @param pos カメラ位置 float[3]
	/// @param fwd カメラ前方ベクトル float[3]
	/// @param up  カメラ上方ベクトル float[3]
	void updateFromCamera(const float pos[3], const float fwd[3], const float up[3]) noexcept
	{
		m_listener.position = {pos[0], pos[1], pos[2]};
		m_listener.forward = {fwd[0], fwd[1], fwd[2]};
		m_listener.up = {up[0], up[1], up[2]};
	}

private:
	AudioListener m_listener;

	[[nodiscard]] float calculateAttenuation(
		float distance, const SpatialSourceConfig& config) const noexcept
	{
		if (distance <= config.minDistance) { return 1.0f; }
		if (distance >= config.maxDistance) { return 0.0f; }

		switch (config.attenuation)
		{
		case AttenuationModel::None:
			return 1.0f;

		case AttenuationModel::InverseDistance:
		{
			// 1/d model: volume = minDist / (minDist + rolloff * (dist - minDist))
			const float denom = config.minDistance
				+ config.rolloffFactor * (distance - config.minDistance);
			return (denom > 0.0f) ? (config.minDistance / denom) : 0.0f;
		}

		case AttenuationModel::LinearDistance:
		{
			// Linear: volume = 1 - rolloff * (dist - minDist) / (maxDist - minDist)
			const float range = config.maxDistance - config.minDistance;
			if (range <= 0.0f) { return 0.0f; }
			return 1.0f - config.rolloffFactor * (distance - config.minDistance) / range;
		}

		case AttenuationModel::ExponentialDistance:
		{
			// Exponential: volume = (dist / minDist) ^ (-rolloff)
			if (config.minDistance <= 0.0f) { return 0.0f; }
			return std::pow(distance / config.minDistance, -config.rolloffFactor);
		}
		}

		return 1.0f;
	}

	[[nodiscard]] float calculatePan(const AudioVec3& sourcePos) const noexcept
	{
		// リスナーからソースへの方向ベクトル
		const AudioVec3 toSource = {
			sourcePos.x - m_listener.position.x,
			sourcePos.y - m_listener.position.y,
			sourcePos.z - m_listener.position.z,
		};

		const AudioVec3 dir = toSource.normalized();

		// リスナーの右方向ベクトルを計算 (forward × up)
		const AudioVec3 right = {
			m_listener.forward.y * m_listener.up.z - m_listener.forward.z * m_listener.up.y,
			m_listener.forward.z * m_listener.up.x - m_listener.forward.x * m_listener.up.z,
			m_listener.forward.x * m_listener.up.y - m_listener.forward.y * m_listener.up.x,
		};

		// ソース方向と右方向の内積 = パン値
		const float dot = dir.x * right.x + dir.y * right.y + dir.z * right.z;
		return std::clamp(dot, -1.0f, 1.0f);
	}
};

} // namespace mitiru::audio
