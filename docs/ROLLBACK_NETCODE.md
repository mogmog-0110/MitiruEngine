# ロールバック netcode

オンライン対戦で、相手の入力を待たずに予測で進め、予測が外れていたら過去へ巻き戻して正しい入力で
進め直す方式。格闘ゲームで標準になっている (GGPO 方式)。

MitiruEngine では、これを **ゲーム DLL に何も足さずに** 載せる。ゲームは「1 台のキーボードを 2 人で
分けるローカル対戦」として書くだけで、回線と巻き戻しは host 側が受け持つ。

## なぜエンジンの形と相性がいいか

ロールバックがゲームに要求するのは 3 つだけで、MitiruEngine はどれも元から持っている。

| ロールバックが要るもの | MitiruEngine にあるもの |
|---|---|
| 状態を丸ごと保存・復元できる | GameMemory は flat POD で全状態。rewind の ring と同じく `memcpy` で済む |
| 同じ入力なら同じ結果 | InputSnapshot の記録・再生が bit-exact (決定論ゲート G4 と `--synctest` が見ている) |
| 入力だけを回線に流せる | on_update が読むのは InputSnapshot だけ。外の世界 (時刻・マウス・音声クロック) を入力の外で読まない |

逆に、GameMemory の外に状態を持つゲーム (DLL の static に進行を置く、`MITIRU_GAME_OBJECTS` で場面を
非 POD に持つ = `kModuleStatePartial`) は載らない。`RollbackPeer` は後者を最初に断る。前者は
desync として検出される (下の「確かめ方」)。

## ライブラリの比較: GGPO と GekkoNet

| | GGPO | GekkoNet |
|---|---|---|
| ライセンス | MIT | BSD 2-Clause |
| 最終更新 | 2019-11 (以後止まっている) | 2026-09 (活動中) |
| API | C (`ggponet.h`) | C (`gekkonet.h`)、中身は C++20 |
| 制御の流れ | **callback**。`ggpo_advance_frame` / `ggpo_idle` の中から `advance_frame` callback が呼ばれ、その中で更に `ggpo_synchronize_input` を呼ぶ入れ子 | **イベントを返す**。`gekko_update_session` が Save / Load / Advance の列を返し、呼ぶ側が順に処理する |
| 回線 | 内蔵の UDP (Winsock) だけ。差し替え口が無い | adapter (send / receive / free の 3 関数) を差し替えられる。同梱の asio UDP は使わなくてよい |
| 時刻 | 同期・品質報告に壁時計を使う | timeout と ping に `std::chrono` を使う (シミュレーションの結果には効かない) |
| 検証用のモード | SyncTest (毎フレーム巻き戻して比べる) | Stress session (同じ目的)、desync 検出 (確定フレームの checksum を相手と交換) |
| その他 | 観戦 | 観戦 (途中参加)、リプレイ記録、runahead、保存の間引き、回線統計 |

**GekkoNet を採った。** 決め手は次の 2 つで、どちらもエンジンの哲学から来る。

1. **host がループの主導権を持ったままでいられる。** エンジンの host は「GameMemory を保存する」
   「書き戻す」「入力を渡して on_update を 1 回呼ぶ」をすでに単独の操作として持っている。GekkoNet の
   イベントはこの 3 つに 1 対 1 で写る。GGPO の入れ子の callback だと、host のループを GGPO の中に
   明け渡すことになる。
2. **回線を差し替えられるので、ロールバックそのものを決定的にテストできる。** 同じプロセスの中の
   loopback (遅延を tick で数える) で 2 人をつなげば、packet の届く順まで毎回同じになり、
   「巻き戻しが起きた上で結果が一致する」を ctest で固定できる。GGPO は実ソケットと壁時計が前提で、
   これができない。本番の回線は、エンジンが opt-in で持っている GameNetworkingSockets を同じ
   adapter に包めばよい。

GekkoNet の弱いところも書いておく:
- ライブラリが標準出力に `dropped packet!` を出す (接続が済む前に届いた packet を捨てた時。害は無い)
- adapter の関数が利用者のポインタを受け取らないので、loopback は static な受け口を経由する
  (同時に作れる `RollbackLoopback` は 1 つ)
- 同梱の CMakeLists は MSVC の CRT を静的 (/MT) に固定するので、エンジンはそれを使わずソースを直接
  まとめている (`CMakeLists.txt` の `mitiru_gekkonet`)
- packet ごとに確保する (回線の層なので GameMemory の hot path ではない)

## 作ったもの (prototype、opt-in)

