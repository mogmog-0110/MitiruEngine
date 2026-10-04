# オンライン協力プレイ

2..4 人が別の PC から同じゲームを遊ぶ。既定はロールバック (GekkoNet) で、相手の入力を待たずに予測で進め、外れたら
巻き戻して進め直す。巻き戻しの重い場面は host 権威 (`--net-mode authority`) にでき、host だけが進めて状態を配る。
どちらでもゲーム DLL は回線を知らない。ローカルの多人数プレイとして書けば、そのままオンラインになる。
決めたことは [ADR 0066](adr/0066-online-coop-rollback-host.md) と [ADR 0068](adr/0068-online-host-authority.md)、
仕組みは [ROLLBACK_NETCODE.md](ROLLBACK_NETCODE.md)。

```bash
cmake --preset default -DMITIRU_WITH_GEKKONET=ON        # 既定では入らない (opt-in)
mitiru_host game.dll --net                              # 待合室の画面で「部屋を作る」か「参加する」を選ぶ
mitiru_host game.dll --net-host 47100 --net-players 3   # port 47100 で 3 人の部屋を作る
mitiru_host game.dll --net-host 47100 --net-mode authority --net-rate 20   # host 権威の部屋 (状態を毎秒 20 回配る)
mitiru_host game.dll --net-join 3Z000-03Q96             # 参加コード (か 192.168.1.10:47100) の部屋に入る (方式は host に従う)
```

部屋を作ると参加コードが出る。コードは host の住所を 10 文字で表しただけで、相手探しのサーバーは無い。
全員が「準備できた」を押すと始まり、待合室の画面は消える。画面の無い実行 (`--headless`) は最初から準備ができている。

## ゲームが守ること

| 守ること | 理由 |
|---|---|
| シミュレーションに効く状態は全部 GameMemory か窓口 (`MITIRU_SIDE_STATE`、[SIDE_STATE.md](SIDE_STATE.md)) に置く。Jolt の world は `JoltGameWorld`、群衆は `NavCrowd` を窓口にする | 巻き戻すたびに全部を戻す。外に残った状態は食い違いになる |
| on_update は入力だけで決める。時刻・乱数の種・ポインタ値・ファイルを読まない。乱数は `in.rngSeed` から作る | 全員が同じ入力から同じ結果を出す |
| 人の入力は「その人の口」から読む。キーは 1P = WASD 側、2P = 矢印側。パッドは n 人目 = `in.pad(n)`。3P と 4P はパッドだけ | 手元ではどの人も WASD とパッド 1 で操作し、host が番号の口へ入れ直す |
| マウスと文字入力は使えない (0 になる)。キー割り当て後の操作は人ごとに `in.actionDown(人, 操作)` で読む (ABI v50) | 回線に流すのは 1 人 24 byte (キー 32 個とパッド 1 台と操作) だけ |
| ボタンの絵を選ぶ機器 (`in.inputDevice()`) は 0 (キーボード、Xbox) になる | PC ごとに違う値なので合成に入れない |
| dt は 1/60 秒で固定。一時停止 (F8) とヒットストップの dt は効かない | 1 人だけ止まると全員がずれる |
| 音・HUD・セーブの intent は確定した進行の分だけが届く | 巻き戻しのたびに同じ音が鳴らないように |
| 全員が同じビルドの DLL を使う | 待合室が GameMemory と窓口の初期値の checksum を比べ、違えば参加を断る |

人数は `in.netPlayers()` (オフラインは 0、全員の PC で同じ値なので update で読んでよい)。自分の席・状態・ping・食い違ったフレームは
`s.netView()` (Canvas も同じ) で、**描画だけで読む** (PC ごとに違う値なので、update で GameMemory に書くと食い違う。ABI v50、ADR 0067)。
待合室の様子は `view.net_*` (`net_ping`、`net_seat`、`net_rollbacks` ほか) へも押し出すので、ゲームの RML の HUD からも読める。

