# Audio Mixing Guide (G-07)

> エンジンの中の話。ゲームから音を鳴らす方法 (`hud.play` / `hud.music`、sounds.json、mix.json) は [AUDIO.md](AUDIO.md)。

`mitiru::audio::AudioMixer` (`include/mitiru/audio/AudioMixer.hpp`) is the
header-only logical mixer used inside the host. Game DLLs do not call it; they
request sounds through `hud.play` / `hud.music`. This doc closes out
the pandd-dodo G-07 request by documenting the three required behaviours and
how each is achieved.
## Requirements & resolution

| Requirement | Resolution |
|---|---|
| BGM / SE separation | `SoundCategory::Bgm` / `Se` / `Voice` with per-category volume (`setCategoryVolume`). BGM and Voice categories are exclusive (1 active channel); SE allows up to 32 simultaneous. |
| BGM crossfade | `crossfadeBgm(newSoundId, durationSeconds, newVolume)` fades the current BGM out while fading a new BGM in over the same duration. |
| SE clipping prevention | `setSeLimiterMode(SeLimiterMode::EqualPower \| Linear)` scales the effective volume of every active SE so that N simultaneous SEs at unit volume do not saturate the downstream mixer. Default `None` preserves historical behaviour. |

## 出力が割れた時 (ClipWatch)

`MiniaudioEngine` は出力のミックス (device の callback と `renderOffline`) で 0 dBFS を超えたサンプルを
`audio/ClipWatch.hpp` で数え、`update()` で初めて超えた時に `warnOnce("audio.clip")` で 1 行知らせる。音は変えない。
master に limiter を既定で掛けない理由は 2 つある。1 つは、記録した WAV と音の確かめ (`tools/audio_report.py` の
ピーク・帯域・鳴り始め、`tests/e2e/run_sfx_voices.py`) の値が limiter の掛かり方で変わること。もう 1 つは、大きすぎる素材が
割れずに小さく鳴るだけになり、直す手掛かりが消えること。Godot も master の limiter は既定で入れず、足すよう勧めている。
数えるだけで音には触らないので、同じ入力から書き出す WAV は数える前とバイト単位で同じになる。

## SE limiter modes

When N SE channels are active, `effectiveVolume(handle)` for each SE is
multiplied by:

- `SeLimiterMode::None` — `1.0` (no scaling; summed output can exceed 0 dBFS)
- `SeLimiterMode::EqualPower` — `1 / sqrt(N)` (equal-power law; preserves
  perceived loudness while keeping summed RMS bounded)
- `SeLimiterMode::Linear` — `1 / N` (conservative; summed peak bounded)

BGM and Voice categories are never scaled by the SE limiter. The limiter does
not clamp the number of active SE channels; if you need a hard cap, combine
with `activeCategoryChannelCount(SoundCategory::Se)` and refuse new `play()`
calls.

## Usage

```cpp
#include <mitiru/audio/AudioMixer.hpp>
using namespace mitiru::audio;

AudioMixer mixer;
mixer.setMasterVolume(0.8f);
mixer.setCategoryVolume(SoundCategory::Bgm, 0.6f);
mixer.setCategoryVolume(SoundCategory::Se, 1.0f);
mixer.setSeLimiterMode(SeLimiterMode::EqualPower);

const int bgm = mixer.play("title_bgm", SoundCategory::Bgm, /*loop=*/true);

// Cross-fade to action BGM over 2 seconds
mixer.crossfadeBgm("action_bgm", 2.0f, /*newVolume=*/1.0f);

// Multiple SEs — automatically attenuated so their sum does not clip
mixer.play("hit_01", SoundCategory::Se);
mixer.play("hit_02", SoundCategory::Se);
mixer.play("hit_03", SoundCategory::Se);

// Per-frame fade advancement
mixer.update(deltaTime);
```

## Web Audio parity

For web-first games (e.g. KaeruCrape, pandd-dodo web shell), the Web Audio
`GainNode` hierarchy is:

```
destination
  └─ masterGain
       ├─ bgmGain
       ├─ seGain
       │    └─ (per-SE GainNode, dynamically scaled to match EqualPower)
       └─ voiceGain
```

The engine-side `AudioMixer` and the JS-side gain graph must stay in sync if
both are running. `effectiveVolume(handle)` is the source of truth for the
final per-channel attenuation.

## OPNA / MML との関係 (#F7)

`external/mml/include/mitiru_mml/OpnaSequencer.hpp` の YM2608 (OPNA) チップ
エミュレーションは `AudioMixer` の外にある独立した経路。`OpnaSequencer::render()`
は MML 文字列全体を tick 単位でオフラインレンダリングし、1 本の完成済み PCM
(`RenderResult::mixed`) を返すだけで、`AudioMixer` の `SoundCategory` (Bgm/Se/Voice)
やチャンネル管理には一切関与しない。レンダリング結果をゲームが鳴らす段になって
初めて、通常のファイル/sound_id として `mixer.play(id, SoundCategory::Bgm, ...)`
に載る。つまり OPNA は「音源」、`AudioMixer` は「その音源が吐いた 1 トラックを
含む、全チャンネルの論理ミキシング」という役割分担であり、OPNA 側にミキサーの
音量/クロスフェード機能を重複実装する必要はない。

