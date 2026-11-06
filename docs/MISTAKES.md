# ゲームを書く人の誤りと、それを何が知らせるか

MitiruEngine でゲームを書く人がしそうな誤りを並べ、今のエンジンがそれをどこで止めるかを書く。
「前」は v0.38.0 の main、「今」は ABI v52 (ADR 0072) の後である。2026-10-03 に、
誤りを 1 つずつ埋めた小さな DLL を作ってコンパイルし、Debug の `mitiru_host --headless` で素のまま・`--synctest`・
`--record` と `--replay-test` で走らせて確かめた。16〜19・27・28 は 2026-10-04 に v52 で同じように確かめた。
確かめていない行は「推定」と書いた。

止める場所は次の 5 つで、上ほど早い。

- コンパイル: ビルドが止まる
- 読み込み: DLL や JSON を読む時に断るか、1 行で知らせる
- 実行中: 普通に走らせているだけで知らせる (Debug の host の見張りを含む)
- 検査の時だけ: `--synctest`・`--replay-test`・ロールバックを回した時だけ分かる
- 黙る: どこも知らせず、画面か動きがおかしくなる

## 表

| # | 誤り | 前 | 今 | 知らせるもの |
|---|---|---|---|---|
| 1 | update で時計 (QueryPerformanceCounter・`std::chrono`) を読む | 検査の時だけ | 実行中 | 1 フレームのやり直し (下の「見張り」) |
| 2 | 共有の `rand()` を使う | 検査の時だけ (`--replay-test` は通る) | 実行中 | 同上 |
| 3 | ポインタの順に並ぶ `std::set` / `unordered_map` を回して状態を書く | 検査の時だけ | 実行中 | 同上 |
| 4 | スレッドの終わる順で状態を書く | 検査の時だけ | 実行中 | 同上 |
| 5 | 変数のアドレスを数として状態に書く | 検査の時だけ (`--replay-test` は通る) | 実行中 | 同上 |
| 6 | DLL の static 変数が GameMemory に効く | 検査の時だけ (`--replay-test` は通る) | 実行中 | 同上。メッセージに書いたフィールドの名前が出る |
| 7 | static が持つヒープの値が restart で戻らない | 検査の時だけ | 実行中 | 同上 |
| 8 | Jolt の world を `MITIRU_SIDE_STATE` で預けずに static に置く | 検査の時だけ (推定、6 と同じ仕組み) | 実行中 (推定) | 同上 |
| 9 | draw で GameMemory を書く (アニメの時刻・数え) | 検査の時だけ (`--replay-test` は通る) | 実行中 | draw の前後の比較。書いたフィールドの名前が出る |
| 10 | Input を通さずに OS からキーを読む (`GetAsyncKeyState` 等) | 黙る (headless では常に離した扱い) | 読み込み | DLL の import を見る |
| 11 | GameMemory に生のポインタ・`const char*`・関数ポインタを置く | 黙る (同じ実行の巻き戻しは動き、ロード・リプレイ・ホットリロードで壊れる) | コンパイル | `GameMemoryPointerField<持ち主, 番号, 型>` |
| 12 | GameMemory に `std::vector` / `std::string` / `unordered_map` / `std::function` / `std::atomic` を置く | コンパイル | コンパイル | `MITIRU_GAME` の flat POD の検査 |
| 13 | GameMemory に仮想関数を持たせる | コンパイル | コンパイル | 同上 |
| 14 | GameMemory に参照のメンバを置く | コンパイル (既定のコンストラクタが無い、と分かりにくい) | 同じ | MSVC の C2512 |
| 15 | GameMemory に 256 MB の配列を置く | コンパイル (コンパイラのヒープ切れ C1060、分かりにくい) | 同じ | なし |
| 16 | GameMemory に暗黙のパディングがある | 黙る | 読み込み | Debug の host が byte 数を 1 回知らせる (`mitiru_module_padding_bytes`)。`MITIRU_ASSERT_NO_PADDING` を書けばコンパイルで位置が出る |
| 17 | float が NaN になる | 黙る (反射の無い game) | 実行中 | nan オラクル。反射が無ければ `mitiru_module_float_offsets` の位置で見て「GameMemory の 4 byte 目の float」と出す |
| 18 | `update(Input, Hud, int dt)` と dt を整数で受ける | 黙る (dt が 0 になり何も動かない) | コンパイル | `registerGame` の引数の形の検査。正しい形 `void update(mitiru::Input in, mitiru::Hud hud, float dt)` を文に出す |
| 19 | update の引数が違い、draw はある (`update(Input)`) | 黙る (update が呼ばれない) | コンパイル | 同上 |
| 20 | update の名前を `Update` にする (draw も無い) | コンパイル | コンパイル | `MITIRU_GAME` の入口の検査 |
| 21 | 保存と復元のインターフェースに `saveState` / `restoreState` が無い | コンパイル | コンパイル | `MITIRU_SIDE_STATE` |
| 22 | `MITIRU_GAME_OBJECTS` の build が進行データを書き換える | コンパイル | コンパイル | 同上の検査 |
| 23 | Jolt の body を組み立ての後に作る | 実行中 (戻すのを断る) | 同じ | `JoltGameWorld` ([SIDE_STATE.md](SIDE_STATE.md)) |
| 24 | 保存と復元のインターフェースの bytes にポインタ値を書く | 検査の時だけ (推定) | 同じ | `--replay-test` のインターフェースごとの hash |
| 25 | GPU スキニングの結果・描画の LOD・TAA のずらし・パーティクルの状態を update で使う | 書けない | 書けない | update は Input と Hud しか受け取らない (ADR 0005) |
| 26 | RML に gameplay の状態を持たせる | 書けない | 書けない | RML に script 層が無い (ADR 0051) |
| 27 | 度とラジアンを取り違える (`drawArc` / `pushRotation` はラジアン、`drawRectRotated` / `drawGroup` / 3D は度) | 黙る | コンパイル (警告) | `mitiru::Deg` / `Rad` のバージョンはどちらの単位でも正しく変わる。float のバージョンは `[[deprecated]]` で、単位と書き方を出す |
| 28 | `camera3D` の fovDeg にラジアンを渡す | 黙る (推定) | コンパイル (警告) | 同上。`Rad{x}` を渡せば度へ変わる |
| 29 | 色を 0..255 で `Colorf` に入れる | 黙る (推定、1 に詰まる) | 黙る | なし |
| 30 | Z を上にして書く (エンジンは Y が上の右手系) | 黙る | 黙る | なし |
| 31 | cm 単位で書き出した glTF を置く | 黙る (推定、100 倍になる) | 黙る | なし |
| 32 | 手で書く JSON が壊れている (sounds / music / mix / animgraph / anim / springs / ragdoll / input_actions / world / region / settings / ビヘイビアツリー) | 読み込み (どこが壊れたかは出ない。springs.json は何も出ない) | 読み込み | 「music.json の 3 行 1 列が JSON として読めない」 |
| 33 | sounds.json のフィールドの名前を綴り違える (`volumDb`) | 黙る (既定値で鳴る) | 読み込み | 「sounds.jump: 知らないフィールド 'volumDb' ('volumeDb' のつもりなら直す)」 |
| 34 | ほかの JSON のフィールドの名前を綴り違える | 黙る | 黙る | 下の「残り」 |
| 35 | モデルの隣の sidecar (anim.json 等) が無い | 黙る (既定値を使う。そういう作り) | 同じ | なし |
| 36 | ファイル名の大文字小文字が違う | 黙る (Windows では読める。.mtpak は未確認) | 同じ | なし |
| 37 | Debug の DLL を Release の host で読む | 読み込み | 読み込み | ABI の数と build の指紋の一致 |
| 38 | 古い ABI で組んだ DLL を読む | 読み込み | 読み込み | 同上 |
| 39 | DLL に `MITIRU_GAME` を書き忘れる | 読み込み | 読み込み | 入口が無いことと、書き方の案内 |
| 40 | ホットリロードで GameMemory の形を変える | 実行中 | 実行中 | 形の hash。名前で移すか、最初からやり直す |
| 41 | GameMemory の形を変えて、移す関数を書かずに古いセーブを読む | 実行中 | 実行中 | 名前で移した旨か、ロード拒否の 1 行 |
| 42 | オンラインで GameMemory の外の状態を使う | 実行中 (DESYNC の報告) | 実行中 | checksum の比較 ([ONLINE.md](ONLINE.md)) |
| 43 | 自分の機械だけの値 (言語・設定) を sim に使う | 実行中 (推定、checksum) | 同じ | 同上 |
| 44 | `mitiru_host --synctest game.dll` と書く | 読み込み (DLL のパスが K に取られ、「コピーできない」と出る) | 正しく動く | K は数字だけの時に取る |
| 45 | `--synctest` を CI に置く | 黙る (見つけても終了コードは 0) | 検査の時だけ | 食い違いがあれば exit 1 |
| 46 | update の中でヒープを確保する | 黙る | 黙る | なし (Tracy で見る) |
| 47 | ゲームのコードで `near` / `far` を名前に使う | コンパイル (windows.h のマクロで C2513、分かりにくい) | 誤りでなくなった | `Game.hpp` が読み終わりで windows.h の near / far を消す |
| 48 | `MITIRU_REFLECT_STRUCT` に field を 17 個以上並べる | 読み込み (17 個目から inspector と state-trace に出ない旨の 1 行) | コンパイル | `MITIRU_REFLECT_STRUCT` の static_assert。型の名前と上限を文に出す |
| 49 | update で `std::sin` / `std::cos` / `std::pow` などを使い、別の PC・別の Windows で録画を再生する | 黙る (同じ PC では通る。UCRT が CPU と Windows のバージョンで実装を選ぶ) | 検査の時だけ | 録画の `envTag` の `fma3=` / `ucrt=` が今と違えば、再生の最初に 1 行で知らせる (ずれるかは言わない)。host は起動時に FMA3 を切る。根治は `mitiru/math/DMath.hpp` の `dsin` `dcos` `dpow` など ([DETERMINISM.md](DETERMINISM.md)) |

