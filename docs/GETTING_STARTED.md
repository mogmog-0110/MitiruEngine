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

Visual Studio 2022 を入れて、インストーラで **「C++ によるデスクトップ開発」** ワークロードを選んでおけば、コンパイラと CMake と Git がまとめて入ります。Go は [go.dev/dl](https://go.dev/dl/) からどうぞ。

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
- `[cef]` — Mode B (CEF 併用) のとき、最初に読む HTML。Mode A 純 C++ なら `start_url` は無視されます。
- `[build]` — グラフィクス backend。`auto` でプラットフォームから自動選択。明示するなら `dx11` / `vulkan` / `webgl2` / `null` 等。

---

## 動かしかた 2 種 (Mode A / Mode B)

- **Mode A — Native (C++ のみ)**: デスクトップ / ヘッドレス。3D アクション、performance-critical な処理、CEF を使わない最小実行。`mitiru.toml` の `[cef]` を消すか `enabled = false` を入れます。
- **Mode B — Hybrid (Mode A + CEF + JS)**: デスクトップのみ。UI を HTML/CSS で組み、C++ から `state` を push する。HUD、メニュー、ノベル風の演出。`mitiru new` のデフォルトは Mode B 寄りのテンプレートです。

詳しい線引きは [`SCOPE.md`](SCOPE.md)。

---

## 最小のゲームコード (Mode A)

`mitiru new` が `src/main.cpp` にこういうのを置きます。置き換える出発点として使ってください:

```cpp
#include <cmath>
#include <mitiru/Mitiru.hpp>

constexpr int kKeyEscape = 27;
constexpr int kKeySpace  = 32;

class MyGame final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_elapsed += dt;
        if (hasInput() && input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear(sgc::Colorf{0.04f, 0.05f, 0.09f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        const bool  boost = hasInput() && input().isKeyDown(kKeySpace);
        const float pulse = 0.5f + 0.5f * std::sin(m_elapsed * 2.0f);
        const float size  = 80.0f + (boost ? 60.0f : 20.0f) * pulse;

        screen.drawRect(
            sgc::Rectf{w * 0.5f - size * 0.5f, h * 0.5f - size * 0.5f, size, size},
            sgc::Colorf{0.30f, 0.95f, 0.85f, 0.9f});

        screen.drawTextInRect(
            sgc::Rectf{0.0f, 24.0f, w, 32.0f},
            "Hello, Mitiru!",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            24.0f,
            mitiru::Screen::TextAlignH::Center, mitiru::Screen::TextAlignV::Top);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    float m_elapsed = 0.0f;
};

int main()
{
    mitiru::Engine engine;
    MyGame        game;

    mitiru::EngineConfig cfg;
    // Mode A: CEF を完全に切る (libcef.dll に依存しない)。
    cfg.enableCef = false;
    // Latin-only atlas → 起動 ~1 s (デフォルトの日本語 atlas は ~15 s)。
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
```

`cfg.title` / `cfg.windowWidth` / `cfg.windowHeight` は **書かなくて OK** です。`mitiru build` が `mitiru.toml` の `[window]` を C++ ヘッダに焼き込みます。

Mode B のときの `cefStartUrl` も同様に、`mitiru.toml` の `[cef] start_url` から自動で焼き込まれます。

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

- [チュートリアル 1: 最初のシーン](tutorials/01_first_vn.md) — `src/main.cpp` を少しずつ膨らませて、台詞表示まで。
- [チュートリアル 2: アクションのプロトタイプ](tutorials/02_arcade_game.md) — 入力を取って、Player を動かす。
- [チュートリアル 3: セーブ / ロード](tutorials/03_save_load.md) — JSON 永続化。
- [Reading Order — 次に読むべきページ](READING_ORDER.md)
- [Mode A / Mode B の使い分け](SCOPE.md)
- [Hybrid Runtime — Mode B の C++ / JS / JSON 分担](HYBRID_RUNTIME.md)
- [Architecture — エンジン全体の設計](ARCHITECTURE.md)
- [Troubleshooting](TROUBLESHOOTING.md)

---

## CMake から直接消費したい (上級)

`mitiru` CLI を使わずに、既存の CMake プロジェクトに足したい場合:

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

`find_package(Mitiru CONFIG REQUIRED)` も install 後なら有効。

エンジン本体をクローンしてテストまで走らせるには:

```bash
git clone --recursive https://github.com/mogmog-0110/MitiruEngine.git
cd MitiruEngine
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

それでも詰まったら、[GitHub Issue](https://github.com/mogmog-0110/MitiruEngine/issues) に投げてください。再現手順と OS / Go バージョンが書いてあると助かります。
