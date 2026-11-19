# MitiruMML 現代的な音源と制作の機能

OPNA の FM と SSG に加えて、現代的な曲 (シンセ、ドラム、サンプルのピアノやベース、サイドチェーン、ステム) を書くための音源と文法。
すべて 1 つの tick の時間軸 (四分音符 48 tick) の上で動くので、OPNA と同じ曲に混ぜられる。
同じ MML から、同じバイナリで毎回同じ PCM (16bit、ディザ込み) になる。

## 音源 (チップ)

| チップ | 中身 | 使うヘッダー |
|---|---|---|
| `synth` | 減算シンセ。オシレーター 2 つ (ノコギリ、矩形、三角、正弦、ノイズ)、ユニゾン 7 本 (supersaw)、サブ、フィルター (LP、HP、BP)、エンベロープ 2 つ、LFO。16 チャンネル × 8 声 | `SynthDriver.hpp` |
| `drums` | サンプルなしのドラム。キック、スネア、クローズドとオープンのハイハット、タム、シンバル、リムショット、クラップ | `DrumSynthDriver.hpp` |
| `sampler` | SF2 の GM の音色とドラム。16 チャンネル (9 番がパーカッション) × 12 声 | `SamplerDriver.hpp` |

3 つとも `ChipDevice` と `ChipDriver` の組で、`MultiChipSequencer::addChip` に足せる。OPNA と同じ曲で鳴らすときは、同じ `MultiChipSequencer` に両方を足す。

```cpp
MultiChipSequencer seq;
const int opna = seq.addChip(std::make_unique<OpnaDriver>());
auto sampler = std::make_unique<SamplerDriver>();
sampler->setSoundFont(sf2);
const int drums = seq.addChip(std::move(sampler));
seq.addTrack(opna, 0, "T120 O4 L4 CDEF", 0);
seq.addTrack(drums, 9, "T120 O2 [C R D R]4", 0);   // 9 番 = パーカッション。C = 36 = バスドラム、D = 38 = スネア
```

## MML の文法の拡張

拡張は、チャンネルが `ChannelCaps::extendedSyntax` を持つとき (上の 3 つ) だけ効く。OPNA のトラックでは今までと同じ。

| 書き方 | 意味 |
|---|---|
| `@n` | 音色を選ぶ。synth は組み込みの音色の番号、sampler は GM のプログラム番号 (0〜127) |
| `Xname=値` | パラメーターを設定する (下の表) |
| `Xname=a>b/長さ` | a から b まで、音長 (L と同じ書き方。`8` は八分音符、`4.` は付点四分) の間に動かす。時間は進まない |
| `Xname=>b/長さ` | 直前の値から動かす |
| `Xwave=saw` | 名前で選ぶ項目は名前でも書ける |
| `'CEG'4` | 和音。音は上へ向かって並べる。`'CEG>C'` のように `>` `<` でオクターブを動かせる |
| `Q1`〜`Q8` | 音を鳴らす長さの割合 (Q8 が最長、Q4 は半分) |

カットオフのように対数で聞こえる項目 (`cutoff`) は、ランプも対数で動く。ランプは 1 tick ごとの値になり、Driver が 8 ミリ秒の時定数でなめらかにつなぐ。

### アルペジエーター

`Xarp=up` (`off`、`up`、`down`、`updown`、`random`)、`Xarprate=16` (1 音の長さ。音長の値)、`Xarpoct=2` (使うオクターブ数)、`Xarpgate=70` (1 音の長さのうち鳴らす %)。
和音 `'CEG'` を書くと、その長さのあいだ、順番に 1 音ずつ鳴る。`random` はシードが (トラック、位置、音) で決まるので、毎回同じ並びになる。

### パラメーター (X)

