# ADR 0006 — `mitiru` CLI を Go で書く

- **Status:** Accepted (2026-05-21、既存実装の遡及記録)
- **Context:** Engine 本体は C++ (header-only)。 user 向けの CLI (`mitiru new / build / run / watch / debug / replay / inspect / doctor / install`) を engine 本体とは独立した別 repo (`mogmog-0110/mitiru-cli`) として実装する判断は確定済。**どの言語で実装するか** が本 ADR の決定対象。
- **Supersedes / extends:** [SCOPE.md](../SCOPE.md) の「CLI が一級市民、IDE optional」 + 「機能別独立」 を満たす実装手段の選定。

## 決定

**Go を採用する。**

`mitiru-cli` repo は Go 1.26+ で、CLI framework に [Cobra](https://github.com/spf13/cobra) を使用する。Build artifact は single static `.exe` で、外部ランタイム依存ゼロ。Windows / Linux / macOS への cross-build を CI で標準対応する。

## 選定基準

CLI 言語選定に当たり以下の要件を立てた:

| # | 要件 | 重み |
|---|---|---|
| R1 | **単一バイナリ配布** (user に「ランタイム入れて」 と言わせない) | ★★★ |
| R2 | **OS 操作の stdlib カバレッジ** (PATH / registry / exec / file watch / HTTP / tar) | ★★ |
| R3 | **業界標準の CLI framework** が存在する (subcommands / flags / shell completion) | ★★ |
| R4 | **cross-build** が trivial (Linux dev 環境から Windows .exe を作れる) | ★★ |
| R5 | **iteration cost** が低い (compile が秒) | ★ |
| R6 | engine 本体 (C++) と **言語が違っても齟齬が出にくい** こと | ★ |

## 候補比較

| 言語 | R1 配布 | R2 stdlib | R3 framework | R4 cross | R5 iter | 採用評価 |
|---|---|---|---|---|---|---|
| **Go** | ✅ static .exe | ✅ 充実 | ✅ Cobra (de-facto) | ✅ `GOOS=windows` | ✅ 秒 | ★ 採用 |
| Rust | ✅ static .exe | △ crate 多用 | △ clap (使えるが分散) | ◯ | ✗ 初回数分 | 不採用 (R3, R5) |
| C# / .NET | ✅ self-contained .exe (zip 肥大) | ✅ 充実 | ◯ Spectre.Console | ◯ | ◯ | 不採用 (R1 zip size / Windows lock-in) |
| Python | ✗ interpreter 要 / pyinstaller frozen は不安定 | ✅ | ✅ click | ◯ | ✅ | 不採用 (R1) |
| Node.js | ✗ Node 要 / pkg / nexe 不安定 | ◯ | ◯ commander | ◯ | ✅ | 不採用 (R1) |
| C++ | ✅ static .exe | ✗ 標準では薄い | ✗ 自前 | △ | ✗ | 不採用 (R2, R3 — engine 本体と別 repo にする意義が薄れる) |

## Go を選ぶ理由 (詳述)

### 1. 単一バイナリ配布 (R1)

`go build -ldflags="-s -w"` で **~10MB の static .exe** が出る。Windows ターゲットなら CRT も不要 (Go 自身が syscall を直接叩く)。

これがあるから、

- 配布 zip に `mitiru.exe` を入れるだけで「Go を install してください」 を言わずに済む
- `mitiru install` (=本 ADR の文脈で `installer.exe` の中身も Go) 自身を bootstrap として走らせられる

### 2. OS 操作の stdlib カバレッジ (R2)

`mitiru-cli` で実際に使ってる stdlib / 標準的なライブラリ:

| 用途 | パッケージ |
|---|---|
| HTTP (engine source DL) | `net/http` |
| tar.gz 解凍 (engine source 展開) | `archive/tar` + `compress/gzip` |
| 子プロセス起動 (cmake / vcvars64) | `os/exec` |
| file watcher (`mitiru watch`) | `github.com/fsnotify/fsnotify` |
| TOML 読み込み (`mitiru.toml`) | `github.com/BurntSushi/toml` |
| Windows レジストリ (`mitiru install`) | `golang.org/x/sys/windows/registry` |
| CLI framework | `github.com/spf13/cobra` |

**stdlib + 3 外部パッケージ**で済む。Rust だと `reqwest` / `tokio` / `clap` / `winreg` / `notify` / `flate2` / `tar` 等を寄せ集める必要がある。

### 3. Cobra (R3)

`kubectl`, `docker`, `gh`, `hugo`, `helm`, `terraform`, `aws-cli (v2 の Go port)` 全部 Cobra。

subcommand / flag binding / `--help` 出力 / shell completion (bash / zsh / fish / powershell) が宣言的に書ける:

```go
cmd := &cobra.Command{
    Use:   "build",
    Short: "Build the current project",
    RunE:  func(cmd *cobra.Command, args []string) error { ... },
}
```

「`mitiru` CLI が一級市民」 を支える framework が事実上業界標準として確立されてる。

### 4. Cross-build (R4)

```bash
GOOS=windows GOARCH=amd64 go build -o mitiru.exe ./cmd/mitiru
```

これ 1 行で Linux / macOS dev 機からも Windows binary が出る。CI (GitHub Actions) で 3 OS × 2 arch matrix が trivial。

将来 Linux / macOS 対応 (P3 以降) を視野に入れると、cross-build の安さは structural な選択肢を維持する。

### 5. Iteration cost (R5)

`go build ./...` が 1-2 秒。Rust だと初回 compile に 1-5 分、incremental も数十秒。CLI のような頻繁に小修正する layer では Go の compile 速度は実装速度に直結する。

### 6. C++ engine との分業 (R6)

Engine 本体 = C++ (header-only)、CLI = Go、UI layer = HTML/CSS/JS。**役割が違うレイヤーは言語を変える** を philosophy として明示する:

| Layer | 言語 | 理由 |
|---|---|---|
| Engine (gameplay / rendering / hot path) | C++ | 必須 — performance, ABI, DX11 直叩き |
| CLI (orchestration / OS I/O) | Go | static .exe + stdlib OS coverage |
| UI overlay / inspector (CEF) | HTML/CSS/JS | Web 開発者の skill 流用 |

混在 (例: Rust で全部書く) より、**適材適所で言語を分けた方が個々のレイヤーが薄く保てる**。

## 5 軸との関係

直接 5 軸を強化はしない。だが **5 軸を支える基盤**として:

- 軸 1 (HTML UI) — 関係なし
- 軸 2 (タイムトラベル inspector) — 関係なし (engine 内部実装)
- 軸 3 (全 system 単独起動) — CLI が subsystem 起動 `mitiru renderer / audio / inspect / ...` を統一窓口で提供。Cobra subcommand で自然
- 軸 4 (deterministic replay) — `mitiru replay <file>` が record / replay の唯一の窓口
- 軸 5 (modular sub-window) — `mitiru inspect <pid>` が sub-window を spawn

## 影響

- **採用**: `mogmog-0110/mitiru-cli` repo を Go module として運用。release.yml で windows-amd64 binary を `mitiru.exe` として GitHub Releases に attach (2026-05-21 追加予定)
- **影響を受ける docs**: `docs/GETTING_STARTED.md`、`docs/FIRST_TOUCH.md`、`README.md` で「`go install` または release zip 同梱 .exe で取得」 のいずれかを案内
- **将来再評価する条件**: (a) Go 自体の long-term support が変化したとき、(b) CLI で 1000+ ファイルの規模になり Go の generics 制約等が痛くなったとき、(c) WASM ターゲット (engine の Emscripten build と一緒に) で実行する需要が出たとき

## 関連 ADR

- [ADR 0001](0001-cef-as-ui-overlay.md) — CEF を UI overlay 化 (gameplay は C++)
- [ADR 0005](0005-host-game-c-abi-signal-flow.md) — Host-Game 境界の C-only signal flow