## `AudioMixer::play()` のロック粒度 (#F7)

`AudioMixer` は全メソッドが単一の `std::mutex m_mutex` を `play()` を含めて
丸ごとロックする粗粒度設計（`include/mitiru/audio/AudioMixer.hpp`）。チャンネル
単位のロックや lock-free 化はしていない。既存ゲームはメインスレッドから
毎フレーム呼ぶ前提で、実測でこのロックがボトルネックになった事例は無い。
lock-free化の要否は「まず計測してから」判断する（測らずに book-keeping を
増やすと、粗粒度ロックより複雑な割に効果が測れないコードになりやすい）。

## BGM の low-pass (一時停止中)

`AudioMixer` は論理ミキサーで音を加工しない。実際の音の加工は再生側の
`MiniaudioEngine` が miniaudio のノードグラフで行う。BGM だけが `ma_lpf_node`
(2 次) を経由でき、`setMusicLowPass(cutoffHz)` で掛け外しする (`<= 0` で直結に戻す)。
効果音とボイスは通さないので、こもった BGM の上でも操作音ははっきり鳴る。

`Engine::setPaused(true)` / `togglePaused()` は `IAudioEngine::setMusicLowPass(700)` を
呼び、再開時に 0 を呼ぶ。音は GameMemory に戻らないので replay / rewind には影響しない。
WebAudioEngine は同じ係数を BiquadFilterNode で掛ける (BGM だけが通る `musicBus` を、
外している間は master へ直結する)。係数は `MusicLowPassParams.hpp` で両者共通: 2 次 Butterworth、
周波数は 10Hz〜ナイキスト手前。Web Audio の lowpass は Q を dB で受けるので `qWebAudioDb` (-3.01dB) を渡す。
NullAudioEngine は既定の no-op。

テスト: `tests/mitiru/TestMusicLowPass.cpp` はデバイスを開かない
`MiniaudioEngine(MiniaudioEngine::Offline{...})` のミックス結果を `renderOffline()` で
引いて、4kHz の BGM が 1/20 以下に落ち、外すと戻り、効果音は削られないことを確かめる。
Web 側は emscripten が無い環境では組めないので、係数 (`musicLowPassParams`) だけを同じファイルで確かめる。
## 効果音の声の経路 (sounds.json の裏側)

sounds.json の書き方は [AUDIO.md](AUDIO.md)。ゲームの効果音 (SoundIntent の SE、予約再生を除く) は、host の
`SoundIntentRouter` が持つ `audio::sfx::VoiceManager` を通って鳴る。声はゲームが鳴らした 1 本ごとに 1 つで、音の長さは
ファイルから測り、鳴り終わりはステップの時計で決めるので、デバイスの有無で結果は変わらない。
host の C++ から遮蔽を渡すときは `SoundIntentRouter::voices().setOcclusion(番号, 量)`、残響の場所を決めるときは
`MiniaudioMixDriver::forceReverbZone(id)` を使う。

## 3D の音を両耳へ置く (Steam Audio、opt-in)

既定では 3D の効果音は左右の振り (pan) で鳴る。`-DMITIRU_WITH_STEAMAUDIO=ON` で configure し、
`python tools/fetch_steamaudio.py` で SDK (170 MB 超、external/steamaudio、配布物に入れない) を置くと、
`MiniaudioEngine` は 3D の声ごとに `detail::MiniaudioSpatialNode` を挟み、Steam Audio の HRTF で両耳へ畳み込む。
前後と上下も耳に届く音の差として出る。HRTF は 256 フレーム単位で畳み込むので、3D の声だけ 256 フレーム
(48 kHz で約 5 ms) 遅れて出る。向きは VoiceManager が聞き手から見た向きを毎ステップ渡す。

壁や床での反射 (Steam Audio の reflections) は使っていない。反射を計算するには当たり判定の形を音の場面
(IPLScene) として渡す必要があり、ゲームの形をどう渡すか (焼いたアセットにするか) を決めるまで入れない。

## ダッキングと残響の実装 (mix.json の裏側)

mix.json の書き方は [AUDIO.md](AUDIO.md)。ダッキングと場所ごとの残響は毎ステップ `MiniaudioMixDriver` が反映する。
残響は Freeverb (並列コム 8 本 + オールパス 4 本) で、`include/mitiru/audio/mix/Freeverb.hpp` にある。

## 音量とフェードの持ち方 (miniaudio)

miniaudio の `ma_sound` は音量 (`ma_sound_set_volume`) とフェード (fader) を掛け算する。`MiniaudioEngine`
では BGM と one-shot の音量を ma_sound の音量に、フェードイン・フェードアウト・ダッキングを fader に持たせる。
番号付きの音 (ループや声) は音量を fader だけに持たせ、変える時は 40 ms の傾きでつなぐ。

## Tests

See `tests/mitiru/TestAudioMixer.cpp` — the `[g07]` tag filter runs only the
limiter-related cases. 声・ダッキング・残響は `tests/mitiru/TestSfxVoices.cpp` と
`tests/mitiru/TestAudioMixBus.cpp`、実際の mitiru_host を通した確かめ方は
`tests/e2e/run_sfx_voices.py` と `tests/e2e/run_adaptive_music.py` (書き出した WAV を
`tools/audio_report.py --band` で読む)。