| 名前 | 単位と範囲 | 意味 |
|---|---|---|
| `cutoff` | Hz 20〜20000 | フィルターのカットオフ |
| `res` | 0〜100 | レゾナンス |
| `fenv` | 半音 -96〜96 | フィルターエンベロープの量 |
| `fkey` | % 0〜100 | キートラッキング |
| `ftype` | `lp` `hp` `bp` | フィルターの種類 |
| `wave` `wave2` | `saw` `square` `tri` `sine` `noise` | オシレーター 1、2 の波形 |
| `mix2` | % 0〜100 | オシレーター 2 の量 |
| `detune` | セント -100〜100 | オシレーター 2 の離調 (うなり) |
| `tune2` | 半音 -24〜24 | オシレーター 2 の移調 |
| `unison` `udetune` `spread` | 本数 1〜7、セント 0〜100、% 0〜100 | supersaw: 重ねる本数、離調の幅、左右の広がり |
| `pw` | % 1〜99 | 矩形波のパルス幅 |
| `sub` `noise` | % 0〜100 | サブオシレーター (1 オクターブ下の正弦波)、ノイズの量 |
| `atk` `dec` `sus` `rel` | ミリ秒、%、ミリ秒 | アンプのエンベロープ (dec と rel は 60 dB 下がる時間) |
| `fatk` `fdec` `fsus` `frel` | 同上 | フィルターのエンベロープ |
| `lfor` `lfowave` | 0.1 Hz 単位、`sine` `tri` `saw` `square` `sh` | LFO の速さ、波形 |
| `lfop` `lfof` `lfoa` `lfopw` | セント、半音、%、% | LFO がピッチ、カットオフ、音量、パルス幅を揺らす深さ |
| `lfodelay` | ミリ秒 | LFO が全開になるまでの時間 |
| `gain` `vol` `pan` `tune` | %、0〜127、0〜127 (64 が中央)、セント | 音色の音量、チャンネルの音量 (V より細かい)、定位、音程 |
| `dec` `tune` `tone` `snap` | ミリ秒、セント、% 0〜100、% 0〜100 | ドラム: 減衰の長さ、音程、明るさ (スネアはノイズの量、ハイハットはフィルター、キックは歪み)、アタックの鋭さ |
| `rel` | ミリ秒 | sampler: リリースの長さ (0 なら SF2 の値) |
| `arp` `arprate` `arpoct` `arpgate` | 上のとおり | アルペジエーター |

チップが知らないパラメーターは無視される。名前を打ち間違えたときは、警告を出してそのコマンドを捨てる (音符として読まれない)。

### 組み込みの synth の音色

`@0` saw-bass、`@1` square-lead、`@2` supersaw、`@3` pad、`@4` pluck、`@5` sub-bass、`@6` tri-lead、`@7` wobble、`@8` riser、`@9` acid。

### ドラムの割り当て

- `drums` のチャンネル 0〜7 は種類 (0 キック、1 スネア、2 クローズドハイハット、3 タム、4 シンバル、5 リムショット、6 クラップ、7 オープンハイハット)。既存のリズム MML (`B S H F Y I`) は `addRhythmTrack(chip, 0, ...)` で鳴る。
- チャンネル 8 は GM の割り当て。O2 の C = 36 キック、D = 38 スネア、D# = 39 クラップ、F+ = 42 クローズドハイハット、A+ = 46 オープンハイハット、タムは 41〜50、シンバルは 49。
- `sampler` のチャンネル 9 は GM のパーカッション。鍵 (O2 の C = 36 など) が SF2 のドラムの種類。16〜21 は既存のリズム MML 用。

## 曲の文書 (`ModernSongScript`)

`#` で始まる行が曲の指示で、そのほかの行は直前の `#track` の MML。`;` で始まる行はコメント。

