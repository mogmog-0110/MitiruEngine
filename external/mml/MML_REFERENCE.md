# MitiruMML 作曲リファレンス

## MML基本文法

### 音符
C D E F G A B — ドレミファソラシ
C+ / C# — 半音上（シャープ）
C- — 半音下（フラット）
R — 休符

### 音長
数字で指定: 1=全音符, 2=二分, 4=四分, 8=八分, 16=十六分, 32=三十二分
. — 付点（1.5倍）
& — タイ（同じ音を接続）
~ — スラー/レガート（異なる音をkey-offなしで接続）

### 制御コマンド
T120 — テンポ (BPM)
O4 — オクターブ (1-8, 中央C=O4C)
L8 — デフォルト音長
V12 — トラック音量 (0-15)
v10 — ノート音量 (0-15, 次の1音のみ)
Q7 — ゲートタイム (1-8, 8=レガート)
> — オクターブ上げ
< — オクターブ下げ
[ ] — ループ: [CDEF]4 = 4回繰り返し

### 拡張コマンド
@FM0-@FM11 — FM音色プリセット選択
@SSG — SSG矩形波モード
@RHYTHM — リズムモード
W25 — デューティ比 (12/25/50/75)
H3 — デチューン (セント, ±)
M5,8 — ビブラート (速度Hz, 深さセント)
M5,8,100 — ビブラート+遅延 (ms)
Y6,3 — トレモロ (速度Hz, 深さ)
_ — ポルタメント（次の音へスライド）
{CD}E — 装飾音（C→Dを素早く経過してEへ）
(CDE) — クレッシェンド
SE9,500 — SSGハードウェアエンベロープ

### リズムモード音符
B=バスドラム S=スネア H=ハイハット T=タム Y=シンバル I=リムショット R=休符

## FMプリセット音色表

| # | 名前 | 特徴 | 適する用途 |
|---|------|------|-----------|
| 0 | Piano | 明るいアタック、速い減衰 | メロディ、伴奏、イントロ |
| 1 | Bell | 金属的、長い余韻 | アクセント、SE、神秘的場面 |
| 2 | Brass | 力強い、豊かな持続音 | メインメロディ（戦闘/壮大） |
| 3 | Strings | 柔らかいアタック、長い持続 | パッド、ハーモニー、感動場面 |
| 4 | Organ | 持続的、暖かい | 教会、荘厳、ハーモニー |
| 5 | E.Piano | 温かみのある減衰音 | VN/恋愛シーン、メランコリー |
| 6 | Bass | 短く太い低音 | ベースライン全般 |
| 7 | Flute | 澄んだ、ピュアな音 | カウンターメロディ、牧歌 |
| 8 | Harpsichord | 明るい撥弦音 | バロック風、アクセント |
| 9 | SynthLead | 明るく目立つ | リード、カウンター |
| 10 | Vibraphone | 金属的で温かい | ジャズ風、夜の場面 |
| 11 | DistGuitar | 歪んだ重い音 | ロック、ボス戦 |

## トラック構成のテンプレート

### 基本5トラック構成
```
Track 1 [FM melody  ] — メインメロディ（@FM0/2/5）
Track 2 [FM harmony ] — ハーモニー/カウンター（@FM3/4/7）
Track 3 [FM bass    ] — ベース（@FM6）
Track 4 [SSG arp    ] — アルペジオ装飾（@SSG）
Track 5 [RHYTHM     ] — ドラム（@RHYTHM）
```

### 軽量3トラック構成
```
Track 1 [FM melody  ] — メロディ
Track 2 [FM bass    ] — ベース
Track 3 [RHYTHM     ] — ドラム
```

## 調と音名の早見表

### Cメジャー: C D E F G A B
### Aマイナー: A B C D E F G
### Dマイナー: D E F G A B- C (MMLでは B- を使う)
### Eマイナー: E F# G A B C D (MMLでは F+ を使う)
### Fマイナー: F G A- B- C D- E- (MMLでは A-, D-, E- を使う)
### Gメジャー: G A B C D E F# (MMLでは F+ を使う)
### Fメジャー: F G A B- C D E (MMLでは B- を使う)
### Cマイナー: C D E- F G A- B- (MMLでは E-, A-, B- を使う)

