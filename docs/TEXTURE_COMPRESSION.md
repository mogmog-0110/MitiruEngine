# テクスチャ圧縮 (BC7 / BC5 / BC4)

drawModel に渡した glTF / OBJ のテクスチャは、初回の import で GPU がそのまま読める
ブロック圧縮 (BC) の DDS に変わる。2 回目以降の起動は PNG / JPEG を decode せず、
mip も作らず、DDS を読んで GPU へ置くだけになる。

| 役割 | 形式 | 1 画素 | mip の縮め方 |
|---|---|---|---|
| 色 (baseColor / map_Kd) | BC7 (sRGB) | 1 byte | 線形空間で平均してから sRGB へ戻す |
| 法線マップ | BC5 (XY) | 1 byte | 向きを平均して長さ 1 に戻す。Z はシェーダが復元する |
| 1 チャンネル (mitiru_texc --kind mask) | BC4 | 0.5 byte | そのまま平均 |

RGBA8 (4 byte + mip) と比べて 1/4。Sponza (43 枚、最大 1024px) で、テクスチャの VRAM が
214 MiB から 55 MiB、テクスチャ込みの読み込みが約 540 ms から約 55 ms になった
(Release、RTX 5070 Ti)。代わりに初回 import が約 6 秒延びる (43 枚を並列で圧縮)。

## ファイル

- `<画像>.dds` が画像の隣にでき、`<モデル>.clod` のマテリアルはそれを指す。
  どちらも import の生成物なので `.gitignore` に入っている。
- 元画像が dds より新しくなると、次の import (または cache を再利用する起動) で作り直す。
- dds が読めない・消えたときは、元画像を RGBA8 で読んで代用する (警告は 1 回)。
- 幅か高さが 4 の倍数でない画像は圧縮せず、元画像のまま使う (D3D12 の BC の制約)。

## 手で圧縮する

```
mitiru_texc albedo.png                       # → albedo.png.dds (BC7 sRGB)
mitiru_texc normal.png --kind normal         # BC5
mitiru_texc mask.png --kind mask -o out.dds  # BC4
```

出力に mip 段数・サイズ・PSNR を表示する。

## 実装

- エンコーダは [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) (MIT / public domain)
  の bc7enc (BC7) と rgbcx (BC4 / BC5)。`texcompress_impl` (src/texture_compress_impl.cpp、cmake/texcompress) の 1 ライブラリに閉じ込め、
  Debug ビルドでもエンコーダだけは最適化する。
- DirectXTex ではなく bc7enc_rdo にしたのは、初回 import の待ち時間を CPU だけで短く
  保つため (DirectXTex の CPU BC7 は桁違いに遅く、速い経路は D3D11 の compute を要る)。
  入るのは 3 ファイルで、OS に依存しない。
- コンテナは DDS (DX10 拡張ヘッダ)。DX12 は DXGI_FORMAT をそのまま受けるので、
  KTX2 / Basis のような変換層が要らない。読み書きは `render/DdsFile.hpp`。
- GPU への転送は `render/dx12/Dx12TextureUpload.hpp` の `uploadMipImage` 1 本で、
  clod も forward のアルベドも D3D12MA で確保する。
- forward 経路 (drawModelBlend / drawModelRot の glb) のテクスチャは RGBA8 のまま。
