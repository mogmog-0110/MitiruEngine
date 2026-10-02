#pragma once

/// @file SpatialAudio.hpp
/// @brief 3D オーディオのスペーシャリゼーション
/// @details リスナーとソースの位置・速度から、距離減衰・パン・ドップラーを計算する。
///          音を鳴らす側 (SpatialRenderer 等) へ渡す係数を作るだけで、ゲーム状態には書き戻さない。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::audio
{

/// @brief 3D 座標
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

	[[nodiscard]] float dot(const AudioVec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }

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

/// @brief 3D オーディオのリスナー
struct AudioListener
{
	AudioVec3 position = {0, 0, 0};
	AudioVec3 forward = {0, 0, -1};
	AudioVec3 up = {0, 1, 0};
	AudioVec3 velocity = {0, 0, 0}; ///< 単位/秒。ドップラーにだけ使う
};

/// @brief 3D オーディオのソース設定
struct SpatialSourceConfig
{
	float minDistance = 1.0f;    ///< 減衰開始距離
	float maxDistance = 100.0f;  ///< 減衰終了距離（これ以上は無音）
	float rolloffFactor = 1.0f; ///< 減衰係数
	float dopplerFactor = 0.0f; ///< ドップラー効果係数（0で無効）
	float speedOfSound = 343.3f; ///< 音速 (単位/秒)。ワールドの 1 単位 = 1m のとき空気中の値
	AttenuationModel attenuation = AttenuationModel::InverseDistance;
};

/// @brief スペーシャリゼーションの計算結果
struct SpatialResult
{
	float volume = 1.0f;   ///< 距離減衰後のボリューム (0-1)
	float pan = 0.0f;      ///< ステレオパン (-1=左, 0=中央, 1=右)
	float doppler = 1.0f;  ///< ドップラーピッチ倍率
};

/// @brief 3D オーディオのスペーシャリゼーション計算
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

	/// @param sourceVelocity ソースの速度 (単位/秒)。ドップラーにだけ使う
	[[nodiscard]] SpatialResult calculate(
		const AudioVec3& sourcePos,
		const SpatialSourceConfig& config = {},
		const AudioVec3& sourceVelocity = {}) const noexcept
	{
		SpatialResult result;
		const float dist = m_listener.position.distanceTo(sourcePos);
		result.volume = calculateAttenuation(dist, config);
		result.pan = calculatePan(sourcePos);
		result.doppler = calculateDoppler(sourcePos, sourceVelocity, config);
		return result;
	}

	/// @brief ソースの向きをリスナー空間の単位ベクトルに変換する (+x 右、+y 上、-z 前)
	/// @details ISpatialRenderer::process の direction。ソースがリスナーと重なっていれば正面。
	[[nodiscard]] AudioVec3 toListenerSpace(const AudioVec3& sourcePos) const noexcept
	{
		const AudioVec3 dir = AudioVec3{sourcePos.x - m_listener.position.x,
		                                sourcePos.y - m_listener.position.y,
		                                sourcePos.z - m_listener.position.z}.normalized();
		if (dir.dot(dir) == 0.0f) { return {0, 0, -1}; }
		const AudioVec3 fwd = m_listener.forward.normalized();
		const AudioVec3 right = cross(fwd, m_listener.up).normalized();
		const AudioVec3 up = cross(right, fwd);
		return {dir.dot(right), dir.dot(up), -dir.dot(fwd)};
	}

	/// @brief ドップラーで速度を制限する割合 (音速比)。ピッチ倍率は [1/3, 3] に収まる
	/// @details 音速に近づくと分母が 0 に近づいて倍率が発散し、1 フレームの位置の飛びによって
	///          耳を傷めるほどの音が出る。音速の半分に制限する。
	static constexpr float kDopplerVelocityLimit = 0.5f;

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

	[[nodiscard]] static AudioVec3 cross(const AudioVec3& a, const AudioVec3& b) noexcept
	{
		return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	}

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
			// 1/d 減衰: volume = minDist / (minDist + rolloff * (dist - minDist))
			const float denom = config.minDistance
				+ config.rolloffFactor * (distance - config.minDistance);
			return (denom > 0.0f) ? (config.minDistance / denom) : 0.0f;
		}

		case AttenuationModel::LinearDistance:
		{
			// 線形減衰: volume = 1 - rolloff * (dist - minDist) / (maxDist - minDist)
			const float range = config.maxDistance - config.minDistance;
			if (range <= 0.0f) { return 0.0f; }
			return 1.0f - config.rolloffFactor * (distance - config.minDistance) / range;
		}

		case AttenuationModel::ExponentialDistance:
		{
			// 指数減衰: volume = (dist / minDist) ^ (-rolloff)
			if (config.minDistance <= 0.0f) { return 0.0f; }
			return std::pow(distance / config.minDistance, -config.rolloffFactor);
		}
		}

		return 1.0f;
	}

	/// @details OpenAL 1.1 の式。SL = リスナー - ソース方向の速度成分を見る。
	///          ソースが近づく (vss > 0) か、リスナーが近づく (vls < 0) とピッチが上がる。
	[[nodiscard]] float calculateDoppler(const AudioVec3& sourcePos, const AudioVec3& sourceVelocity,
	                                     const SpatialSourceConfig& config) const noexcept
	{
		if (config.dopplerFactor <= 0.0f || config.speedOfSound <= 0.0f) { return 1.0f; }
		const AudioVec3 sl = {m_listener.position.x - sourcePos.x,
		                      m_listener.position.y - sourcePos.y,
		                      m_listener.position.z - sourcePos.z};
		const AudioVec3 dir = sl.normalized();
		if (dir.dot(dir) == 0.0f) { return 1.0f; }
		const float c = config.speedOfSound;
		const float limit = kDopplerVelocityLimit * c / config.dopplerFactor;
		const float vls = std::clamp(dir.dot(m_listener.velocity), -limit, limit);
		const float vss = std::clamp(dir.dot(sourceVelocity), -limit, limit);
		return (c - config.dopplerFactor * vls) / (c - config.dopplerFactor * vss);
	}

	[[nodiscard]] float calculatePan(const AudioVec3& sourcePos) const noexcept
	{
		return std::clamp(toListenerSpace(sourcePos).x, -1.0f, 1.0f);
	}
};

} // namespace mitiru::audio