## コード→MML変換

### Am調の主要コード
- Am (Im): A C E → O3 A4 >C4 E4 or arp O4 L16 ACEA
- Dm (IVm): D F A → O3 D4 F4 A4
- E7 (V7): E G# B D → O3 E4 G+4 B4 >D4
- F (VI): F A C
- G (VII): G B D

### Cメジャー調の主要コード
- C (I): C E G
- F (IV): F A C
- G (V): G B D
- Am (vi): A C E

### Dマイナー調の主要コード
- Dm (Im): D F A
- Gm (IVm): G B- D
- A7 (V7): A C+ E G
- B- (VI): B- D F
- C (VII): C E G

### Eマイナー調の主要コード
- Em (Im): E G B
- Am (IVm): A C E
- B7 (V7): B D+ F+ A
- C (VI): C E G
- D (VII): D F+ A

### Fマイナー調の主要コード
- Fm (Im): F A- C
- B-m (IVm): B- D- F
- C7 (V7): C E G B-
- D- (VI): D- F A-
- E- (VII): E- G B-

### Cマイナー調の主要コード
- Cm (Im): C E- G
- Fm (IVm): F A- C
- G7 (V7): G B D F
- A- (VI): A- C E-
- B- (VII): B- D F

### Fメジャー調の主要コード
- F (I): F A C
- B- (IV): B- D F
- C (V): C E G
- Dm (vi): D F A

### Gメジャー調の主要コード
- G (I): G B D
- C (IV): C E G
- D (V): D F+ A
- Em (vi): E G B

## 作曲ガイドライン

### メロディの原則
1. 順次進行（2度）を基本にし、跳躍（3度以上）はアクセントとして使う
2. 跳躍の後は反対方向へ順次進行で戻す
3. フレーズは4小節単位で構成（問い→答え）
4. 最後はトニック（1度）に解決する
5. 同じ音の連続は2回まで。3回以上はリズムの変化を付ける

### ハーモニーの原則
1. メロディの3度下or6度上で並行させると安全
2. コードトーンを長い音符で鳴らす（全音符or二分音符）
3. メロディが動いているときはハーモニーは静止、その逆も

### ベースの原則
1. 各小節の頭でルート音を鳴らす
2. 小節後半で5度に移動すると安定感が出る
3. 次のコードのルートに半音で接近すると（経過音）滑らか

### SSGアルペジオの原則
1. コード構成音を16分音符で繰り返す
2. 上昇→下降パターンが最も自然
3. 音量は控えめ（V7程度）

### ドラムパターン
- 8ビート基本: L8 BHSHBHSH (1小節)
- バラード: L8 B4HSHB4HSH (1小節 = 8個の8分音符相当)
- 速い曲: L16 BRHRBHSHBRHRBHSH (1小節 = 16個の16分音符)
- ハーフタイム: L8 B4H4SH B4H4SH

### 曲構成
- イントロ: 2-4小節（メロディの一部を予告）
- Aセクション: 8小節（メインテーマ）
- Bセクション: 8小節（対照的なメロディ/転調）
- A'セクション: 8小節（Aの変奏、終止形で締める）

### 小節数の合わせ方（重要！）
全トラックの小節数を完全に一致させること。
BPMとL値から1小節の音符数:
- L1 = 1音/小節
- L2 = 2音/小節
- L4 = 4音/小節
- L8 = 8音/小節
- L16 = 16音/小節
例: 16小節の曲でL8 = 128個の8分音符相当の音価が必要。
音価の混合（L4とL8を混ぜる等）は可能だが、合計拍数が全トラック同一であること。

### 音価計算の早見表
- 全音符(1) = 8分音符8個分
- 二分音符(2) = 8分音符4個分
- 付点二分音符(2.) = 8分音符6個分
- 四分音符(4) = 8分音符2個分
- 付点四分音符(4.) = 8分音符3個分
- 八分音符(8) = 8分音符1個分
- 十六分音符(16) = 8分音符0.5個分

### MMLコード進行の実装パターン

