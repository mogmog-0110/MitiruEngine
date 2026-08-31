# 巻き戻しウィンドウ

ゲームの過去フレームを観測し、グラフをクリックしてその瞬間に巻き戻す。
MitiruEngineの看板機能のひとつ。作者がやることは2つだけ。

1. ゲームの全状態を1個のstruct (`GameMemory`)にまとめ、それを **flat POD**
   (ポインタも`std::vector`も持たない、丸ごとコピーできるstruct)にする
2. 「その状態から追いたい値を引く関数」を`MITIRU_GAME_SERIES`で宣言する

これだけでhostが過去フレームの状態を自動で記録し、巻き戻し
ウィンドウにHP履歴などの系列を出す。手で履歴を貯めたりJSONを組んだりするコードは要らない。

模範サンプル: `examples/rewind/`。

## なぜflat PODなのか

hostはゲームの全状態(`GameMemory`)をバイト列として丸ごとコピーして記録し、巻き戻すときは
そのバイト列をliveの状態に書き戻す。だから状態structに`std::vector` /
`std::string`のような**内部ポインタを持つ型**があると、コピーしても復元できない。

`MITIRU_GAME`はこれをコンパイル時に確認する。可変長が欲しいときは固定長コンテナを使う:

```cpp
#include <mitiru/core/FixedVec.hpp>

struct GameMemory {
    mitiru::FixedVec<Enemy, 64> enemies;   // std::vector<Enemy> の代わり
    mitiru::FixedString<32>     name;       // std::string の代わり
    int hp = 100;
};
// MITIRU_GAME が static_assert(std::is_trivially_copyable_v<GameMemory>) を自動でかける
```

`FixedVec`は`push_back` / `at` / `size` / `clear` / `removeAt`を持ち、vectorに近い手触り。
上限を型に焼くので「敵は最大何体か」を設計時に意識できる(毎フレームのメモリ確保も無くなる)。

> 観測ログのようなgameplayでない状態(イベントログ・統計)は状態structの外
> (DLL内の`static`)に置く。巻き戻すべきはgameplay stateだけ。

## probeを宣言する

probeとは、状態から値を1個取り出す観測関数のこと。`MITIRU_GAME(T)`の代わりに
`MITIRU_GAME_SERIES(T, …)`を使い、追いたいスカラーごとに
「`GameMemory`からdoubleを引くcapture無しの関数」を並べる:

```cpp
namespace {
double hpProbe(const void* m)      { return static_cast<const GameMemory*>(m)->hp; }
double playerXProbe(const void* m) { return static_cast<const GameMemory*>(m)->playerX; }
}

MITIRU_GAME_SERIES(GameMemory,
    { "hp", "HP",       &hpProbe,      35.0, 1 },   // 35 を下抜けたら danger marker を打つ
    { "x",  "Player X", &playerXProbe,  0.0, 0 });  // 閾値なし (最後の 0)
```

各行は`{ id, ラベル, 関数, 閾値, 閾値を使うか(1/0) }`。hostが状態の履歴ringの
各フレームに関数を適用して系列を作り、被弾・回復の段差やdangerライン跨ぎを自動でmarkerに
する。

## inspectorを開いて巻き戻す

巻き戻しウィンドウは独立したsub-window。ゲーム窓には何も出ない(game窓はpure)。

```bash
mitiru run --inspect rewind
# または host に直接:  mitiru_host.exe yourgame/yourgame.dll --inspect rewind
```

ウィンドウにHP / Player Xのsparklineが出る。グラフの過去の地点を **クリック** すると、
hostがその瞬間の状態バイト列をliveに書き戻し、ゲームがそこから再開する。HPバーや
敵の位置が当時に戻る。`←` / `→`キーでも1フレームずつ巻き戻せる。

これは「観測しているカーソルを動かすだけ」ではなく、ゲームの状態を実際に過去へ戻す本物の
巻き戻し。戻すのはhost (observer)で、ゲームDLLは自分が巻き戻されたことを
知らない。次フレームの`update`が、差し替えられた状態を「現在」として淡々と処理する
だけ(DLLは「状態を受け取って処理する」純関数に近い形だから成り立つ)。

## replayとの関係

同じ1個の状態structが、録画再生(`mitiru run --record` / `mitiru replay`)のbit-exact
(1 bitも違わず一致)な記録源にもなる。巻き戻し・リプレイ・観測がすべて単一の状態バイト列に
乗る。「同じ入力・違うコードでどのフレームから分岐したか」を
`mitiru_host --state-diff <録画A> <録画B>` (2つの録画の状態バイト列を突き合わせる)で
特定できるのも、この単一源のおかげ。

## 仕組み(内部)

- `mitiru::observe::GameMemoryRing` — hostが所有する固定容量(300フレーム = 5秒)の
  状態bytesリング。`Engine`が`on_update`後に毎フレームpushする。
- `mitiru::observe::SeriesMarkers` — double系列からmarker / sparklineを導く純関数群。
- `mitiru::observe::ScrubControlChannel` — inspector → hostのコマ送り移動コマンド
  (`{scrubTo, seq}`)をtemp fileで渡す逆チャネル。hostが`ScrubControlReader`でpollする。
- `Engine::rewindModuleMemory` — size guard付きでliveの状態を過去bytesにmemcpy。
