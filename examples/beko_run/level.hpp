// level.hpp。ステージの寸法と配置（すべて数値だけのデータ）。
#pragma once

// 画面と物理の定数
constexpr float SCR_W = 1280.0f, SCR_H = 720.0f;
constexpr float WORLD_W = 3600.0f;                 // 画面より広い世界
constexpr float GROUND_Y = 560.0f;                 // 地面の上端
constexpr float GRAV = 2000.0f, SPEED = 330.0f, JUMP = 780.0f;
constexpr float PW = 58.0f, PH = 92.0f;            // 赤べこの当たり箱
constexpr float GOAL_X = 3440.0f;                  // ゴール（幟）の位置
constexpr float COIN_DISP = 40.0f;                 // 古銭の表示サイズ

// 足場。先頭は地面（切れ目なし）、残りは飛び乗れる薄い踏み台。
// 前半は導入の低い台 → 中盤で 3 段の階段を登り、1 段で降りる → 後半は広い台と平地。
struct Plat { float x, y, w, h; };
constexpr Plat kPlats[] = {
	{0.0f, GROUND_Y, WORLD_W, 200.0f},                 // 地面
	{620.0f, 430.0f, 180.0f, 24.0f},                   // 導入の台
	{980.0f, 432.0f, 160.0f, 24.0f},                   // 階段①
	{1170.0f, 356.0f, 160.0f, 24.0f},                  // 階段②
	{1360.0f, 288.0f, 160.0f, 24.0f},                  // 階段③（頂上）
	{1600.0f, 384.0f, 170.0f, 24.0f},                  // 降りる段
	{2180.0f, 428.0f, 260.0f, 24.0f},                  // 広い台（上に敵）
	{2980.0f, 424.0f, 200.0f, 24.0f},                  // ゴール前の台
};
constexpr int NPLAT = static_cast<int>(sizeof(kPlats) / sizeof(kPlats[0]));

// 敵（起き上がり小法師）の初期位置。地上の各所に配置しつつ、1 体は広い台の上に置く。
struct FoeInit { float x, y; };
constexpr FoeInit kFoes[] = {
	{900.0f, GROUND_Y},    // 階段の手前
	{1780.0f, GROUND_Y},   // 降りた先
	{2300.0f, 428.0f},     // 広い台の上
	{2620.0f, GROUND_Y}, {2740.0f, GROUND_Y},   // 近い 2 体（少し難しい所）
	{3200.0f, GROUND_Y},   // ゴール前
};
constexpr int NFOE = static_cast<int>(sizeof(kFoes) / sizeof(kFoes[0]));
constexpr float FOE_SPEED = 46.0f;   // 小法師の左右の速さ
constexpr float FOE_RANGE = 64.0f;   // 初期位置から左右に往復する幅

// 古銭の位置（中心座標）。階段を登る場所ほど高く並べ、平地では拾いやすい高さに置く。
struct V2 { float x, y; };
constexpr V2 kCoins[] = {
	{300, 500}, {370, 458}, {450, 458}, {530, 500},   // 導入の山なり
	{700, 392},                                        // 導入の台
	{1010, 395}, {1200, 320}, {1390, 252},             // 階段を登る
	{1500, 468},                                        // 降り際の小ジャンプ
	{1670, 346},                                        // 降りる段の上
	{2000, 500},                                        // 平地の一息
	{2260, 392}, {2380, 392},                           // 広い台の上
	{2680, 458},                                        // 2 体の上を跳ぶ
	{3040, 388}, {3100, 388},                           // ゴール前の台
	{3300, 500}, {3380, 500},                           // ゴール前の地上
};
constexpr int NCOIN = static_cast<int>(sizeof(kCoins) / sizeof(kCoins[0]));
