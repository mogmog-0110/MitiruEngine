# Flat POD: エンジンの核心

> ゲームの状態を1つのflatなPOD構造体 に置く。それだけで、巻き戻し・録画再生・
> AIによる全状態の構造的観測が追加コストなしで付いてくる。MitiruEngineの差別化機能の
> ほとんどが、この1つの設計判断から生えている。

## flat PODとは

`MITIRU_GAME(T)`に渡す型`T` (ゲームの全状態を入れる1個のstruct、以下`GameMemory`)を、
ヒープを所有しない平坦な値型にする。

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

ルールは1つ: **`std::vector` / `std::string` / `std::deque` / ポインタを`GameMemory`に入れない。**
代わりに`FixedVec<T,N>` / `FixedString<N>`を使う。`MITIRU_GAME`は`static_assert`で
`std::is_trivially_copyable_v<T>`を強制するので、違反は **コンパイルエラー** になる
(黙って機能が無効化されることはない)。

観測ログのような非gameplay状態は`GameMemory`の外(DLL内`static`)に置く。

## なぜこれが核心なのか

`GameMemory`がflat PODなら、ホストはそれを 意味を知らずに1個の連続バイト列として
扱える。`memcpy` 1発で「状態のスナップショット」が取れる。この1性質から、本来は別々に
作り込むはずの4つの機能が同じ仕組みで生える。

| 機能 | flat PODだから成り立つ理由 |
|---|---|
| **巻き戻しウィンドウ** | 毎フレーム`GameMemory`をring bufferに`memcpy`。巻き戻し = 過去フレームをliveに`memcpy`で戻すだけ。 |
| **録画再生(replay)** | 入力列 + `GameMemory`バイト列を記録。再生は同じ入力を流してbit-exact (1 bitも違わず一致)に再現。状態がPODなので差分比較もバイト単位。 |
| **AI可視化(reflection)** | フィールドの名前・型・オフセットを宣言すれば、ホストがバイト列を構造化JSON化。AIが全状態を構造的に読める。 |
| **反実仮想(what-if)** | 状態を退避 → 試しの入力で更新を進める → 結果を読む → 退避値へ復元。状態が1個の値だから「保存して試して戻す」が成立。 |

普通のエンジンでは、これらはそれぞれ独立した重いsubsystemになる(シリアライザ、スナップショット
システム、デバッガ統合…)。MitiruEngineでは **「状態は1個のflat POD」** という制約を最初に置いた
ことで、4つとも同じ`memcpy` +記述子で実現できている。制約が機能を生んでいる。

## AI可視化(reflection)の例

`GameMemory`のフィールドを宣言する。

```cpp
MITIRU_REFLECT_STRUCT(Enemy, x, y, respawnIn, alive);     // FixedVec の要素型を先に
MITIRU_REFLECT(MyGame, player, enemies, hp, frame);       // GameMemory 本体
```

これだけで、ホストは`GameMemory`を構造化JSONにできる。AIは窓を開かず(headless)全状態を読める:

```bash
mitiru inspect <pid> --json     # state に全フィールドが構造化されて出る
# あるいは zero-config の AI HTTP API:
MITIRU_AI=1 mitiru run
curl http://127.0.0.1:8090/api/ai/state          # 現在の全状態
curl "http://127.0.0.1:8090/api/ai/diff?from=60&to=0"  # 1 秒前 → 現在で何が変わったか
curl -X POST http://127.0.0.1:8090/api/ai/branch -d '{"keys":"Right","frames":"30"}'  # 右を 30 フレーム押したら?
```

詳細は [REWIND.md](REWIND.md)と [AI_WORKFLOW.md](AI_WORKFLOW.md)を参照。

## non-PODゲームでも「現フレーム観測」だけは段階導入できる

既存ゲームの`GameMemory`が`std::vector` / `std::string`を含む(= flat PODでない)場合でも、
現フレームの構造的観測(`/api/ai/state`)だけなら全面flat POD化の前に導入できる:

- `MITIRU_GAME`を使わず手動`mitiru_module_load`で`api->memorySize = sizeof(GameMemory)`を申告し、
  主要なスカラーフィールドだけ`makeFieldDescriptor<T>(name, offset)`で`reflectFields`に申告する
  (`std::vector`等は申告しない → `reflectToJson`が触らないので安全)。
- `api->memorySize`を申告しないと`/api/ai/state`は空`{}`を返す(offset読みの境界に使うため)。
  reflectionを宣言したのに`memorySize=0`だとengineが起動時に警告を出す。
- **ring / diff / branchはflat POD必須**。これらは`GameMemory`を`memcpy`で退避・復元するので、
  非PODだとポインタが壊れる。non-POD gameでは使わないこと(現フレーム観測のみ)。

全面flat POD化すれば巻き戻し・replay・branchも含めて全部使える。観測だけ先に得て、
あとからflat PODへ移行する、という段階導入が可能。

## まとめ

- ゲーム状態をflat POD (`FixedVec` / `FixedString`で固定長化)にする。
- すると巻き戻し・録画再生・AI全状態観測・反実仮想が 同じmemcpy +記述子から無料で 生える。
- `MITIRU_GAME`がコンパイル時にflat PODを強制するので、構造保証が崩れない。

状態を1個のflat PODに収める。この1行の決めごとから、巻き戻しも録画もAIからの観測も、同じ仕組みで出てくる。
