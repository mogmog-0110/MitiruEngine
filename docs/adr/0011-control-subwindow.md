# ADR 0011 — Runtime コントロール sub-window (`mitiru_console`)

- **Status:** Accepted (2026-05-29)
- **Context:** `mitiru run` で起動中のゲームを操作する手段が現状 **runtime hotkey (F7/F8/F9/F12) しか無い**。F-key は invisible で discoverability ゼロ、key の数も限られる。ユーザー要望: 「**独立ウィンドウで GUI 上で操作したい / 入口が分からない**」。
- **Supersedes / extends:** [ADR 0004](0004-modular-sub-window-architecture.md) (sub-window 一般原則)、[ADR 0005](0005-host-game-c-abi-signal-flow.md) (host-game 境界)。 [SCOPE.md](../SCOPE.md) の「CLI 一級・GUI editor 不提供」を逸脱しない範囲で、**runtime コントロール専用の独立ウィンドウ**を追加する。

## 決定

新カテゴリの sub-window を導入する: **コントロールパネル**。

| 種別 | 例 | 既存ルール |
|---|---|---|
| inspector | mitiru_inspector | 読み取り専用 |
| **コントロールパネル** (本 ADR) | `mitiru_console` | **操作可、main game window を gameplay 純粋に保つための受け皿** |
| palette | (未実装) | (将来) |

**実装方針:**
1. **transport**: 既存の `EngineHttpServer` (localhost-only) を流用。新 endpoint を `/api/runtime/*` 名前空間に置く: `pause` / `step` / `timescale` / `status` / `screenshot`。
2. **UI**: HTML/CSS/JS (axis ①整合)。`assets/console.html` 単一ファイル + fetch() で endpoint を叩く。
3. **窓**: `mitiru_console.exe` = engine + CEF を minimal Game で起動するラッパー exe。`start_url = file:///./assets/console.html`。CEF 単一プロセス制約 (ADR 0004) は別 exe なら問題なし (各プロセスが独自 CEF init)。
4. **lifecycle**: `mitiru run --console` で host が `mitiru_console.exe --port <N>` を子プロセスとして spawn、host 終了で子を kill。

## 設計上の決定と失敗モード分析

1. **main game window は gameplay 純粋を維持**。pause/step UI を main 窓に出さず、必ず別窓へ。memory `[[game-window-pure]]` 整合。
2. **F-key 経路は残す**: GUI で叩けるのと並行して、F7/F8/F9/F12 も引き続き有効。CLI/key 派と GUI 派、両ワークフローを排他にしない。
3. **`/api/runtime/*` は localhost-only**: EngineHttpServer は 127.0.0.1 bind 固定 (lines 160)。LAN/外部からの操作を許さない (debug ツールであり security 面は閉鎖前提)。
4. **port 衝突**: `--http-port 0` で OS が空きポートを割り当て、host が stderr に実 port を出す。子 console exe はそれを受け取る (`--port <N>` argv)。
5. **CEF multi-process コスト**: console exe は独自 CEF を init するため +~50MB / +~1s startup。debug ツール許容。
6. **child 終了の取りこぼし**: host が SIGINT で異常終了したとき子 console が孤立する可能性 → host の `defer` で kill 保証 + console 側も「親 PID 監視」ループでフォールバック自殺 (ADR 0004 inspector と同 pattern)。
7. **ABI 不変**: EngineCallbacks に function ポインタ追加するのみ、`module/ModuleApi.hpp` (game DLL 境界) は触らない。

## 結果

- **良くなる:** runtime 操作の入口が GUI として可視化される。F-key を覚える摩擦消失。「もっとわかりやすく」要件達成。main window 純粋化が貫ける (debug UI 退避先ができる)。
- **コスト:** 新 exe 1 個 (mitiru_console)、HTML page 1 枚、CEF 子プロセス × 1 のメモリ/起動コスト。endpoints は ~5 個追加。
- **5 軸との関係:** 直接的には軸⑤ (modular sub-window) を強化。軸① (HTML/CSS UI) も内部活用で強化。
- **段階導入:** phase 1 = endpoints (本コミット) → phase 2 = HTML page + exe → phase 3 = `--console` flag で auto-launch → phase 4 = 残り操作 (snapshot save/load 等)。
