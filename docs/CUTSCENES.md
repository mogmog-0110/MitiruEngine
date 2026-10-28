# カットシーン — JSON 1 本を時刻で再生する

カットシーンは JSON のファイル 1 本に書き、ゲーム DLL が `mitiru/narrative/` の関数で再生する。GameMemory に置くのは
再生の時刻 (`SequencePlayback`、12 byte) だけで、カメラ・役者の位置・アニメ・字幕・暗転・上下の帯は、その時刻から毎回求める。
同じ時刻からは必ず同じ絵になるので、巻き戻し、決定論リプレイ、分岐エディタ (ADR 0035)、Rewind 窓の scrub がそのまま効く。
時間軸を編集するメガエディタは作らない。テキストエディタで書き、`mitiru_host --watch-assets <フォルダ>` で保存のたびに読み直す。
設計は ADR 0063、見本は [`examples/cutscene3d`](../examples/cutscene3d/cutscene3d_dll.cpp)。

## 書き方

```json
{
  "duration": 10.0, "skippable": true, "blendOut": 1.0,
  "cameras": [
    {"name": "fly", "priority": 1, "start": 0, "end": 6.5, "near": 0.1, "far": 120,
     "keys": [{"t": 0, "eye": [0, 16, 26], "target": [0, 0, 5], "fov": 50},
              {"t": 6.5, "eye": [2.6, 2.3, -8.8], "target": [0, 1, 1], "fov": 55, "roll": 0}]},
    {"name": "close", "priority": 2, "start": 5, "end": 9, "blendIn": 1.2, "path": "close.camera.json"}
  ],
  "actors": [{"name": "fox", "interp": "linear", "keys": [{"t": 0, "pos": [0, 0, 8.6], "yaw": 180}]}],
  "clips": [{"actor": "fox", "clip": "Walk", "start": 2.8, "end": 8.3, "blendIn": 0.3, "blendOut": 0.3, "speed": 1.35}],
  "cues": [{"t": 7.2, "kind": "sound", "name": "step", "value": 0.6},
           {"t": 5.0, "kind": "vfx", "name": "sparkle", "duration": 1.6},
           {"t": 10.0, "kind": "call", "name": "start_talk"}],
  "subtitles": [{"start": 7.4, "end": 9.6, "speaker": "狐", "text": "……人の子か。", "key": "intro.3"}],
  "fade": [[0, 1], [1.2, 0]],
  "letterbox": [[0, 1], [9.2, 1], [10, 0]]
}
```

| 欄 | 中身 |
|---|---|
| `cameras` | 仮想カメラ。区間 `[start, end)` の中で priority が一番高いものが映る (同じ priority は先に書いた方)。映り始めは `blendIn` 秒かけて前のカメラから混ぜ、0 なら切り替える。鍵の間は既定で滑らかな曲線 (`"interp": "linear"` で直線)。`roll` は視線まわりの傾き (度) |
| `blendOut` | カメラが全部終わった後、ゲームのカメラへ戻す秒数 |
| `actors` | 役者の位置と向き (`yaw` は +Y まわりの度、0 で +Z を向く)。巻かずに書く (350 の次は 370) |
| `clips` | 役者に流すアニメの区間。`clip` はモデルのクリップ名で、ゲームが `findClip` で引く |
| `cues` | 合図。`sound` (`hud.play`)、`music` (`hud.music`、名前が空なら止める)、`vfx` (`duration` 秒のあいだ描く)、`mark` (`hud.mark`)、`call` (ゲームの関数)、`anim` (アニメの状態機械の trigger、`actor` 付き) |
| `videos` | 映像の区間 `{"name", "start", "end", "offset", "speed", "rect"}`。動画 (`assets/video/<名前>.mpg`) の秒は `offset + (今 - start) × speed`。`rect` は画面に対する割合で、既定は全画面。ゲームは `drawSequenceVideos` で描く。音は同じ時刻の `music` か `sound` の合図で鳴らす ([動画](VIDEO.md)) |
| `subtitles` | 字幕。`key` があれば訳 (`LocalizationManager` の JSON) を引く |
| `fade` / `letterbox` | `[時刻, 量 0..1]` の列。直線で結ぶ |

