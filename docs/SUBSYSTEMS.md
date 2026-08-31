# Subsystems: 全system単独起動

MitiruEngineはengineを構成する各subsystemを 単独のexeとして起動できる。rendererだけ・audioだけ・inputだけを、game logic / CEF / inspectorを一切loadせずに動かせる。

これは [SCOPE.md](SCOPE.md)の **「全system単独起動」** のdeliverableであり、アトミックツール哲学(必要なものしか画面に出さない)のengine内verにあたる。Unix philosophyの「1プログラム = 1仕事」をengine内部にも適用したもの。

## なぜ単独起動か

- **学習**。engineが「機能別に小さく分解されている」ことが学習者から見える。renderer subsystemを起動すれば、gameもsceneも無い状態で描画パイプラインだけが動いていることが目で確認できる。
- **debugの二分**。「rendererが壊れているのかgameplayが壊れているのか」を切り分けられる。subsystem単独で再現すれば原因の層が確定する。
- **iteration**。shader / pipeline編集中にgameを起動せずrenderer subsystemだけをcold-start (CEF init無しで1s未満)で回せる。

各subsystemは同じ`mitiru::Engine`を使うが、`EngineConfig::enableCef = false`でCEFを切り、gameplay層を一切持たない。host-game境界が本物で、game codeにload-bearingでないことの証明にもなっている(hostとgameはCの関数と生データだけで会話する設計)。

## 一覧

| subsystem | 何を見せるか | 起動(CLI) | 起動(exe直) |
|---|---|---|---|
| renderer | grid +水平に往復するrect (描画パイプライン単独) | `mitiru renderer` | `mitiru_subsys_renderer.exe` |
| audio | 440Hz tone + RMS level meter | `mitiru audio [file]` | `mitiru_subsys_audio.exe` |
| input | 256-key live grid + mouse panel + press log | `mitiru input` | `mitiru_subsys_input.exe` |
| scene | 12 entityが独立animate (反射 +回転) | `mitiru scene` | `mitiru_subsys_scene.exe` |

CLI列は別repo `mitiru-cli`のsubcommand。sourceは`examples/subsys/mitiru_subsys_<name>/`、exeは従来どおり`build/examples/mitiru_subsys_<name>/`に出力され、release zipでは`mitiru.exe`と並んで同梱される。終了は基本ESC。

record / replay (録画リプレイ)はhostの実経路(`mitiru_host <game>.dll --record <f>` → `--replay-test <f>`)で
bit-exact (1 bitも違わず一致)を検証する。

## 各subsystemがisolateする層

### renderer (`mitiru_subsys_renderer`)
`Engine` + `Screen` + 60Hz update/draw loopだけ。silver-grayのSaturn背景に64pxのgridを敷き、中央の60x60 rectが水平に往復し、左上に`frame: N`カウンタが出る。描画backend (DX11等)のvisual smokeを兼ねる。描画パイプラインをgame抜きでisolateする。

### audio (`mitiru_subsys_audio`)
`Engine` + miniaudioの`ma_device`がaudio threadで`SineSynth`からsampleをpullする。MIDI 69 (A4 = 440Hz)の連続正弦波をdefault出力deviceに流し、その出力RMSを中央のlevel meter barで可視化する。5.0sでauto-exit。deviceが無ければmeterは静止する(graceful degradation)。audio mixer / device層をisolateする。

### input (`mitiru_subsys_input`)
`Engine` +毎フレームpullした`InputState`だけ。raw 256-key VK tableを16×16 gridで表示し(押下中はSaturn red)、mouse座標とL/M/R button状態、press/releaseをnewest-firstのscroll logで出す。engineが毎フレーム何を見ているかを手で確かめる窓。入力plumbing層をisolateする。

### scene (`mitiru_subsys_scene`)
`Engine`のGame/update/draw contractだけでper-frameのscene loopを回す。12 entityが独立した(vel, angularSpeed)を持ち、playfield rectの縁で反射しながらangleを積分する(決定的LCGでlayout固定)。full ECSではなく、「engineのloop contractが他の層抜きでsceneを回せる」ことの証明。per-frame scene loopをisolateする。

### replay: hostの`--record` / `--replay-test`
deterministic入力channelの証明はhostの実経路そのもので行う:
`mitiru_host <game>.dll --record run.mtrr`
が毎フレームの`InputSnapshot` (1フレーム分の入力をまとめたPOD struct)と、
ゲームの全状態を1個のstructにまとめたもの(型名`GameMemory`)のbytesを記録し、
`--replay-test run.mtrr`が同じ入力列を再投入して全フレームのゲームの全状態が
bit-exactに一致するかを検証する。`InputSnapshot`がgame logic
への唯一の入力channelなので、同じ入力列は同じ状態列を必ず再現する。

## 哲学との関係

各subsystem exeはCEFもgame logicもloadせず、**該当subsystemだけ**を画面に出す。これは「必要なものしか画面に出さない」の最も直接的な実装で、文脈外の機能(使わないrenderer、使わないaudio)を視界に存在させない。1関心事 = 1 exe = 1 window。mega editorが50 panelを同時に見せる状態の対極にある。
