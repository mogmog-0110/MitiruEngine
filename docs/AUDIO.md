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
| `hud.play(名前, 音量, 高さ)` | 効果音。1 フレームに 32 本まで。超えたら止めと小さな音から捨て、端末に 1 度知らせる (ABI v55) |
| `hud.playLoop(名前)` / `hud.stopLoop(名前)` | 押している間ずっと鳴る音 |
| `hud.music(名前, ループ, 音量, 移る秒)` / `hud.stopMusic(秒)` | BGM。同じ名前なら鳴らし直さない |
| `hud.voice(名前)` | 台詞。BGM と効果音とは別の 1 本で鳴る |
| `hud.busVolume(SoundBus::Music, 0.5f)` | オプション画面の音量 (`Master` `Music` `Sfx` `Voice` `Ui` `Ambient`) |
| `hud.playAt(名前, 秒)` | `in.audioTime()` の時刻ちょうどに鳴らす (リズムゲーム) |
| `hud.busPitch(SoundBus::Sfx, 0.7f, 0.2f)` | バスの高さを 0.2 秒かけて 0.7 倍へ。鳴っている音にもすぐ効く (時間をゆるめる間など) |
| `hud.busLowPass(SoundBus::Master, 800.0f, 0.3f)` | バスの 800 Hz より上を削る。周波数は対数で動くのでスイープになる。0 で外す |
| `hud.musicLowPass(600.0f, 0.5f)` | 曲だけを削る (`busLowPass(SoundBus::Music, …)` と同じ) |

3D の音は位置を付けて鳴らす。聞く位置はふつうカメラにする。

```cpp
hud.listener(view.eye, view.target - view.eye);   // カメラが動いたフレームだけ呼べばよい
hud.play("hit").at(enemy.position);              // 遠いほど小さく、左右に振れる
```

高さが効くのは効果音のバス (`Sfx` `Ui` `Ambient` と 6, 7) で、`Master` はその全部に掛かる。曲は拍の時計がずれるので、
ボイスは台詞を聞き取れるよう、高さを変えない。low-pass は効果音のバスと曲に効き、ボイスには掛からない。host の一時停止の
こもり (700 Hz) とゲームの指定が重なったときは低い方が効く。値は host が覚えているので、変える時だけ呼べばよい (ADR 0080)。

鳴った音は `mitiru run --inspect mixer` の窓で見られる。音はゲームの状態に入らないので、巻き戻しと録画の照合には影響しない。

同時に鳴る音を足した大きさが上限 (0 dBFS) を超えると音が割れる。host は最初に割れた時に端末へ 1 行だけ知らせ、
`tools/selfcheck.py` は summary.md の頭に「注意: 音が割れている」と出す。音を勝手に小さくする仕掛け (limiter) は既定で掛けない。
重なる音の音量を `hud.play` / `hud.music` の引数か、下の sounds.json の `volumeDb` で下げる。

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
| `avoidRepeat` | 直前の何本と同じ変種を避けるか (0〜16、`variants` の数 - 1 以下)。既定は 0 で、避けずに無作為に選ぶ |
| `shuffle` | `true` なら変種を 1 周するまで同じものを選ばず、周の変わり目でも同じものが続かない。変種が 2 つなら交互。`avoidRepeat` とは同時に書けない |
| `priority` | 0..255、大きいほど残す |
| `maxInstances` / `steal` | 同じ音の同時数と、超えた時に止める声 (`oldest` / `quietest` / `none`)。`quietest` で新しい音の方が小さければ新しい音を鳴らさない |
| `attenuation` | 距離での減り方。`curve` (距離, 音量) の折れ線か、`model` (`inverse` / `linear` / `exponential` / `none`) と `min` / `max` / `rolloff` |
| `doppler` | ドップラーの強さ (0 で無効)。位置の変化から速度を求める |
| `occlusionDb` / `occlusionLowpassHz` | 完全に遮られた時に下げる量と low-pass の周波数 |
| `caption` | 鳴った時に出す字幕。`textKey` (strings.json の鍵)、`kind` (`dialogue` / `ambient` / `music` / `alert`)、`directional`、`speakerKey`。`defaults` には書けない ([アクセシビリティ](ACCESSIBILITY.md)) |

揺らぎの乱数は (seed, ステップの番号, id, そのステップで何本目か, 番号) だけから作るので、録画の再生や `--audio-capture` の書き出しでは何度回しても同じ差し替え・同じ高さになる。ステップの番号は、rewind で GameMemory を戻すと一緒に戻る。戻した所から同じ鳴らし方をすれば、戻す前と同じ変種・音量・高さが出る (鳴っている声そのものは戻らない)。

`avoidRepeat` と `shuffle` の並びは、seed、id、その id を鳴らした通しの回数だけで決まる。頭からの replay でも rewind のあとでも同じ並びになる。履歴は id ごとに 512 本まで持つので、それより深く戻すと並びが最初からに戻る。

