# HDR 出力

モニターが HDR のとき、画面への Present だけを scRGB (FP16) にします。キャプチャ、リプレイの動画、VRT は今までどおり SDR です。ゲームには HDR かどうかを渡しません (リプレイが別の機械で食い違わないようにするためです)。

## 使い方

| 方法 | 内容 |
|---|---|
| 設定ファイル | `settings.json` の `graphics.hdr` を `"auto"` にする。既定は `"off"` |
| 起動引数 | `mitiru_host --hdr` (設定より優先) |

`auto` でも、ウィンドウがあるモニターが HDR (Windows の Advanced Color で、色空間が G2084/P2020) のときだけ有効になります。SDR のモニターでは何も変わりません。約 120 フレームごとにモニターを見直し、HDR を入り切りしたときや窓を別のモニターへ動かしたときに切り替えます。

## 仕組み

1. 3D と UI は今までどおり RGBA8 のレンダーターゲット (`Dx12SwapChain` が HDR のときだけ持つ proxy) に描きます。色覚のフィルタ、2D の MSAA、キャプチャも proxy を見ます。
2. tonemap の直前に、「SDR の絵に載らなかった highlight excess」を RGBA16F に別に書きます。肩を表示のピーク (`DXGI_OUTPUT_DESC1::MaxLuminance`) まで開いた色から SDR の色を引いた線形の値で、肩に届かない色では 0 です。
3. Present の直前に 1 パスで、(SDR の絵を線形へ戻した色 + highlight excess) に OS の SDR の基準の白 (`DISPLAYCONFIG_SDR_WHITE_LEVEL`、取れなければ 203 nits) / 80 を掛けて、FP16 の実バッファへ書きます。scRGB は (1, 1, 1) が 80 nits です。

式は `include/mitiru/render/HdrMath.hpp` にあります。

## 限りのあること

- highlight excess は 3D の絵だけに付きます。UI を重ねた画素にも excess が足されるので、強い光の上の UI は少し明るくなります。
- 色の仕上げの LUT は SDR の絵 (0 から 1) だけに掛かり、highlight excess は素通しです。
- 副ビューには highlight excess がありません。
- Dawn は HDR を出せません。固定したバージョンの Dawn は色管理 (`SurfaceColorManagement`) を受け付けないためで、機能表の `hdr_output` は Dawn を「なし」としています。Dawn で頼まれたときは SDR のまま出し、機能表の note に数えます。
- HDR10 (R10G10B10A2 と G2084) は未対応です。スワップチェーンの形式は R16G16B16A16_FLOAT の scRGB だけです (Microsoft が汎用に勧める形)。

## 実機で見る手順

この開発機のモニターは SDR なので、HDR の見た目は測れていません。数値は試験が測ります (`ctest -R "hdr"`、[render][gpu][hdr])。実機の HDR モニターでは次を見てください。

1. Windows の「設定 > システム > ディスプレイ > HDR」を入れ、SDR コンテンツの明るさを中ほどにする。
2. `mitiru_host --hdr <game.dll>` で起動する。
3. 太陽や光源など白より明るい面が、SDR の白より明るく出ること。UI の白が SDR のときと同じ明るさに見えること。
4. SDR コンテンツの明るさのスライダーを動かすと、UI と 3D の白が追従すること (約 120 フレーム以内)。
5. HDR を切る (Win+Alt+B) と、画面が黒くならず SDR の絵に戻ること。
