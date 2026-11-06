# 録画リプレイを動画にする

`.mtrr` を再生しながら、毎フレームの画像と音を撮り、走り終えたあとに 1 本の AVI (Motion JPEG + 16bit PCM) にまとめる。
依存は増やさない。H.264 の符号化器も Media Foundation も使わないので、N エディションの Windows でも動く。
設計の理由は ADR 0134。

## 使い方

```
mitiru_host <game.dll> --replay-test run.mtrr --export-video out.avi
mitiru_host <game.dll> --replay-test run.mtrr --export-video out.avi --headless-3d --backend dx12   # 実 GPU の画像 (3D、UI)
mitiru_host <game.dll> --input-script play.txt --max-frames 600 --export-video out.avi   # --replay-test か --max-frames が要る
```

- ウィンドウは出ない。`--headless-3d` は GPU を使う。`--backend dx12` を付けないと「3D の絵を描けない」で止まる環境がある。 <!-- wording-ok: 起動時の通知の原文 -->
- 画像は 1 フレーム 1 枚で、フレームレートは 60 fps 固定 (`targetTps`)。長さは再生したフレーム数 / 60 秒。
- 走っている間は `out.avi.frames/` に PNG の連番と `out.avi.audio.wav` が溜まる。まとめ終えたら消す。失敗したときは調べられるよう残し、次に同じ出力先で走らせるときに消す。
  PNG は 1 分で 2D が 200 MB 前後 (rewind、neon_asteroids の実測)。3D は測っていないが、もっと大きい。出力先のドライブの空きを先に見る。
- 止まる条件が要る。`--replay-test` か `--max-frames N` のどちらかが無いと、PNG を止まるまで書き続けてディスクを埋めるので、起動時に断る (`--replay` の GUI 再生は EOF で止まらないので使えない)。
- `--capture-dir` / `--capture-every` / `--audio-capture` とは一緒に使えない。撮り方は `--export-video` が決める。
- 終了コード 5 は、AVI を書けなかったとき (理由は 1 行で出る)。

撮ったあとの PNG と WAV が既にあるなら、まとめるだけの `mitiru_avi` を使う。ゲームも host も動かさない。

```
mitiru_avi --frames cap_dir --audio run.wav --out out.avi [--fps 60] [--quality 90]
```

`cap_dir` には `frame_000000.png` から途切れず並べる。`--audio` を省くと映像だけになる。

## 何が入るか

| 項目 | 中身 |
|---|---|
| 映像 | MJPEG。品質 90 は色差を半分に間引く (どの再生ソフトでも開ける形)。95 以上は間引かない |
| 音 | 48 kHz ステレオの 16bit PCM。WAV は float32 なので、1.0 を超えた分は丸める (割れた音は割れたまま) |
| 並び | 画像 1 枚のすぐ後に、その update の 800 サンプル (1/60 秒) |
| 画像と音の対応 | n 枚目は n 回目の update の後。音の n 番目の区間は、その update で積んだ音から始まる |
| 索引 | idx1 (全部キーフレーム)。OpenDML は使わない |
| 上限 | 2 GB 未満。近づくと理由を出して止まる。そのときは `--max-frames` で分けるか画質を下げる |

録画の最後の update は画像を持たず音だけ回るので、WAV が画像より 800 サンプル長くなる。そこは捨てる。

## 確かめ方

`ctest -R "^e2e_video_export$"` と `ctest -R "^AVI: "`。verify_kit の fixture で、n 枚目の円の x、音の鳴り始め (30 フレーム目の左、90 フレーム目の右)、
2 回書き出したバイト列の一致を数字で見る。ffprobe / ffmpeg があれば、同じフレーム数と音の形式が読めて復号の警告が無いことも見る。

## まだ無いもの

- H.264 / mp4。必要なら AVI を ffmpeg に渡す (エンジンには同梱しない)。
- 2 GB を超える長さ。OpenDML が要る。
- Windows 標準のプレイヤーでの再生は未確認。
- `mitiru` CLI の 1 コマンド化 (別リポジトリ)。
