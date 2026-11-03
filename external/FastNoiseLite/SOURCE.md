# FastNoiseLite の出どころ

- リポジトリ: https://github.com/Auburn/FastNoiseLite (MIT)
- 固定したバージョン: タグ `v1.1.1`、コミット `7ccfbc16eb1c932568f177d63a9ba51d89bbe516` (2026-10-29 に取得)
- 取った物 (同じコミットの raw URL から、本文は変えていない):

| ファイル | 元のパス | byte | SHA-256 |
|---|---|---|---|
| `LICENSE` | `LICENSE` | 1120 | `c08da3239b919c12f4ec616b457a8dd0fc923c8102c8a742c4006fcb5de28fb0` |
| `FastNoiseLite.h` | `Cpp/FastNoiseLite.h` | 107699 | `47a29750dfe13d15651febd235f3c26b1cc57af7c6e26583b838fd7ab5215dc2` |

HLSL / GLSL 実装は取っていない (材質から include できるかは未確認のまま)。

## 使い方

ゲームからは `include/mitiru/math/Noise.hpp` (`mitiru::noise`) 経由で使う。この header を直接 include しない。