## ゲームから部屋を作る

起動の引数でなく、タイトル画面からも頼める (ABI v50)。`hud.netOpen()` は待合室の画面を出し、`hud.netHost(人数)` は部屋を作り、
`hud.netJoin(参加コード)` は参加する。方式も選ぶなら `hud.netHost(人数, module::kNetModeAuthority, 20)` (ABI v51、回数は 60 の約数へ
丸める。0 は起動の引数のまま)。部屋を作るか参加すると、host は GameMemory を on_init の直後に戻してから待合室に入る (全員が同じ
GameMemory から始めることを待合室が確かめるため)。ゲームは `in.netPlayers()` が 0 でなくなったら遊びの場面へ進む。
`hud.netLeave(席 + 1)` と `hud.netReady(準備, 席 + 1)` は、席を付けて頼む。シミュレーションは全員の PC で同じに進むので、席を付けないと
全員の PC が同時に抜ける。`--record` と `--replay` の間は依頼を受けない。

## 何フレーム巻き戻せるか

片道の遅延が入力遅延を超えた分だけ巻き戻す (片道 100 ms なら 6 − 2 = 4 フレーム)。1 フレームの進め・保存・戻しを Release で測った
(`python tools/perf_bench.py --rollback`、RTX 5070 Ti の機械、900 フレーム)。予算は 60Hz の半分 (8.33 ms、残りは描画)。

| 場面 | GameMemory | 窓口 | 進め p95 | 保存 p95 | 戻し p95 | 予算内に巻き戻せるフレーム |
|---|---:|---:|---:|---:|---:|---:|
| physics_rewind (Jolt の world) | 1.3 KB | 23 KB | 0.03 ms | 0.02 ms | 0.01 ms | 120..170 |
| action3d | 0.6 KB | 0 | 0.01 ms | 0.00 ms | 0.00 ms | 400 以上 |
| crowd (DetourCrowd、群衆) | 4.8 KB | 78 KB | 0.42 ms | 0.09 ms | 0.01 ms | 16 |

巻き戻す深さは予測の幅 (`RollbackConfig::predictionWindow`、8 フレーム) までなので、進め + 保存が 1 ms を超える場面は
一番深い巻き戻しが予算を超える。自分のゲームは `mitiru_rollback <game.dll> --bench`
で測る。

## 重い場面 (host 権威)

巻き戻しが予算に収まらない場面 (大きな物理、多くの敵) は、部屋を作る側が `--net-mode authority` を付ける。待合室の
画面は方式を出し、参加者はそれに従う。

- host だけが on_update を呼ぶ (1 フレーム 1 回)。参加者の入力は届いた順に使う
- host は `--net-rate` (60 の約数、既定 20) の回数だけ、GameMemory と窓口をつないだ状態を、参加者が持つ前の状態との差分
  (巻き戻しのリングと同じ XOR の RLE) + zstd で送る。間に使った全員の入力も小さな別の packet で送る
- 参加者は状態に戻し、次の状態までのフレームを host の入力で on_update を呼んで作る。ゲームは入力だけで決まるので、作った
  フレームは host と bit まで同じになる (値を線形に混ぜる補間はしない。engine は GameMemory の意味を知らない)。音と HUD も
  鳴る。描くのは作れる一番先から 2 フレーム後ろ
