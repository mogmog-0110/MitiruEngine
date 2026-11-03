# テクスチャ圧縮 (BC7 / BC5 / BC4 / BC6H)

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
- 書き出しのコンテナは DDS (DX10 拡張ヘッダ)。DX12 は DXGI_FORMAT をそのまま受けるので、
  書き出しに変換層は要らない。読み書きは `render/DdsFile.hpp`。
- 外から渡された KTX2 (Basis Universal の UASTC / ETC1S) は、読むときに BC へ transcode する
  (下の「KTX2」)。圧縮済みの `.ktx2` は import で圧縮し直さず、名前をそのまま `.clod` に残す。
- GPU への転送は `render/dx12/Dx12TextureUpload.hpp` の `uploadMipImage` 1 本で、
  clod も forward のアルベドも D3D12MA で確保する。
- forward 経路 (drawModelBlend / drawModelRot の glb) のテクスチャは RGBA8 のまま。

## KTX2

`render/Ktx2Texture.hpp` の `decodeKtx2(bytes, kind, error)` が、KTX2 (Basis Universal の UASTC か ETC1S、2D、1 層 1 面)
を DDS と同じ `MipImage` にする。`drawModel` が引く glTF の `KHR_texture_basisu` の画像 (`image/ktx2`) と、
モデルの隣に置いた `.ktx2` が通る。

| 役割 | 出力 | 備考 |
|---|---|---|
| 色 | BC7 (sRGB は KTX2 の転送関数で決める) | 辺が 4 の倍数でなければ RGBA8 に戻す |
| データ | BC7 (線形) | 同上 |
| 法線 | BC5 | R と A を X, Y に置いた KTX2 (basisu の `-separate_rg_to_color_alpha`) |
| 1 チャンネル | BC4 | |

読まないもの: cubemap、配列、3D、UASTC HDR、ASTC。理由は `decodeKtx2` の error に出る。
実装は `basisu_impl` (src/ktx2_texture_impl.cpp、external/basis_universal の transcoder だけ)。
KTX2 を作る側 (エンコーダ) は入れていない。作るには basisu の道具を使う (`tests/fixtures/ktx2/make_fixtures.py`)。

## HDR のテクスチャ (BC6H)

`mitiru_texc` は Radiance の `.hdr` を BC6H (符号なし、半精度の範囲) の DDS にする。

```
mitiru_texc sky.hdr --kind hdr               # → sky.hdr.dds (BC6H UF16、mip 連鎖つき)
```

出力に BC6H の相対誤差 (線形 RGB の |復号 - 元| / 元、分母に 1/1024 の下限) の平均と最大を表示する。
`examples/hdr_sky` の `day_field_2k.hdr` (2048x1024) で測った値は ADR 0114 にある。

- 符号化器は自前で、1 ブロックを mode 11 (10 bit の端点 2 つ、4 bit の添字) だけで符号化する。端点は最小・最大の箱から始め、
  添字を決めては最小 2 乗で端点を引き直すのを 3 回くり返す。bc7enc_rdo に BC6H の符号化器は無い。
  mode 11 だけなので圧縮率は BC6H の中で最良ではなく、色の違いが大きいブロックは他の mode より誤差が出る。GPU は全 mode を復号するので、
  他の道具が作った BC6H の DDS もそのまま読める。CPU の復号 (`decompressLevelHdr`) は mode 11 だけで、確認と試験に使う
- 負の値と NaN は 0、65504 を超える値は 65504 に止める。mip は線形のまま平均する
- 幅と高さは 4 の倍数のみ。ほかは断る
- DDS の読み書きは BC6H (UF16 / SF16) と RGBA16F、キューブ (6 面)、配列、レガシーの FourCC (DXT1〜5、ATI1/2、BC4U/BC5U、A16B16G16R16F) を通す。
  体積テクスチャと未知の形式は理由付きで断る

## Dawn (WebGPU) の経路

Dawn の経路も、モデルの材質の画像 (基本色、法線、自発光) を BC のまま GPU へ上げる。DX12 と同じ `<モデル>.m<材質>.<役割>.dds` を読み、無ければ作る
(DX12 と Dawn のどちらが先に作っても、もう一方がそのまま使える)。

- GPU が `texture-compression-bc` を持つとき (D3D12 / D3D11 の GPU の大半) だけ BC で上げる。持たない GPU や、`MITIRU_DAWN_NO_BC=1` のときは、従来どおり RGBA8 の mip 連鎖を作って上げる (展開した BC ではなく元の画素から作るので、画質は落ちない)
- 辺が 4 の倍数でない画像、4096 を超える画像は BC にしない (従来の経路が縮めて上げる)
- 法線は BC5 の XY で、Z は Dawn のシェーダが復元する (RGBA8 の法線でも同じ式)
- BC6H は Dawn では使わない (HDR の環境は DX12 のみ。ADR 0110)