遮蔽の量 (0..1) は `hud.play(id).handle(n).occlusion(量)` で渡す。番号を付けて鳴らし直すたびに置き換わるので、当たり判定のレイの結果を毎フレーム渡せばよい。優先度は `.priority(1..255)` で sounds.json の値を 1 本ずつ上書きできる。

壁の向こうの声を host が自動で遮るなら、sounds.json に `autoOcclusion` を書く。書いたときだけ働き、`--collision` で渡した静的な世界へ、聞き手から 3D の声にレイを 1 本飛ばす。世界を持たない起動では遮らない。

```json
{"autoOcclusion": {"intervalSteps": 6, "maxRaysPerStep": 8, "smoothSec": 0.1, "marginM": 0.25}}
```

| 項目 | 意味 |
|---|---|
| `intervalSteps` | 1 本の声を調べ直す間隔 (ステップ、既定 6) |
| `maxRaysPerStep` | 1 ステップに飛ばすレイの上限 (既定 8)。越える分は次のステップに回し、全部の声に順番が回る |
| `smoothSec` | 遮蔽の量が 0 と 1 の間を動く秒数 (既定 0.1) |
| `marginM` | 声の手前でレイを止める距離 (既定 0.25)。壁に埋まった声が自分の壁に遮られない |

遮られているかは 0 か 1 の 2 値で、変わるときだけ `smoothSec` で滑らかにする。ゲームが `.occlusion()` で送る量とは大きい方を使う。3D でない音は調べない。

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

### EQ・コンプレッサ・limiter

バスの音色を整えたいときだけ、`mix.json` に足す。書かなければ何も掛からず、音は書く前と同じバイト列になる。

```json
{
  "eq": {
    "fadeSec": 0.2,
    "buses": {"music": [{"type": "highPass", "hz": 60}, {"type": "peaking", "hz": 3000, "gainDb": -2, "q": 1.0}]},
    "zones": [{"id": "cave", "buses": {"sfx": [{"type": "lowShelf", "hz": 200, "gainDb": 4}]}}]
  },
  "compressor": {"sfx": {"thresholdDb": -14, "ratio": 3, "attackSec": 0.02, "releaseSec": 0.2, "kneeDb": 6, "makeupDb": 2}},
  "limiter": {"ceilingDb": -1, "releaseSec": 0.05}
}
```

| 項目 | 意味 |
|---|---|
| `eq.buses.<バス>` | 帯域の配列 (8 本まで、書いた順に直列)。バスは `master` `music` `sfx` `voice` `ui` `ambient` `user6` `user7` |
| 帯域 | `type` は `peaking` `lowShelf` `highShelf` `highPass` `lowPass` `bandPass` `notch`。`hz` は 10〜20000、`q` は 0.1〜20 (既定 0.707)、`gainDb` は ±24 (peaking と shelf だけ)。範囲を外れると読み込みが誤りになる |
| `eq.zones` | `reverb.zones` の `id` に合わせた切り替え。その箱に聞き手がいる間、書いたバスだけ既定の帯域を置き換える。箱は残響と共有で、`hud.reverbZone` でも切り替わる。`fadeSec` かけて移る |
| `compressor.<バス>` | ピークを測るコンプレッサ。`attackSec` は下げ幅が目標に追い付く 63% の時定数、`releaseSec` は測ったピークが下がる 63% の時定数。他のバスの音で下げるのは `ducking` |
| `limiter` | master に掛ける天井。先読みしないので音は遅れず、天井を超えるサンプルだけ即座に下げる。**既定では掛けない** (割れの手掛かりが消え、`audio_report.py` の値が変わるため)。掛けても、割れの数え (`ClipWatch`) は掛ける前の音で行う |

### ディストーション・ピッチシフト・コーラス・ディレイ

音色を作る効果。master には書けない (master は EQ・コンプレッサ・limiter だけ)。

```json
{
  "distortion": {"sfx": {"driveDb": 18, "mix": 0.8, "levelDb": -3}},
  "pitchShift": {"ui": {"semitones": -5, "algorithm": "grain"}, "music": {"semitones": 3, "tonalityLimitHz": 8000}},
  "chorus": {"voice": {"rateHz": 1.5, "depthMs": 3, "delayMs": 20, "mix": 0.4}},
  "delay": {"ambient": {"timeSec": 0.25, "feedback": 0.4, "wet": 0.3, "dampHz": 2000}}
}
```

