# ADR 0010 — `mitiru update` / `mitiru self-update` による更新機構

- **Status:** Accepted (2026-05-27)
- **Context:** engine version は project ごとに `mitiru.toml` の `engine = "X.Y.Z"` で
  pin する ([reference: 二リモート release-snapshot 運用])。CLI binary (`mitiru.exe`)
  は engine とは別 repo (`mogmog-0110/mitiru-cli`) で配布する ([ADR 0006](0006-go-for-cli.md))。
  この 2 つの「更新」が現状どちらも手作業 — toml を手で書き換える / `go install` し直す
  / release zip を配り直す。これは「**CLI が一級市民**」([SCOPE.md](../SCOPE.md)) に反する。
  単一エントリポイントである `mitiru` が更新も owns すべき。

## 決定

更新を **2 つの独立コマンド**として実装する。混ぜない (1 ツール = 1 関心事)。

| コマンド | 関心事 | 対象 |
|---|---|---|
| **`mitiru update`** | このプロジェクトの engine pin を最新に揃える | `mitiru.toml` の `engine` |
| **`mitiru self-update`** | CLI binary 自体を最新に置き換える | `mitiru.exe` |

どちらも **pulled** (ユーザーが叩く)。バックグラウンドの自動更新 nag は哲学
(「必要なものしか画面に出さない」) に反するため**実装しない**。

## 設計上の決定と失敗モード分析

哲学優先 ([Meta-rule](../../CLAUDE.md))。実装の都合より先に「どこで mistake が起こるか」を列挙する。

### `mitiru update` (層A)

1. **"latest" の解決元は tag 一覧 + semver max**。GitHub *Releases* は release ごとに
   作られるとは限らず遅延する (snapshot pipeline は tag を push する)。`releases/latest`
   API に依存すると最新 tag を取りこぼす。→ `GET /repos/.../tags` を列挙し semver 最大を選ぶ。
2. **ABI break を黙って上げない**。0.x では minor bump が ABI 破壊を含みうる
   (例 v0.6→v0.7 で InputSnapshot ABI v4→v5)。major/minor が上がる更新は赤字警告し
   確認を必須にする。`--yes` でのみ非対話。patch bump は警告なし。
3. **`MITIRU_ENGINE_ROOT` override 検出**。override 中は toml pin が無視される
   ([cache.go の解決順])。その場合は「ローカル engine 使用中、pin は cosmetic」と明示し、
   prefetch はスキップする。
4. **downgrade 防止** (現在 pin ≧ latest なら "up to date")。**offline graceful**
   (tag 取得失敗時は既存 pin を壊さず error で抜ける)。
5. **toml の surgical 書換**。`toml.Encode` で再 marshal するとコメントが全消滅する
   (sachi_errand の `[lofi]` 等、設計意図がコメントに載っている)。→ `engine = "..."`
   行のみ正規表現で置換し、他は 1 byte も触らない。
6. `--check` で副作用なしの差分表示のみ。

### `mitiru self-update` (層B) + release pipeline

7. **配布基盤が前提**。CLI の prebuilt binary 配布は現状死んでいる (release は古い
   tag で停止、CI 無し)。self-update が download すべき binary が存在しないと機能しない。
   → 先に **goreleaser + tag push trigger の workflow** を整備し、tag ごとに
   `mitiru.exe` を build して GitHub Release に attach する。
8. **実行中 exe は上書きできない (Windows)**。`mitiru.exe` 自身が動いている間は同名
   置換が `ERROR_SHARING_VIOLATION` で失敗する。→ rename-swap: 現 exe を `*.old` に
   rename → 新 exe を書き込み → `*.old` は次回起動時に best-effort 削除。
9. **engine release pipeline は GitHub Release も作る**。`build_release_snapshot.py` は
   tag だけでなく Release も作成し、`install` / `latest` 系の解決を権威化する (上記 #1 の
   tag 列挙は冗長な保険として残す)。

## 結果

- **良くなる:** 更新が `mitiru` の 1 コマンドに収まり、CLI 一級市民の原則が更新フェーズ
  でも貫かれる。ABI 破壊の見落としを警告でガードする。
- **コスト:** mitiru-cli に release 基盤 (goreleaser/CI) を持つ責務が増える。self-update の
  rename-swap は Windows 固有の地雷を含むため test が要る。
- **5 軸との関係:** 更新機構自体は 5 つの差別化軸を直接強化しない DX 整備である。正当化は
  「CLI が一級市民」+ first-touch north star (console = install/update のみ) に置く。
  差別化ではなく table-stakes と認識した上で採用する。
