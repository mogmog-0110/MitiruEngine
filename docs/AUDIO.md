# 音を鳴らす

ゲームの DLL は音を名前で頼み、host が鳴らす。まずは WAV を置いて `hud.play` と `hud.music` で鳴らす。足音の揺らぎ、同時に鳴る数、場面での曲の切り替えが要るようになったら、JSON を足す。

## 1. WAV を置いて名前で鳴らす

音のファイルを `assets/audio/<名前>.wav` に置く (.ogg と .mp3 も読める)。`mitiru new` で作ったプロジェクトなら、`assets/` はビルドのたびに DLL の隣へ写される。

```cpp
void update(Input in, Hud hud, float dt)
{
    hud.music("field");                          // assets/audio/field.wav を BGM でループ。毎フレーム呼んでも重ならない
    if (in.pressed(Key::Space)) { hud.play("jump"); }           // 効果音
    if (in.pressed(Key::J))     { hud.play("swing", 0.8f, 1.1f); }   // 音量 0.8、高さ 1.1 倍
    if (bossAppeared)           { hud.music("boss", true, 1.0f, 0.5f); }   // 0.5 秒で前の曲から移る
    if (talkStarted)            { hud.voice("line_001"); }   // 台詞。前の台詞が鳴っていれば差し替える
}
```

| 呼び方 | 使う場面 |
|---|---|
| `hud.play(名前, 音量, 高さ)` | 効果音。1 フレームに 8 本まで |
| `hud.playLoop(名前)` / `hud.stopLoop(名前)` | 押している間ずっと鳴る音 |
| `hud.music(名前, ループ, 音量, 移る秒)` / `hud.stopMusic(秒)` | BGM。同じ名前なら鳴らし直さない |
| `hud.voice(名前)` | 台詞。BGM と効果音とは別の 1 本で鳴る |
| `hud.busVolume(SoundBus::Music, 0.5f)` | オプション画面の音量 (`Master` `Music` `Sfx` `Voice` `Ui` `Ambient`) |
| `hud.playAt(名前, 秒)` | `in.audioTime()` の時刻ちょうどに鳴らす (リズムゲーム) |

3D の音は位置を付けて鳴らす。聞く位置はふつうカメラにする。

```cpp
hud.listener(view.eye, view.target - view.eye);   // カメラが動いたフレームだけ呼べばよい
hud.play("hit").at(enemy.position);              // 遠いほど小さく、左右に振れる
```

鳴った音は `mitiru run --inspect mixer` の窓で見られる。音はゲームの状態に入らないので、巻き戻しと録画の照合には影響しない。

## 2. 効果音の鳴らし方を JSON で決める (assets/audio/sounds.json)

足音のように同じ音が続くもの、爆発のように重なると割れるもの、遠くで鳴るものは、鳴らし方を `assets/audio/sounds.json` に書く。コードは `hud.play("footstep")` のままでよい。書いていない音は、優先度 128、揺らぎなし、1 m〜100 m で 1/d に減る既定で鳴る。

実際に鳴らすのは `voices.maxReal` 本 (既定 32) まで。優先度、聞こえる大きさ、新しさの順に選び、外れた音と `virtualBelowDb` (既定 -60 dB) より小さい音は鳴らさずに時間だけ進めておく。もう一度選ばれたら、経過した位置から鳴らす。

```json
{
  "seed": 1234,
  "voices": {"maxReal": 32, "maxVirtual": 128, "virtualBelowDb": -60},
  "defaults": {"priority": 128},
  "sounds": {
    "footstep": {"variants": ["sfx/step1", "sfx/step2", "sfx/step3"], "volumeDb": [-3, 0], "pitchSemitones": [-1, 1],
                 "maxInstances": 4, "steal": "oldest",
                 "attenuation": {"model": "inverse", "min": 1, "max": 30}},
    "explosion": {"priority": 220, "attenuation": {"curve": [[0, 1], [10, 0.5], [60, 0]]},
                  "occlusionDb": -18, "occlusionLowpassHz": 800},
    "car": {"doppler": 1}
  }
}
```