- 参加者の操作は片道 + 状態 1 回分の間隔 + 片道 + 描く位置のずれの後に画面に出る (20 Hz、片道 50 ms で 130..230 ms ほど)
- ゲームが `MITIRU_NET_PREDICT(T)` で `T::predict(Input in, int player, float dt)` を出していれば、参加者は描く直前に GameMemory の
  写しを作り、host がまだ使っていない自分の入力を 1 フレーム分ずつ predict に渡して、自分の分だけを先に動かした写しを描く (ABI v51)。
  押したボタンはそのフレームに画面に出る (20 Hz、片道 40±12 ms、落ち 5 % で平均 255 ms → 0 ms)。predict は update と同じ式で
  **player の分だけ**を進める (敵・拾い物・他の人には触らない。写しは描くだけで、次の状態で作り直す)。窓口 (Jolt の world 等) の中の
  自分も predict で進めてよい。host は predict の前に窓口の image を取り、後ですぐ戻す (ADR 0073)。Jolt なら world を止めたまま
  `CharacterVirtual::ExtendedUpdate` で自分だけを進め、姿勢を写し (drawMemory) へ写す。`PhysicsSystem::Update` で world 全体を
  進めると、先読みのフレームの数だけ全部の物を進める費用がかかる。手触りが全部の物に要る場面はロールバックにする
- 方式・状態の回数・補間の進み方・描いている状態の遅れ・先読みしたフレームの数は `s.netModeView()` (Canvas も同じ) で、描画だけで読む
  (ABI v51)。`examples/coop3d` は参加者の画面の左上に出す

ゲームが守ることはロールバックと同じ (上の表)。状態の間を作るので、参加者の PC も同じビルドの DLL が要る。作ったフレームが
届いた状態と違うと `[net] 食い違い: frame F で、状態の間を作ったフレームが host の状態と違った` を 1 度出す (絵は次の状態で直る)。

1 人あたりの回線の量を `mitiru_rollback <game.dll> --authority --rate N [--link L,J,P]` で測った。値は UDP の中身で、
括弧内は IP と UDP の頭を含む。

| 場面 | 状態の大きさ | 10 Hz | 20 Hz | 30 Hz | 20 Hz、片道 50±15 ms、落ち 3 % |
|---|---:|---:|---:|---:|---:|
| crowd (DetourCrowd) | 4.8 KB + 窓口 78 KB | 255 KB/s (261) | 438 KB/s (449) | 583 KB/s (598) | 575 KB/s |
| physics_rewind (Jolt) | 1.3 KB + 窓口 23 KB | 20 KB/s (21) | 39 KB/s (41) | 57 KB/s (60) | 42 KB/s |

参加者から host へは 7 KB/s (60 packets/s)。crowd の窓口は毎フレームほぼ全部が変わるので、差分でも 1 回 22 KB になる。
量を減らすには `--net-rate` を下げる。間のフレームは入力で正確に作るので、下げて増えるのは遅れだけ。

## 予測が外れた時の絵

ロールバックで相手の入力を外すと、巻き戻して進め直した状態がそのまま次の絵になり、相手のキャラと相手を追うカメラが
1 フレームで跳ぶ (coop3d、往復の平均 80 ms・落ち 5 % で、普段 4 px のカメラが 33 px)。host 権威の参加者は、host が自分の入力を
待ったり飛ばしたりすると、先読みした自分が跳ぶ。

host は予測が外れたフレームごとに、正す前と正した後の GameMemory を組で 32 フレーム残す (`network/NetCorrections.hpp`)。
ゲームの描画は `network/NetSmoothing.hpp` で、描く位置を正す前の位置から正した位置へ数フレームかけて寄せる。
シミュレーションの状態は変えないので、checksum・リプレイ・巻き戻しは今までどおり合う。draw は GameMemory に書かない。

- `smoothPosition(view, *this, [&](const Game& g) { return g.pos[i]; })` が描く位置を返す。向きは `smoothRotation`、`smoothAngleRad`。
  既定は時定数 50 ms (200 ms で 9 割戻る) で、3 m (120 度) を超えて正した組は戻さずに飛ぶ (瞬間移動・復活・場面の切り替え)
- カメラも同じ関数に通す。キャラだけを戻すと、キャラを追うカメラが跳ぶ。action ライブラリなら `CharacterController` の位置と
  向き、`CameraView` の eye と target を通す
- 窓口 (Jolt の world) の中の物は組に入らない。描く物の姿勢は update で GameMemory へ写しておく

