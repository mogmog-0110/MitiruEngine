# 動画を映す

動画 (MPEG-1 の `.mpg`) は、ゲームの時刻 (秒) から映す 1 コマを決める。壁時計も音の時計も見ないので、同じ秒からは必ず同じ絵になり、
巻き戻し、`--capture-dir` の撮影、決定論リプレイが動画の絵まで届く。ゲームの全状態に置くのは動画の秒 (float 1 つ) だけで、
絵は host が復号して描く。見本は [`examples/title_video`](../examples/title_video/title_video_dll.cpp)、設計は [ADR 0104](adr/0104-video-playback.md)。

## 使い方

1. `mitiru.toml` の `features` に `video` を書く (書いた build だけに復号器が入る。host が約 31 KB 増える)。

   ```toml
   [engine]
   features = ["video"]
   ```

2. 動画を `assets/video/<名前>.mpg` に置く。音は別の `assets/audio/<名前>.ogg` (または `.wav`) に置く (下の「音」)。
3. 秒を GameMemory に持ち、`drawVideo` に渡す。

   ```cpp
   struct Title { float t = 0.0f; };

   void update(Input in, Hud hud, float dt) { t += dt; }                       // 秒を進めるのはゲーム
   template <class Surface> void drawImpl(Surface& s) const
   {
       s.drawVideo("title", Rect{0.0f, 0.0f, 1280.0f, 720.0f}, t);            // 動画の名前、描く矩形、動画の中の秒
   }
   ```

`drawVideo` の第 4 引数は色 (掛け算)。白が既定で、`Colorf{1, 1, 1, 0.5f}` なら半透明になる。動画の終わりを超えた秒は最後の絵のまま止まり、
負の秒は最初の絵になる。ループは、ゲームが `fmod` した秒を渡す。

コマは `floor(秒 × fps)` 番のちょうど 1 枚になる。境目の秒 (fps が 30 なら 0.5 秒ちょうど) は次の絵に入る。

## 動画の書き出し

エンコーダは作る側の道具で、出荷物に入れない。ffmpeg の例を挙げる。

```
ffmpeg -i source.mp4 -an -vf "scale=1280:720,tpad=stop=1:stop_mode=clone" ^
       -c:v mpeg1video -q:v 3 -g 15 -bf 2 -pix_fmt yuv420p -f mpeg title.mpg
```

| 指定 | 理由 |
|---|---|
| `-an` | 音は動画に入れない。音は別のファイルで、既存の音の経路 (`hud.music`) が鳴らす |
| `tpad=stop=1:stop_mode=clone` | 最後の絵を 1 枚足す。pl_mpeg は最後の 1 コマを返さないので、足さないと最後の絵が 1 コマ欠ける |
| `-f mpeg` | MPEG-PS に包む。拡張子は `.mpg` |
| `-c:v mpeg1video` | MPEG-1 の映像。H.264 と MP4 は読めない |
| `-q:v 3` | 画質。小さいほど良く、大きくなる (1 から 31) |
| `-g 15 -bf 2` | キーフレームの間隔と B フレーム。どちらも復号の結果は変えないので、好みでよい |
| フレームレート | MPEG-1 が持てるのは 23.976 / 24 / 25 / 29.97 / 30 / 50 / 59.94 / 60 fps。可変フレームレートの元は固定にしてから渡す |

作った動画の 1 コマ目と途中と最後は、`mitiru_host --headless-3d --capture-dir <dir>` で撮った PNG を見て確かめる (`tools/grab_scene.py` も使える)。
サンプルの動画は [`tools/video/make_title_video.py`](../tools/video/make_title_video.py) が作る。

### 大きさと向き不向き

MPEG-1 は古い方式で、同じ画質では新しい方式よりずっと大きい。実測 (調査 D の V0、VMAF 95 で最小のファイル) では、実写調の 10 秒が
720p で 3.9 MB、1080p で 7 MB。グラデーションと小さな文字の映像は、最良の設定でも VMAF が 80 前後で止まり、30 秒の映像が 20 MB になった。
そのため次のように使う。

- 短いクリップ (タイトル映像、カットシーンの数秒) 向け。長い映像は別の方式 (AV1、V1b) を待つ
- ベタ塗りの形と大きな文字で作る。細かい文字となめらかなグラデーションは崩れる
- 解像度は 720p まで。1 コマの復号は 720p で約 3 ms、1080p で約 6 ms (i5-12400、1 コア)。復号は専用の thread が先読みするので、描画の thread は待たない

## 音

音は動画と別のファイルで、既存の音の経路が鳴らす。動画の秒に合わせるのはゲームの仕事で、次の形にすると、一時停止と巻き戻しの後もずれない。

