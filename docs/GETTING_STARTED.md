# はじめに: MitiruEngineセットアップ

このページの目的は 「動くものを5分で作る」 こと。
ゲーム設計の話は後回しで、まずは`mitiru` CLIを入れて、プロジェクトを作って、画面が開くところまで一緒に行きます。

## 必要なもの

対応OSはWindows 10 / 11 (x64)です(Linux / Macは今後対応予定)。必要なビルドツールは下の`installer.exe`がまとめて入れるので、自分で用意する必要はありません。

| ツール | バージョン目安 | ひとこと |
|------|------------|--------|
| MSVC Build Tools | 2022 | C++20が通るコンパイラ。`mitiru build`が裏で呼びます。 |
| CMake | 3.21以降 | 同上。CMakeを直接触ることはありません。 |
| Windows SDK | — | シェーダコンパイラなどに必要。 |

**IDEはoptional** です。MitiruEngineはCLI中心の設計なので、テキストエディタ(VS Code / Vim / Helix / メモ帳 等)と`mitiru`コマンドだけで完結します。Visual Studio IDEを使うのも自由ですが、必須ではありません。

---

## mitiru CLIを入れる

1. [最新リリース](https://github.com/mogmog-0110/MitiruEngine/releases/latest)からzipを落として、好きな場所に展開します。
2. 中の`installer.exe`をダブルクリックすると、C++ビルドツール(MSVC Build Tools・CMake・Windows SDK)と`mitiru`コマンドが入り、`PATH`に通ります。導入済みの物は自動で飛ばすので、何度実行しても安全です。
3. `PATH`を反映させるため、**新しいターミナル**を開いてから`mitiru`を使います。

ビルドツールを自分で管理したい人は、MSVC Build Toolsを手動で入れて`mitiru`に`PATH`を通すだけでも構いません。

通っているか確認:

```bash
mitiru version
```

ここで`mitiru: command not found`と言われたら`PATH`の問題です。`installer.exe`の後に開いたターミナルが古い(installerより前から開いていた)可能性があるので、ターミナルを開き直してください。

> **すぐ動かしたいだけなら** ── 展開したzipの中の`MitiruEngine_Launcher.bat`をダブルクリックすると、同梱サンプルを一覧から選んで起動できます(コンパイラ不要)。

---

## 最初のプロジェクトを作る

```bash
mitiru new my-game
cd my-game
mitiru run
```

`mitiru new`がフォルダを作って、`src/main.cpp` / `mitiru.toml` / `assets/scene.html`の最小セットを置きます。
`mitiru run`がビルドして実行します。初回だけ、HTML UI用のCEFラッパとheader-onlyのエンジン本体を一度コンパイルするため5〜10分ほどかかります。2回目以降は数秒です。

これだけで、`MitiruEngine`のウィンドウが手元で開きます。お疲れさまでした。

### よく使うコマンド

| コマンド | やること |
|------|------|
| `mitiru new <name>` | テンプレートから新しいプロジェクトを作る |
| `mitiru build` | ビルドだけ。実行はしない。 |
| `mitiru run` | ビルドして実行 |
| `mitiru clean` | `build/`を消してconfigureからやり直す |
| `mitiru doctor` | 環境チェック(CMake / コンパイラの版など) |
| `mitiru version` | CLIのバージョン |

---

## mitiru.tomlに何が書いてあるか

`mitiru new`が生成するマニフェスト:

```toml
[project]
name = "my-game"
version = "0.1.0"
engine = "0.1.0"

[window]
title = "my-game"
width = 1280
height = 720
vsync = true

[cef]
start_url = "assets/scene.html"
skip_default_font = true

[build]
backend = "auto"
```

- `[window]` — ウィンドウのタイトルとサイズ。C++側でハードコードしなくて済みます。
- `[cef]` — UI / HUDをHTML/CSSで書く場合の初期HTML。CEFレイヤーを使わない純C++ runtimeなら`start_url`は無視されます。
- `[build]` — グラフィクスbackend。`auto`でプラットフォームから自動選択。

---

## C++のみで動かす場合

UIをHTML/CSSで書かず、純C++で完結したい場合は`mitiru.toml`の`[cef]`セクションを消すか、`main.cpp`で`cfg.enableCef = false`を設定します。CEFのプロセスが起動しないので、libcef.dll依存も無くなります。

詳細は [`SCOPE.md`](SCOPE.md)。

---

## 最小のゲームコード

ゲームは「状態のstruct + update + draw」だけで書けます。これが現在の推奨形です:

```cpp
#include <mitiru.hpp>
using namespace mitiru;

struct MyGame
{
    // 状態はここに置くだけ。ポインタや std::vector を持たない丸ごとコピー
    // できる struct (= flat POD) なら、ホットリロード・巻き戻し・
    // リプレイが何もしなくても全部乗ります。
    float x = 640.0f;
    float y = 360.0f;

    void update(Input in, float dt)
    {
        constexpr float kSpeed = 320.0f;  // ← mitiru watch 中に書き換えて保存してみてください
        if (in.down(Key::Right)) x += kSpeed * dt;
        if (in.down(Key::Left))  x -= kSpeed * dt;
        if (in.down(Key::Down))  y += kSpeed * dt;
        if (in.down(Key::Up))    y -= kSpeed * dt;
    }

    void draw(Screen& s)
    {
        s.fillScreen(hex(0x14182A));
        s.fillCircle(x, y, 24.0f, color::Cyan);
    }
};

MITIRU_GAME(MyGame)  // これ 1 行で DLL の入口が生成されます
```

ゲームはDLLとしてビルドされ、ホスト(`mitiru run`が起動する実行体)が
毎フレーム入力を渡し、状態を保持します。コードを保存するとDLLだけが
リロードされ、状態は生きたまま挙動が変わります(`mitiru watch`)。

APIの全体像は [機能リファレンス](https://mogmog-0110.github.io/MitiruEngine/features.html)に
コピペできる形で並んでいます。HTML/CSSでHUDを作る例は同梱の
`examples/rewind/`を参照してください。

---

## ディレクトリ構成(生成された直後)

```
my-game/
├── mitiru.toml         # プロジェクトマニフェスト
├── src/
│   └── main.cpp        # ゲーム本体
├── assets/
│   └── scene.html      # HTML/CSS UI 用の初期 HTML
└── build/              # mitiru build が生成 (gitignore 推奨)
```

`mitiru build`は裏でCMake `FetchContent`経由でエンジン本体を引いてきて、ビルドツリーを作ります。`include/mitiru/`以下のヘッダはエンジンリポジトリ側にあり、消費プロジェクトには複製されません。

---

## 次に何を見るか

- [`examples/rewind/`](../examples/rewind/) — 状態を1個のstructに置いた巻き戻し + HTML/CSS HUDの動くshowcase
- [Reading Order — 次に読むべきページ](READING_ORDER.md)
- [Scope & Identity — engineのidentity / 特徴 / target user](SCOPE.md)
- [Architecture — エンジン全体の設計](ARCHITECTURE.md)
- [Troubleshooting](TROUBLESHOOTING.md)

---

## CMakeから直接消費したい(上級)

`mitiru` CLIを使わずに、既存のCMakeプロジェクトに足したい場合:

```cmake
include(FetchContent)
FetchContent_Declare(Mitiru
  GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngine.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(Mitiru)

add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE Mitiru::mitiru)
```

`find_package(Mitiru CONFIG REQUIRED)`もinstall後なら有効。

エンジン本体をクローンして直接ビルドするには:

```bash
git clone https://github.com/mogmog-0110/MitiruEngine.git
cd MitiruEngine
cmake --preset default
cmake --build build --config Debug
```

公開リポジトリは依存(Box2D / Jolt / tracy / zstd等)を`external/`に同梱済みなので、submoduleの取得は不要です。

---

## ハマったとき

| 症状 | たぶんこれ |
|------|----------|
| `mitiru: command not found` | `installer.exe`の後にターミナルを開き直していない。`PATH`の反映には新しいターミナルが要ります。 |
| `mitiru doctor`でCMakeが見つからない | CMake 3.21以上を入れる。古いとpresetが読めません。`installer.exe`を再実行すると入ります。 |
| `mitiru build`でC++20系のエラー | コンパイラが古い。MSVC Build Tools 2022に。 |
| 初回buildが異様に遅い | 初回だけCEFラッパとheader-onlyエンジンをコンパイルします。5〜10分は普通です。 |
| Windowsでリンカが`libcef.dll`を見つけられない | `mitiru build`をやり直すとCEFランタイムがexeの隣にコピーされます。 |
| `mitiru`が古いengineを引いてしまう | `mitiru.toml`の`[project] engine`を直すか、`mitiru clean`で`build/`を作り直す。 |

それでも詰まったら、[GitHub Issue](https://github.com/mogmog-0110/MitiruEngine/issues)に投げてください。再現手順とOS / `mitiru version`の出力が書いてあると助かります。
