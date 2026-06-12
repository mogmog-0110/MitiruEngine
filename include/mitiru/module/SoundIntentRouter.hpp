#pragma once

/// @file SoundIntentRouter.hpp
/// @brief SoundIntent (DLL → host の intent) を IAudioEngine 操作へ写像する host 専用 glue
/// @details
/// ADR 0008: game は audio mixer を持たず SoundIntent を書くだけ。host がこの関数で
/// intent を解釈し、自分が所有する IAudioEngine を駆動する (ADR 0005 整合)。
///
/// この header は **host 側のみ** が include する。DLL は ModuleApi.hpp だけを include する
/// ので、audio 依存が DLL 側へ漏れない。
///
/// SoundIntent のフィールド解釈:
///   - category: 0=SE, 1=BGM, 2=Voice (BGM のみ playMusic 経路、他は playSound 経路)
///   - stop:     1 なら再生でなく停止 (BGM→stopMusic / SE→stopSound)
///   - loop:     BGM のループ可否
///   - volume:   0.0–1.0。0 (zero-init の既定) は「未指定 = 既定音量」とみなし 1.0 とする。
///               FrameIntents は毎フレーム zero-init されるため、音量を設定しない game が
///               無音化する footgun を防ぐ。

#include <cstring>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

/// @brief SoundIntent 1 件を IAudioEngine 操作へ写像する (stateless 版)
/// @param engine 駆動対象の audio engine
/// @param s 解釈する SoundIntent
/// @note BGM の同 id 連打を冪等化したい場合は SoundIntentRouter::apply を使う (host はそちら)。
inline void applySoundIntent(audio::IAudioEngine& engine, const SoundIntent& s)
{
	const bool isMusic = (s.category == 1);  // 1 = BGM

	if (s.stop != 0)
	{
		// fade 0 は素の stop に落とす (backend によって fade(0) の扱いが違い得るため従来挙動を維持)
		if (isMusic)
		{
			if (s.fadeOutSec > 0.0f) { engine.stopMusicFade(s.fadeOutSec); }
			else                     { engine.stopMusic(); }
		}
		else if (s.id[0] != '\0')
		{
			if (s.fadeOutSec > 0.0f) { engine.stopSoundFade(s.id, s.fadeOutSec); }
			else                     { engine.stopSound(s.id); }
		}
		return;
	}

	if (s.id[0] == '\0') { return; }

	const float vol   = (s.volume     > 0.0f) ? s.volume     : 1.0f;  // 0 = 未指定 → 既定
	const float pitch = (s.pitchScale > 0.0f) ? s.pitchScale : 1.0f;  // 0 = 未指定 → 1.0
	if (isMusic) { engine.playMusicEx(s.id, vol, s.loop != 0, s.fadeInSec); }
	else         { engine.playSoundEx(s.id, vol, pitch,           s.fadeInSec); }
}

/// @brief SoundIntent の host 側 router。BGM (category=1) の同 id 連打を冪等化する。
/// @details 仕様: 「直前に開始した music id と同一 & その後 stop 無し & loop / volume も
///          同じ」の play intent は skip する。これにより game が update() 内で毎フレーム
///          `hud.music("bgm")` と書いても BGM は毎フレーム再スタートしない (仕様として保証)。
///          - loop / volume が変わった場合は skip せず適用する (記憶も更新)。
///          - stopMusic 後の同 id 再生は skip しない (記憶をクリアする)。
///          - crossfade: 別 id への切替で fadeInSec > 0 なら、新曲 play の前に
///            stopMusicFade(同じ秒数) を発行して旧曲をフェードアウトさせる。
///          - SE / Voice (category != 1) は常に素通しで applySoundIntent に委譲する。
///          DLL は再生状態を知らない (ADR 0005) ので、この dedupe は host 側にしか置けない。
class SoundIntentRouter
{
public:
	/// @brief intent 1 件を適用する。music dedupe で skip したら false を返す。
	bool apply(audio::IAudioEngine& engine, const SoundIntent& s)
	{
		if (s.category == 1)  // BGM
		{
			if (s.stop != 0)
			{
				m_lastMusicId[0] = '\0';  // stop 後の同 id は再び再生される
			}
			else if (s.id[0] != '\0')
			{
				// 比較は「実際に engine へ渡る値」で行う (volume 0 = 未指定 → 1.0)。
				const float        vol  = (s.volume > 0.0f) ? s.volume : 1.0f;
				const std::uint8_t loop = (s.loop != 0) ? 1 : 0;
				const bool sameId =
					std::strncmp(m_lastMusicId, s.id, sizeof(m_lastMusicId)) == 0;
				if (sameId && loop == m_lastLoop && vol == m_lastVolume)
				{
					return false;  // 同一 BGM が再生中 → 重複開始を skip
				}
				// crossfade: 別 id へ切替 & fadeInSec > 0 → 旧曲を同じ秒数で fade-out
				// してから新曲を開始する。同 id の再適用 (loop/volume 変更) では発行しない。
				if (!sameId && m_lastMusicId[0] != '\0' && s.fadeInSec > 0.0f)
				{
					engine.stopMusicFade(s.fadeInSec);
				}
				std::memcpy(m_lastMusicId, s.id, sizeof(m_lastMusicId));
				m_lastMusicId[sizeof(m_lastMusicId) - 1] = '\0';
				m_lastLoop   = loop;
				m_lastVolume = vol;
			}
		}
		applySoundIntent(engine, s);
		return true;
	}

private:
	char         m_lastMusicId[sizeof(SoundIntent::id)] = {};   ///< 直前に開始した music id
	std::uint8_t m_lastLoop   = 0;                              ///< その loop フラグ
	float        m_lastVolume = 0.0f;                           ///< その実効 volume (0→1.0 解決後)
};

}  // namespace mitiru::module
