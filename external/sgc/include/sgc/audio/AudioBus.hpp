#pragma once

/// @file AudioBus.hpp
/// @brief オーディオバス管理システム
///
/// BGM・SE・UI・ボイス等のカテゴリ別音量制御を行うミキサー。
/// 親子関係により、マスターバスの音量変更が全子バスに伝播する。
///
/// @code
/// sgc::audio::AudioBusManager mixer;
/// mixer.setVolume(sgc::audio::BusId::BGM, 0.8f);
/// mixer.setVolume(sgc::audio::BusId::Master, 0.5f);
/// float vol = mixer.getEffectiveVolume(sgc::audio::BusId::BGM); // 0.4f
/// @endcode

#include <algorithm>
#include <array>
#include <cstdint>

namespace sgc::audio
{

/// @brief オーディオバスID
enum class BusId : int32_t
{
	Master = 0,  ///< マスターバス
	BGM,         ///< BGMバス
	SFX,         ///< 効果音バス
	UI,          ///< UIサウンドバス
	Voice,       ///< ボイスバス
	Ambient,     ///< 環境音バス
	Count        ///< バス数（番兵）
};

/// @brief バス数定数
inline constexpr std::size_t BUS_COUNT = static_cast<std::size_t>(BusId::Count);

/// @brief 個別バスの設定
struct BusConfig
{
	float volume = 1.0f;              ///< バス音量 (0.0-1.0)
	bool muted = false;               ///< ミュート状態
	BusId parent = BusId::Master;     ///< 親バス（Masterは自身）
};

/// @brief オーディオバス管理（ミキサー）
///
/// 各バスの音量・ミュート状態を管理し、親子関係に基づいた
/// 実効音量の計算を提供する。
class AudioBusManager
{
public:
	/// @brief デフォルトコンストラクタ（全バスを初期化）
	AudioBusManager()
	{
		for (std::size_t i = 0; i < BUS_COUNT; ++i)
		{
			m_buses[i].parent = BusId::Master;
		}
		m_buses[static_cast<std::size_t>(BusId::Master)].parent = BusId::Master;
	}

	/// @brief バスの音量を設定する
	/// @param bus 対象バス
	/// @param volume 音量 [0.0, 1.0]
	void setVolume(BusId bus, float volume)
	{
		m_buses[idx(bus)].volume = std::clamp(volume, 0.0f, 1.0f);
	}

	/// @brief バスの音量を取得する
	/// @param bus 対象バス
	/// @return 音量 [0.0, 1.0]
	[[nodiscard]] float getVolume(BusId bus) const
	{
		return m_buses[idx(bus)].volume;
	}

	/// @brief 親バスの音量を考慮した実効音量を取得する
	/// @param bus 対象バス
	/// @return 実効音量 [0.0, 1.0]
	[[nodiscard]] float getEffectiveVolume(BusId bus) const
	{
		if (isEffectivelyMuted(bus)) return 0.0f;

		float vol = m_buses[idx(bus)].volume;
		BusId current = m_buses[idx(bus)].parent;

		/// マスターに到達するまで親を辿る
		while (current != bus)
		{
			vol *= m_buses[idx(current)].volume;
			const BusId next = m_buses[idx(current)].parent;
			if (next == current) break;  ///< マスターの親は自身
			current = next;
		}
		return vol;
	}

	/// @brief バスのミュート状態を設定する
	/// @param bus 対象バス
	/// @param muted ミュートするならtrue
	void setMuted(BusId bus, bool muted)
	{
		m_buses[idx(bus)].muted = muted;
	}

	/// @brief バス自身のミュート状態を取得する
	/// @param bus 対象バス
	/// @return ミュート中ならtrue
	[[nodiscard]] bool isMuted(BusId bus) const
	{
		return m_buses[idx(bus)].muted;
	}

	/// @brief 親バスのミュート状態も考慮した実効ミュート状態を取得する
	/// @param bus 対象バス
	/// @return 実効的にミュートならtrue
	[[nodiscard]] bool isEffectivelyMuted(BusId bus) const
	{
		if (m_buses[idx(bus)].muted) return true;

		BusId current = m_buses[idx(bus)].parent;
		while (current != bus)
		{
			if (m_buses[idx(current)].muted) return true;
			const BusId next = m_buses[idx(current)].parent;
			if (next == current) break;
			current = next;
		}
		return false;
	}

	/// @brief バスの親を設定する
	/// @param bus 対象バス
	/// @param parent 親バス
	void setParent(BusId bus, BusId parent)
	{
		m_buses[idx(bus)].parent = parent;
	}

	/// @brief 全バスをミュートする
	void muteAll()
	{
		for (auto& b : m_buses) b.muted = true;
	}

	/// @brief 全バスのミュートを解除する
	void unmuteAll()
	{
		for (auto& b : m_buses) b.muted = false;
	}

private:
	/// @brief BusIdからインデックスへの変換
	[[nodiscard]] static constexpr std::size_t idx(BusId bus) noexcept
	{
		return static_cast<std::size_t>(bus);
	}

	std::array<BusConfig, BUS_COUNT> m_buses{};  ///< バス設定配列
};

} // namespace sgc::audio