#### ベースラインでコード進行を表現（最も基本的）
```
; Am -> Dm -> E7 -> Am (各2小節、計8小節、L8)
O3 L8 A4 E4 A4 E4 A4 E4 A4 E4      ; Am (16個の8分音符 = 2小節)
D4 A4 D4 A4 D4 A4 D4 A4              ; Dm
E4 B4 E4 B4 E4 B4 E4 B4              ; E7
A4 E4 A4 E4 A4 E4 A4 E4              ; Am
```

#### ハーモニーでコードを表現（ロングトーン）
```
; Cm -> Fm -> G7 -> Cm (各2小節)
O4 L1 E-1 G1  A-1 >C1  B1 D1  E-1 G1
```

## FM Preset Data Sources

| # | Name | Source | Authentic |
|---|------|--------|-----------|
| 0 | Piano | VALSOUND Aco Piano2 | Yes |
| 1 | Bell | Furnace TFI bell.tfi | Yes |
| 2 | Brass | Furnace TFI trumpet.tfi | Yes |
| 3 | Strings | Furnace TFI guitar.tfi adapted | Adapted |
| 4 | Organ | Furnace TFI organ.tfi | Yes |
| 5 | E.Piano | Furnace TFI elecbass.tfi adapted | Adapted |
| 6 | Bass | Furnace TFI bass.tfi | Yes |
| 7 | Flute | VALSOUND Old Flute | Yes |
| 8 | Harpsichord | VALSOUND Koto | Yes |
| 9 | SynthLead | Furnace TFI guitar.tfi | Yes |
| 10 | Vibraphone | Furnace TFI bell.tfi adapted | Adapted |
| 11 | DistGuitar | Furnace TFI distguit.tfi | Yes |

9/12 presets are from real instrument data. 3 are adapted variants.

## Composition Checklist

Before finalizing a composition, verify the following:

1. **Track alignment** — All tracks have the same total beat count
2. **Tempo consistency** — T command is at the start of every track (or inherited)
3. **Octave range** — Melody stays within O3-O6, bass in O2-O3
4. **Volume balance** — Melody V12-15, harmony V8-10, bass V10-12, SSG V6-8, drums V10-12
5. **Gate time** — Q6-7 for staccato, Q8 for legato passages
6. **Loop count** — Verify `[...]N` repeat counts match intended length
7. **Key signature** — All accidentals consistent with stated key
8. **Ending** — Final note resolves to tonic on beat 1
9. **Rest placement** — Breathing space between phrases (at least R8 every 4 bars)
10. **Preset selection** — FM preset matches intended timbre (see table above)

## Song text (`#track` lines, read by `ModernSongScript` and `ChipSong`)

One text form covers every chip. The MML after a `#track` line is ordinary MML, unchanged.

```
#track [name] chip=<chip> [voice=<name|number>] [ch=<channel>] [pan=left|center|right] [vol=0-15]
              [group=<group>] [drum=<kind>] [part=fm|ssg|rhythm] [rhythm] [noswing]
```

- `name` is the one word right after `#track` (used by `#duck by=`). Without it the track is `track1`, `track2`, ... Any other bare word is an error, except the flags `rhythm` and `noswing`.
- `chip` omitted means `opna` (FM). `opna-ssg` is `chip=opna part=ssg`, `opna-rhythm` is `chip=opna rhythm`.
- `pan=` and `vol=` are the same as writing `P` and `V` at the start of the MML. `ch=` is the driver channel; without it the chip's melodic channels are used in declaration order.
- Text without any `#track` line is one OPNA FM track (sample-identical to `OpnaSequencer::addFmTrack(mml, 0)` rendered with `renderStereo()`). When the text has a `#track` line, MML before the first one is an error.
- A line starting with `;` is a comment. Every other line starting with `#` is a directive (below). Bad lines are reported as `N 行目: ...`.

