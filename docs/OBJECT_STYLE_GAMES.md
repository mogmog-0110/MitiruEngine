# クラスとコンポーネントで game を書く — `MITIRU_GAME_OBJECTS`

Scene クラス、GameObject の継承、仮想関数のコンポーネント、`std::vector<std::unique_ptr<...>>`。
この形で書きたい game の入口。設計判断は [ADR 0040](adr/0040-object-style-games.md)。

新しく始めるなら `mitiru new myGame -t objects` (この形のテンプレート)。動く見本は `examples/objects_kitchen`。

全状態を 1 個の flat POD に置ける game は、これまでどおり `MITIRU_GAME` を使う。そちらは
フレーム単位の rewind と分岐エディタが使える。どちらを選ぶかは下の表で決める。

## 最小の形

```cpp
#include <mitiru/module/Game.hpp>

// 進行データ。flat POD。セーブ・ロード・録画・/api/ai/state の対象はこれだけ。
struct Progress
{
    int           day      = 1;
    int           money    = 0;
    std::uint32_t rngState = 12345;   // 乱数の種はここに置く (replay を通すため)
};

struct GameObject
{
    virtual ~GameObject() = default;
    virtual void update(Progress& progress, float dt) = 0;
};

// 場面の中身。普通の C++。DLL の中に 1 個だけ生きる。
struct Kitchen
{
    std::vector<std::unique_ptr<GameObject>> objects;

    // 進行データから場面を組み立てる。初回・ホットリロード後・ロード後・restart 後に呼ばれる。
    void build(const Progress& progress);   // 読むだけ

    void update(Progress& progress, mitiru::Input in, mitiru::Hud hud, float dt);
    void draw(const Progress& progress, mitiru::Screen& screen);
};

MITIRU_GAME_OBJECTS(Kitchen, Progress)
```

`update` は `(Progress&, Input, Hud, float)` / `(Progress&, Input, float)` / `(Progress&, float)` のどれでもよい。
`draw` は `(const Progress&, Screen&)` か `(const Progress&, Canvas&)`。`build` と `shutdown(Progress&)` は任意。

## どちらで書くか

| | `MITIRU_GAME(T)` | `MITIRU_GAME_OBJECTS(Game, Progress)` |
|---|---|---|
| 状態の形 | 全部 flat POD | 進行データだけ POD、場面は普通の C++ |
| セーブ / ロード | 全状態 | 進行データ。ロード後に `build` し直す |
| `--replay` / `--capture` | 全状態を bit-exact 照合 | 通る。照合は進行データの範囲、絵は capture で見る |
| ホットリロード | 状態を温存 | 進行データを温存、場面は `build` し直す |
| rewind の scrub / resim | 使える | 使えない (host が断る) |
| 分岐エディタ / 候補の並走 | 使える | 使えない (host が断る) |

向いているもの: オブジェクトの種類が多く、振る舞いが組み合わせで決まる game (調理場、ショップ、探索)。
向いていないもの: 1 フレーム単位で巻き戻して調整したいアクション (そちらは `MITIRU_GAME`)。

## 場面はいつ作られ、いつ捨てられるか

| 出来事 | 起きること |
|---|---|
| 初回 load | `Progress` を既定値で確保 (`Progress::init()` があれば呼ぶ)。最初の `update` で `build` |
| ホットリロード | host は `Progress` の bytes を温存して新しい DLL に渡す。新しい DLL の場面は空なので、最初の `update` で `build` |
| `hud.load(slot)` | host が `Progress` を書き戻し、`on_rebuild` を呼ぶ。場面を捨てて `build` |
| `hud.requestRestart()` | `Progress` を既定値に戻し、場面を捨てる。次の `update` で `build` |
| unload / shutdown | `shutdown(Progress&)` (あれば) の後、場面を破棄 |

**場面の中で「戻ってほしいもの」は `Progress` に置く。** 置いていないものは、作り直しのたびに初期状態になる。
営業中の鉄板の焼き加減をロードで復元したければ `Progress` に持たせる。持たせないなら「ロードすると
その日の頭からやり直し」になる。どちらにするかは game の仕様として決める。

## `--replay` を通す条件

replay はフレーム 0 から入力を流し直す。場面が入力だけで決まれば、同じ絵になる。

- dt は host から来る固定値だけを使う。壁時計 (`std::chrono` の now) を読まない。
- 乱数の状態は `Progress` に置き、場面の中に別の乱数源を持たない。
- ポインタ値の順序に依存しない。`std::unordered_map<GameObject*, ...>` を回して処理順を決めない
  (アドレスは実行のたびに変わる)。順序が要るなら `std::vector` の並びか、自前の連番を使う。
- `build` は同じ `Progress` から常に同じ場面を作り、`Progress` を書き換えない (`const Progress&` をコンパイルで強制)。
  replay のロードは「記録済みの bytes を書き戻してから `build`」で代用するので、`build` が乱数を進めると
  そのフレームで録画と食い違う。乱数が要るなら日数や位置から決めるか、`update` の中で引く。

検証は `mitiru_host <dll> --headless --input-script <f> --record a.mtrr` → `--replay-test a.mtrr` と、
`--capture-dir` の PNG の目視。

## シーン管理

`mitiru::scene::IScene` / `SceneRouter` は game DLL 向けの部品ではない (`onUpdate(float)` しか渡らない)。
Title / Menu / Game / Result の切り替えは game 側で持つ。`Progress` に現在のシーン番号を置き、
`Kitchen` の中で `std::unique_ptr<Scene>` を差し替える形で 100 行ほどになる。シーン番号を `Progress` に
置いておけば、ロードとホットリロードで同じシーンに戻る。

## コンポーネントを POD のまま書きたい場合

`MITIRU_GAME` のまま GameObject 風に書く部品もある: `PodPool` (ハンドル + 世代)、`PodHsm`、`PodArena`、
`MsgQueue`、`PodTimeline`、`FixedVec`、`WorldObjects`、`LocalTransform`、`Tags`。仮想関数は使えないので
型 ID + 関数表になる。rewind と分岐が要るならこちら。
