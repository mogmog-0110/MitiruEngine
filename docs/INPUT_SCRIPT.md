# 入力台本 — `--input-script` / `--input-record`

`mitiru_host <game.dll> --input-script <file>` は、台本に書いた入力を OS を経由せずゲームへ直接流す。
窓を前面に出さずに操作を再現できるので、`--headless --capture-dir` や `--record` / `--replay-test` と組んで
自動検証に使う。`--input-record <file>` は実際に遊んだ入力を同じ形式で書き出す。録った台本はそのまま再生できる。

## 行の形式

1 行 1 イベント。先頭の数字はフレーム番号（0 始まり、固定 60fps）。`#` 以降はコメント。

| 形式 | 意味 |
|---|---|
| `<frame> <KEY> <down\|up>` | キーを押す / 離す。`<frame> <down\|up> <KEY>` の順でもよい |
| `<frame> MouseL <down\|up>` | マウスボタン。`MouseL` / `MouseR` / `MouseM` / `MouseX1` (戻る) / `MouseX2` (進む) |
| `<frame> ime <文字列> [キャレット]` | IME で変換中の文字列を置く（次の `ime` 行まで保持。`-` で変換の終わり。キャレットは先頭からの byte 数で、省くと末尾。`in.imeComposition()`） |
| `<frame> wheel <縦> [横]` | そのフレームにホイールを回す（ノッチ数。縦は + が奥、横は + が右。`in.wheel()` / `in.wheelH()`） |
| `<frame> pos <x> <y>` | カーソルの絶対座標（論理スクリーン座標）を置く。次の `pos` まで保持される |
| `<frame> pos <x> <y> <frames>` | 直前の `pos` からその位置まで、`frames` フレームかけて直線で動かす |
| `<frame> move <dx> <dy> [frames]` | マウスの移動量だけを注入する（視点を回す game 用。座標は動かない） |

`KEY` は `Left` `Right` `Up` `Down` `Space` `Enter` `Escape` `A`〜`Z` `0`〜`9` など（`mitiru::Key` と同じ名前）。

`pos` で位置が動いたぶんは、実マウスと同じく移動量（`mouseDeltaX/Y`）にも出る。

## 例: 掴んで運ぶ

```
# (300,300) で押し、30 フレームかけて (900,500) まで運んで離す
10 pos 300 300
12 MouseL down
13 pos 900 500 30
45 MouseL up
```

`pos` をボタンより前の行に書くと「その位置で押した」になる。`--input-record` の出力もこの順で書かれる。

## 検証の組み方

```
mitiru_host game.dll --headless --input-script play.txt --record play.mtrr --capture-dir cap --max-frames 300
mitiru_host game.dll --replay-test play.mtrr
```

3D を描く game は `--headless-3d --backend dx12` を足す（`--headless` だけだと 2D しか描かれない）。
