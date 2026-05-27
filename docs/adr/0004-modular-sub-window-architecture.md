# ADR 0004 — Modular Sub-Window Architecture

- **Status:** Accepted (2026-05-20)
- **Context:** P1 (CLI integration + HTML UI sample) 段階で hello_game に
  Pause badge / Input Monitor を inline panel として実装した結果、
  「**gameplay と debug が同じ window 内で混在する**」状態が発生。screenshot 撮ると
  debug overlay が映り込み、ゲーム本体の見た目を汚染する。

## 決定

**MitiruEngine は「メインウィンドウ + サブウィンドウ群」のマルチウィンドウ
アーキテクチャを採用する。** これを engine の 5 つ目の差別化軸とする。

具体的に:

1. **Main window** はゲーム本体のみを表示する:
   - gameplay rendering (player / enemies / shapes)
   - ゲームに不可欠な HUD (HP / score / インベントリ等、**ゲーム体験の一部**)
   - **screenshot 撮影時に映ってよいもの**だけ
2. **Sub-window** は機能別に独立した OS-level window として spawn される:
   - Input Monitor (`MITIRU_DEBUG=1` で起動)
   - Time-travel Inspector (P2)
   - Scene Tree / FSM Inspector / Audio Mixer 等の subsystem inspector (P2-P3)
   - 将来的に gameplay UI (インベントリ / マップ / 設定) も同様にできる
3. **1 ツール = 1 ウィンドウ**: 1 つの sub-window は 1 つの関心事のみ
4. **デフォルトでは何も開かない**: pulled UI 原則。debug mode opt-in 時のみ自動 spawn

## 既存哲学との関係

これは [アトミックツール哲学](../SCOPE.md) の **OS-window レベルへの自然な拡張**:

- 「**必要なものしか画面に出さない**」 → screenshot に debug 情報が映らない
- 「**1 ツール = 1 ウィンドウ**」 → 文字通り OS window として実装
- 「**デフォルトでは何も開かない**」 → debug mode opt-in 時のみ
- 「**CLI が一級**」 → `mitiru inspect <subject>` が個別 sub-window を spawn する

## 実装 path

### 候補比較

| Path | 仕組み | 工数 | 採用 |
|---|---|---|---|
| **A. CEF popup** | main CEF browser から `window.open()` で child を spawn | 中 | **第 1 候補** |
| **B. HTTP server + 外部ブラウザ** | engine が localhost で serve、Chrome 等で開く | 中 | 補助 |
| **C. Win32 child window + WebView** | OS window 直接管理、各々 browser 埋め込み | 大 | 不採用 |
| **D. 別プロセス debug viewer** | IPC で engine と通信、standalone viewer app | 大 | 不採用 (overkill) |

### Path A の選定理由

- engine は既に CEF 統合済み → 追加 dep 無し
- 既存の `mitiru::cef::StateStore` を per-window 拡張で再利用可
- 1 プロセスで運用、bridge ロジック共有
- Window 管理は OS に委譲、cross-monitor / 配置自由

### Path A 実装で必要になる engine work

現状 `MitiruCefContext` は **1 browser 前提**。multi-window 対応に必要な変更:

1. `MitiruCefContext::openSubWindow(url, title, opts)` メソッド追加
   - 新規 OS top-level window を作成
   - 別 CEF browser インスタンスを attach
   - 独立した bridge channel を割り当てる
2. `mitiru::cef::StateStore` を per-window 化:
   - state key prefix で window を区別 (例: `sub.input.*`, `sub.inspector.*`)
   - もしくは StateStore インスタンスを window 毎に持つ
3. JS 側 `mitiru_cef_state.js` は単一 window 動作前提。sub-window でも動くよう改修
4. **CEF single-process mode を解除する**:
   - 2026-05-20 の実証で判明: 現状 engine は CEF を single-process mode
     で起動している (`Cannot use V8 Proxy resolver in single process mode`
     エラー)。`window.open()` で popup browser を spawn しようとすると
     `STATUS_BREAKPOINT (-2147483645)` で crash する
   - multi-process mode への切替が前提条件。CEF 設定の見直し + 各
     subprocess (renderer / utility / GPU) の起動経路の整備が必要

これらは **P2 (Time-travel inspector)** で本格実装する。

### 2026-05-20 proof-of-concept の結果

- `assets/debug.html` を作成 (Input Monitor + Pause indicator の独立 UI)
- `scene.html` から `window.open('debug.html', 'mitiruDebug', ...)` で
  popup spawn を試みた → CEF が single-process mode のため crash
- popup を呼ばない、postMessage forwarding 経路のみ残した状態では正常動作
- **結論**: design artifact (`debug.html`) は repo に残し、`scene.html` は
  inline fallback panel に戻した。P2 で engine 側を multi-process 化したら
  scene.html を 1 行書き換えるだけで popup 経由に切り替わる

### Path B (HTTP server) の補助的位置付け

将来的に「**外部ブラウザで debug を見たい人**」向けに、engine が
`MITIRU_HTTP_DEBUG_PORT=9292` 等の環境変数で HTTP server を立てる選択肢も提供する。
Path A と排他ではなく並列に提供する。

## トレードオフ

### Pros
- ゲーム window の screenshot が clean
- マルチモニタユーザーに革命的 UX
- アトミックツール哲学の completion
- Unity / Godot / raylib いずれも持たない差別化要素
- gameplay UI まで含めれば、Stardew Valley を 2 画面で遊ぶようなことが可能になる

### Cons
- シングルモニタユーザーは window 切替コスト
- newcomer は「debug window どこ?」と迷う → 起動時 console に明示出力する
- 実装が main + sub の 2-tier 管理になり engine 内部が複雑化

## 影響

### docs/SCOPE.md
- 「4 つの独自軸」を「**5 つの独自軸**」に拡張
- 5 軸目として "Modular sub-window architecture" を追加

### README.md
- タグライン / 軸リストを 5 軸版に更新

### CLAUDE.md
- 「**新規 UI 機能は main vs sub どちらに置くか質問**」を AI 制約に追加

### memory/project_architecture.md / project_roadmap_2026_05_20.md
- 軸の数を 5 に変更
- P2 で multi-window engine work が必要、と明記

## 関連

- 提案セッション: 2026-05-20
- 関連 ADR: 0001 (C++ gameplay pivot — 同方向のリファクタ起源)
- 関連 phase: P2 (Time-travel inspector で本格実装)