```
#tempo 124                               曲の最初のテンポ (MML の T は、その上に重なる)
#swing 0.54 grid=8                       表拍が周期に占める割合 (0.5 でなし、0.667 で 3 連符の跳ね)。grid は 4、8、16
#limiter ceiling=-1 release=90           マスターのリミッター。#limiter off で切る
#master gain=-1 width=1.15               マスターのゲイン (dB) とステレオ幅
#tail 2.0                                最後のイベントのあとに描く秒数
#loop 2                                 曲を流す回数
#dither on                               16bit にするときのディザ (既定は on)
#rate 48000                              サンプルレート (既定は 44100)
#sf2 path/to/file.sf2                    SF2 の読み込み (API では ModernSong::setSoundFont)
#group bass gain=-2                      グループのゲイン (dB)
#duck target=bass,pad by=kick depth=-9 attack=3 release=170
#duck target=pad by=drums mode=signal threshold=-34 ratio=10 lowpass=160 attack=2 release=140
#track lead chip=synth voice=supersaw group=lead
#track kick chip=drums drum=kick group=drums
#track beat chip=drums rhythm group=drums
#track keys chip=sampler voice=0 group=keys
#track hat chip=drums drum=hat group=drums noswing
#track lead2 chip=opm voice=lead pan=left vol=12 group=retro
```

`#track` の行は ChipSong と同じ読み取り (SongText.hpp) で、古いチップ (opna、opm、opl、opl2、opl3、sn76489、ay8910、nes) も同じ行で選べる。`ch=`、`pan=`、`vol=` も使える。書き方の全部は MML_REFERENCE.md の「Song text」。`chip=` を省くと OPNA の FM になる。

- トラックは `group` に属する。グループはバス: 自分のチップを持ち、別のグループとは何も共有しない。グループごとに別の WAV (ステム) で書き出せる (`ModernSong::exportStems`)。
- ステムは、グループのゲインとダッキングを掛けたあと、マスターのゲイン、幅、リミッターの手前の音。すべてのステムの和が、マスターが中立 (ゲイン 0 dB、幅 1、リミッターなし) のときのミックスになる。`send=` を使うと、エフェクトの戻りも `send_reverb`、`send_delay`、`send_chorus` のステムとして最後に並ぶ。
- `#duck` の `mode=event` (既定) は、`by` のトラック (またはグループの全トラック) の発音の時刻に合わせて、`depth` dB まで `attack` ミリ秒で下げ、`release` ミリ秒で 90% 戻す。検出がないので、深さと時刻が楽譜だけで決まる。`mode=signal` は `by` のグループの音を検出に使うサイドチェーン (`lowpass` でキックだけに反応させる)。
- テンポは全グループで共通。どのトラックの `T` も全グループに効き、同じ tick に別のテンポがあれば、トラックの並びが後ろのものが効く。
- ほかのプログラムが同じ文書を読む場合のために、知らない `#` の行は `ParseResult::unknownDirectives` に残る。
- `#let` (変数) と `#macro` (マクロ) は読み取りの前半 (SongText) が先に読んで MML に展開するので、`unknownDirectives` には残らない。`#track` の行の `send=reverb:30,delay:10,chorus:20` (量は % の 0〜100) は、リバーブ、ディレイ、コーラスの送りバスに効く (post-fader。`ModernSong::Options::sendReverb` などで送り先の設定を変えられる)。マクロの引数 (`#macro riff(o, len) = ...`、`!riff(4, 8)`) と整数の式 (`%a+1`、`%{a*2}`) も含めて、書き方は MML_REFERENCE.md の「変数、マクロ、エフェクトの送り」。

## 決定論

- 乱数 (ノイズ、ユニゾンの初期位相、S&H の LFO、ディザ、ランダムのアルペジオ) は、(チャンネルと発音の通し番号) などから作ったシードの xorshift だけを使う。`rand()` と時刻は使わない。
- チャンネルとグループは状態を共有しない。一部のトラックだけを描いても、残りの音は 1 サンプルも変わらない。
- 同じバイナリでは、スレッド数 (`parallelGroups`) にもよらず同じ PCM。ビルド環境をまたいだ一致は確かめていない (`sin`、`exp2`、`tan` を使う)。
