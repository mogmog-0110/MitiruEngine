# タイムトラベル inspector

ゲームの過去フレームを観測し、グラフをクリックして**その瞬間に巻き戻す** —— MitiruEngine の
差別化軸②。作者がやることは 2 つだけ。

1. GameMemory を **flat POD** にする
2. 「GameMemory から追いたい値を引く関数」を `MITIRU_GAME_SERIES` で宣言する

これだけで host が過去フレームの GameMemory を自動で記録し、inspector の time-travel
ウィンドウに HP 履歴などの系列を出す。手で履歴を貯めたり JSON を組んだりするコードは要らない。

模範サンプル: `examples/timetravel_demo/`。

## なぜ flat POD なのか

host はゲームの状態 (GameMemory) を **バイト列として丸ごとコピー** して記録し、巻き戻すときは
そのバイト列を live の GameMemory に書き戻す。だから GameMemory に `std::vector` /
`std::string` のような**内部ポインタを持つ型**があると、コピーしても復元できない (ADR 0017)。

`MITIRU_GAME` はこれをコンパイル時に確認する。可変長が欲しいときは固定長コンテナを使う:

```cpp
#include <mitiru/core/FixedVec.hpp>

struct GameMemory {
    mitiru::FixedVec<Enemy, 64> enemies;   // std::vector<Enemy> の代わり
    mitiru::FixedString<32>     name;       // std::string の代わり
    int hp = 100;
};
// MITIRU_GAME が static_assert(std::is_trivially_copyable_v<GameMemory>) を自動でかける
```

`FixedVec` は `push_back` / `at` / `size` / `clear` / `removeAt` を持ち、vector に近い手触り。
上限を型に焼くので「敵は最大何体か」を設計時に意識できる (毎フレームのメモリ確保も無くなる)。

> 観測ログのような **gameplay でない状態** (イベントログ・統計) は GameMemory の外
> (DLL 内の `static`) に置く。巻き戻すべきは gameplay state だけ。

## probe を宣言する

`MITIRU_GAME(T)` の代わりに `MITIRU_GAME_SERIES(T, …)` を使い、追いたいスカラーごとに
「GameMemory から double を引く capture 無しの関数」を並べる:

```cpp
namespace {
double hpProbe(const void* m)      { return static_cast<const GameMemory*>(m)->hp; }
double playerXProbe(const void* m) { return static_cast<const GameMemory*>(m)->playerX; }
}

MITIRU_GAME_SERIES(GameMemory,
    { "hp", "HP",       &hpProbe,      35.0, 1 },   // 35 を下抜けたら danger marker を打つ
    { "x",  "Player X", &playerXProbe,  0.0, 0 });  // 閾値なし (最後の 0)
```

各行は `{ id, ラベル, 関数, 閾値, 閾値を使うか(1/0) }`。host が GameMemory の履歴 ring の
各フレームに関数を適用して系列を作り、被弾・回復の段差や danger ライン跨ぎを自動で marker に
する。

## inspector を開いて巻き戻す

time-travel ウィンドウは独立した sub-window。ゲーム窓には何も出ない (game 窓は pure)。

```bash
mitiru run --inspect timetravel
# または host に直接:  mitiru_host.exe yourgame/yourgame.dll --inspect timetravel
```

ウィンドウに HP / Player X の sparkline が出る。グラフの過去の地点を **クリック** すると、
host がその瞬間の GameMemory バイト列を live に書き戻し、ゲームがそこから再開する。HP バーや
敵の位置が当時に戻る。`←` / `→` キーでも 1 フレームずつ巻き戻せる。

これは「観測しているカーソルを動かすだけ」ではなく、ゲームの状態を**実際に過去へ戻す**真の
time-travel debugging。巻き戻すのは host (observer) で、ゲーム DLL は自分が巻き戻されたことを
知らない — 次フレームの `update` が、差し替えられた GameMemory を「現在」として淡々と処理する
だけ (ADR 0005 の純関数モデル)。

## replay との関係

同じ flat POD GameMemory が、録画再生 (`mitiru run --record` / `mitiru replay`) の bit-exact な
記録源にもなる。time-travel・rewind・replay・観測がすべて単一の GameMemory バイト列に乗る
(ADR 0013 / 0017)。「同じ入力・違うコードでどのフレームから分岐したか」を `mitiru replay
--state-diff` で特定できるのも、この単一源のおかげ。

## 仕組み (内部)

- `mitiru::observe::GameMemoryRing` — host が所有する固定容量 (300 フレーム = 5 秒) の
  GameMemory bytes リング。`Engine` が `on_update` 後に毎フレーム push する。
- `mitiru::observe::SeriesMarkers` — double 系列から marker / sparkline を導く純関数群。
- `mitiru::observe::ScrubControlChannel` — inspector → host の scrub command (`{scrubTo, seq}`)
  を temp file で渡す逆チャネル。host が `ScrubControlReader` で poll する。
- `Engine::rewindModuleMemory` — size guard 付きで live GameMemory を過去 bytes に memcpy。
