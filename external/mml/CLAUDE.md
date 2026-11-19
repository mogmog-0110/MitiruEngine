# MitiruMML - Claude Configuration

## Project Overview
MitiruMML は YM2608 (OPNA) FM音源エミュレーションベースの MML 音楽エンジン。
MitiruEngine の git submodule として `external/mml/` に配置される。

## 依存関係
```
MitiruEngine (親)
├── external/sgc/  ← ShiggyGameCore (submodule)
└── external/mml/  ← MitiruMML (このプロジェクト, submodule)
    └── external/ymfm/ ← ymfm YM2608エミュレータ (submodule, BSD-3)
```

- **上位**: MitiruEngine が依存する（MitiruMML は MitiruEngine に依存しない）
- **下位**: ymfm (YM2608チップエミュレーション)
- **SGC**: ShiggyGameCore には依存しない（完全独立）

## Language & Standard
- **C++20** / header-only + ymfm static library
- コンパイラ: MSVC 2022, GCC 12+, Clang 15+

## Build
```bash
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## Directory Structure
```
MitiruMML/
├── include/mitiru_mml/
│   ├── MmlTypes.hpp         — 共通型定義
│   ├── MmlParser.hpp        — MMLテキストパーサー
│   ├── Synthesizer.hpp      — ソフトウェア波形合成（旧方式、後方互換）
│   ├── Track.hpp            — トラック状態管理
│   ├── Sequencer.hpp        — ソフトウェアシーケンサー（旧方式）
│   ├── ChipDevice.hpp       — チップの口 層1: レジスタ書き込みとサンプル生成だけの Device
│   ├── ChipDriver.hpp       — チップの口 層2: 音符やイベントをレジスタ書き込みに変える Driver
│   ├── OpnaDevice.hpp       — YM2608 の Device（ymfm を包む）
│   ├── OpnaDriver.hpp       — YM2608 の Driver（FMドラム合成含む。ChipDriver を実装）
│   ├── OpnaVoice.hpp        — OPNA の FM 音色データ型
│   ├── Timeline.hpp         — チップ非依存のイベント、安定な並び、整数の時間軸（TickClock）
│   ├── MmlEventBuilder.hpp  — MML コマンド列 → チップ非依存のイベント
│   ├── MultiChipSequencer.hpp — 複数チップを同じ時間軸で鳴らし float ステレオで返す
│   ├── BoxDecimator.hpp     — 箱型フィルターの流し込み式デシメーター
│   ├── StereoBuffer.hpp     — float ステレオのバッファ
│   ├── OpmDevice/OpmDriver/OpmPresets.hpp — YM2151 (OPM)。FM 8ch、手で作ったプリセット 8 種
│   ├── OplDevice/OplDriver/OplPresets.hpp — YM3526、YM3812、YMF262 (OPL 系)。2 オペレーター FM、プリセット 8 種
│   ├── Sn76489Device/Sn76489Driver.hpp — SN76489 (emu76489、MIT)。矩形波 3 声とノイズ
│   ├── Ay8910Device/Ay8910Driver.hpp — AY-3-8910 / YM2149 (ymfm)。矩形波 3 声、ノイズ、エンベロープ
│   ├── NesApuDevice/NesApuDriver.hpp — NES の APU (vgsound_emu、zlib)。矩形波 2、三角波、ノイズ (DPCM は無音)
│   ├── DcBlocker.hpp        — 矩形波チップの直流除去
│   ├── ChipCatalog.hpp      — チップの種類、名前、音色、チャンネルの表と、古いチップの Driver の工場
│   ├── SongText.hpp         — 曲のテキスト (`#track` の行と # の指示) の読み取り。ChipSong と ModernSongScript が共有
│   ├── SongDefinitions.hpp  — `#let` の変数と `#macro` のマクロ (引数つき)、整数の式の定義と展開 (MML をパーサーへ渡す前の文字列の置き換え)
│   ├── SongLint.hpp         — 曲のテキストの検査 (音域外、音量の飽和、ループ点、未定義のマクロと変数)。行と列つきで報告する
│   ├── ChipSong.hpp         — 古いチップのトラックを MultiChipSequencer で鳴らす曲のテキストの読み手
│   ├── VgmMultiPlayer.hpp   — 複数チップの VGM プレイヤー (SN76489、OPM、OPNA、OPL 系、AY、NES)
│   ├── OpnaPresets.hpp      — FM音色プリセット12種（Furnace TFI実データ）
│   ├── OpnaSequencer.hpp    — OPNAシーケンサー（メイン再生エンジン）
│   ├── WavWriter.hpp        — PCM→WAV変換
│   ├── AudioOutput.hpp      — Win32 PlaySound再生
│   ├── MusicTheory.hpp      — スケール/コード/進行
│   ├── PatternLibrary.hpp   — ドラム/ベース/アルペジオパターン
│   ├── PhraseDictionary.hpp — 旋律フレーズ辞書（46フレーズ）
│   ├── PhraseComposer.hpp   — フレーズベース自動作曲
│   ├── SongBuilder.hpp      — 宣言的楽曲構築
│   ├── MusicPrompt.hpp      — AI向け音楽プロンプト
│   ├── AiComposer.hpp       — AI作曲支援フレームワーク
│   ├── MotifEngine.hpp      — モチーフ変奏エンジン
│   ├── MmlExtensions.hpp    — 拡張文法 (X パラメーター、和音) の読み取りとパラメーターの表
│   ├── ModernTrackState.hpp — 拡張文法をイベントに展開 (ランプ、和音、アルペジエーター、Q)
│   ├── ModernDsp.hpp / ModernRng.hpp — 現代的な音源が共有する DSP の部品と、シード固定の乱数
│   ├── SynthPatch.hpp / SynthDevice.hpp / SynthDriver.hpp — 減算シンセ (supersaw を含む)
│   ├── DrumSynthDevice.hpp / DrumSynthDriver.hpp — サンプルなしのドラムシンセ
│   ├── SamplerDevice.hpp / SamplerDriver.hpp — SF2 のストリーミング再生 (GM の音色とドラム)
│   ├── Dynamics.hpp         — リミッター、サイドチェーン、ダッキング、ステレオ幅、ディザ
│   ├── EventTransforms.hpp  — スウィング
│   ├── SendBus.hpp          — トラックの send= のバス (リバーブ、ディレイ、コーラス。既存の Reverb、Delay、Chorus を 1 サンプルずつ通す)
│   ├── ModernSong.hpp       — グループ (バス) ごとに描き、ステムとミックスを返す
│   ├── ModernSongScript.hpp — # の指示と MML の文書から ModernSong を作る
│   ├── LayerGains.hpp       — 層のゲインの自動操作 (LayerGainTrack)、小節の位置の表 (TickGrid)、LayerControl
│   ├── ChipStreamSource.hpp — チップ 1 つを 1 tick ずつ進めて引き出す音源と、足す口 (ChipMixStream)
│   ├── StreamingDynamics.hpp — リミッター、ダッキング、サイドチェーンを 1 フレームずつ流せる版 (一括版とビットが同じ)
│   ├── MultiChipStream.hpp  — MultiChipSequencer の曲を小さな塊で出すストリーミング
│   ├── ModernSongStream.hpp — ModernSong の曲 (ダッキング、リミッター込み) を小さな塊で出すストリーミング
│   ├── MmlValidator.hpp     — トラック長検証 (validate) と、曲のテキスト全体の検査の入口 (validateSong → SongLint)
│   ├── TfiImporter.hpp      — TFI音色ファイルインポート
│   └── MitiruMML.hpp        — アンブレラヘッダー
├── external/ymfm/           — YM2608 などのエミュレータ（submodule）
├── external/emu76489/       — SN76489 (MIT。取得したまま)
├── external/vgsound_emu/    — NES APU (zlib。2 か所を直した)
├── THIRD_PARTY_NOTICES      — インポートした外部ソースの出典と licence
├── tests/TestMml.cpp         — テスト（157ケース）
├── tests/TestGrammar.cpp     — マクロ、変数、send=、SongLint の検査と examples/ の曲 (mml_modern_tests)
├── tests/TestMacroArgs.cpp   — マクロの引数と整数の式の検査 (mml_modern_tests)
├── tests/TestSend.cpp        — send= がミックスに効くことの検査 (mml_modern_tests)
├── examples/*.mml            — マクロ、変数、引数、式、send= を使った曲のテキストの例 4 本
├── tests/TestFoundation.cpp  — チップの口、時間軸、複数チップの検査（ダミーチップは tests/DummyChip.hpp）
├── tests/TestOpnaFoundation.cpp — OPNA の載せ替えと、従来の出力との一致の検査
├── tests/MmlBench.cpp        — render の時間とハッシュを測る道具（Release でビルド）
├── tests/TestModern*.cpp     — 現代的な音源と制作の機能の検査 (mml_modern_tests)。tests/ModernHashes.hpp は例の曲のハッシュ表
├── tests/TestStreamMulti.cpp / TestStreamModern.cpp — ストリーミングと層の切り替えの検査。tests/StreamTestUtil.hpp は塊の切り方の道具
├── tests/ModernDemo.cpp      — 例の曲を WAV (ミックスとステム) に書く道具 (mml_modern_demo、Release)
├── MML_MODERN.md             — 現代的な音源と制作の機能の文法と使い方
└── MML_REFERENCE.md          — Claude作曲リファレンス
```

## MML文法（主要コマンド）
- 音符: `C D E F G A B` / シャープ `C+` / フラット `C-`
- 音長: `C4`=四分, `C8`=八分, `C4.`=付点
- 制御: `T120`=テンポ, `O4`=オクターブ, `L8`=デフォルト音長, `V12`=音量
- FM音色: `@FM0`〜`@FM11` (Piano/Bell/Brass/Strings/Organ/E.Piano/Bass/Flute/Koto/SynLead/Vibes/DistGt)
- SSG: `@SSG`, リズム: `@RHYTHM` (B=キック,S=スネア,H=ハイハット)
- パン: `P0`=左, `P1`=中央, `P2`=右
- ループ: `[CDEF]4`, ループポイント: `$`

## チップの口（2 層）
- Device（層1）: `nativeRate()` `write(port, reg, value)` `generate(float* LR, frames)` `saveState/loadState`。音楽の知識を持たない。
- Driver（層2）: `noteOn` `noteOff` `setVoice` `setVolume` `setPan` `setPitch` `setParam`。チャンネル番号は Driver が決める連番。
- 新しいチップは Device と Driver を 1 組作り、`MultiChipSequencer::addChip` に渡す。最小の例は `tests/DummyChip.hpp`。
- 同じ tick のイベントは (tick, トラック番号, 通し番号) で並ぶ。tick→サンプル数は整数（`TickClock`）。

## 複数チップ
- 曲のテキストは 1 つの形で、`#track [名前] chip=<チップ> [voice=] [ch=] [pan=] [vol=] [group=] [drum=] [part=] [rhythm] [noswing]` の行でトラックごとにチップを選ぶ。
  チップ: opna、opna-ssg、opna-rhythm、opm、opl、opl2、opl3、sn76489、ay8910、nes、synth、drums、sampler。書き方の全部は MML_REFERENCE.md の「Song text」。
- 読み取りは `SongText::parse` (SongText.hpp) の 1 本。チップの名前、音色、チャンネルの表は ChipCatalog.hpp。
- `#let 名前 = 整数` と `#macro 名前 = MML` (複数行は `#macro 名前` から `#endmacro`) は SongText が先に全部読み、MML の `!名前` と `%名前` (名前のあとが英数字なら `!{名前}`) を文字列のまま展開してから MmlParser へ渡す。定義は書く場所を問わない。定義が無い曲でも `!名前` は未定義のエラー (MmlParser は `!` を読み飛ばすので、音符として鳴ってしまうのを防ぐ)。
- マクロの引数: `#macro 名前(a, b) = 本体` と `!名前(1, 2)`。本体の `%a` が渡された文字 (整数か MML の断片) に置き換わる。引数は呼んだ側で先に展開してから渡す。整数の式は `%a+1` (`%名前` の直後に `+ - *` と項を空白なしで続ける) と `%{(a+b)*2}` (中は名前に % が要らない)。足し算、引き算、掛け算、括弧、±1000000000 まで。MmlParser は変えていない (SongDefinitions.hpp の展開だけ)。
- 報告の位置: 本体を調べるだけで分かる問題 (未定義の名前、本体の中の呼び出しの引数の数、読めない式) は `SongDefinitions::validate` が本体を書いた位置に 1 度だけ出す。値が決まって初めて分かる問題 (整数でない引数を式に使った、式の値が大きすぎる) は、展開中に一番外側の呼び出しの位置に `(マクロ '名前' の中)` つきで出す (呼ばれないマクロには出ない)。
- `send=reverb:30,delay:10` は `ModernSongScript` が `ModernSong::TrackSpec::sends` (0〜1) に入れてミックスに効かせる (`SendBus`)。送りのあるトラックだけ、そのトラックだけを描く Driver をもう 1 組作り、グループのゲインとダッキングを掛けた音をモノラルにしてバスへ足す。戻りは グループの和のあと、マスターの手前に足し、`send_reverb`、`send_delay`、`send_chorus` のステムにもなる。`send=` を書かない曲はこの経路に入らず、出力は 1 ビットも変わらない。`ChipSong` は読んで無視する。`ChipTrackDecl::sends` と `ModernSongScript::ParseResult::sends` にも値が残る。
- 報告は `N 行目 M 列: ...` の形 (列が分からないものは `N 行目: ...`)。`MmlCommand::offset` は MmlParser が読んだ位置で、SongLint が展開前の行と列に戻すのに使う。
- 検査は `MmlValidator::validateSong(text)`。Error は書いたとおりに鳴らない物 (未定義、対応しない `[` `]`)、Warning は鳴るが意図と違うかもしれない物 (音域外、V の 15 超え、T の範囲外、`$` の食い違い、使われない定義)。
- `ModernSongScript::parse(text, soundFont)` が全部のチップと `#tempo`、`#swing`、`#tail`、`#group`、`#duck` などの指示を読み、グループごとのステムとミックスを返す (ModernSong)。
- `ChipSong::build(text)` は古いチップだけを `MultiChipSequencer` で鳴らす。synth、drums、sampler のトラックはエラー、# の指示は `ignored` に残る。
- `chip=` を省いたトラックは OPNA の FM。`#track` が 1 つも無いテキストは OPNA の FM 1 本。#track があるのにその前に MML があるとエラー。
- MmlParser は `X` `x` `'` を 1 つのコマンドとして読む。拡張文法が無いチャンネル (OPNA を含む古いチップ) では、X は捨てられ、和音は最初の音だけが鳴る。
- 音程は周期や F-Number の整数で決まるチップが多く、12 平均律から 1 セントを超えてずれる音がある (各 Driver の先頭の説明)。
- 本物の SF2 (GeneralUser GS など) は環境変数 MITIRU_MML_TEST_SF2 にパスを渡すと tests/TestRealSf2.cpp が動く。無ければスキップ。ファイルはリポジトリに含めない。
- 外部ソース: external/emu76489 は取得したまま。external/vgsound_emu/src/nes は 2 か所を直してあり (ノイズの LFSR と三角波の出力)、zlib の条件どおり先頭に明記した。THIRD_PARTY_NOTICES を見ること。

## ストリーミングと層の切り替え
- 一括の render() と別に、小さな塊 (512 フレームなど) ずつ取り出す口がある。`MultiChipStream(MultiChipSequencer&, loopCount)` と `ModernSongStream(const ModernSong&)`。使い方は MML_REFERENCE.md の「Streaming and adaptive layers」。
- 約束: 塊の切り方を変えても、取り出した float が一括の render() の mix とビットまで同じ。主な検査は tests/TestStreamMulti.cpp と TestStreamModern.cpp (1 フレームずつから曲全体まで、ばらばらの大きさ、全例の曲、テンポの変化、ループ、効いているリミッター)。
- 同じにする仕組み: チップへの generate() を一括と同じ 1 tick ごとの塊で呼び、箱型フィルターへの流し込みも同じ区切りにする。引き出す塊の大きさは、内部の FIFO から取り出す量にしか効かない。`BoxDecimatorStereo::finalizedFrames()` までしか外へ渡さない (finish() に捨てられる最後の 1 フレームを出さないため)。
- `TimelineCursor` は playTimeline を 1 tick ずつ進められる版。playTimeline 自体は変えていない。
- 層 (レイヤー): ModernSongStream はグループ、MultiChipStream はチップ。外しても鳴らし続け、出口のゲインだけを出力のフレーム番号で動かす (`scheduleLayerGain`、`scheduleLayerGainAtBar`)。実時間も乱数も使わない。ダッキングのあと、グループを足す手前で掛かる。
- 処理済みのフレームより前を指す命令は次のフレームから始まる (`processedFrames()`)。その場合だけ、結果が塊の切り方に依存する。
- 一括の render() が使う private の準備 (チャンネルの割り当て、イベント、Driver の生成) は ModernSong の `prepare()` と `makeGroupDrivers()` に切り出して、ModernSongStream (friend) と共有している。
- OpnaDevice、OpmDevice、OplDevice の reset() は ymfm のチップを作り直す。ymfm の reset() だけではエンベロープと LFO のカウンターが残り、同じシーケンサーの 2 回目の再生が 1 回目と違う PCM になったため。1 回目の PCM は変わらない。
- send= の送りも一括と同じ `SendBus` で処理する (`ModernSongStream::SendSource` が、送りのあるトラックごとのチップ 1 組と ChipMixStream を持つ)。層を外すと、そのグループの送りも止まる。検査は tests/TestStreamModern.cpp の `sendCoverageSong`。
- 未対応: OpnaSequencer::renderStereo() (旧方式のミックスバス)、ModernSong のステム別の取り出し。

## Known Issues
- ADPCM ROMデータ未搭載（FMドラム合成で代替）
- SSGハードウェアエンベロープの活用が限定的
- 自動作曲(PhraseComposer)は品質に限界あり。Claude手書きMMLを推奨

## Coding Standards
- CLAUDE.md は ShiggyGameCore の規則に準拠
- ファイル: UTF-8 BOM付き
- インデント: タブ
- ブレース: Allman style
- コメント: 日本語Doxygen
- TEST_CASE名: ASCII限定
