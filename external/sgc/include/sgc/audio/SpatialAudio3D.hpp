#pragma once

/// @file SpatialAudio3D.hpp
/// @brief 3D空間音響システム
///
/// リスナーと複数音源の管理、距離減衰モデルの選択、
/// パンニング・ドップラー効果・ミキシングを統合的に提供する。
///
/// @code
/// sgc::audio::SpatialAudioMixer mixer;
/// mixer.setListener({{0,0,0}, {0,0,-1}, {0,1,0}});
/// uint32_t id = mixer.addSource({{10,0,0}, {0,0,0}});
/// auto result = mixer.calculate(id, AttenuationModel::InverseDistance, 1.0f, 100.0f);
/// // result.volume, result.pan, result.dopplerShift
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"

namespace sgc::audio
{

/// @brief 3Dリスナー（聴取者）
struct Listener
{
	Vec3f position{0, 0, 0};   ///< リスナー位置
	Vec3f forward{0, 0, -1};   ///< 前方ベクトル（正規化済み）
	Vec3f up{0, 1, 0};         ///< 上方ベクトル（正規化済み）
	Vec3f velocity{0, 0, 0};   ///< リスナー速度（ドップラー用）
};

/// @brief 空間音源
struct SpatialSource
{
	Vec3f position{0, 0, 0};   ///< 音源位置
	Vec3f velocity{0, 0, 0};   ///< 音源速度（ドップラー用）
	uint32_t sourceId = 0;     ///< 音源ID
	float baseVolume = 1.0f;   ///< 基本音量 [0.0, 1.0]
	bool active = true;        ///< アクティブフラグ
};

/// @brief 距離減衰モデル
enum class AttenuationModel
{
	Linear,            ///< 線形減衰: 1 - (dist - min) / (max - min)
	InverseDistance,   ///< 逆距離減衰: min / (min + rolloff * (dist - min))
	ExponentialDecay   ///< 指数減衰: exp(-rolloff * (dist - min) / (max - min))
};

/// @brief 空間音響計算結果
struct SpatialResult
{
	float volume = 0.0f;        ///< 最終音量 [0.0, 1.0]
	float pan = 0.0f;           ///< パン値 [-1.0, 1.0]
	float dopplerShift = 1.0f;  ///< ドップラーシフト倍率
};

/// @brief 距離減衰を計算する
///
/// 指定モデルに基づいて距離減衰値を算出する。
///
/// @param distance リスナーと音源の距離
/// @param minDist 最小距離（これ以下で最大音量）
/// @param maxDist 最大距離（これ以上で無音）
/// @param rolloff 減衰係数（Linear以外で使用、デフォルト1.0）
/// @param model 減衰モデル
/// @return 減衰値 [0.0, 1.0]
[[nodiscard]] inline float calculateVolume(
	float distance,
	float minDist,
	float maxDist,
	float rolloff,
	AttenuationModel model)
{
	if (distance <= minDist) return 1.0f;
	if (distance >= maxDist) return 0.0f;

	switch (model)
	{
	case AttenuationModel::Linear:
	{
		const float range = maxDist - minDist;
		if (range < 1e-6f) return 1.0f;
		return 1.0f - (distance - minDist) / range;
	}
	case AttenuationModel::InverseDistance:
	{
		return minDist / (minDist + rolloff * (distance - minDist));
	}
	case AttenuationModel::ExponentialDecay:
	{
		const float range = maxDist - minDist;
		if (range < 1e-6f) return 1.0f;
		return std::exp(-rolloff * (distance - minDist) / range);
	}
	}
	return 0.0f;
}

/// @brief リスナーと音源からパン値を計算する
///
/// リスナーの右方向ベクトルとの内積でパン値を算出。
///
/// @param listener リスナー
/// @param sourcePos 音源位置
/// @return パン値 [-1.0（左）, 1.0（右）]
[[nodiscard]] inline float calculatePan(
	const Listener& listener,
	const Vec3f& sourcePos)
{
	const Vec3f toSource = sourcePos - listener.position;
	const float dist = toSource.length();
	if (dist < 1e-6f) return 0.0f;

	const Vec3f dir = toSource / dist;
	/// 右方向 = forward × up
	const Vec3f right = listener.forward.cross(listener.up).normalized();
	return std::clamp(dir.dot(right), -1.0f, 1.0f);
}

/// @brief ドップラー効果の周波数シフトを計算する
///
/// @param listener リスナー
/// @param source 空間音源
/// @param speedOfSound 音速（m/s、デフォルト343.0）
/// @return 周波数倍率（1.0が基準）
[[nodiscard]] inline float calculateDoppler(
	const Listener& listener,
	const SpatialSource& source,
	float speedOfSound = 343.0f)
{
	const Vec3f toSource = source.position - listener.position;
	const float dist = toSource.length();
	if (dist < 1e-6f) return 1.0f;

	const Vec3f dir = toSource / dist;
	const float vListener = listener.velocity.dot(dir);
	const float vSource = source.velocity.dot(dir);

	const float denom = speedOfSound + vSource;
	if (std::abs(denom) < 1e-6f) return 1.0f;

	return std::clamp((speedOfSound + vListener) / denom, 0.1f, 10.0f);
}

/// @brief 音源の空間音響パラメータを一括計算する
///
/// @param listener リスナー
/// @param source 空間音源
/// @param model 減衰モデル
/// @param minDist 最小距離
/// @param maxDist 最大距離
/// @param rolloff 減衰係数
/// @param speedOfSound 音速
/// @return 空間音響計算結果
[[nodiscard]] inline SpatialResult calculateSpatial(
	const Listener& listener,
	const SpatialSource& source,
	AttenuationModel model,
	float minDist,
	float maxDist,
	float rolloff = 1.0f,
	float speedOfSound = 343.0f)
{
	const float dist = listener.position.distanceTo(source.position);
	return SpatialResult{
		.volume = source.baseVolume * calculateVolume(dist, minDist, maxDist, rolloff, model),
		.pan = calculatePan(listener, source.position),
		.dopplerShift = calculateDoppler(listener, source, speedOfSound)
	};
}

/// @brief 複数音源を管理する空間音響ミキサー
///
/// リスナーと音源の登録・削除・更新を行い、
/// 各音源の空間音響パラメータを一括計算する。
class SpatialAudioMixer
{
public:
	/// @brief リスナーを設定する
	/// @param listener リスナーパラメータ
	void setListener(const Listener& listener)
	{
		m_listener = listener;
	}

