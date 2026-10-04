# 入力台本 — `--input-script` / `--input-record`

`mitiru_host <game.dll> --input-script <file>` は、台本に書いた入力を OS を経由せずゲームへ直接流す。
窓を前面に出さずに操作を再現できるので、`--headless --capture-dir` や `--record` / `--replay-test` と組んで
自動検証に使う。`--input-record <file>` は実際に遊んだ入力を同じ形式で書き出す。録った台本はそのまま再生できる。

## 行の形式

1 行 1 イベント。先頭はフレーム番号（0 始まり、固定 60fps）か `t=<秒>`。`#` 以降はコメント。
読めない行があると host は `ファイル:行: 理由` を出して起動をやめる（終了コード 2）。キー名を間違えた台本が
何も押さないまま最後まで流れることはない。

| 形式 | 意味 |
|---|---|
| `<frame> <KEY> <down\|up>` | キーを押す / 離す。`<frame> <down\|up> <KEY>` の順でもよい |
| `<frame> press <KEY>` | そのフレームで押し、次のフレームで離す |
| `t=<秒> ...` | 秒で書く。`t=1.5 press F5` は 90 フレーム目。`move` の長さも秒になる |
| `<frame> MouseL <down\|up>` | マウスボタン。`MouseL` / `MouseR` / `MouseM` / `MouseX1` (戻る) / `MouseX2` (進む) |
| `<frame> ime <文字列> [キャレット]` | IME で変換中の文字列を置く（次の `ime` 行まで保持。`-` で変換の終わり。キャレットは先頭からの byte 数で、省くと末尾。`in.imeComposition()`） |
| `<frame> wheel <縦> [横]` | そのフレームにホイールを回す（ノッチ数。縦は + が奥、横は + が右。`in.wheel()` / `in.wheelH()`） |
| `<frame> pos <x> <y>` | カーソルの絶対座標（論理スクリーン座標）を置く。次の `pos` まで保持される |
| `<frame> pos <x> <y> <frames>` | 直前の `pos` からその位置まで、`frames` フレームかけて直線で動かす |
| `<frame> move <dx> <dy> [frames]` | マウスの移動量だけを注入する（視点を回す game 用。座標は動かない） |

## キー名

`KEY` には `mitiru::Key` の列挙子と同じ名前を書く。どちらも `include/mitiru/input/KeyNames.hpp` の表から作っている。
大文字小文字は区別しない。キー割り当ての設定ファイル (`key:F5`) も同じ名前で読む。

| 種類 | 名前 |
|---|---|
| 英字・数字 | `A`〜`Z`、`0`〜`9` (`Digit0`〜`Digit9` でもよい。`Num0` はどちらの 0 か決まらないので誤りにする) |
| 矢印 | `Left` `Right` `Up` `Down` |
| 編集 | `Space` `Enter` `Escape` `Tab` `Backspace` `Delete` `Insert` `Home` `End` `PageUp` `PageDown` `Pause` `CapsLock` |
| 修飾 | `Shift` `Ctrl` `Alt` `LShift` `RShift` `LCtrl` `RCtrl` `LAlt` `RAlt` |
| ファンクション | `F1`〜`F12` |
| テンキー | `Numpad0`〜`Numpad9` |
| 記号 | `Semicolon` `Comma` `Minus` `Period` `Slash` `Backquote` |
| 別名 | `Return` (= Enter) `Esc` (= Escape) `Control` (= Ctrl) `Back` (= Backspace) |
| 表に無いキー | 仮想キーコードの数 (`186`、`0xBA`、`VK186`) |

知らない名前を書くと、いちばん近い名前を添えて止まる。

```
mitiru: --input-script の台本を読めません。play.txt:12: 知らないキー名 'Escpae'。近い名前は Escape (使える名前は docs/INPUT_SCRIPT.md)
```

`pos` で位置が動いたぶんは、実マウスと同じく移動量（`mouseDeltaX/Y`）にも出る。

## パッド

実機を挿さずにパッドの入力を流せる。`in.padDown()` / `in.leftStick()` / `in.pad(n)` などからそのまま読める。

| 形式 | 意味 |
|---|---|
| `<frame> pad[N] <BUTTON> <down\|up>` | ボタンを押す / 離す。`BUTTON` は `A` `B` `X` `Y` `LB` `RB` `Start` `Back` `LS` `RS` `Up` `Down` `Left` `Right` (十字キー) |
| `<frame> axis[N] <AXIS> <値>` | 軸を置く（次に同じ軸を書くまで保持）。`LX` `LY` `RX` `RY` は -1〜1 (上が +)、`LT` `RT` は 0〜1 |
| `<frame> pad[N] TOUCHPAD <down\|up>` | タッチパッドを押し込む / 離す (`in.pad(n).touchpadPressed()`) |
| `<frame> gyro[N] <x> <y> <z>` | ジャイロ (rad/s) を置く。`in.pad(n).gyro()` に届き、`motionActive()` が立つ |
| `<frame> accel[N] <x> <y> <z>` | 加速度 (m/s^2) を置く (`in.pad(n).accel()`) |
| `<frame> touch[N] <指 1\|2> <x> <y>` / `<frame> touch[N] <指> up` | タッチパッドに指を置く (x, y は 0〜1、左上が 0) / 離す (`in.pad(n).touch(i)`) |

`N` は 1〜4 で、省くと 1 台目（`pad2 A down` は 2 台目の A）。台本に出てきたパッドは frame 0 から
接続済みになる。台本にパッドの行が 1 行でもあれば、パッドの欄 (機種・電池・ジャイロ・タッチの拡張の欄も) は台本だけで決まり、
実機のパッドは無視される。台本のパッドの機種は Standard、電池は不明 (-1) に見える。

```
# 10 フレーム目から左スティックを右へ倒し、30 フレーム目に A
10 axis LX 1.0
30 pad A down
31 pad A up
40 axis LX 0
```

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

F7〜F12 は host が使う ([HOST_KEYS.md](HOST_KEYS.md))。host は実際のキーボードを読むので、台本で押した F7〜F12 はゲームにだけ届き、host の操作は起きない。