カメラの鍵は `"path"` で別のファイル (`{"keys": [...]}`) から読める。Blender のアドオン
(`tools/blender/mitiru_level_export.py`) の Export Camera Path が、アクティブなカメラのアニメをこの形で書き出す。
エラーは `cameras[1].keys[2]: t が前の鍵より小さい` のように場所を付けて返し、読み直しに失敗したら前の中身を使い続ける。

## ゲームから使う

```cpp
#include <mitiru/narrative/NarrativeFile.hpp>
#include <mitiru/narrative/NarrativeHud.hpp>
namespace nr = mitiru::narrative;

nr::NarrativeFile<nr::Sequence> g_intro("mygame/assets/intro.seq.json", &nr::loadSequenceFile);

struct MyGame
{
    nr::SequencePlayback cut{};

    void init() { nr::playSequence(cut, g_intro.get()); }
    void update(Input in, Hud hud, float dt)
    {
        g_intro.reloadIf(in.actionPayload("asset.reloaded"));
        const nr::Sequence& seq = g_intro.get();
        const auto onCue = [&](const nr::Cue& c) { if (!nr::playCue(c, hud)) { /* vfx / call / anim はゲームが受ける */ } };
        const float before = cut.time;
        if (in.confirmPressed()) { nr::skipSequence(cut, seq, onCue); }
        else { nr::advanceSequence(cut, seq, dt, onCue); }
        if (nr::cameraCutBetween(seq, before, cut.time)) { hud.cameraCut(); }
        nr::pushSequence(hud, cut, seq);   // 字幕を UI へ (view.sub_text など)
    }
    void draw(Screen& s) const
    {
        const nr::CameraShot c = nr::evaluateCamera(g_intro.get(), cut.time, myGameplayCamera);
        s.camera3D(c.eye, c.target, c.up, Deg{c.fovDeg}, c.nearDist, c.farDist);
        // 役者は evaluateActor、アニメは evaluateClips、帯と暗転は letterboxAt / fadeAt、VFX は forEachActiveCue
    }
};
```

- 合図は `advanceSequence` が「前の時刻 <= t < 今の時刻」のものを渡す (終わりに着いた回は終わりちょうどのものも)。巻き戻して進め直せば同じ合図がもう一度出る
- 飛ばす (`skipSequence`) と、残りの `call` と、残りの中で最後の `music` だけが届く。後の状態と BGM が飛ばさない時とそろう
- VFX は合図の時刻からの経過秒で描く (`forEachActiveCue`)。粒の種を固定すれば、巻き戻した時刻の姿になる
- 上下の帯と暗転は draw で時刻から描く。`hud.letterbox` / `hud.fadeOut` の intent は遷移を host が持ち、巻き戻しでは戻らない
- アニメの状態機械を使うゲームは、`anim` の合図を trigger として、`evaluateClips` の代わりに状態機械の入力へ渡す
- `pushSequence` は再生中に `hud.cinematic()` と `hud.timelineMarker(名前, 時刻, 長さ)` も呼ぶ (ABI v50)。mitiru_host はその間 F7〜F9
  (コマ送り・一時停止・速さ) を効かせず、UI へ `view.cinematic` を送る。Rewind 窓のバーにはカットシーンの区間が名前つきの帯で出る
- story の窓 (`--page story`) に再生位置を名前で出すには、`narrative::StoryInspect` に `playback(offsetof(MyGame, cut))` と
  `add(seq)` を渡し、`MITIRU_INSPECT_ASSETS` で返す (`docs/DIALOGUE.md` の「窓で見る」)

## 確かめ方

`mitiru_host <dll> --headless-3d --backend dx12 --capture-dir D --capture-every 60` で 1 秒ごとの絵を撮る。
`mark` の合図は `--state-trace` の `_marks` に出る。決定論は `ctest -R determinism_cutscene3d` (記録して照合)。
