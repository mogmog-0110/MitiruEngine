# MitiruEngine

C++ でゲームを書くための、ふだん使いのエンジン。

- **動きは C++、画面は HTML/CSS。** シーンの進行、状態、ノベルの分岐、物理、AI は全部 C++ で書きます。メニューや HUD は HTML/CSS。Web のデザイン資産がそのまま生きます。
- **ヘッダだけで動く。** プリビルドの .lib を配って回る必要はありません。CMake の FetchContent で取り込んで、`Mitiru::mitiru` を link するだけ。
- **C++20、Windows がメイン。** Linux と Mac でも一応動きますが、踏み固められているのは Windows + MSVC 2022。

> このリポジトリは [`MitiruEngineDev`](https://github.com/mogmog-0110/MitiruEngineDev) から
> 抽出された **公開向けスナップショット** です。日々の開発はあちらで行われていて、
> 一定間隔でここに反映されます。Issue やプルリクエストはこのリポジトリ宛でかまいません。

## ドキュメントとサンプル

- 公式ページ : <https://mogmog-0110.github.io/MitiruEngine/>
- はじめに : <https://mogmog-0110.github.io/MitiruEngine/getting-started/>
- チュートリアル : <https://mogmog-0110.github.io/MitiruEngine/tutorials/>
- できること (チャプター) : <https://mogmog-0110.github.io/MitiruEngine/chapters/>
- API リファレンス : <https://mogmog-0110.github.io/MitiruEngine/api/>
- アーキテクチャ : <https://mogmog-0110.github.io/MitiruEngine/architecture/>

ドキュメントは日本語で書かれています。

## さっと触ってみる

```bash
git clone --recursive https://github.com/mogmog-0110/MitiruEngine.git
cd MitiruEngine
cmake --preset default
cmake --build build --config Debug
```

詳しい手順とサンプルプロジェクトの組みかたは [はじめに](https://mogmog-0110.github.io/MitiruEngine/getting-started/) を見てください。サンプル集は Round 39 で刷新中で、次のリリースで新しいセットが並びます。

## 自分のゲームから使う

`CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(Mitiru
    GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngine.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(Mitiru)

add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE Mitiru::mitiru)
```

`src/main.cpp`:

```cpp
#include <mitiru/Mitiru.hpp>

class MyGame final : public mitiru::Game {
public:
  mitiru::Size layout(int w, int h) override { return {w, h}; }
  void update(float dt) override { /* ゲームのロジック */ }
  void draw(mitiru::Screen& screen) override {
    screen.drawRect({0, 0, (float)screen.width(), (float)screen.height()},
                    {0.1f, 0.1f, 0.2f, 1.0f});
  }
};

int main() {
  mitiru::Engine engine;
  MyGame game;
  mitiru::EngineConfig cfg;
  cfg.title = "MyGame";
  cfg.windowWidth = 1280;
  cfg.windowHeight = 720;
  engine.run(game, cfg);
}
```

これで動くウィンドウが手に入ります。

## 何が同梱されているか

- 2D / 3D の描画パイプライン (DX11 / DX12 / Vulkan / OpenGL / WebGL / Null から自動選択)
- CEF 統合 (`include/mitiru/cef/...`) — UI / HUD を HTML/CSS で書くため
- ノベル VM (`include/mitiru/vn/...`) — シナリオを JSON で書ける
- セーブ / ロード (`include/mitiru/data/SaveSchema.hpp`) — JSON ベース、スキーマ migration 付き
- 入力抽象 (Win32 / SDL2 / GLFW)
- オーディオ (miniaudio ベース)
- Tracy プロファイラとのフック
- Catch2 ベースの単体・統合テスト
- 動作検証用ツール (`include/mitiru/validate/...`)

## 状態

**0.x 系。実験段階。**

API はまだ変わります。`v0.1.0` のタグで取れる範囲では安定していますが、`main` ブランチを直接追うと API 変更を踏むことがあります。

`v0.x` のあいだは「壊さないように努力する」レベル。`v1.0` でロックします。

## ライセンス

MIT。`LICENSE` ファイルを見てください。
