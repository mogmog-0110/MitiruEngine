#pragma once

/// @file AudioMixer.hpp
/// @brief マルチチャンネルオーディオミキサー
/// @details 複数のサウンドチャンネルを同時管理し、
///          BGM・SE・ボイスの3カテゴリに分けてボリューム制御する。
///          各チャンネルは独立したボリュームとフェード状態を持つ。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::audio
{

/// @brief サウンドカテゴリ
enum class SoundCategory : uint8_t
{
	Bgm = 0,    ///< BGM（同時1トラック）
	Se,          ///< 効果音（同時複数可）
	Voice,       ///< ボイス（同時1トラック）
};

/// @brief ミキサーチャンネルの状態
enum class ChannelState : uint8_t
{
	Idle = 0,    ///< 空き
	Playing,     ///< 再生中
	Paused,      ///< 一時停止中
	FadingIn,    ///< フェードイン中
	FadingOut,   ///< フェードアウト中
};

/// @brief 個別チャンネルの情報
struct Channel
{
	int handle = -1;                           ///< ハンドルID
	std::string soundId;                       ///< サウンドID
	SoundCategory category = SoundCategory::Se; ///< カテゴリ
	ChannelState state = ChannelState::Idle;    ///< 再生状態
	float volume = 1.0f;                       ///< チャンネルボリューム [0.0, 1.0]
	float fadeTarget = 1.0f;                   ///< フェード目標値
	float fadeSpeed = 0.0f;                    ///< フェード速度（1秒あたりの変化量）
	bool looping = false;                      ///< ループフラグ
};

/// @brief マルチチャンネルオーディオミキサー
/// @details 実際のオーディオ出力は行わず、再生状態のみを管理する。
///          具体的な出力はIAudioEngineの実装に委譲する。
///
/// @code
/// mitiru::audio::AudioMixer mixer;
/// auto bgmHandle = mixer.play("battle_bgm", SoundCategory::Bgm, true);
/// auto seHandle = mixer.play("sword_hit", SoundCategory::Se, false);
/// mixer.update(0.016f); // 毎フレーム呼び出し
/// mixer.fadeOut(bgmHandle, 2.0f); // 2秒フェードアウト
/// @endcode
class AudioMixer
{
public:
	/// @brief 最大チャンネル数
	static constexpr int MAX_CHANNELS = 32;

	/// @brief コンストラクタ
	AudioMixer() noexcept
	{
		m_channels.resize(MAX_CHANNELS);
	}

	/// @brief サウンドを再生する
	/// @param soundId サウンドID
	/// @param category サウンドカテゴリ
	/// @param loop ループ再生するか
	/// @param volume 初期ボリューム [0.0, 1.0]
	/// @return 再生ハンドル（-1 = チャンネル不足）
	int play(std::string_view soundId, SoundCategory category,
		bool loop = false, float volume = 1.0f)
	{
		/// BGM/Voiceカテゴリは同時1トラック制限
		if (category == SoundCategory::Bgm || category == SoundCategory::Voice)
		{
			stopByCategory(category);
		}

		const int idx = findFreeChannel();
		if (idx < 0)
		{
			return -1;
		}

		const int handle = ++m_nextHandle;
		auto& ch = m_channels[static_cast<size_t>(idx)];
		ch.handle = handle;
		ch.soundId = std::string(soundId);
		ch.category = category;
		ch.state = ChannelState::Playing;
		ch.volume = std::clamp(volume, 0.0f, 1.0f);
		ch.fadeTarget = ch.volume;
		ch.fadeSpeed = 0.0f;
		ch.looping = loop;

		m_handleMap[handle] = idx;
		return handle;
	}

	/// @brief ハンドル指定でサウンドを停止する
	/// @param handle 再生ハンドル
	void stop(int handle)
	{
		auto* ch = findChannel(handle);
		if (ch)
		{
			freeChannel(*ch);
		}
	}

	/// @brief カテゴリ内の全サウンドを停止する
	/// @param category サウンドカテゴリ
	void stopByCategory(SoundCategory category)
	{
		for (auto& ch : m_channels)
		{
			if (ch.state != ChannelState::Idle && ch.category == category)
			{
				freeChannel(ch);
			}
		}
	}

	/// @brief 全サウンドを停止する
	void stopAll()
	{
		for (auto& ch : m_channels)
		{
			freeChannel(ch);
		}
		m_handleMap.clear();
	}

	/// @brief ハンドル指定で一時停止する
	/// @param handle 再生ハンドル
	void pause(int handle)
	{
		auto* ch = findChannel(handle);
		if (ch && ch->state == ChannelState::Playing)
		{
			ch->state = ChannelState::Paused;
		}
	}

	/// @brief ハンドル指定で一時停止を解除する
	/// @param handle 再生ハンドル
	void resume(int handle)
	{
		auto* ch = findChannel(handle);
		if (ch && ch->state == ChannelState::Paused)
		{
			ch->state = ChannelState::Playing;
		}
	}

	/// @brief フェードインを開始する
	/// @param handle 再生ハンドル
	/// @param targetVolume 目標ボリューム
	/// @param durationSeconds フェード時間（秒）
	void fadeIn(int handle, float targetVolume, float durationSeconds)
	{
		auto* ch = findChannel(handle);
		if (!ch || durationSeconds <= 0.0f)
		{
			return;
		}
		ch->state = ChannelState::FadingIn;
		ch->fadeTarget = std::clamp(targetVolume, 0.0f, 1.0f);
		ch->fadeSpeed = (ch->fadeTarget - ch->volume) / durationSeconds;
	}

	/// @brief フェードアウトを開始する
	/// @param handle 再生ハンドル
	/// @param durationSeconds フェード時間（秒）
	void fadeOut(int handle, float durationSeconds)
	{
		auto* ch = findChannel(handle);
		if (!ch || durationSeconds <= 0.0f)
		{
			return;
		}
		ch->state = ChannelState::FadingOut;
		ch->fadeTarget = 0.0f;
		ch->fadeSpeed = -ch->volume / durationSeconds;
	}

	/// @brief BGMクロスフェードを行う
	/// @param newSoundId 新しいBGM ID
	/// @param durationSeconds フェード時間（秒）
	/// @param newVolume 新BGMのボリューム
	/// @return 新BGMの再生ハンドル
	int crossfadeBgm(std::string_view newSoundId, float durationSeconds,
		float newVolume = 1.0f)
	{
		/// 現在のBGMをフェードアウト（停止はしない）
		for (auto& ch : m_channels)
		{
			if (ch.state != ChannelState::Idle &&
				ch.category == SoundCategory::Bgm)
			{
				fadeOut(ch.handle, durationSeconds);
			}
		}

		/// 新BGMをSEカテゴリで一旦確保し、後からBGMに変更する
		/// （play()のBGM排他処理を回避するため）
		const int idx = findFreeChannel();
		if (idx < 0)
		{
			return -1;
		}

		const int handle = ++m_nextHandle;
		auto& ch = m_channels[static_cast<size_t>(idx)];
		ch.handle = handle;
		ch.soundId = std::string(newSoundId);
		ch.category = SoundCategory::Bgm;
		ch.state = ChannelState::FadingIn;
		ch.volume = 0.0f;
		ch.fadeTarget = std::clamp(newVolume, 0.0f, 1.0f);
		ch.fadeSpeed = ch.fadeTarget / durationSeconds;
		ch.looping = true;

		m_handleMap[handle] = idx;
		return handle;
	}

	/// @brief 毎フレーム更新（フェード処理）
	/// @param deltaTime 前フレームからの経過時間（秒）
	void update(float deltaTime)
	{
		for (auto& ch : m_channels)
		{
			if (ch.state == ChannelState::FadingIn ||
				ch.state == ChannelState::FadingOut)
			{
				ch.volume += ch.fadeSpeed * deltaTime;

				if (ch.state == ChannelState::FadingIn)
				{
					if (ch.volume >= ch.fadeTarget)
					{
						ch.volume = ch.fadeTarget;
						ch.state = ChannelState::Playing;
					}
				}
				else
				{
					if (ch.volume <= 0.0f)
					{
						ch.volume = 0.0f;
						freeChannel(ch);
					}
				}
			}
		}
	}

	/// @brief カテゴリ別ボリュームを設定する
	/// @param category カテゴリ
	/// @param volume ボリューム [0.0, 1.0]
	void setCategoryVolume(SoundCategory category, float volume)
	{
		m_categoryVolumes[static_cast<size_t>(category)] =
			std::clamp(volume, 0.0f, 1.0f);
	}

	/// @brief カテゴリ別ボリュームを取得する
	/// @param category カテゴリ
	/// @return ボリューム [0.0, 1.0]
	[[nodiscard]] float categoryVolume(SoundCategory category) const noexcept
	{
		return m_categoryVolumes[static_cast<size_t>(category)];
	}

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setMasterVolume(float volume) noexcept
	{
		m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
	}

	/// @brief マスターボリュームを取得する
	/// @return ボリューム [0.0, 1.0]
	[[nodiscard]] float masterVolume() const noexcept
	{
		return m_masterVolume;
	}

	/// @brief チャンネルの最終ボリュームを計算する
	/// @param handle 再生ハンドル
	/// @return マスター × カテゴリ × チャンネルの積
	[[nodiscard]] float effectiveVolume(int handle) const
	{
		const auto* ch = findChannelConst(handle);
		if (!ch)
		{
			return 0.0f;
		}
		return m_masterVolume *
			m_categoryVolumes[static_cast<size_t>(ch->category)] *
			ch->volume;
	}

	/// @brief 指定ハンドルが再生中か判定する
	/// @param handle 再生ハンドル
	/// @return 再生中（Paused含む）なら true
	[[nodiscard]] bool isPlaying(int handle) const
	{
		const auto* ch = findChannelConst(handle);
		return ch && ch->state != ChannelState::Idle;
	}

	/// @brief 指定ハンドルのチャンネル状態を取得する
	/// @param handle 再生ハンドル
	/// @return チャンネル状態（見つからない場合はIdle）
	[[nodiscard]] ChannelState channelState(int handle) const
	{
		const auto* ch = findChannelConst(handle);
		return ch ? ch->state : ChannelState::Idle;
	}

	/// @brief アクティブなチャンネル数を取得する
	/// @return Idle以外のチャンネル数
	[[nodiscard]] int activeChannelCount() const noexcept
	{
		int count = 0;
		for (const auto& ch : m_channels)
		{
			if (ch.state != ChannelState::Idle)
			{
				++count;
			}
		}
		return count;
	}

	/// @brief コールバックを設定する（チャンネル終了時に呼ばれる）
	/// @param callback ハンドルを引数に取るコールバック関数
	void setOnChannelStopped(std::function<void(int)> callback)
	{
		m_onChannelStopped = std::move(callback);
	}

private:
	/// @brief 空きチャンネルを探す
	/// @return チャンネルインデックス（-1 = 空きなし）
	[[nodiscard]] int findFreeChannel() const
	{
		for (int i = 0; i < static_cast<int>(m_channels.size()); ++i)
		{
			if (m_channels[static_cast<size_t>(i)].state == ChannelState::Idle)
			{
				return i;
			}
		}
		return -1;
	}

	/// @brief ハンドルからチャンネルを検索する
	/// @param handle 再生ハンドル
	/// @return チャンネルへのポインタ（見つからない場合はnullptr）
	[[nodiscard]] Channel* findChannel(int handle)
	{
		const auto it = m_handleMap.find(handle);
		if (it == m_handleMap.end())
		{
			return nullptr;
		}
		auto& ch = m_channels[static_cast<size_t>(it->second)];
		if (ch.handle != handle)
		{
			return nullptr;
		}
		return &ch;
	}

	/// @brief ハンドルからチャンネルを検索する（const版）
	/// @param handle 再生ハンドル
	/// @return チャンネルへのconstポインタ
	[[nodiscard]] const Channel* findChannelConst(int handle) const
	{
		const auto it = m_handleMap.find(handle);
		if (it == m_handleMap.end())
		{
			return nullptr;
		}
		const auto& ch = m_channels[static_cast<size_t>(it->second)];
		if (ch.handle != handle)
		{
			return nullptr;
		}
		return &ch;
	}

	/// @brief チャンネルを解放する
	/// @param ch 解放するチャンネル
	void freeChannel(Channel& ch)
	{
		const int handle = ch.handle;
		m_handleMap.erase(handle);
		ch = Channel{};
		if (m_onChannelStopped && handle >= 0)
		{
			m_onChannelStopped(handle);
		}
	}

	std::vector<Channel> m_channels;                   ///< チャンネル配列
	std::unordered_map<int, int> m_handleMap;          ///< ハンドル → チャンネルインデックス
	int m_nextHandle = 0;                              ///< 次のハンドルID
	float m_masterVolume = 1.0f;                       ///< マスターボリューム
	float m_categoryVolumes[3] = {1.0f, 1.0f, 1.0f};  ///< カテゴリ別ボリューム（Bgm, Se, Voice）
	std::function<void(int)> m_onChannelStopped;       ///< チャンネル終了コールバック
};

} // namespace mitiru::audio