```cpp
if (!audioOn) { hud.music("title", false, 0.8f); audioOn = 1; }       // 映像の頭と同じフレームで始める
if (in.paused() != wasPaused)
{
    if (in.paused()) { hud.pauseMusic(); }                              // 一時停止の間は音も止める
    else             { hud.seekMusic(t); hud.resumeMusic(); }           // 戻るときに、映像の秒へ音を合わせてから続ける
}
```

音は GameMemory の外にあるので、リプレイの照合には入らない。

## カットシーンの JSON から映す

カットシーン ([CUTSCENES.md](CUTSCENES.md)) の `videos` に書くと、`Sequence` が秒から動画の位置を決める。

```json
"videos": [{"name": "title", "start": 0, "end": 8, "offset": 0, "speed": 1, "rect": [0, 0, 1, 1]}]
```

| フィールド | 中身 |
|---|---|
| `name` | `assets/video/<名前>.mpg` |
| `start` / `end` | 映す区間 (カットシーンの秒)。`end` を省くと `duration` |
| `offset` / `speed` | `start` の時の動画の秒と、進む速さ。動画の秒は `offset + (今 - start) × speed` |
| `rect` | 画面に対する割合 `[x, y, 幅, 高さ]`。既定は全画面 |

ゲームは `nr::drawSequenceVideos(s, seq, cut.time, Rect{0, 0, 1280, 720})` を呼ぶ。書いた順に重ねて描く。字幕と帯と暗転より前に呼ぶ。
動画の音は `cues` の `music` か `sound` を、動画の `start` と同じ時刻に置く。

## 描く先ごとの違い

| 描く先 | 動き |
|---|---|
| DX12 | `drawSprite` と同じ経路。カメラと変換に従い、色も GPU で掛ける。絵が替わるたびに GPU の写しの行だけを更新する |
| Dawn (WebGPU) | 画面に固定した矩形へ描く。色は host が画素に掛ける |
| DX11 (明示の fallback) | 画面に固定した矩形へ描く。色は host が画素に掛ける |
| ソフトウェア (`--headless`) | 最近傍で拡縮して重ねる。色は掛けられる |

同じフレームの中で、同じ動画を別の秒で 2 回描くと、DX12 では 2 つとも後の絵になる (GPU の写しが動画 1 本に 1 つのため)。別々の動画にするか、1 回だけ描く。

## 巻き戻しと撮影

- 絵は秒だけで決まるので、巻き戻すと動画も戻る
- ウィンドウで遊ぶ間は、復号が間に合わなかったフレームだけ直前の絵を出し続ける (落とした回数は端末に 1 度だけ知らせる)
- 撮影、`--headless`、リプレイの照合 (`asyncLoads` が false の実行) は、その番号の絵が復号されるまで待つ。同じ入力から同じ PNG ができる
- 過去の秒へ飛ぶと、最初から復号し直す (キーフレームの索引は持たない)。720p で動画の 10 秒目へ戻るには約 1 秒かかる (1 秒あたり約 0.1 秒)。巻き戻しのウィンドウで細かく動かすと、先の秒ほど待つ

## 配布

- `mitiru dist` は、`assets/video` に動画があるのに `features` に `video` が無いゲームを、組む前に止める
- `--check` は動画が配布物の assets へ写っているかを、pack に畳む前に見る。host が動画を描けなかったときの知らせ (`読めません`、`開けません`、`描けません`) も欠けとして拾う
- 動画は `assets.mtpak` に入り、`<DLL の dir>/assets/video/<名前>.mpg` の鍵で読まれる
- 配布物の `THIRD_PARTY_NOTICES.txt` に pl_mpeg (MIT) の表記が入る。`video` を使わないゲームの配布物には入らない
- MPEG-1 と MP2 の特許は失効していると pl_mpeg の README が書いている (弁護士の確認ではない)

## 決まり

| 項目 | 内容 |
|---|---|
| 形式 | MPEG-1 の映像だけ (MPEG-PS の `.mpg`)。8 ビット 4:2:0。音声の流れは読まない |
| 名前 | 96 byte まで (UTF-8)。サブフォルダは `/` で区切る |
| 時刻 | `floor(秒 × fps)`。PTS は見ない (pl_mpeg が見ない) |
| 最後の絵 | 書き出しで 1 枚足す。足さなくても、最後に復号できた絵を保つ |
| 復号 | host の専用 thread。1 本の動画に先読み 3 枚と後ろ 1 枚を持つ。使われなくなって 180 フレームたった動画は閉じる |
| 開けない | 読めない・形式が違うときは 1 度だけ知らせて描かない |
| feature が無い | 1 度だけ知らせて描かない |