| chip | channels (auto order) | voice |
|---|---|---|
| opna (alias ym2608) | FM 0-5 (0-2 when an opna-rhythm track exists in `ChipSong`) | 0-11 or piano, bell, brass, strings, organ, epiano, bass, flute, koto, synthlead, vibes, distguitar |
| opna-ssg (ssg) | SSG 6-8 | none |
| opna-rhythm (rhythm) | drums, B S H F Y I rhythm MML | none |
| opm (ym2151) | 0-7 | epiano, brass, bass, strings, lead, bell, organ, pluck |
| opl (ym3526), opl2 (ym3812) | 0-8 | same names as opm |
| opl3 (ymf262) | 0-17 | same names as opm |
| sn76489 (psg, sn) | tone 0-2; noise only by `ch=noise` | noise only: white, periodic |
| ay8910 (ay, ym2149) | 0-2 (also `ch=a`..`c`) | tone, noise, both |
| nes (apu, 2a03) | pulse1, pulse2, triangle; noise only by `ch=noise` | pulse: duty50, duty25, duty12, duty75; noise: long, short |
| synth | 0-15 | 0-9 or saw-bass, square-lead, supersaw, pad, pluck, sub-bass, tri-lead, wobble, riser, acid (extended MML: `@n`, `X`, chords, `Q`) |
| drums | `drum=kick snare hat tom cymbal rim clap openhat` (one kind per track), `drum=kit` or no `drum=` (GM key map), `rhythm` (B S H F Y I) | none |
| sampler (sample, sf2) | 0-15 except 9; `drum=kit` or `percussion=on` is the GM percussion channel 9 | GM program 0-127 |

`group=` puts the track on a bus; only `ModernSongScript` mixes groups. A chip that runs out of channels is an error, not a spill onto another channel.

Directives read by `ModernSongScript` (`ChipSong` leaves them in `ignored`; other readers get them in `unknownDirectives`):

```
#tempo 124                               starting tempo (MML T goes on top)
#swing 0.54 grid=8                       share of the beat taken by the downbeat (0.5 = none, 0.667 = triplet feel); grid 4, 8, 16
#limiter ceiling=-1 release=90           master limiter; #limiter off
#master gain=-1 width=1.15               master gain (dB) and stereo width
#tail 2.0                                seconds rendered after the last event
#loop 2    #dither on    #rate 48000
#sf2 path/to/file.sf2                    SF2 for sampler tracks (API: ModernSong::setSoundFont)
#group bass gain=-2                      group (bus) gain in dB
#duck target=bass,pad by=kick depth=-9 attack=3 release=170
#duck target=pad by=drums mode=signal threshold=-34 ratio=10 lowpass=160
```

Which reader to use:

- `ModernSongScript::parse(text, soundFont)` reads the whole text and plays every chip in groups with the mix features (stems, ducking, limiter). It is the general entry point; classic chips are mixed at their native gain, so balance them with `vol=` and `#group gain=`.
- `ChipSong::build(text)` plays the classic chips (opna, opm, opl, sn76489, ay8910, nes) on one `MultiChipSequencer` with the fixed master (0.8 gain, soft clip). A `synth`, `drums` or `sampler` track is an error there, and every directive is left in `ignored`.

Both readers share `SongText::parse`, so the same line gives the same track in either.

OPNA tracks do not use the extended syntax: an `X...=...` command is read as one command and dropped, and a chord `'CEG'` plays its first tone only.

```
#tempo 140
#track lead chip=opm voice=lead pan=left group=lead
O5 L8 E G A B >C4 <B A G E4 R4 [EG]2 A4 B4 >C2
#track noise chip=sn76489 ch=noise group=chip
O4 V9 L16 [CRRR]16
#track kick chip=drums drum=kick group=drums
O4 L4 [CCCC]4
```

Pitch note: SN76489, AY-3-8910 and NES pulse divide a clock by an integer, OPL by a 10-bit F-Number, so some notes are
off the 12-tone scale by more than one cent. See the comment at the top of each Driver.

## Streaming and adaptive layers (`MultiChipStream`, `ModernSongStream`)

`render()` paints the whole song before returning. The two stream classes hand out PCM a few hundred frames at a time instead, so a game can start playing at once, loop, and change the music while it plays. They are opt-in: `render()` and every existing hash are unchanged.

