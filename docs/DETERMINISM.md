# 決定論はどこまで保証されるか

ADR 0005 は「bit-exact 再現の構造保証」を謳い、`.mtrr` は ABI 指紋 (`kWireApiVersion`) と
`envTag[64]` (`include/mitiru/replay/Recorder.hpp:20,21`) を持つ。だが「何が保証されないか」を
書いた一次文書がこれまで無かった。

他エンジンは 3 者とも保証しない範囲を明記している:

- **Unity Burst** `FloatMode.Deterministic` — 全サポートプラットフォームで一貫した結果を返すが
  **64-bit アーキテクチャのみ**、かつ **NaN のビット表現の一致は保証しない**
  ([Unity Burst マニュアル](https://docs.unity3d.com/Packages/com.unity.burst@1.8/manual/compilation-burstcompile.html))。
- **Factorio** の deterministic lockstep — サーバーはユーザー入力だけを送る。desync の原因は
  コア/MOD/ハードウェアの誤計算であり、「ネットワーク遅延・性能問題は desync の原因にならない」
  と明記 ([Factorio Wiki: Desynchronization](https://wiki.factorio.com/Desynchronization))。
- **Overwatch** (GDC 2017, Tim Ford) — 公開一次資料からは「ネットワーク化されたシミュレーションが
  responsiveness と precision のために決定論をどう活用しているか」までしか裏取りできず、
  更新順・ラグ補償の具体・固定小数点の有無は非公開。

MitiruEngine も同じ形式 (**保証する / 保証しない / 検出手段**) で書く。新しい仕組みは作らず、
既にあるもの (ABI 指紋・envTag・決定論オラクル・`replay --expect` ゲート・`--synctest`) を
対応づけるだけ。

## 保証範囲

| 項目 | 保証する | 保証しない | 検出手段 |
|---|---|---|---|
| 同一ビルド・同一 CPU 機能 (AVX2 有無) | 同じ `.mtrr` を同じバイナリ・同じ CPU で再生すれば bit-exact | 同じバイナリを i5-12400 (Alder Lake) と i7-13700F (Raptor Lake) で流して一致した (下の「別 CPU の実測」)。AVX2 を持たない CPU・AMD・Arm は測れていない (Unity Burst と同じ「64-bit アーキテクチャのみ」相当の制約に近い) | `--synctest [K]` (毎フレーム K フレーム前から再シミュレーションして memcmp)、`oracleDeterminism` |
| 固定ステップ (`deterministic=true` / `targetTps`) | 同じ `InputSnapshot` 列 → 同じ `GameMemory` 列 (POD かつ乱数・時刻が `InputSnapshot::rngSeed` 経由のみの場合) | `deterministic=false` (実時間 dt) での再生一致は保証外 — リフレッシュレート依存で加算順が変わる | `EngineConfig::deterministic`、`replay --test` |
| 浮動小数点モード | ビルド設定 (Debug/Release とも同一コンパイラ・同一最適化フラグ) 内での再現 | コンパイラ/最適化フラグを跨いだ FP 演算順序の一致は未検証。NaN のビットパターンの一致も保証しない (Unity Burst と同じ立場) | `--synctest`、golden `.mtrr` の byte 比較 (`Player::diffState`) |
| スレッド数 | シングルスレッドの `tickFixedUpdatePhase` 経路 | 並列化された subsystem (将来追加されれば) を跨いだ実行順の一致は対象外。現状 hot path は非並列なので該当なし | 該当なし (非並列であることが保証の前提) |
| 外部ライブラリ (Jolt / NativeEngine / miniaudio) | ライブラリ自身が決定論的な API を使っている前提での再現 (Mitiru 側は入力を POD で渡すだけ) | ライブラリ内部の決定論はライブラリ側の保証に依存する。Box2D だけは Mitiru も測った (下の行と「別 CPU の実測」)。Jolt・NativeEngine・miniaudio は検証していない | `--synctest`、golden replay ゲート (`tests/replay_golden/*.mtrr`) |
| DX11 / DX12 backend 差 | 同一 backend 内での再生一致 | backend をまたいだ描画結果の bit-exact 一致は主張しない (描画は決定論オラクルの対象外。`gfxBackend` は `docs/CONFIG.md` の「起動時のみ」参照) | 画面オラクル (`oracleScreenCheck`) は不変検知のみで backend 間比較はしない |
| 数学関数 (`std::sin` `std::cos` `std::pow` など) | host が起動時に UCRT の FMA3 を切る (`_set_FMA3_enable(0)`)。1 台の中で FMA3 の経路の違いは消える。`dsin` などの DMath (`include/mitiru/math/DMath.hpp`) は、足し算・掛け算・割り算・sqrt だけで決まる。MSVC の 4 通りの設定で同じビット列を確かめた | 別の Windows のバージョン・Arm・AMD での一致は未検証。Windows のビルドが同じ (26200) Intel 2 台では一致した (下の「別 CPU の実測」)。静的 CRT で組んだ DLL には host の切り替えが届かない。clang / gcc の DMath は未検証 | `.mtrr` の `envTag` (`fma3=` と `ucrt=`)。環境が違う録画を再生すると 1 行で知らせる。`TestDMath.cpp` (固定の checksum) |
| JSON からの初期化 (`spawnFromJson`) | 同じ JSON から同じ初期 GameMemory を生成する | フルベイクと増分ベイクのような「初期化順序が変わり得る」経路は現状無い (Unity Entities Baking のような二重経路が無いため該当なし)。将来 hot-reload 系の初期化経路が増えたら要再検証 | JSON schema テスト、`spawnFromJson` の単体テスト |
| 物理問い合わせ job (`hud.raycast` / `hud.overlapSphere`、ABI v37) | 結果は `InputSnapshot::physicsResults` に乗るので、リプレイでは host の物理 world を回さずに記録値がそのまま返る (bit-exact) | live 実行で `--collision` の JSON が違えば結果も違う (録画には箱の中身は無い) | `--replay-test` の state 不一致 |
| host 側の物理 (`PhysicsSystem3D` / `JoltPhysicsWorld3D` / `Box2DWorld`) | Jolt は同じ順で API を呼べばスレッド数・OS・コンパイラを跨いで bit 一致 (`CROSS_PLATFORM_DETERMINISTIC=ON`、問い合わせ結果と接触はエンジン側で並べ直す)。Box2D v3.2.0 は AVX2 と SSE2 の経路・スレッド数 1/4/8・CPU 2 台・Debug/Release で同じビット列だった (下の「別 CPU の実測」。`Box2DWorld` の既定はスレッドを使わない) | ワールドは GameMemory の外にあるので巻き戻しの対象にならない。v3.1.1 と v3.2.0 は同じ入力でも結果が違うので、バージョンを跨いだ再現は無い。AVX2 を持たない実 CPU・AMD・Arm・OS またぎは未検証 | `[determinism]` タグの単体テスト (同じ入力を 2 回流してハッシュ一致) |
| ノイズ (`math/Noise.hpp`、FastNoiseLite) と文節 (`i18n/PhraseBreak.hpp`、BudouX) | 文節は整数の点数だけで決めるので、どのビルド・どの機種でも同じ文は同じ所で切れる。ノイズは同じビルドの中で同じ設定・同じ座標なら bit 一致 (`[noise][determinism]` の単体テスト) | ノイズは float の計算なので、別のコンパイラ・別の CPU との bit 一致は保証しない (上の「浮動小数点モード」と同じ立場)。シミュレーションの状態に使うなら、replay と同じ「同じビルド」の範囲で考える (`docs/NOISE.md`) | `TestNoise.cpp` (2 回流して memcmp)、`TestPhraseBreak.cpp` (上流の Python の答え 1000 文と一致) |
| 実行中のアセット差し替え (`asset.reload` / `--watch-assets`) | 「何フレーム目に `asset.reloaded` が届いたか」は `InputSnapshot::actionEvents` に乗り、リプレイで同じフレームに再現される | 差し替え後のファイルの中身は録画に無い。再生時はディスクの現状が読まれる | 再生前に配置 JSON/.baked/レベルの glb を録画時のバージョンへ戻す (git)。`--replay-test` の state 不一致で検出 |
| 起動時にしか決まらない設定 | 同一 `EngineConfig` (起動時のみフィールドを含む) での再生一致 | 起動時のみフィールドを録画後に変えて再生した場合の一致は保証外 | `docs/CONFIG.md` の表、`.mtrr` の `envTag` (backend/CPU arch/Tracy 有無を記録) |

## 検出手段の位置づけ

- **`--synctest [K]`** (`apps/mitiru_host/main.cpp`) — GGPO の SyncTest 相当。毎フレーム K
  フレーム前の state から再シミュレーションして現在の state と memcmp する。決定論オラクル
  (`oracleDeterminism`) の薄い別名で、実行中に継続的な検証を行う。
- **`mitiru_rollback`** (opt-in、`-DMITIRU_WITH_GEKKONET=ON`) — ゲーム DLL を 2 つ読み、遅延つきの
  loopback でロールバック対戦させて、2 人の GameMemory と素直な replay を比べる。GameMemory の外に
  状態を持つゲームを desync として炙り出す ([ROLLBACK_NETCODE.md](ROLLBACK_NETCODE.md))。
- **決定論オラクル** (`EngineConfig::oracleDeterminism` / `oracleDeterminismEveryFrames`) —
  常時 ON にはしない (GameMemory サイズに比例したコストがかかるため明示 opt-in)。
- **`replay --expect`** — 録画を再生し、最終 `view.*` state を期待値 JSON と diff する。
  CI のゴールデン再生ゲート (`tests/replay_golden/*.mtrr`) はこの経路を子プロセスで回す
  (ADR 0035 O5、`runReplayGateAsync`)。
- **ABI 指紋・envTag** — 再生不能な録画を「壊れているように見えるが実は ABI が違うだけ」と
  誤診断しないための診断情報。`envTag` は `backend|arch|tracy有無|fma3=cpuN,useN|ucrt=バージョン` の形式
  (`apps/mitiru_host/main.cpp` の `buildRecordEnvTag`。64 byte の自由記述に収まり、形式は変えていない)。
  決定論の証明にはならないが、「そもそも比較可能な録画か」を早期に弾く。`fma3=` は CPU が FMA3 を持つか
  (`cpu`) と UCRT が FMA3 の経路を使っているか (`use`)、`ucrt=` は計算している UCRT の製品バージョン
  (Debug の CRT は末尾に `d`)。`fma3=` を持つ録画を、違う環境で `--replay-test` / `--replay` すると、
  違う項目だけを 1 行で知らせる。ずれるかどうかは言わない。`--verbose` では今の環境も 1 行出る。

## 数学関数と FMA3 (`std::sin` などの結果は環境で変わる)

UCRT は CPU の命令セットで超越関数の実装を実行時に選ぶ。Microsoft の文書は、CPU・Windows のバージョン・
Debug / Release のどれでも結果が変わりうると書く
([Math and floating-point support](https://learn.microsoft.com/en-us/cpp/c-runtime-library/floating-point-support)、
[`_set_FMA3_enable`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/get-fma3-enable-set-fma3-enable))。

host (`mitiru_host`、`mitiru_rollback`) は起動の最初に `_set_FMA3_enable(0)` を呼び、FMA3 を使わない経路に固定する。
ゲームの DLL は動的 CRT で host と同じ UCRT を共有するので、DLL の `std::sin` にも効く (Debug の host と Debug の
DLL で確かめた。Release は測っていない)。環境変数 `MITIRU_FMA3=auto` で固定せず CRT の既定のまま動く
(FMA3 の PC で録った古い録画を、同じ種類の PC で再生し直す口)。静的 CRT で組んだ DLL は自分の CRT を持つので、
固定は届かない。

### 測定 (1 台。FMA3 の使う / 使わないだけを変えて比べた)

| 測った物 | 入力 | FMA3 の違いで変わったか | 結果 |
|---|---|---|---|
| 関数単体: double の `sin` `cos` `tan` `exp` | 20 万個 (`tools/fma3_probe.cpp`) | 1853 / 1902 / 5335 / 289 個で結果のビットが違う | 変わる |
| 関数単体: float の `sinf` `cosf` `tanf` `atan2f` `expf` `powf` | 同上 | 0 個 | 変わらない (`logf` だけ 6 個) |
| 同梱の golden 4 本の `--replay-test` (rewind 302、beko_run 151、neon_asteroids 151、objects_kitchen 201 フレーム) | 固定前の host、固定した host、`MITIRU_FMA3=auto` の 3 通り | 全部 PASS (bit_exact) | 変わらない。golden の作り直しは要らない |
| 混沌写像の小さい DLL を録画して、環境を変えて再生 (400 フレーム) | double の `std::sin`、float の `std::sin` と `std::pow`、DMath | 録画と再生で FMA3 が違うと 120 フレーム目で FAIL。ずれたのは double の `std::sin` の項目だけ | 変わる (double だけ) |

float だけのゲームでは、FMA3 の有無はほとんど結果に出ない (float の関数は内部で double に直して計算し、丸めで差が隠れる)。
double を使うゲームは分岐する。測り方と数字の全体は ADR 0133。
再生の FAIL のときは、録画と今の環境の違いが 1 行で出る (PASS のときは出ない)。

### DMath

シミュレーションの状態に入る数学は、`std::` の代わりに `mitiru/math/DMath.hpp` の
`mitiru::math::dsin` `dcos` `dtan` `datan` `datan2` `dexp` `dlog` `dpow` `dsqrt` (float) を使う。
openlibm (FreeBSD msun 由来) の float 用で、足し算・掛け算・割り算・sqrt だけで決まり、FMA の融合は止めてある。
MSVC の 4 通りの設定 (Release、`/arch:AVX2`、Debug、`/fp:fast` の静的 CRT) で、結果のビット列が一致することを確かめた
(固定値は `tests/mitiru/TestDMath.cpp`)。clang / gcc は未検証で、融合を止めるマーカーは MSVC にだけ付いている。
精度は `std` の double 用を float に丸めた値と 1〜2 ulp 以内。double では呼べない (`= delete`)。
描画だけに使う値は `std::` のままでよい。切り替えると、そのゲームの既存の `.mtrr` は途中から分岐する。

## 別 CPU の実測 (2026-11-06)

同じバイナリを 2 台で流した。どちらも Intel で AVX2 と FMA3 を持ち、Windows のビルドは 26200。
同じ exe を使うため、片方で組んだ物をもう片方にコピーして流している (cl 19.44、Debug)。
Debug の CRT (`ucrtbased.dll`) も exe の隣に同梱した同じファイルなので、2 台の UCRT の差は測れていない。

| | CPU | AVX2 | FMA3 | Windows |
|---|---|---|---|---|
| 1 台目 | i5-12400 (Alder Lake、6 コア 12 スレッド) | 有り | 有り | 10.0.26200 |
| 2 台目 | i7-13700F (Raptor Lake、16 コア 24 スレッド) | 有り | 有り | 10.0.26200 |

AVX2 を持たない CPU は手元に無いので、Box2D v3.2.0 の `b2World_EnableSSE2Fallback` で SSE2 の経路へ落とした物で代用した。
実 CPU の SSE2 専用機とは別物で、AVX2 命令を実行しない点だけが同じ。

### golden の再生 (`tools/box2d_probe/golden_cpu_report.py`)

`tests/replay_golden/*.mtrr` を `--replay-test` で再生し、最終 state の sha256 の先頭 16 桁を比べた。`--synctest 1` は 300 フレーム。

| golden | フレーム | i5-12400 | i7-13700F | 最終 state の sha256 |
|---|---:|---|---|---|
| rewind_small | 302 | PASS | PASS | 832c5f09b75eec0a (両方同じ) |
| beko_run_small | 151 | PASS | PASS | 563db21f7dc2747c (両方同じ) |
| neon_asteroids_small | 151 | PASS | PASS | 433955234235173a (両方同じ) |
| objects_kitchen_small | 201 | PASS | PASS | 293ce6b03ee5cfab (両方同じ) |

`--synctest` は 4 本とも両方の機械で exit 0。この 4 本は float だけで、Box2D を使わない。

### double の `std::sin` を使う混沌写像 (`tools/box2d_probe/chaos_cross.py`)

FMA3 を切った host (pin) と `MITIRU_FMA3=auto` (auto) で 400 フレームずつ録画し、2 台で全部の録画を両方の環境で再生した。

| 録画 | 同じ環境で再生 (i5-12400) | 同じ環境で再生 (i7-13700F) | 違う環境で再生 (両方) |
|---|---|---|---|
| i5-12400 の pin / auto | PASS / PASS | PASS / PASS | どちらも 120 フレーム目で FAIL |
| i7-13700F の pin / auto | PASS / PASS | PASS / PASS | どちらも 120 フレーム目で FAIL |

CPU が違っても、環境 (FMA3 の固定) が同じなら再生は一致した。FMA3 の固定が外れると、どちらの機械でも同じ 120 フレーム目でずれる
(1 台で測った結果と同じ)。

### Box2D (`tools/box2d_probe/box2d_probe.cpp`、`probe_matrix.py`)

箱 210 個のピラミッドと球 40 個を 600 step 回し、毎 step の全ボディの位置・速度・角度の FNV-1a をとった。
行数 32 の表(`probe_matrix.py` の出力)が 2 台で 1 行も違わなかった。

| Box2D | 条件 | 2 台で一致 | 備考 |
|---|---|---|---|
| v3.1.1 (現行。AVX2 無しで組まれる) | Debug / Release、`Box2DWorld` 経由、ピラミッド 210 箱と 820 箱 | 一致 | |
| v3.2.0 | 上の全部 + AVX2 / SSE2 + スレッド数 1・4・8 | 一致。条件間でも全部同じ hash | 3 回ずつ流して揺れなし |
| v3.1.1 と v3.2.0 の間 | 同じ入力 | 違う | 球 1 個の初期位置を 1 ulp ずらすと hash が変わる (測定が効いている) |

3.2.0 の AVX2 の経路 (`contact_solver_w8.c`) の機械語に FMA 命令は無く (`dumpbin /disasm`)、幅 8 の ymm 命令だけが入っている。
SSE2 へ落とすと実行が遅くなる (Release で約 1.2 倍) ので、落とした経路で実際に動いている。

測っていない物: 関節 (`Box2DWorld` が出していない)、AVX2 を持たない実 CPU、AMD、Arm、clang、Windows 以外。

## Known Limitations

- 上表の「保証しない」列は裏取りできた範囲の記述である。実機で測った CPU は Intel の 2 台 (どちらも AVX2 有り、
  Windows のビルドも同じ) だけで、AVX2 を持たない CPU・AMD・Arm は未測定 (`docs/PROFILING_GUIDE.md` の Known Limitations と同じ立場)。
- GPU backend 間の画素一致は最初から主張していない (描画は決定論の対象外)。

## 関連

- [`docs/MISTAKES.md`](MISTAKES.md) — 決定論を崩すゲーム側の誤りと、どれを何が知らせるか (Debug の host の見張りを含む)。
- `docs/CONFIG.md` — 起動後に変えても効かない設定の一覧 (決定論の前提条件と重なる)。
- [`include/mitiru/replay/Recorder.hpp`](../include/mitiru/replay/Recorder.hpp) — `.mtrr`
  format と ABI 指紋・envTag。
- [`docs/PROFILING_GUIDE.md`](PROFILING_GUIDE.md) — Tracy フレームと `.mtrr` フレームの対応。