### 数 (49 件)

| | コンパイル | 読み込み | 実行中 | 検査の時だけ | 黙る | 書けない | 誤りでなくなった |
|---|---|---|---|---|---|---|---|
| 前 | 8 | 6 | 5 | 10 | 18 | 2 | 0 |
| 今 | 13 (うち 27・28 は警告) | 7 | 15 | 3 | 7 | 2 | 2 (44・47) |

## 見張り (Debug の host の既定)

- 1 フレームのやり直し (`EngineConfig::determinismSentinelEveryFrames`、既定 60): 60 フレームごとに、
  直前の GameMemory と保存と復元のインターフェースと記録した入力で update をもう 1 回呼び、今の GameMemory と比べる。
  update 1 回分の手間で、1〜9 を 1 秒以内に知らせる。`--synctest` は同じ検査を毎フレーム回し、
  食い違えば exit 1 で終わる。
- draw の前後 (`drawWriteCheckEveryFrames`、既定 30): draw の前に GameMemory を控え、後で比べる。
- Release の host では両方とも 0 (回さない)。オンライン・部分状態の game (`MITIRU_GAME_OBJECTS`)・
  scrub 中・分岐やロードで live が記録の最新と違うフレームでは、やり直しを飛ばす。
- パディング (`warnGameMemoryPadding`、Debug で既定 ON): 読み込みとホットリロードの後に、`MITIRU_GAME` が出す
  `mitiru_module_padding_bytes` を見て、1 byte 以上なら 1 回知らせる。Release の host では知らせない。
- NaN (組込オラクル、既定 ON): 反射のフィールドで見る。反射が 0 個の game は、`mitiru_module_float_offsets` の
  位置 (1,048,576 個まで) を毎フレーム見る。どちらも型をたどれない GameMemory (基底クラス・65 個以上のフィールド・
  #pragma pack) の中は見ない。

## 残り

- 18・19: update / draw を多重定義している game は、引数の形を確かめない (`&T::update` が 1 つに決まらない)。
  入口が 1 つも無い時の検査は残る。
- 27・28: float のバージョンは警告だけで、ビルドは通る。次に境界を開ける時に float のバージョンを消せばコンパイルで止まる。
- 34 (ほかの JSON の綴り違い): `data::unknownKeyError` を各読み込みのフィールドの表に掛ける。
