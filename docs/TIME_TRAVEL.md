# 巻き戻しウィンドウ

ゲームの過去フレームを観測し、グラフをクリックして**その瞬間に巻き戻す** —— MitiruEngine の
看板機能のひとつ。作者がやることは 2 つだけ。

1. ゲームの全状態を 1 個の struct (`GameMemory`) にまとめ、それを **flat POD**
   (ポインタも `std::vector` も持たない、丸ごとコピーできる struct) にする
2. 「その状態から追いたい値を引く関数」を `MITIRU_GAME_SERIES` で宣言する

これだけで host が過去フレームの状態を自動で記録し、巻き戻し
ウィンドウに HP 履歴などの系列を出す。手で履歴を貯めたり JSON を組んだりするコードは要らない。

模範サンプル: `examples/rewind/`。

## なぜ flat POD なのか

host はゲームの全状態 (`GameMemory`) を **バイト列として丸ごとコピー** して記録し、巻き戻すときは
そのバイト列を live の状態に書き戻す。だから状態 struct に `std::vector` /
`std::string` のような**内部ポインタを持つ型**があると、コピーしても復元できない。

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

> 観測ログのような **gameplay でない状態** (イベントログ・統計) は状態 struct の外
> (DLL 内の `static`) に置く。巻き戻すべきは gameplay state だけ。

## probe を宣言する

probe とは、状態から値を 1 個取り出す観測関数のこと。`MITIRU_GAME(T)` の代わりに
`MITIRU_GAME_SERIES(T, …)` を使い、追いたいスカラーごとに
「`GameMemory` から double を引く capture 無しの関数」を並べる:

```cpp
namespace {
double hpProbe(const void* m)      { return static_cast<const GameMemory*>(m)->hp; }
double playerXProbe(const void* m) { return static_cast<const GameMemory*>(m)->playerX; }
}

MITIRU_GAME_SERIES(GameMemory,
    { "hp", "HP",       &hpProbe,      35.0, 1 },   // 35 を下抜けたら danger marker を打つ
    { "x",  "Player X", &playerXProbe,  0.0, 0 });  // 閾値なし (最後の 0)
```

各行は `{ id, ラベル, 関数, 閾値, 閾値を使うか(1/0) }`。host が状態の履歴 ring の
各フレームに関数を適用して系列を作り、被弾・回復の段差や danger ライン跨ぎを自動で marker に
する。

## inspector を開いて巻き戻す

巻き戻しウィンドウは独立した sub-window。ゲーム窓には何も出ない (game 窓は pure)。

```bash
mitiru run --inspect timetravel
# または host に直接:  mitiru_host.exe yourgame/yourgame.dll --inspect timetravel
```

ウィンドウに HP / Player X の sparkline が出る。グラフの過去の地点を **クリック** すると、
host がその瞬間の状態バイト列を live に書き戻し、ゲームがそこから再開する。HP バーや
敵の位置が当時に戻る。`←` / `→` キーでも 1 フレームずつ巻き戻せる。

これは「観測しているカーソルを動かすだけ」ではなく、ゲームの状態を**実際に過去へ戻す**本物の
巻き戻し。戻すのは host (observer) で、ゲーム DLL は自分が巻き戻されたことを
知らない — 次フレームの `update` が、差し替えられた状態を「現在」として淡々と処理する
だけ (DLL は「状態を受け取って処理する」純関数に近い形だから成り立つ)。

## replay との関係

同じ 1 個の状態 struct が、録画再生 (`mitiru run --record` / `mitiru replay`) の bit-exact
(1 bit も違わず一致) な記録源にもなる。巻き戻し・リプレイ・観測がすべて単一の状態バイト列に
乗る。「同じ入力・違うコードでどのフレームから分岐したか」を
`mitiru_host --state-diff <録画A> <録画B>` (2 つの録画の状態バイト列を突き合わせる) で
特定できるのも、この単一源のおかげ。

## 仕組み (内部)

- `mitiru::observe::GameMemoryRing` — host が所有する固定容量 (300 フレーム = 5 秒) の
  状態 bytes リング。`Engine` が `on_update` 後に毎フレーム push する。
- `mitiru::observe::SeriesMarkers` — double 系列から marker / sparkline を導く純関数群。
- `mitiru::observe::ScrubControlChannel` — inspector → host のコマ送り移動コマンド
  (`{scrubTo, seq}`) を temp file で渡す逆チャネル。host が `ScrubControlReader` で poll する。
- `Engine::rewindModuleMemory` — size guard 付きで live の状態を過去 bytes に memcpy。
