# ADR 0002 — Lua scripting と visual scripting (NodeGraph) を engine から廃止する

- **Status**: Accepted
- **Date**: 2026-05-14
- **Accepted**: 2026-05-14 (削除完了後の ctest 2110/2110 green、Lua 復活要求なしと判定)
- **Supersedes**: なし
- **Related**: [ADR 0001](0001-cpp-gameplay-cef-view-only.md) (C++ gameplay pivot)、memory `feedback_siv3d_role_model`

## Context

ADR 0001 で「gameplay は C++ で書く、二言語依存を避ける」と決めた。これに伴いロールモデルとして **Siv3D** を採用 (memory `feedback_siv3d_role_model`)。Siv3D は scripting 言語 (Lua / Python / GDScript) を embed しない pure C++ 路線。

一方、MitiruEngine には以下の scripting 系コードが既に存在する:

| ファイル | 役割 |
|---|---|
| `include/mitiru/scripting/LuaEngine.hpp` | Lua VM ラッパ |
| `include/mitiru/scripting/LuaHotReload.hpp` | `.lua` ファイル監視 |
| `include/mitiru/scripting/LuaCodeGen.hpp` | NodeGraph → Lua コード生成 |
| `include/mitiru/scripting/LuaBindings.hpp` | C++ ⇔ Lua バインディング |
| `include/mitiru/scripting/LuaConsole.hpp` | in-game Lua コンソール |
| `include/mitiru/scripting/NodeGraph.hpp` | ビジュアルスクリプティングのノードグラフ |
| `include/mitiru/scripting/NodeTypes.hpp` | ノード型定義 |
| `include/mitiru/scripting/NodeExecutor.hpp` | ノードグラフ実行器 |
| `include/mitiru/scripting/DirectiveNodes.hpp` | ナラティブ用 directive ノード |
| `include/mitiru/vn/LuaScriptingEngine.hpp` | VN 用 Lua adapter (`@script ... @endscript` ブロックを Lua として評価) |
| `tests/mitiru/TestLuaScriptingEngine.cpp` | Lua のテスト |
| `CMakeLists.txt:326-338` | `find_package(Lua)` + `MITIRU_HAS_LUA` define |

これらは「C++ engine + Lua/Visual scripting で gameplay 拡張」という、ADR 0001 / Siv3D ロールモデルと真逆の世界観の名残。

## Decision

**engine から Lua scripting と NodeGraph 関連コードを完全に削除する**。

具体的に:
1. **削除対象**:
   - `include/mitiru/scripting/` ディレクトリ全体 (Lua* と Node* 合計 9 ヘッダ)
   - `include/mitiru/vn/LuaScriptingEngine.hpp` (VN 用 Lua adapter)
   - `tests/mitiru/TestLuaScriptingEngine.cpp` (Lua テスト)
2. **CMakeLists.txt 更新**:
   - `find_package(Lua QUIET)` ブロック (326-338 行) を削除
   - `MITIRU_HAS_LUA` define を削除
   - `mitiru` target から Lua include / link 設定を削除
3. **downstream 影響の処理**:
   - `tests/mitiru/TestVnModules.cpp` — scripting / LuaScriptingEngine への include 参照を除去し、テスト内容を NullScriptingEngine だけで成立させる
   - VN `@script ... @endscript` ブロックは `NullScriptingEngine` (no-op) のみが残る形になる。実行されるユーザ script コードは存在しない前提
4. **代替の方向性**: scripting で書きたかった処理は **C++ で直接書く**。iteration speed は `MITIRU_HEADER_ONLY=ON` の incremental compile + `EngineCommands` (`include/mitiru/core/EngineCommands.hpp`) ベースの runtime コマンド + asset hot reload で確保

## Alternatives Considered

### Alt-1: Lua を declarative content 専用に残す (案 b、ADR 0001 起草時の暫定推奨)

**Rationale**: 既存資産を活用、宣言的データの表現力を上げる。
**理由 (不採用)**: 「if / for を書かせない」縛りは原理的に困難で、結局 JSON で十分。"declarative-only" の境界線は実運用で破られる。Siv3D は scripting 無しで成立しており、無理に残す根拠が薄い。

### Alt-2: Lua を gameplay scripting として残す (案 c)

**Rationale**: 既存実装が動く、iteration speed が上がる。
**理由 (不採用)**: ADR 0001 の「二言語依存を避ける」と真正面から矛盾。Siv3D ロールモデル違反。

### Alt-3: NodeGraph (visual scripting) のみ残し、Lua VM を削除

**Rationale**: ノンプログラマ向けインターフェースは欲しい。
**理由 (不採用)**: NodeGraph は `LuaCodeGen` で Lua に変換して実行する設計のため、Lua VM 削除と同時に実行系を失う。再設計するコスト > 価値。Siv3D も visual scripting を持たない。

### Alt-4 (採用): 全削除

ADR 0001 + Siv3D ロールモデルに最も忠実。engine surface が小さくなり保守コストが下がる。

## Consequences

### Positive
- engine のコードサイズ削減 (約 9-10 ヘッダ + 1 テスト + CMake ブロック)
- `MITIRU_HAS_LUA` 分岐の消滅で build 系がシンプル化
- 「MitiruEngine の API は何か?」が「C++ public headers が全て」に一本化される (Lua bindings は別 surface だった)
- Siv3D ロールモデルとの整合が取れる
- 新規開発者が "Lua も覚えるべきか" と迷う点が消える

### Negative
- VN の `@script ... @endscript` ブロックを実 Lua として評価する機能が失われる (Null fallback のみ)
- NodeGraph ベースの visual scripting に依存していた consumer (もしあれば) は移行が必要
- iteration speed の改善は engine 側ではなく consumer 側で incremental compile に頼る形になる

### 中立
- consumer (例: KaeruCrape) は別リポジトリで現状 JS で書かれているため、本 ADR の影響は engine 内に閉じる
- `external/tracy/manual/filter.lua` `external/zstd/contrib/premake/*.lua` 等は vendor 内部の build/tool ファイルであり本 ADR の対象外 (削除しない)

## Migration Path

1. **Phase 1 (本 ADR で実施)**: 削除対象ファイルを git rm、CMake 更新、`TestVnModules.cpp` の include 整理、`ctest` green を確認
2. **Phase 2**: VN script の `@script` ブロック処理を doc 化 (機能停止の周知)
3. **Phase 3**: scripting 跡地に残った設計負債 (もしあれば) を C++ API で置き換える計画を `cpp-gameplay-api-gaps.md` 経由で進める

## Validation

- **正しかった判断と分かるサイン**: 1 か月以内に「Lua があれば楽だった」と感じる場面が出てこない、engine の build/test がシンプルになり保守が楽になる
- **間違っていた判断と分かるサイン**: scripting 言語が無いと書けない非自明な要件 (例: モッダー向けの拡張点) が複数回出てくる。その場合は本 ADR を Superseded にして再考

## References

- [ADR 0001 — C++ gameplay + CEF UI only](0001-cpp-gameplay-cef-view-only.md)
- [cpp-gameplay-api-gaps.md §8](../cpp-gameplay-api-gaps.md) — 廃止決定を反映済み
- memory `feedback_siv3d_role_model` — 廃止の根拠
- Siv3D (OpenSiv3D) — pure C++ ゲームフレームワークの参考