```bash
git submodule update --init external/GekkoNet
cmake --preset default -DMITIRU_WITH_GEKKONET=ON
cmake --build build --target mitiru_rollback rollback_duel
build/apps/mitiru_rollback/mitiru_rollback.exe build/apps/mitiru_host/rollback_duel/rollback_duel.dll --latency 6
```

| 部品 | 役割 |
|---|---|
| `include/mitiru/network/RollbackInput.hpp` | 1 人分の入力 `PadInput` (キー 32 個とパッド 1 台、16 byte) と、全員分を 1 枚の InputSnapshot に合成する規約 (1P = WASD 側、2P = 矢印側、n 人目のパッド = `gamepads[n]`) |
| `include/mitiru/network/RollbackSession.hpp` | `RollbackPeer`: ゲーム DLL 1 つを対戦の 1 人として進める。Save/Load = GameMemory の memcpy、Advance = 合成した InputSnapshot で on_update |
| `include/mitiru/network/RollbackLoopback.hpp` | 同じプロセス内の回線。遅延を tick 単位で足す |
| `include/mitiru/network/OnlineSession.hpp` | 本物の UDP (`DatagramEndpoint` + `RollbackUdp`) と待合室 (`NetLobby`) をつないだ 1 人分。host の `--net` が使う |
| `apps/mitiru_rollback` | ゲーム DLL を 2 つ読んで loopback で対戦させ、最後に 2 人の GameMemory と「確定した入力をロールバック無しで流した 3 つ目の DLL」を比べる。一致で 0、食い違いで 1 |
| `examples/rollback_duel` | 1 台で 2 人のテニス。回線を知らない普通のゲーム DLL |

決めごと:
- 合成した InputSnapshot はボタンと固定値 (dt・論理解像度・seed) だけで決まる。マウス・音声クロック・
  テキスト入力など端末ごとに違う値は 0 にする
- 「押した瞬間」は 1 フレーム前のボタンとの差で作る。その 1 フレーム前のボタンも保存する状態に含める
  (巻き戻した先で押した瞬間がずれないように)
- 巻き戻し中と runahead 中の on_update が出した intent (音・セーブ要求など) は捨てる。確定した進行の
  intent だけを `RollbackPeer::intents()` で渡す

## 確かめ方と結果

| 確かめること | どこで |
|---|---|
| 遅延 4 フレームで巻き戻しが起き、2 人の GameMemory と素直な replay が一致する | `tests/mitiru/TestRollbackSession.cpp` (ctest ラベル `rollback`) |
| 同じ対戦を 2 回やると巻き戻しの回数まで同じ | 同上 |
| GameMemory の外に状態を持つゲームは desync として報告される | 同上、と `rollback_loopback_reports_hidden_state` (fixture DLL) |
| 実物のゲーム DLL が対戦に載る | `rollback_loopback_rollback_duel` (`mitiru_rollback` を ctest から呼ぶ) |
| ゲームが入力だけで決まる (前提) | 決定論ゲート `determinism_rollback_duel` (既定のビルドでも回る) |

手元の結果 (Debug、RTX 5070 Ti の機械):

```
rollback_duel  --latency 6 --frames 900   frames 910  rollbacks 145  resimulated 581  desyncs 0/0  A==B yes  A==replay yes
navmesh        --latency 3 --frames 300   frames 322  rollbacks 48   resimulated 49   desyncs 0/0  A==B yes  A==replay yes
fixture (GameMemory の外に状態)            desyncs 203/203  A==B NO   → DESYNC
```

## host で遊ぶ

`mitiru_host` に入った (`--net` / `--net-host` / `--net-join`、2..4 人、本物の UDP)。遊び方と、ゲームが守ること・
巻き戻せるフレーム数は [ONLINE.md](ONLINE.md)、決めたことは [ADR 0066](adr/0066-online-coop-rollback-host.md)。

## まだやっていないこと

- **NAT 越えと相手探しのサーバー。** 今は住所か参加コードで直接つなぐ。Steamworks がある時の Steam のロビーと
  Steam Datagram Relay は、GekkoNet の adapter を差し替えて足す
- **巻き戻しと音。** 今は巻き戻し中の音を捨てるだけ。予測で鳴らした音を「当たっていれば鳴らし続け、
  外れていたら止める」には、音の intent にフレーム番号を付けて重複を消す仕組みが要る
- **機械をまたいだ決定性。** 同じバイナリ (x64 / SSE2 / `/fp:precise`) なら浮動小数の結果は一致する。
  コンパイラやオプションが違うビルド同士は一致を保証しない。ABI の指紋 (`kWireApiVersion`) が揃って
  いることを対戦の前提にする
- **入力遅延の自動調整、観戦、リプレイ保存** は GekkoNet が持っているが、まだつないでいない
