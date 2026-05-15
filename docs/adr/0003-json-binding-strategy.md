# ADR 0003 — JSON binding strategy: nlohmann macros + Versioned envelope

- **Status**: Accepted
- **Date**: 2026-05-14
- **Deciders**: リードエンジニア (ユーザー)
- **Author**: Technical Director
- **Supersedes**: なし
- **Related**: [ADR 0001](0001-cpp-gameplay-cef-view-only.md) (C++ pivot)、[ADR 0002](0002-remove-lua-scripting.md) (Lua 廃止)、`docs/cpp-gameplay-api-gaps.md` §5 / §9、memory `feedback_siv3d_role_model`

## Context

ADR 0001 で「gameplay は C++」と決定。gap doc §5 (save/load schema) と §9 (data-driven authoring) は両方とも **「C++ struct を JSON で読み書きする規約」** を engine が標準化する必要がある、と挙げている。

候補は 3 つ:

| 候補 | コスト | 帰結 |
|---|---|---|
| (a) nlohmann/json マクロ (`NLOHMANN_DEFINE_TYPE_*`) | 0 (既に依存) | 各 struct で 1 行マクロ追加。型→JSON は static、struct を変更すると save format に影響 |
| (b) Custom reflection / codegen | 数週間 | clang AST parser 等の build pipeline 投資。自動化された schema 生成や migration が可能 |
| (c) 外部ライブラリ (reflectcpp, boost.describe, magic_enum 拡張) | 数日 | dependency 1 つ追加。完成度高いが engine surface に外部 type が露出 |

Siv3D を tiebreaker に: Siv3D は `Serializer<JSONOutput>` archive pattern を採用しているが、各 type が member fn として `template <class Archive> void serialize(Archive& a)` を持つ "intrusive" 規約。これは nlohmann/json の `NLOHMANN_DEFINE_TYPE_INTRUSIVE` とほぼ同じ思想 (struct 側に 1 行書く)。Siv3D の路線に最も近いのは (a)。

## Decision

**(a) nlohmann/json の既存マクロを engine 標準とする**。engine 側は薄い wrapper layer のみを提供:

1. `include/mitiru/data/JsonBinding.hpp` — 共通ヘルパ
   - `toJson<T>(T)` / `fromJson<T>(Json) -> optional<T>` / `fromJsonResult<T>(Json) -> {value,error}`
   - `Versioned envelope`: `{ "version": N, "data": <T> }` 形式の共通レイアウト
   - `toJsonVersioned(T, ver)` / `fromJsonVersioned<T>(Json, ver)`
   - `MigrationChain<T>`: 旧 version JSON を最新まで step ごとに変換するチェーン

2. `include/mitiru/data/SaveSchema.hpp` — Save 専用 stateful wrapper
   - 現在の schema version を保持
   - `MigrationChain<T>` を内包
   - `toJsonString(T) / fromJsonString(string) -> FromJsonResult<T>`

3. `include/mitiru/data/ContentLoader.hpp` — Content 読み込み専用 stateless utility
   - `loadFile / loadString / loadJson` の 3 form
   - 失敗時のエラーメッセージ込みで返す

consumer 側は struct に `NLOHMANN_DEFINE_TYPE_INTRUSIVE(T, field1, field2, ...)` (struct 内) または `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(T, field1, field2, ...)` (struct 外) を 1 行書くだけで、上記すべての API で T が使えるようになる。

## Alternatives Considered

### Alt-1: 完全 codegen (clang AST → C++ + JSON Schema)

**Pros**: schema 自動生成 / migration 自動生成 / IDE 補完 / ドキュメント生成。
**Cons**: build pipeline 投資が重く、Siv3D ロールモデル (依存最小) と衝突。当面の use case (save / balance table / dialogue) は 1 行マクロで足りる。**不採用**。

### Alt-2: 外部 reflection library (reflectcpp 等)

**Pros**: マクロ不要、自動的にすべての field を拾う。
**Cons**: 依存追加 + engine surface に外部 type が漏れる。consumer が `mitiru::data::Json` 経由で nlohmann::json を既に使っており、ここに別 reflection 系を足すと二重投資。**不採用**。

### Alt-3: 完全 hand-written `to_json` / `from_json` を強制

**Pros**: 完全制御。
**Cons**: 毎 struct ごとにボイラーが増える。nlohmann のマクロ展開と等価なコードを手書きする意味は薄い。consumer の負担が大きく gap §5/§9 の動機 (「毎 struct で手書きするのが辛い」) を解消できない。**不採用**。

## Consequences

### Positive

- **consumer の負担最小**: struct に 1 行マクロを足すだけで全機能が使える
- **依存追加なし**: nlohmann/json は既に依存
- **Siv3D ロールモデル整合**: scripting 言語に追加 reflection は持ち込まない
- **Versioning が宣言的**: `SaveSchema<T>` の `migrations()` に step を append するだけ
- **テスト容易**: `fromJson` は failure 時 nullopt / `fromJsonResult` はエラー詳細付きで返すため例外を呼び出し側で扱わずに済む

### Negative

- **コンパイル時間**: nlohmann/json の template heavy 実装は header-only でビルド時間に効く。`tests/CMakeLists.txt` には既に PCH 設定があり、これでカバーされる
- **マクロの位置**: `NLOHMANN_DEFINE_TYPE_INTRUSIVE` は struct 定義内、`NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` は struct 定義外で書く必要がある。MSVC は struct 外で INTRUSIVE を使うと C2255 (friend は class 内のみ) で fail する — 開発者がドキュメントを読まないと一度踏みうる pitfall (実際このセッションで踏んだ、commit `be6b0b36` で修正済)
- **migration step は append-only**: 一度 production で書いた `addStep(N, N+1, ...)` を後から削るとデータが壊れる。doc で明示し運用ルールとする

### Neutral

- 完全な reflection (任意の field を runtime に列挙) は提供しない。必要なら別 ADR で議論
- 巨大 save data に対する performance: nlohmann::json は per-call alloc が多い。frame 内には呼ばない (header の note で明示) ことで運用カバー

## Validation Criteria

正しかった判断と分かるサイン:
- consumer (KaeruCrape 等) が migration を engine API だけで完結でき、SaveStore 直叩きを避けるようになる
- gap doc §5/§9 の「毎 struct で nlohmann::json を手書き」という不満が消える
- 1 か月以内に「reflection が必要」というユースケースが具体的に出てこない

間違っていた判断と分かるサイン:
- consumer 側で `NLOHMANN_DEFINE_TYPE_*` の代わりに raw nlohmann::json を書く事例が多発 (= マクロが使いにくい)
- migration step の append-only ルールを破る誘惑 (= API 設計が悪い)
- runtime reflection が要る具体的需要 (例: editor / inspector) が複数同時に立つ

これらが起きたら本 ADR を再評価し、(b) codegen または (c) external lib への移行を検討する。

## References

- 実装: `include/mitiru/data/JsonBinding.hpp` / `SaveSchema.hpp` / `ContentLoader.hpp`
- テスト: `tests/mitiru/TestJsonBinding.cpp` / `TestSaveSchema.cpp` / `TestContentLoader.cpp`
- 使用 pattern: `docs/CPP_GAMEPLAY_GUIDE.md` §4 Pattern H / Pattern I
- 関連 gap: `docs/cpp-gameplay-api-gaps.md` §5 §9
- ライブラリ: [nlohmann/json](https://github.com/nlohmann/json) `NLOHMANN_DEFINE_TYPE_INTRUSIVE` / `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE`