| 項目 | 意味 |
|---|---|
| `variants` | 鳴らすファイルの候補。鳴らすたびに 1 つ選ぶ (無ければ id そのもの) |
| `volumeDb` / `pitchSemitones` | 鳴らすたびの揺らぎの幅 [下, 上]。1 つの数なら固定 |
| `priority` | 0..255、大きいほど残す |
| `maxInstances` / `steal` | 同じ音の同時数と、超えた時に止める声 (`oldest` / `quietest` / `none`)。`quietest` で新しい音の方が小さければ新しい音を鳴らさない |
| `attenuation` | 距離での減り方。`curve` (距離, 音量) の折れ線か、`model` (`inverse` / `linear` / `exponential` / `none`) と `min` / `max` / `rolloff` |
| `doppler` | ドップラーの強さ (0 で無効)。位置の変化から速度を求める |
| `occlusionDb` / `occlusionLowpassHz` | 完全に遮られた時に下げる量と low-pass の周波数 |

揺らぎの乱数は (seed, ステップの番号, id, そのステップで何本目か, 番号) だけから作るので、録画の再生や `--audio-capture` の書き出しでは何度回しても同じ差し替え・同じ高さになる。

遮蔽の量 (0..1) は `hud.play(id).handle(n).occlusion(量)` で渡す。番号を付けて鳴らし直すたびに置き換わるので、当たり判定のレイの結果を毎フレーム渡せばよい。優先度は `.priority(1..255)` で sounds.json の値を 1 本ずつ上書きできる。

## 3. ダッキングと場所ごとの残響 (assets/audio/mix.json)

```json
{
  "ducking": [
    {"sidechain": "voice", "target": "music", "depthDb": -9, "attackSec": 0.08, "releaseSec": 0.5},
    {"sidechain": "sfx", "target": "music", "depthDb": -6, "releaseSec": 0.4, "threshold": 0.7}
  ],
  "reverb": {
    "fadeSec": 0.5,
    "outside": {"roomSize": 0.3, "damping": 0.5, "send": 0.0},
    "zones": [{"id": "cave", "min": [-20, -5, -20], "max": [20, 10, 20], "roomSize": 0.9, "damping": 0.3, "send": 0.5}]
  }
}
```

ダッキングは sidechain のバスが鳴っている間 (ボイスは台詞が鳴っている間、効果音は音量が `threshold` 以上の音を鳴らした直後) に target のバスを `depthDb` まで下げる。`attackSec` で下げ、きっかけが無くなってから `holdSec` 待って `releaseSec` で戻す。同じバスへの規則が重なったら、いちばん深い規則を使う。target は今は `music` (BGM と曲) と `sfx` (効果音のバス) が効く。mix.json が無いか `ducking` を書かなければ、台詞の間は BGM を 6 dB 下げ、音量 0.7 以上の効果音の直後は BGM を半分にして 0.4 秒で戻す。

残響は効果音のバスにだけ掛ける (台詞と BGM には掛けない)。聞き手 (`hud.listener`) が入っている箱の響きと送る量を使い、箱が重なったら `priority` の大きい方、同じなら小さい箱を選ぶ。響きを変える時は、送る量をいったん 0 まで下げてから変えて上げ直す。場所をゲームが決めるときは `hud.reverbZone("cave")`、聞き手の位置から選ぶ既定に戻すときは `hud.reverbZone("")` を呼ぶ。

## 4. 場面で曲を切り替える (assets/audio/music.json)

戦闘が始まったら次の小節で戦闘曲へ移る、敵が増えるほど打楽器の層を足す、といった切り替えは `assets/audio/music.json` に書く。ゲームは `hud.music("combat")` で区間の名前を頼み、`hud.musicIntensity(0.8f)` で強さを渡す。書き方は [ADAPTIVE_MUSIC.md](ADAPTIVE_MUSIC.md)。

## エンジンの中

host の中のミキサー (`mitiru::audio::AudioMixer`)、miniaudio のつなぎ方、Steam Audio の両耳の音 (opt-in のビルド) は [AUDIO_MIXING.md](AUDIO_MIXING.md) にある。ゲームの DLL からは使わない。