| 項目 | 意味 |
|---|---|
| `distortion.<バス>` | ソフトクリップ。`driveDb` (0〜40) だけ持ち上げて tanh に通す。`mix` はディストーションを通した音の割合 (0〜1)、`levelDb` (-24〜0) は歪んだ後に下げる量 |
| `pitchShift.<バス>` | `semitones` (-24〜24)。`algorithm` は `signalsmith` (既定。Signalsmith Stretch の位相ボコーダ、倍音を保つ、レイテンシは `blockMs` と同じ約 40 ms) か `grain` (自前の 2 tap、レイテンシ約 10 ms、440 Hz の純音で 1 オクターブ上げると約 +10 Hz ずれてうなりが出る)。`blockMs` (10〜200、既定 40) は signalsmith のブロック、`grainMs` (10〜200、既定 40) は grain のウィンドウ。`tonalityLimitHz` は signalsmith だけで、その上の帯域の高さを変えず音色を保つ |
| `chorus.<バス>` | `delayMs` (1〜50) を中心に `depthMs` (0〜20) の幅で `rateHz` (0〜10) の正弦波で揺らした音を `mix` の割合で混ぜる。右は LFO の位相を 4 分の 1 ずらす。`delayMs - depthMs` は 0.5 以上 |
| `delay.<バス>` | `timeSec` (0.001〜2) 遅れた音を `wet` (0〜1) の大きさで足し、`feedback` (0〜0.95) で繰り返す。`dampHz` は戻りの経路の low-pass (0 で掛けない)。元の音は 1 のまま通す |

### スロー・ヒットストップの間の音 (timeFlow)

`hud.hitStop` の間と、host の `timeScale` / `layerTimeScale[0]` が 1 未満の間、バスの高さを下げて高域を削る。書いたときだけ働く。

```json
{"timeFlow": {"bus": "sfx", "pitchAtStop": 0.5, "lowPassHzAtStop": 1800, "rampSec": 0.05}}
```

| 項目 | 意味 |
|---|---|
| `bus` | 掛けるバス (既定 `sfx`)。`master` と `voice` には掛けない。`music` は高さを変える APIが無いので、`pitchAtStop` を 1 にして `lowPassHzAtStop` だけ書く |
| `pitchAtStop` | 止まったときの高さの倍率 (0.125〜1、既定 0.5)。流れの量に比例して 1 から近づく |
| `lowPassHzAtStop` | 止まったときの low-pass (0 で掛けない)。周波数の対数で近づく |
| `rampSec` | 切り替えにかける秒数 (既定 0.05) |
| `followHitStop` / `followTimeScale` | それぞれの間を流れ 0 / `timeScale × layerTimeScale[0]` とみなすか (既定 `true`) |

ゲームが自前の dt で作るスローは host から見えないので、従来どおり `hud.busPitch` / `hud.busLowPass` を書く。同じバスに両方が掛かると、高さは掛け合わさり、low-pass は低い方が効く (同じバスに書いたときは端末に 1 度知らせる)。`hud.playAt` (時刻の予約) の音と曲の区間のスティンガーは効果音の声を通らないので、高さと low-pass は効かない。値は host が持ち、GameMemory には入らない。

### 3D の声をヘッドモデルで両耳へ置く (binaural)

`{"binaural": true}` を書くと、Steam Audio の HRTF が無い構成でも、3D の声をヘッドモデルで両耳へ置く。球の頭の時間差 (真横で約 0.66 ms) と、耳に向くと高域が持ち上がり反対側は削れる影のフィルタ、後ろの声の高域の low-pass を使う。測った HRTF のデータは持たないので、前後の区別は粗く、上下は区別しない。スピーカーでは位置がかえって曖昧になるので、既定では使わない (ヘッドホン向け)。Steam Audio が入った構成では、そちらが先に使われる。

チェーンの順は EQ、コンプレッサ、ディストーション、ピッチシフト、コーラス、ディレイ。`ui` `ambient` `user6` `user7` は効果音のバスの子で、どれかを書いたときだけ専用の経路を持ち、通ったあとに効果音のバスの EQ・コンプレッサ・ダッキング・残響の送りも受ける。書かなければ今までどおり効果音のバスに入る。専用の経路に入るのは `hud.play` と `hud.playLoop` の音で、`hud.playAt` (時刻の予約) と曲の区間のスティンガーは効果音のバスのまま。

`mix.json` の最上位や、効果の設定に知らない項目があると、近い名前を添えて読み込みが誤りになる (`timeflow` のような綴りの誤りで、黙って効かないままにしない)。

何か 1 つでも掛けると、`--audio-capture` の `events.jsonl` の先頭に `{"mixDsp": [...]}` が 1 行入り、`selfcheck` の `summary.md` の頭と `audio_report.py` の 1 行目に「mix.json の … が有効」と出る。ピーク・ラウドネス・帯域の値が、掛けない場合と違うことを忘れないため。

## 4. 場面で曲を切り替える (assets/audio/music.json)

戦闘が始まったら次の小節で戦闘曲へ移る、敵が増えるほど打楽器の層を足す、といった切り替えは `assets/audio/music.json` に書く。ゲームは `hud.music("combat")` で区間の名前を頼み、`hud.musicIntensity(0.8f)` で強さを渡す。書き方は [ADAPTIVE_MUSIC.md](ADAPTIVE_MUSIC.md)。

## エンジンの中

host の中のミキサー (`mitiru::audio::AudioMixer`)、miniaudio のつなぎ方、Steam Audio の両耳の音 (opt-in のビルド) は [AUDIO_MIXING.md](AUDIO_MIXING.md) にある。ゲームの DLL からは使わない。
