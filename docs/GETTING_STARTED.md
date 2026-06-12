# はじめに — MitiruEngine セットアップ

このページの目的は **「動くものを 5 分で作る」** こと。
ゲーム設計の話は後回しで、まずは `mitiru` CLI を入れて、プロジェクトを作って、画面が開くところまで一緒に行きます。

## 必要なもの

| ツール | バージョン目安 | ひとこと |
|------|------------|--------|
| Go | 1.22 以降 | `mitiru` CLI を `go install` で入れるため。 |
| C++ コンパイラ | MSVC 2022 / GCC 13+ / Clang 18+ | C++20 が通るやつ。`mitiru build` が裏で呼びます。 |
| CMake | 3.21 以降 | 同上。CMake を直接触ることはありません。 |
| Git | わりと新しめ | submodule もあるので、古すぎなければ OK。 |

### Windows のひと

**Visual Studio Build Tools 2022** (または full Visual Studio 2022) を入れて、インストーラで **「C++ によるデスクトップ開発」** ワークロードを選んでおけば、MSVC コンパイラと CMake と Git がまとめて入ります。Go は [go.dev/dl](https://go.dev/dl/) からどうぞ。

**IDE は optional** です。MitiruEngine は CLI 中心の設計なので、テキストエディタ (VS Code / Vim / Helix / メモ帳 等) と `mitiru` コマンドだけで完結します。Visual Studio IDE を使うのも自由ですが、必須ではありません。

### Linux のひと

```bash
sudo apt install golang-1.22 cmake g++-13 git
```

`g++-13` 以上が入っていれば C++20 のビルドは通ります。

### Mac のひと

```bash
brew install go cmake
```

Xcode に付いてくる Clang は新しいので、それで足ります。

---

## mitiru CLI を入れる

```bash
go install github.com/mogmog-0110/mitiru-cli/cmd/mitiru@latest
```

`$GOPATH/bin` (デフォルトは `$HOME/go/bin`) に `mitiru` バイナリが入ります。`PATH` が通っていれば、すぐに使えます。

通っているか確認:

```bash
mitiru version
```

ここで `mitiru: command not found` と言われたら `PATH` の問題です。`go env GOPATH` で出てきたパスの `bin/` を `PATH` に追加してください。

---

## 最初のプロジェクトを作る

```bash
mitiru new my-game
cd my-game
mitiru run
```

`mitiru new` がフォルダを作って、`src/main.cpp` / `mitiru.toml` / `assets/scene.html` の最小セットを置きます。
`mitiru run` がビルドして実行します。初回は CMake configure + engine 取得で 1〜2 分くらい時間がかかります。

これだけで、`MitiruEngine` のウィンドウが手元で開きます。お疲れさまでした。

### よく使うコマンド

| コマンド | やること |
|------|------|
| `mitiru new <name>` | テンプレートから新しいプロジェクトを作る |
| `mitiru build` | ビルドだけ。実行はしない。 |
| `mitiru run` | ビルドして実行 |
| `mitiru clean` | `build/` を消して configure からやり直す |
| `mitiru doctor` | 環境チェック (Go / CMake / コンパイラの版) |
| `mitiru version` | CLI のバージョン |

---

## mitiru.toml に何が書いてあるか

`mitiru new` が生成するマニフェスト:

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

- `[window]` — ウィンドウのタイトルとサイズ。C++ 側でハードコードしなくて済みます。
- `[cef]` — UI / HUD を HTML/CSS で書く場合の初期 HTML。CEF レイヤーを使わない純 C++ runtime なら `start_url` は無視されます。
- `[build]` — グラフィクス backend。`auto` でプラットフォームから自動選択。

---

## C++ のみで動かす場合

UI を HTML/CSS で書かず、純 C++ で完結したい場合は `mitiru.toml` の `[cef]` セクションを消すか、`main.cpp` で `cfg.enableCef = false` を設定します。CEF のプロセスが起動しないので、libcef.dll 依存も無くなります。

詳細は [`SCOPE.md`](SCOPE.md)。

---

## 最小のゲームコード

ゲームは「状態の struct + update + draw」だけで書けます。これが現在の推奨形です:

```cpp
#include <mitiru/module/Game.hpp>
using namespace mitiru;

struct MyGame
{
    // 状態はここに置くだけ。flat POD ならホットリロード・巻き戻し・
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

ゲームは DLL としてビルドされ、ホスト (`mitiru run` が起動する実行体) が
毎フレーム入力を渡し、状態を保持します。コードを保存すると DLL だけが
リロードされ、**状態は生きたまま挙動が変わります** (`mitiru watch`)。

API の全体像は [機能リファレンス](https://mogmog-0110.github.io/MitiruEngine/features.html) に
コピペできる形で並んでいます。HTML/CSS で HUD を作る例は同梱の
`examples/timetravel_demo/` を参照してください。

---

## ディレクトリ構成 (生成された直後)

```
my-game/
├── mitiru.toml         # プロジェクトマニフェスト
├── src/
│   └── main.cpp        # ゲーム本体
├── assets/
│   └── scene.html      # Mode B 用の初期 HTML
└── build/              # mitiru build が生成 (gitignore 推奨)
```

`mitiru build` は裏で CMake `FetchContent` 経由でエンジン本体を引いてきて、ビルドツリーを作ります。`include/mitiru/` 以下のヘッダはエンジンリポジトリ側にあり、消費プロジェクトには複製されません。

---

## 次に何を見るか

- [`examples/timetravel_demo/`](../examples/timetravel_demo/) — flat POD 状態 + 巻き戻し + HTML/CSS HUD の動く showcase
- [Reading Order — 次に読むべきページ](READING_ORDER.md)
- [Scope & Identity — engine の identity / 4 軸 / target user](SCOPE.md)
- [Architecture — エンジン全体の設計](ARCHITECTURE.md)
- [Troubleshooting](TROUBLESHOOTING.md)

---

## CMake から直接消費したい (上級)

`mitiru` CLI を使わずに、既存の CMake プロジェクトに足したい場合:

```cmake
include(FetchContent)
FetchContent_Declare(Mitiru
  GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngineDev.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(Mitiru)

add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE Mitiru::mitiru)
```

`find_package(Mitiru CONFIG REQUIRED)` も install 後なら有効。

エンジン本体をクローンしてテストまで走らせるには:

```bash
git clone --recursive https://github.com/mogmog-0110/MitiruEngineDev.git
cd MitiruEngineDev
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

`--recursive` は `external/` 下の submodule (Box2D, Jolt, ozz-animation, tracy, zstd, MitiruMML, ShiggyGameCore) の取得に必要です。

---

## ハマったとき

| 症状 | たぶんこれ |
|------|----------|
| `mitiru: command not found` | `$HOME/go/bin` が `PATH` にない。`go env GOPATH` で確認。 |
| `mitiru doctor` で CMake が見つからない | CMake 3.21 以上を入れる。古いと preset が読めません。 |
| `mitiru build` で C++20 系のエラー | コンパイラが古い。MSVC 2022 / GCC 13+ / Clang 18+ に。 |
| 初回 build が異様に遅い | `FetchContent` が submodule を取りに行っています。1〜2 分は普通。 |
| Windows でリンカが `libcef.dll` を見つけられない | `mitiru build` をやり直すと CEF ランタイムが exe の隣にコピーされます。 |
| `mitiru` が古い engine を引いてしまう | `mitiru.toml` の `[project] engine` を直すか、`mitiru clean` で `build/` を作り直す。 |

それでも詰まったら、[GitHub Issue](https://github.com/mogmog-0110/MitiruEngineDev/issues) に投げてください。再現手順と OS / Go バージョンが書いてあると助かります。
