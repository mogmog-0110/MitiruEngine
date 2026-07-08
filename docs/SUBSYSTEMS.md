# Subsystems — 全 system 単独起動

MitiruEngine は engine を構成する各 subsystem を **単独の exe として起動できる**。renderer だけ・audio だけ・input だけを、game logic / CEF / inspector を一切 load せずに動かせる。

これは [SCOPE.md](SCOPE.md) の **「全 system 単独起動」** の deliverable であり、アトミックツール哲学 (必要なものしか画面に出さない) の engine 内 ver にあたる。Unix philosophy の「1 プログラム = 1 仕事」を engine 内部にも適用したもの。

## なぜ単独起動か

- **学習**: engine が「機能別に小さく分解されている」ことが学習者から見える。renderer subsystem を起動すれば、game も scene も無い状態で描画パイプラインだけが動いていることが目で確認できる。
- **debug の二分**: 「renderer が壊れているのか gameplay が壊れているのか」を切り分けられる。subsystem 単独で再現すれば原因の層が確定する。
- **iteration**: shader / pipeline 編集中に game を起動せず renderer subsystem だけを cold-start (CEF init 無しで 1s 未満) で回せる。

各 subsystem は同じ `mitiru::Engine` を使うが、`EngineConfig::enableCef = false` で CEF を切り、gameplay 層を一切持たない。host-game 境界が本物で、game code に load-bearing でないことの証明にもなっている (host と game は C の関数と生データだけで会話する設計)。

## 一覧

| subsystem | 何を見せるか | 起動 (CLI) | 起動 (exe 直) |
|---|---|---|---|
| renderer | grid + 水平に往復する rect (描画パイプライン単独) | `mitiru renderer` | `mitiru_subsys_renderer.exe` |
| audio | 440Hz tone + RMS level meter | `mitiru audio [file]` | `mitiru_subsys_audio.exe` |
| input | 256-key live grid + mouse panel + press log | `mitiru input` | `mitiru_subsys_input.exe` |
| scene | 12 entity が独立 animate (反射 + 回転) | `mitiru scene` | `mitiru_subsys_scene.exe` |

CLI 列は別 repo `mitiru-cli` の subcommand。source は `examples/subsys/mitiru_subsys_<name>/`、exe は従来どおり `build/examples/mitiru_subsys_<name>/` に出力され、release zip では `mitiru.exe` と並んで同梱される。終了は基本 ESC。

record / replay (録画リプレイ) は host の実経路 (`mitiru_host <game>.dll --record <f>` → `--replay-test <f>`) で
bit-exact (1 bit も違わず一致) を検証する。

## 各 subsystem が isolate する層

### renderer (`mitiru_subsys_renderer`)
`Engine` + `Screen` + 60Hz update/draw loop だけ。silver-gray の Saturn 背景に 64px の grid を敷き、中央の 60x60 rect が水平に往復し、左上に `frame: N` カウンタが出る。描画 backend (DX11 等) の visual smoke を兼ねる。**描画パイプライン**を game 抜きで isolate する。

### audio (`mitiru_subsys_audio`)
`Engine` + miniaudio の `ma_device` が audio thread で `SineSynth` から sample を pull する。MIDI 69 (A4 = 440Hz) の連続正弦波を default 出力 device に流し、その出力 RMS を中央の level meter bar で可視化する。5.0s で auto-exit。device が無ければ meter は静止する (graceful degradation)。**audio mixer / device 層**を isolate する。

### input (`mitiru_subsys_input`)
`Engine` + 毎フレーム pull した `InputState` だけ。raw 256-key VK table を 16×16 grid で表示し (押下中は Saturn red)、mouse 座標と L/M/R button 状態、press/release を newest-first の scroll log で出す。engine が毎フレーム何を見ているかを手で確かめる窓。**入力 plumbing 層**を isolate する。

### scene (`mitiru_subsys_scene`)
`Engine` の Game/update/draw contract だけで per-frame の scene loop を回す。12 entity が独立した (vel, angularSpeed) を持ち、playfield rect の縁で反射しながら angle を積分する (決定的 LCG で layout 固定)。full ECS ではなく、「engine の loop contract が他の層抜きで scene を回せる」ことの証明。**per-frame scene loop**を isolate する。

### replay — host の `--record` / `--replay-test`
deterministic 入力 channel の証明は host の実経路そのもので行う:
`mitiru_host <game>.dll --record run.mtrr`
が毎フレームの `InputSnapshot` (1 フレーム分の入力をまとめた POD struct) と、
ゲームの全状態を 1 個の struct にまとめたもの (型名 `GameMemory`) の bytes を記録し、
`--replay-test run.mtrr` が同じ入力列を再投入して全フレームのゲームの全状態が
bit-exact に一致するかを検証する。`InputSnapshot` が game logic
への唯一の入力 channel なので、同じ入力列は同じ状態列を必ず再現する。

## 哲学との関係

各 subsystem exe は CEF も game logic も load せず、**該当 subsystem だけ**を画面に出す。これは「必要なものしか画面に出さない」の最も直接的な実装で、文脈外の機能 (使わない renderer、使わない audio) を視界に存在させない。1 関心事 = 1 exe = 1 window。mega editor が 50 panel を同時に見せる状態の対極にある。