```cpp
// Classic chips (ChipSong, MultiChipSequencer): float or 16-bit, 44100 Hz stereo
auto song = mitiru_mml::ChipSong::build(text);          // song.sequencer is a MultiChipSequencer
mitiru_mml::MultiChipStream stream(*song.sequencer);   // borrows the sequencer; do not render it elsewhere meanwhile
std::int16_t pcm[512 * 2];
while (stream.readPcm16(pcm, 512) > 0) { /* hand 512 frames to the audio device */ }

// Everything else (ModernSongScript, groups, ducking, limiter, stems): owns its own chips
auto parsed = mitiru_mml::ModernSongScript::parse(text);
mitiru_mml::ModernSongStream modern(parsed.song);       // a snapshot: the song may change or go away afterwards
float mix[512 * 2];
std::size_t n = modern.read(mix, 512);
```

- `read(out, frames)` returns `frames` until the song ends, then a smaller number, then 0. `finished()` says whether anything is left. `position()` is the number of frames handed out so far.
- **Same bits as the one-shot render, for any chunk size.** Concatenating the chunks gives exactly the `render()` float mix (`MultiChipSequencer::render()`, `ModernSong::render().mix`), and `readPcm16()` gives exactly `toPcm16Stereo()` / `Result::toPcm16(dither)`. One frame at a time, 512 at a time, the whole song at once, or random sizes: all identical. The streams drive every chip with the same per-tick `generate()` calls as the one-shot path and only vary how much they pop from an internal FIFO, so even a Device whose output depends on the block size stays identical.
- `ModernSongStream` runs the same mix chain frame by frame (group gain, event ducking, sidechain ducking, group sum, master gain, stereo width, limiter, dither). The limiter looks 3 ms ahead (default), so the mix lags the chips by that much inside the stream; nothing is dropped at the end. For `readPcm16()` the dither noise advances in the order frames are handed out, so use either `read()` or `readPcm16()` for one stream, not both.
- Cost: one tick of every chip is generated whenever the FIFO runs dry (about 11 ms of music at 120 BPM). The first `read()` after construction can take about one tick of work; later reads spread evenly. OPNA is generated at about 998 kHz, so it is the expensive chip. Measured in a Release build (one `read()` of 512 frames, budget 11.6 ms at 44100 Hz): `ModernSongStream` on a 6-track song (drums, synth, OPM, SN76489, both duck modes, limiter) took 0.41 ms on average and 1.95 ms at worst (about 28x real time); `MultiChipStream` on 4 OPNA tracks (FM x2, SSG, rhythm) took 0.17 ms on average and 0.43 ms at worst. The hidden test `[.stream-bench]` in `mml_modern_tests` prints these numbers.
- Starting again: `MultiChipStream` resets the chips when it is constructed, so a new stream on the same sequencer starts from the first sample again with the same bits. (`OpnaDevice`, `OpmDevice` and `OplDevice` now rebuild the ymfm chip in `reset()`: ymfm's own reset leaves envelope and LFO counters behind, which made a second playback of the same sequencer differ from the first. The first playback is unchanged, so no existing hash moved.)
- `OpnaSequencer::renderStereo()` (the old mono effect bus path) is not streamed. A text with no `#track` line goes through `ChipSong` / `ModernSongScript` as one OPNA FM track, which is.

### Changing the music with the game state

A **layer** is something that can be turned down or up while the song keeps running: in `ModernSongStream` a layer is a group (`findLayer("pad")` looks it up by name), in `MultiChipStream` a layer is a chip (the number `addChip` returned). A muted layer is still playing silently, so when it comes back it is in step with the others.

```cpp
const std::size_t pad = modern.findLayer("pad");
// Fade the pad out over 50 ms starting at the first bar line at least 0.5 s from now
std::uint64_t start = modern.scheduleLayerGainAtBar(pad, modern.position() + 22050, 0.0f, 2205);
// ... later, bring it back on a beat instead of a bar
modern.scheduleLayerGainAtBar(pad, modern.position() + 22050, 1.0f, 2205,
                              mitiru_mml::FadeCurve::EqualPower, mitiru_mml::TickGrid::TICKS_PER_BEAT);
// Or by absolute output frame, e.g. from the game frame counter at 60 fps:  gameFrame * 735
modern.scheduleLayerGain(pad, gameFrame * 735ull, 0.0f, 0);          // 0 frames of ramp = a hard switch
```

- Targets are 0 (silent) to 1 (original level); a ramp runs from wherever the gain is when the command starts. `FadeCurve::EqualPower` (default) goes up as sin and down as cos, so fading one layer out while another fades in keeps the loudness steady; `FadeCurve::Linear` ramps the amplitude.
- A bar is 192 ticks (4/4); pass `ticksPerBar` for another length, or `TickGrid::TICKS_PER_BEAT` for beats. The boundaries come from the song's own tempo changes and loops, computed up front without rendering, so `nextBarFrame(frame)` can be asked at any time. When no bar line is left in the song it returns `frame` unchanged.
- **Deterministic by construction.** Commands name output frame numbers (or "the first bar line at or after frame F"), never wall-clock time or random numbers. The same song and the same commands produce the same bits whatever the chunk sizes are. For a game loop that reads 735 frames per game frame, issue commands with a start frame in the future; replaying the same game inputs reproduces the same audio.
- **One caveat: late commands.** Gains are applied to frames as they are processed, which is slightly ahead of `position()` (the internal block, plus the limiter look-ahead in `ModernSongStream`). A command whose start frame is before `processedFrames()` starts at `processedFrames()` instead, and then the result depends on how the caller chunked its reads. Keep a margin (0.1 s or more) or use bar/beat boundaries.
- In `ModernSongStream` the layer gain is applied after the group gain and the ducking and before the groups are summed, so sidechain and event ducking keep reacting to a muted group (a muted kick still pumps the bass). The layer gain multiplies by exactly 1.0 when unused, so it costs nothing and leaves the bits alone.
- `scheduleLayerGain*` throws `std::out_of_range` for a layer number that does not exist, and `std::invalid_argument` for a target outside 0 to 1 or NaN.
- **Threads.** A stream is not thread-safe. Calling `scheduleLayerGain*` on the game thread while an audio callback runs `read()` is a data race. Either call `read()` and the schedule functions from the same thread (the game loop reads a chunk per game frame and queues the PCM to the device), or guard both with one lock.

## 変数、マクロ、エフェクトの送り

`#track` の行を使う曲のテキスト (`ModernSongScript`、`ChipSong`) の文法の拡張。`OpnaSequencer::addFmTrack` などに MML だけを直接渡すときは、今までと同じで何も展開されない。
MML の中に `!名前` と `%名前` を書かず、`#let` と `#macro` も 1 つも書かない曲は、読み方が 1 文字も変わらない (出力 PCM のハッシュも同じ)。`send=` を書かない曲の出力も、1 ビットも変わらない。

### 変数 `#let`

```
#let bpm = 140
#let lead = 12
#tempo %bpm
#track lead chip=opm vol=%lead
V%lead O5 C D E
```

- 値は整数 (-100000〜100000)。`%名前` が 10 進の数字に置き換わるので、`V%lead`、`T%bpm`、`H%detune` のように MML の数の位置に書く。
- `%名前` は MML の中と、`#` の指示の行 (`#tempo %bpm`、`#track ... vol=%lead`) で使える。`#sf2` のパスは置き換えない。
- 名前のあとに英数字か `_` が続くときは `%{名前}` と書く (`O%{oct}4`)。

#### 整数の式

```
#let base = 3
V%base+1          ; V4
O%{base*2-1}      ; O5。{ } の中は名前に % が要らず、空白も書ける
L%base*(%base+1)  ; L12
T%{(base+1)*30}   ; T120
```

- 足し算 `+`、引き算 `-`、掛け算 `*`、括弧、単項のマイナス。掛け算が先、あとは左から。割り算とあまりは無い。
- 式の書き方は 2 つ。`%名前` の直後に、`+`、`-`、`*` と項 (数字、`%名前`、`%{ }`、`( )`) を空白なしで続ける (`%a+1`、`%a*(%b+2)`)。または `%{ }` の中に全部を書く (`%{a+1}`、`%{ (a+b) * 2 }`)。
- 演算子のあとに項が続かない形は式にならない。`C%a +1`、`C%a+`、`C%a-C` は、これまでどおり `%a` の値だけが置き換わる。`+` と `-` の前後に空白が要るときは、`%a` と書いて空白で区切る。
- 値は ±1000000000 まで。大きすぎる値、読めない式、未定義の名前はエラーで、書いた位置が出る。
- `#` の指示の行でも使える (`#tempo %bpm*2`、`vol=%lead-2`)。
- 式の中の名前は、整数の変数か、マクロの引数 (整数のとき)。`#let` の値そのものは、式ではなく整数だけ。

### マクロ `#macro`

```
#macro riff = O4 L8 CDEG
#macro walk
O2 L4 V10   ; 本体は #endmacro まで。; 以降は注釈で、展開されない
E E G E
#endmacro
#track lead chip=opm
[!riff]3 !{riff}
```

- `!名前` が本体の MML に置き換わる。置き換えるだけなので、`[!riff]3` のようにループの中でも、`!riff` の中に `[ ]` や `$` があっても、書き写したのと同じに鳴る。
- 本体の中で別のマクロと変数を使える。自分を呼ぶ循環はエラー。引数のあるマクロは次の節。
- 名前は英字か `_` で始まり、英数字と `_` が続く (大文字と小文字は別の名前)。`!riff2` は `riff2` という名前なので、直後に数字を置くときは `!{riff}2` と書く。
- 1 行で `#macro 名前 = 本体`、複数行なら `#macro 名前` から `#endmacro` まで。複数行の本体は空白 1 つでつながる。本体の途中に `#` の行は書けない。
- 定義は曲全体に効く。使う行より後ろに書いてもよい。同じ名前を 2 度定義するとエラー。
- 定義が 1 つも無い曲でも、MML の中の `!名前` と `%名前` は未定義のエラーになる (MmlParser は `!` を読み飛ばすので、マクロを呼んだつもりの文字が音符として鳴るのを防ぐ)。

#### マクロの引数

```
#macro riff(o, len) = O%o L%len CDEG
#macro twice(x) = %x %x
#macro bar(root)          ; 複数行でも名前の直後に ( ) を書く
O2 L8 [%root %root >%root< %root]2
#endmacro
#track lead chip=opm
!riff(4, 8) !riff(%base+1, 16) !twice(O4 [CD]2) !bar(A-)
```

- `#macro 名前(a, b) = 本体`。呼ぶときは `!名前(1, 2)`、`!{名前}(1, 2)`。引数は 16 個まで。
- 本体の中の `%a` が、渡された文字に置き換わる。渡すのは整数でも、MML の断片 (`CDE`、`[CD]2`、`O4 L8 G E`) でもよい。同じ名前の `#let` の変数より引数が先に使われ、本体の外には効かない。
- 引数はコンマで区切る。`( )`、`[ ]`、`{ }` の中のコンマは区切りにならない。前後の空白は無視する。`!m(, C)` のように空にもできる。
- 呼び出しの `( )` は 1 行の中に書く (引数を行にまたがって書くことはできない。長いときはマクロを分ける)。
- 引数は呼んだ側で先に展開してから渡す (`!riff(%base+1, 16)` の `%base+1` は呼んだ側の変数で計算される。`!twice(!riff(2, 4))` のように引数の中でマクロを呼べる)。
- 整数の式の中で `%a+1`、`%{a*2}` のように使うときは、渡された文字が整数でなければならない。そうでなければ、呼んだ位置にエラーが出る。
- 引数のあるマクロは、必ず `( )` を付けて呼び、数が違えばエラー。引数のないマクロの直後の `(` は、これまでどおり MML の文字として残る。

### エフェクトの送り `send=`

```
#track keys chip=opl voice=epiano send=reverb:35,chorus:20
```

- `名前:量` をコンマでつなぐ。名前は `reverb`、`delay`、`chorus`、量は % の 0〜100 (変数と式も使える)。同じ名前は 1 度だけ。
- `ModernSongScript` (`ModernSong`) では、ミックスに効く。`ChipSong` (古いチップだけを `MultiChipSequencer` で鳴らす) は、読んで検査するだけで音は変わらない。値は `ChipTrackDecl::sends`、`ModernSongScript::ParseResult::sends` と `ModernSong::TrackSpec::sends` (0〜1) に残る。
- 送りは、トラックの音に、グループのゲインとダッキングを掛けたあと (post-fader) の音を、モノラルにして、エフェクトごとのバスに足す。バスは既存の `Reverb`、`Delay`、`Chorus` (`MixBus` と同じ部品) を 1 サンプルずつ通し、戻りは左右に同じ値で、グループを足したあと、マスターのゲイン、幅、リミッターの手前に足す。トラックの dry の音 (グループのステム) は変わらない。
- 送りは dry に足すだけなので、量 100% で、`ModernSong::Options::sendReverb`、`sendDelay`、`sendChorus` の wet の量が戻る。既定は `Reverb` と `Delay` の既定値と、`Chorus` の mix 0.7。エフェクトの設定 (部屋の大きさ、ディレイの時間など) はこの 3 つに書く。
- 戻りは、ステムにも `send_reverb`、`send_delay`、`send_chorus` の名前で足される (使ったエフェクトだけ)。ステムの和がミックスになる関係は保たれる。同じ名前のグループは作れない (`std::invalid_argument`)。
- 送りのあるトラックは、同じグループのほかのトラックと別に、そのトラックだけをもう 1 度描く。描く量が増える (OPNA などの重いチップに多く付けると効く)。送りに入るのはこの単独で描いた音なので、同じチップのほかのトラックと共有する状態 (OPNA の LFO など) まで、dry の中のそのトラックの音と同じとは限らない。
- 量が 0 のエフェクトは、書かないのと同じ (出力は 1 ビットも変わらない)。範囲外は `std::invalid_argument`。
- 曲の終わりは、最後のイベントのあと `#tail` の秒数まで。残響がそれより長いときは、`#tail` を伸ばす。
- `ModernSongStream` も同じ `SendBus` を通すので、塊の切り方によらず `render()` と同じ float になる。層 (`scheduleLayerGain`) を外したグループのトラックの送りも止まる (エフェクトの中の残響は、そのまま鳴り終わる)。

### 検査 `MmlValidator::validateSong(text)`

曲のテキスト全体を、マクロと変数を展開したあとの MML で検査し、書いた位置 (展開される前の行と列) つきで報告する。マクロの中の問題は、マクロを呼んだ位置に出て、末尾に `(マクロ '名前' の中)` が付く。

```
5 行目 3 列: V20 は音量の最大 15 を超えて飽和します (15 に丸められます) (マクロ 'loud' の中)
```

| 重さ | 内容 |
|---|---|
| Error | 未定義のマクロと変数、マクロの循環、同じ名前の再定義、`#endmacro` の欠け、対応しない `[` と `]`、`#track` の行の読めない項目、マクロの引数の数の違いと閉じない `(`、読めない式、整数でない引数の式 |
| Warning | 音域外の音 (チップごとに Driver が選ぶ値が目標から 50 セントを超えてずれる音)、`O` が 1〜8 の外、`>` と `<` が端を越える、`V` と `v` が 15 を超える (飽和)、`T` が 20〜300 の外、`]0` と `]1`、`$` が 2 つ目、`$` が `[ ]` の中、トラックごとに `$` の位置や長さが違う、`X` の値が範囲の外、OPNA などで読み捨てられる `X` と和音、使われないマクロと変数 |

`SongValidation::tracks` にトラックごとの長さと `$` の位置が tick (四分音符 48 tick) で入る。
引数をつけた呼び出しの中の問題も、一番外側の呼び出し (`!名前(` の `!`) の位置に出る。引数にした文字 (`!loud(V20)` の `V20`) も、そのマクロの中として扱う。マクロの本体を調べるだけで分かる間違い (未定義の名前、引数の数、読めない式) は、本体を書いた位置に 1 度だけ出て、末尾に `(マクロ '名前' の本体)` が付く。値が決まって初めて分かる間違い (整数でない引数を式に使った、式の値が大きすぎる) は、そのマクロを呼んだ位置に出る。

例は `examples/` の `macro_chiptune.mml`、`variables_synth.mml`、`sends_and_macros.mml`、`macro_arguments.mml`。
