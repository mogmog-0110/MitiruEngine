# イベント/カットシーン レシピ集

イベント枠を「フレームワーク」としてエンジンに入れることは、意図的に見送っている。
カットシーンの形 (タイムライン式か、状態機械式か、スクリプト式か) はゲームごとに違い、
エンジンが 1 つの型を押し付けるとゲームの型を縛る。代わりに、揃っている小さい部品を
enum + switch の素朴な状態機械で組み合わせる。使う部品は `Timer` / `Tween01`
(PodTiming.hpp)、`hud.letterbox` / `fadeOut` / `fadeIn` / `music` (視覚・音のエンジンへの依頼)、
`Camera` + `applyCamera`。この文書はその定番の組み方 3 つを示す。
手書きで状態爆発していた部分 (帯量の tween、ズームの tween、会話ボックスの表示制御)
が部品でどれだけ薄くなるかが要点。

## レシピ A: ボス登場カットイン

letterbox → カメラズーム → BGM クロスフェード → 確認キーで解除。
帯とクロスフェードの遷移は host が持つので、ゲーム側が tween するのはカメラだけ。

```cpp
enum class Phase : std::uint8_t { Play, BossIntro, BossFight };

struct GameMemory {
    Phase           phase = Phase::Play;
    mitiru::Tween01 introTween;            // 登場演出の進行度 (巻き戻し対象)
    mitiru::Camera  cam;                   // POD カメラ (同上)
    float playerX = 200, bossX = 1800;

    void update(mitiru::Input in, mitiru::Hud hud, float dt) {
        switch (phase) {
        case Phase::Play:
            if (playerX > bossX - 600) {                    // ゲーム固有の発火条件
                phase = Phase::BossIntro;
                introTween.reset();
                hud.letterbox(0.15f, 0.4f);                 // 黒帯 (遷移は host が持つ)
                hud.music("bgm_boss", true, 1.0f, 1.5f);    // 1.5 秒クロスフェード
            }
            break;
        case Phase::BossIntro: {
            const float k = introTween.step(1.2f, dt, mitiru::Ease::OutQuad);
            cam.x    = playerX + (bossX - playerX) * k;     // 注視点をボスへ
            cam.zoom = 1.0f + 0.5f * k;                     // 1.0 → 1.5 倍
            if (introTween.done(1.2f) && in.confirmPressed()) {
                phase = Phase::BossFight;
                hud.letterbox(0.0f);                        // 帯を戻す
                cam = {};                                   // カメラも等倍へ
            }
            break; }
        case Phase::BossFight:
            /* 通常戦闘 */
            break;
        }
    }

    void draw(mitiru::Screen& s) {
        s.applyCamera(cam.x, cam.y, cam.zoom);
        // ... world 描画 ...
        s.endCamera();
        // ... HUD など画面固定の描画 ...
    }
};
```

手書き比較: 帯量の補間変数 (barT) とズームの補間変数 (zoomT) を別々に進めて
draw 側でも同じ計算を繰り返す書き方は不要になる。帯はエンジンへの依頼 1 行、ズームは
`Tween01` 1 個 + `applyCamera` で update/draw の二重実装が消える。

## レシピ B: 会話シーン (JS ゼロ)

会話ボックスの見た目は HTML/CSS の領分。C++ は「表示するか」「何行目か」だけ持つ。
JS は 1 行も書かない。`data-m-show` / `data-m-text` を書いておくと、C++ が送った値を
HTML に自動で流し込む仕組み (binder) が DOM を更新する。

HTML 側:

```html
<div class="m-overlay" data-m-show="ev.talking">
  <p data-m-text="ev.line"></p>
  <small>Z で送り</small>
</div>
```

C++ 側:

```cpp
struct GameMemory {
    bool talking = false;   // 会話中か (巻き戻し対象)
    int  lineIdx = 0;       // 何行目か (同上)

    void update(mitiru::Input in, mitiru::Hud hud, float dt) {
        // セリフ本文はコンテンツであって状態ではない → DLL 焼き込み (配列は
        // GameMemory の外)。GameMemory にはインデックスだけ置く。
        static constexpr const char* kLines[] = {
            "……来たな。", "ここから先は通さない。", "行くぞ!",
        };
        constexpr int kCount = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));

        if (talking && in.confirmPressed()) {               // 送り
            ++lineIdx;
            if (lineIdx >= kCount) { talking = false; lineIdx = 0; }
        }
        hud.set("ev.talking", talking);
        hud.set("ev.line", kLines[lineIdx < kCount ? lineIdx : kCount - 1]);
    }
};
```

手書き比較: 会話ボックスの表示フラグを毎フレーム描画コード側で分岐する
(evShowBox のような) 変数は `hud.set("ev.talking", ...)` + `data-m-show` に畳まれる。
表示の ON/OFF アニメーションを付けたければ CSS transition で足す。C++ は触らない。

## レシピ C: 場面転換

fadeOut → 暗転中に状態切替 → fadeIn。フェードの遷移は host が持つので、
ゲーム側は「暗転しきるまで待つ」`Timer` 1 個だけ。

```cpp
struct GameMemory {
    bool          transition = false;
    mitiru::Timer transTimer;
    int           stage = 0;

    void update(mitiru::Input in, mitiru::Hud hud, float dt) {
        if (!transition && in.confirmPressed()) {           // ゲーム固有の転換条件
            transition = true;
            transTimer.reset();
            hud.fadeOut(0.4f);                              // 1. 覆う
        }
        if (transition && transTimer.once(0.4f, dt)) {      // 暗転しきった瞬間に 1 回
            ++stage;                                        // 2. 暗転中に状態切替
            hud.fadeIn(0.4f);                               // 3. 晴らす
            transition = false;
        }
    }
};
```

## 注意: どの値を `GameMemory` に置くか

| 値 | 置き場所 | 理由 |
|---|---|---|
| Phase enum / `Tween01` / `Timer` / `Camera` | **`GameMemory`** | ゲーム状態。巻き戻し・リプレイに乗せる |
| 帯量・fade 残量・shake 残量・クロスフェード進行 | **host (エンジンへの依頼経由)** | 演出の途中経過であってゲーム状態ではない。観測・巻き戻しの対象はゲーム状態のみに保つ |
| セリフ本文・BGM の id | **DLL 焼き込み (static constexpr)** | コンテンツであって状態ではない。`GameMemory` にはインデックスだけ |

`hud.letterbox(0.15f, 0.4f)` を呼んだ後、帯がどこまで開いたかをゲームは知らないし
知る必要もない。知りたくなったら、それは演出ではなくゲームルールに昇格した
合図なので、`Tween01` を `GameMemory` に置いて自分で進める (レシピ A のカメラと同じ形)。
逆に Phase や `Tween01` を host 側 (static 変数等) に逃がすと、巻き戻したのに
カットシーンだけ進み続ける、という再現性バグになる。境界はこの表で固定する。
