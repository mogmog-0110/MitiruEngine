# 曲を場面に合わせて切り替える

戦闘が始まったら次の小節で戦闘曲へ移る、敵が増えるほど打楽器の層を足す、とどめの一撃に短いフレーズを
拍に合わせて重ねる。これを assets/audio/music.json に書くと、host が小節や拍のサンプルちょうどで鳴らす。
ゲームのコードは `hud.music("combat")` のように区間の名前を頼むだけでよい。スティンガーは
`hud.play("finisher")` で、id が `stingers` にあれば効果音ではなく曲の拍に合わせて鳴る。

## できること

| 仕組み | music.json | 動き |
|---|---|---|
| 区間の切り替え | `segments` と `transitions` | 頼んだ区間へ、規則の区切り (拍・小節・マーカー・区間の終わり) まで待って移る |
| 層 | `layers` と、stem の `layer` | 強さ (intensity) から曲線で層ごとの音量を決め、`fadeSec` かけて変える |
| スティンガー | `stingers` | 次の拍 (か小節) から曲の上に 1 回だけ重ねる |

## 置き方

```
game.dll
assets/audio/music.json
assets/audio/music/explore_pad.ogg   (拡張子なしの名前で書く。wav / ogg / mp3 / flac)
```

```json
{
  "bpm": 120, "beatsPerBar": 4,
  "layers": [
    {"id": "drums", "curve": [[0, 0], [0.5, 1]], "fadeSec": 1.5, "sync": "bar"}
  ],
  "segments": [
    {"id": "explore", "bars": 8, "markers": [16],
     "stems": [{"file": "music/explore_pad"}, {"file": "music/explore_drums", "layer": "drums"}]},
    {"id": "combat_intro", "bars": 1, "loop": false, "stems": [{"file": "music/combat_intro"}]},
    {"id": "combat", "bars": 4, "bpm": 140, "stems": [{"file": "music/combat"}]},
    {"id": "victory", "bars": 2, "loop": false, "next": "explore", "stems": [{"file": "music/victory"}]}
  ],
  "transitions": [
    {"from": "*", "to": "combat", "sync": "bar", "fadeOutSec": 0.25, "via": "combat_intro"},
    {"from": "combat", "to": "explore", "sync": "exit"}
  ],
  "stingers": [{"id": "finisher", "file": "music/finisher", "sync": "beat", "gainDb": -3}]
}
```

- 区間の長さは `bars` (小節数) で決める。音源のファイルはそれより長くてよく、余りは次の区間に重なる響きとして鳴る
- `loop` は既定で true。false の区間は終わったら `next` へ進み、`next` が無ければそこで止まる
- `bpm` と `beatsPerBar` は区間ごとに上書きできる。拍の位置は区間の頭から数える
- `markers` は抜けてよい位置を区間の頭からの拍で書く (`"sync": "marker"` で使う)

## 切り替えの規則

`hud.music(id)` で今と違う区間を頼むと、`transitions` から規則を 1 つ選ぶ。from と to の両方が合う規則を
いちばん優先し、次に to だけ、from だけ、どちらも `*` の順に選ぶ。合う規則が無ければ「次の小節で、フェード無し」。

| sync | 切り替わる位置 |
|---|---|
| `immediate` | すぐ |
| `beat` / `bar` | 今の区間の次の拍 / 次の小節 |
| `marker` | 今の区間の次のマーカー。残っていなければ区間の終わり |
| `exit` | 今の区間の終わり。前の区間は止めず、後ろに残る響きをそのまま鳴らす |

`exit` 以外では、前の区間を `fadeOutSec` かけて消す (0 ならその位置で切る)。新しい区間は `fadeInSec` かけて上げる。
`via` を書くと、切り替わる位置で挟む区間を 1 回鳴らし、その終わりから目的の区間へ入る。
待っている間に今の区間をもう一度頼むと、待っていた切り替えは取り消す。同じ区間を毎フレーム頼んでも切り替えは 1 回だけ。

何も鳴っていない時に頼んだ区間は、その場で鳴り始める。`hud.stopMusic(秒)` は区間の後ろの響きも含めて全部を消す。

## 強さと層

層の音量は `curve` の折れ線 ((強さ, 音量 0..1) の組、強さの昇順) で決まる。強さが変わると層ごとに新しい音量を求め、
層の `sync` の区切りから `fadeSec` かけて寄せる。層に属さない stem (`layer` を書かない) は強さにかかわらず鳴る。

ゲームは `hud.musicIntensity(0.8f)` で強さを変える (ABI v48、ADR 0056)。host が覚えているので、変わった時だけ呼べばよい。
敵との距離で強さを決め、層・スティンガー・`sounds.json` の効果音を一緒に鳴らす見本は `examples/music_layers`。
host の C++ からは `FileAudioEngine::music().director().setIntensity(0.8f)`。

鳴っている区間の拍は `in.music()` で読める (再生中か・区間・小節・拍・拍の中の位置 0..1・区間の頭からの秒)。命令は先読みのぶん
先に出ているので、host は音声スレッドが読んだ位置まで戻し、耳に届いている拍を渡す。InputSnapshot に乗るので、録画の再生でも同じ値になる。

## 時計と決定論

区間・層・スティンガーをいつ鳴らすかは `MusicDirector` がサンプル単位で決め、`MusicRenderer` が音声スレッドで
そのサンプルちょうどに実行する。拍 k の位置は round(k × 60 × rate / bpm) で、端数を積み重ねないので
何小節進めてもずれない。

- デバイスを開かない書き出し (`mitiru_host --audio-capture`、`mitiru_musicrender`) では、director を 1 ステップ
  (1/60 秒) ずつ進める。n フレーム目の要求が何小節目で鳴るかは、何度回しても同じになる
- 実機のデバイスでは director を「音声スレッドが読んだ位置 + 約 85 ms」まで進め、耳に届く曲の小節に合わせる。
  区切りの 85 ms より手前で頼んだ要求は、その次の区切りへ回る

音源はエンコードされたままメモリへ置き、区間を鳴らすたびに命令を出す側のスレッドで読み手を開く。
音声スレッドではファイルを開かず、メモリも確保しない。

## 上限

| 項目 | 数 |
|---|---|
| 1 区間の stem | 8 本 |
| 同時に鳴る区間とスティンガー (後ろの響きを含む) | 16 本 |
| 層 | 16 |
| 待っているスティンガー | 8 本 (それより多い分は捨てる) |

## 確かめる

耳で聞かずに確かめるには、台本どおりに書き出して数値で読む。

```
mitiru_musicrender assets/audio/music.json --script play.txt --frames 480 --out music.wav
python tools/audio_report.py music.wav --tempo 120 --band drums=2500 --band combat=500
```

台本は `<フレーム> segment <id>` / `intensity <値>` / `stinger <id>` / `stop <秒>` / `gain <値>` を 1 行ずつ書く。
`--tempo` は鳴り始めごとに一番近い拍とのずれ (ms) を、`--band` はその周波数帯の音量の時間変化を出す
(層のフェードは帯域の音量の上がり方、切り替えは鳴り始めの拍で読む)。`music.music.jsonl` には区間と
スティンガーを鳴らし始めたサンプルが入る。

ゲームの中で同じことを確かめるなら `mitiru_host --headless --audio-capture out.wav` で同じ解析をする
(tests/e2e/run_adaptive_music.py)。
