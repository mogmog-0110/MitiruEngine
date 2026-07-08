# Flat POD — エンジンの核心

> ゲームの状態を **1 つの flat な POD 構造体** に置く。それだけで、巻き戻し・録画再生・
> AI による全状態の構造的観測が **追加コストなしで** 付いてくる。MitiruEngine の差別化機能の
> ほとんどが、この 1 つの設計判断から生えている。

## flat POD とは

`MITIRU_GAME(T)` に渡す型 `T` (ゲームの全状態を入れる 1 個の struct、以下 `GameMemory`) を、
ヒープを所有しない平坦な値型にする:

```cpp
#include <mitiru/core/FixedVec.hpp>
using namespace mitiru;

struct Enemy { float x, y; float respawnIn; bool alive; };

struct MyGame {
    Vec2                 player;
    FixedVec<Enemy, 16>  enemies;   // std::vector ではなく固定容量
    FixedString<32>      title;     // std::string ではなく固定長
    int                  hp = 100;
    std::uint32_t        frame = 0;

    void update(Input in, Hud hud, float dt) { /* ... */ }
};
MITIRU_GAME(MyGame)
```

ルールは 1 つ: **`std::vector` / `std::string` / `std::deque` / ポインタを `GameMemory` に入れない。**
代わりに `FixedVec<T,N>` / `FixedString<N>` を使う。`MITIRU_GAME` は `static_assert` で
`std::is_trivially_copyable_v<T>` を強制するので、違反は **コンパイルエラー** になる
(黙って機能が無効化されることはない)。

観測ログのような非 gameplay 状態は `GameMemory` の外 (DLL 内 `static`) に置く。

## なぜこれが核心なのか

`GameMemory` が flat POD なら、ホストはそれを **意味を知らずに 1 個の連続バイト列として**
扱える。`memcpy` 1 発で「状態のスナップショット」が取れる。この 1 性質から、本来は別々に
作り込むはずの 4 つの機能が **同じ仕組みで** 生える:

| 機能 | flat POD だから成り立つ理由 |
|---|---|
| **巻き戻し (タイムトラベル窓)** | 毎フレーム `GameMemory` を ring buffer に `memcpy`。巻き戻し = 過去フレームを live に `memcpy` で戻すだけ。 |
| **録画再生 (replay)** | 入力列 + `GameMemory` バイト列を記録。再生は同じ入力を流して bit-exact (1 bit も違わず一致) に再現。状態が POD なので差分比較もバイト単位。 |
| **AI 可視化 (reflection)** | フィールドの名前・型・オフセットを宣言すれば、ホストがバイト列を構造化 JSON 化。AI が全状態を構造的に読める。 |
| **反実仮想 (what-if)** | 状態を退避 → 試しの入力で更新を進める → 結果を読む → 退避値へ復元。状態が 1 個の値だから「保存して試して戻す」が成立。 |

普通のエンジンでは、これらはそれぞれ独立した重い subsystem になる (シリアライザ、スナップショット
システム、デバッガ統合…)。MitiruEngine では **「状態は 1 個の flat POD」** という制約を最初に置いた
ことで、4 つとも同じ `memcpy` + 記述子で実現できている。制約が機能を生んでいる。

## AI 可視化 (reflection) の例

`GameMemory` のフィールドを宣言する:

```cpp
MITIRU_REFLECT_STRUCT(Enemy, x, y, respawnIn, alive);     // FixedVec の要素型を先に
MITIRU_REFLECT(MyGame, player, enemies, hp, frame);       // GameMemory 本体
```

これだけで、ホストは `GameMemory` を構造化 JSON にできる。AI は窓を開かず (headless) 全状態を読める:

```bash
mitiru inspect <pid> --json     # state に全フィールドが構造化されて出る
# あるいは zero-config の AI HTTP API:
MITIRU_AI=1 mitiru run
curl http://127.0.0.1:8090/api/ai/state          # 現在の全状態
curl "http://127.0.0.1:8090/api/ai/diff?from=60&to=0"  # 1 秒前 → 現在で何が変わったか
curl -X POST http://127.0.0.1:8090/api/ai/branch -d '{"keys":"Right","frames":"30"}'  # 右を 30 フレーム押したら?
```

詳細は [TIME_TRAVEL.md](TIME_TRAVEL.md) と [AI_WORKFLOW.md](AI_WORKFLOW.md) を参照。

## non-POD ゲームでも「現フレーム観測」だけは段階導入できる

既存ゲームの `GameMemory` が `std::vector` / `std::string` を含む (= flat POD でない) 場合でも、
**現フレームの構造的観測 (`/api/ai/state`) だけ**なら全面 flat POD 化の前に導入できる:

- `MITIRU_GAME` を使わず手動 `mitiru_module_load` で `api->memorySize = sizeof(GameMemory)` を申告し、
  主要な**スカラー**フィールドだけ `makeFieldDescriptor<T>(name, offset)` で `reflectFields` に申告する
  (`std::vector` 等は申告しない → `reflectToJson` が触らないので安全)。
- ⚠️ `api->memorySize` を申告しないと `/api/ai/state` は空 `{}` を返す (offset 読みの境界に使うため)。
  reflection を宣言したのに `memorySize=0` だと engine が起動時に警告を出す。
- ⚠️ **ring / diff / branch は flat POD 必須**。これらは `GameMemory` を `memcpy` で退避・復元するので、
  非 POD だとポインタが壊れる。non-POD game では使わないこと (現フレーム観測のみ)。

全面 flat POD 化すれば巻き戻し・replay・branch も含めて全部使える。観測だけ先に得て、
あとから flat POD へ移行する、という段階導入が可能。

## まとめ

- ゲーム状態を flat POD (`FixedVec` / `FixedString` で固定長化) にする。
- すると巻き戻し・録画再生・AI 全状態観測・反実仮想が **同じ memcpy + 記述子から無料で** 生える。
- `MITIRU_GAME` がコンパイル時に flat POD を強制するので、構造保証が崩れない。

「状態は 1 個の flat POD」——この 1 行の設計判断が MitiruEngine の差別化の土台になっている。
