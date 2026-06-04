# MitiruEngine

C++ のゲームエンジンです。UI（メニューや HUD）は HTML/CSS で作れます。

- **UI を HTML/CSS で。** ゲームのロジック（シーン進行・状態・ノベル分岐・物理・AI）は C++ で書きます。メニューや HUD は HTML/CSS で組めるので、Web のデザイン資産がそのまま生きます。
- **ヘッダだけで動く。** プリビルドの .lib を配って回る必要はありません。CMake の FetchContent で取り込んで、`Mitiru::mitiru` を link するだけ。
- **C++20、Windows がメイン。** Linux と Mac でも一応動きますが、踏み固められているのは Windows + MSVC 2022。

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

詳しい手順は [はじめに](https://mogmog-0110.github.io/MitiruEngine/getting-started/) を、動くサンプルは [サンプル](https://mogmog-0110.github.io/MitiruEngine/examples/) を見てください。

## 自分のゲームから使う

ゲームは **DLL 形式** で書きます（`mitiru_host` が読み込んで動かす）。`mitiru` CLI が一級市民です。

```bash
mitiru new my_game     # ゲーム DLL の雛形を作る
cd my_game
mitiru run             # ビルドして mitiru_host で起動
mitiru watch           # src/ を編集すると state を保ったままホットリロード
```

雛形の `src/main.cpp` は、状態を `GameMemory` 構造体にまとめ、`mitiru_module_load` で host に
配線する DLL 形式です。host が state ポインタを所有するので、

- **ホットリロード**: コードだけ差し替えても状態が生き残る
- **タイムトラベル / 自動リプレイ**: GameMemory を memcpy するだけで巻き戻し・bit-exact 再生

が成立します（ADR 0005）。HUD は HTML/CSS で手書き JS なし（`StateWriter` で名前付きの値を push し、
`data-m-*` の宣言バインダが描画）。最短の手順は
[はじめに](https://mogmog-0110.github.io/MitiruEngine/getting-started/)、
動くサンプルは [サンプル](https://mogmog-0110.github.io/MitiruEngine/examples/)
（`breakout` / `anchor` / `hello_game`）を参照。

> **旧・静的リンク経路は非推奨（deprecated）**。以前は `mitiru::Game` を継承して `engine.run()` で
> エンジンを自分の exe に静的リンクする書き方も提供していましたが、タイムトラベル・リプレイ・
> ホットリロードといった差別化機能が DLL 形式を前提とするため、**DLL 形式に一本化**します。
> 既存の静的リンクプロジェクトは当面動きますが、新規は `mitiru new` の DLL 形式で始めてください。

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
