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