	/// @brief リスナーを取得する
	/// @return 現在のリスナー
	[[nodiscard]] const Listener& getListener() const noexcept
	{
		return m_listener;
	}

	/// @brief 音源を追加する
	/// @param source 空間音源（sourceIdは自動割り当て）
	/// @return 割り当てられた音源ID
	uint32_t addSource(SpatialSource source)
	{
		const uint32_t id = m_nextId++;
		source.sourceId = id;
		m_sources[id] = source;
		return id;
	}

	/// @brief 音源を削除する
	/// @param sourceId 音源ID
	/// @return 削除できたらtrue
	bool removeSource(uint32_t sourceId)
	{
		return m_sources.erase(sourceId) > 0;
	}

	/// @brief 音源を更新する
	/// @param sourceId 音源ID
	/// @param position 新しい位置
	/// @param velocity 新しい速度
	/// @return 更新できたらtrue
	bool updateSource(uint32_t sourceId, const Vec3f& position, const Vec3f& velocity = {})
	{
		auto it = m_sources.find(sourceId);
		if (it == m_sources.end()) return false;
		it->second.position = position;
		it->second.velocity = velocity;
		return true;
	}

	/// @brief 音源を取得する
	/// @param sourceId 音源ID
	/// @return 音源（見つからない場合はnullopt）
	[[nodiscard]] std::optional<SpatialSource> getSource(uint32_t sourceId) const
	{
		auto it = m_sources.find(sourceId);
		if (it == m_sources.end()) return std::nullopt;
		return it->second;
	}

	/// @brief 登録音源数を返す
	[[nodiscard]] std::size_t sourceCount() const noexcept
	{
		return m_sources.size();
	}

	/// @brief 指定音源の空間音響を計算する
	/// @param sourceId 音源ID
	/// @param model 減衰モデル
	/// @param minDist 最小距離
	/// @param maxDist 最大距離
	/// @param rolloff 減衰係数
	/// @return 計算結果（音源が見つからない場合はnullopt）
	[[nodiscard]] std::optional<SpatialResult> calculate(
		uint32_t sourceId,
		AttenuationModel model = AttenuationModel::InverseDistance,
		float minDist = 1.0f,
		float maxDist = 100.0f,
		float rolloff = 1.0f) const
	{
		auto it = m_sources.find(sourceId);
		if (it == m_sources.end()) return std::nullopt;
		if (!it->second.active) return SpatialResult{};
		return calculateSpatial(m_listener, it->second, model, minDist, maxDist, rolloff);
	}

	/// @brief 全アクティブ音源の空間音響を一括計算する
	/// @param model 減衰モデル
	/// @param minDist 最小距離
	/// @param maxDist 最大距離
	/// @param rolloff 減衰係数
	/// @return 音源IDと計算結果のペアリスト
	[[nodiscard]] std::vector<std::pair<uint32_t, SpatialResult>> calculateAll(
		AttenuationModel model = AttenuationModel::InverseDistance,
		float minDist = 1.0f,
		float maxDist = 100.0f,
		float rolloff = 1.0f) const
	{
		std::vector<std::pair<uint32_t, SpatialResult>> results;
		results.reserve(m_sources.size());
		for (const auto& [id, src] : m_sources)
		{
			if (!src.active) continue;
			results.emplace_back(id, calculateSpatial(m_listener, src, model, minDist, maxDist, rolloff));
		}
		return results;
	}

	/// @brief 全音源をクリアする
	void clear()
	{
		m_sources.clear();
	}

private:
	Listener m_listener{};                                ///< リスナー
	std::unordered_map<uint32_t, SpatialSource> m_sources;  ///< 音源マップ
	uint32_t m_nextId = 1;                                ///< 次の音源ID
};

} // namespace sgc::audio
