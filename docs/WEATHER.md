# 天候 (雨・雪・濡れ・雲)

例は [`examples/weather3d`](../examples/weather3d/weather3d_dll.cpp)。設計は ADR 0108。

## 雨と雪

```cpp
static const render::WeatherPreset kRain = render::defaultRainPreset();   // defaultSnowPreset() もある
const float cam[3] = {eye.x, eye.y, eye.z};
render::WeatherWind wind;   // dirX / dirZ は水平の向き、strength 1 で落ちる向きが 45 度倒れる
wind.dirX = -1.0f; wind.strength = 0.15f;
const render::ParticleEmitterDesc drops = render::weatherEmitter(kRain, cam, intensity, wind, gameTime);
screen.particles3D(&drops, 1);
```

- 毎フレーム呼ぶ。エミッターはカメラの真上に置き直されるが、出た粒は世界に残る
- 風は `OutdoorWorld` の `windDir` / `windStrength` とは別の値。草と雨を揃えるときは、ゲームが同じ向きを `WeatherWind::dirX / dirZ` と `OutdoorDrawPod::windDir` に渡す
- `intensity` は 0..1。急に変えると粒の出方がずれるので、なだらかに動かす
- `gameTime` はゲームの状態の時刻 (壁時計にしない)
- 深度の面に当たった粒は消えるので、屋根の下には降らない
- 見た目は JSON で替えられる。ゲームが読んだ文字列を `render::parseWeatherPresets` に渡す (慣例の置き場は `assets/weather.json`)

```json
{ "presets": [ { "name": "rain", "rate": 6000, "speed": 18, "color": [0.7, 0.8, 0.95, 0.4] },
               { "name": "ash",  "rate": 300,  "size": 0.1, "speed": 0.8 } ] }
```

キーは `rate` `height` `radius` `speed` `size` `stretch` `spread` `nearFadeStart` `nearFadeEnd` `lit` `color`。同じ `name` は既定を上書きし、別の `name` は足される。

## 濡れ

`SceneLook::wetness` (0..255) を毎フレーム `sceneLook3D` で頼む。0 は従来の絵。濡れると拡散色が暗くなり (最大で 0.7 倍)、粗さが下がり (0.3 倍)、鏡面の光沢が強くなる。トゥーンのつやも強まるが、場面のハイライト (`SceneLook::toonSpecular`、既定 0) を上げていないと出ず、既定のトゥーンでは暗くなるだけになる。場面の全部が一様に濡れる。

| backend | 変わる物 |
|---|---|
| DX12 | 拡散色、粗さ (PBR)、鏡面色と光沢 (Phong)、トゥーンのつや (toonSpecular が要る) |
| Dawn | 拡散色、鏡面色と光沢、トゥーンのつや (toonSpecular が要る。粗さは持たない) |

雨が降り始めてから濡れが増えるまで、止んでから乾くまでの時間はゲームが決める (例では濡れの増え方 0.3/秒、乾き方 0.04/秒)。

## 雲

`SkyLook::enabled = 1` (物理ベースの空) のうえで `SceneLook::cloudCover` (0..255) を頼むと、空に雲の層が出る。0 は雲なし。`SkyLook::toonBands` が 2 以上なら雲の量も段に切れる。太陽の側の縁が明るく、日没には空と同じ赤みを受ける。雲の動きは決定論のフレーム番号で決まる。cubemap の空とグラデーションの空には出ない。体積雲は作らない。

雲の式は `render/CloudMath.hpp` に CPU 版がある (試験が撮った絵と突き合わせる)。シェーダーの式を直したら両方を直す。