組は `s.netCorrections()` (Canvas も同じ) で描画だけが読む (ABI v52、ADR 0073)。`examples/coop3d` は箱とカメラを、`examples/rollback_duel`
はパドルと玉を戻して描く。`[net]` の最後の行の `corrections` が残した組の数
(`TestNetSmoothing.cpp` と `TestNetSmoothingRollback.cpp` が測る。相手のキャラ 66 px → 16 px、止まった参加者の自分 61 px → 9 px)。

## 回線と NAT

- 1 つの UDP の port で、待合室とロールバックの両方を通す。待ち受けは既定で 127.0.0.1 だけ。`--net-host <port>` か
  画面の「部屋を作る」の時だけ全部の口で待つ (Windows のファイアウォールが確認を出す)
- 直接つなぐ。LAN、port の転送、VPN (Tailscale など) で届く住所を使う。全員が同じ種類の住所 (全員 LAN か全員公開の住所)
  で host に入る。host が見た住所を他の参加者へ配るため
- 相手探しのサーバー、NAT 越え、Steam のロビーと Steam Datagram Relay はまだ無い。Steamworks がある時に
  GekkoNet の送受信を差し替えて足す
- 相手が 5 秒黙ると切れたとみなす。その人の入力は、残った全員で決めたフレームから空 (何も押さない) になる

## 食い違った時

```
[net] 食い違い: frame 412 で player 1 と状態が食い違った (checksum 自分 1a2b3c4d / 相手 5e6f7a8b)
  自分のその frame の内訳: GameMemory 0c0ffee0 / 窓口 00000000
```

両方の peer の行を並べ、GameMemory と窓口のどちらの内訳が違うかを見る。違う方の外に状態が残っているか、入力以外を
読んでいる。`--net-frames N` は frame N を全員が確定させた後に `[net] frame N checksum …` を出して終わり、
食い違いがあれば exit 1。

## 確かめ方

| 確かめること | どこで |
|---|---|
| 参加コード、住所、回線の遅延・落ち、待合室 (4 人、準備、違うゲームを断る) | `tests/mitiru/TestNetLobby.cpp` (core) |
| 本物の UDP で 2 人と 4 人、遅延・揺れ・落ちの上で巻き戻しが起き、確定フレームの checksum が全員一致 | `tests/mitiru/TestOnlineSession.cpp` (ctest ラベル `rollback`) |
| mitiru_host を 2 個と 4 個起こし、coop3d で全員が同じ checksum を出す | `e2e_online_coop3d_2p` / `_4p` (`tests/e2e/run_online.py`、`--capture` で PNG) |
| host 権威の差分と zstd が戻ること、壊れた byte 列を断ること、欠片の組み直し | `tests/mitiru/TestSnapshotCodec.cpp` (core) |
| host 権威で、遅延・揺れ・落ちの上でも参加者の作ったフレームが host と一致し、入力が host に届き、黙った参加者が切れる | `tests/mitiru/TestAuthoritySession.cpp` (core) |
| host 権威の部屋を待合室から始める (方式が参加者に届く) | `TestNetLobby.cpp` と `TestOnlineSession.cpp` の `[authority]` |
| mitiru_host を 3 個起こし、host 権威の coop3d で全員が同じ checksum を出す | `e2e_online_coop3d_authority_3p` (`run_online.py --mode authority`) |
| 予測が外れた時の組 (正す前と後) と、描く位置を戻す重み・snap・向き。host 権威の先読みが窓口を進めてもシミュレーションが変わらない | `tests/mitiru/TestNetSmoothing.cpp` (core) |
| ロールバックで相手を外した時の組と、相手のキャラの跳びが消えること | `tests/mitiru/TestNetSmoothingRollback.cpp` (`rollback`) |
| 予測が外れた前後の絵 (毎フレーム撮る) | `run_online.py --capture --capture-every 1` |
| 待合室の画面 (部屋を作る・参加・席ごとの準備) | `tests/mitiru/TestRmlShipUi.cpp` の `[net]` |
