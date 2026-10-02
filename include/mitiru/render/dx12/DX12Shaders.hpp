#pragma once

/// @file DX12Shaders.hpp
/// @brief DX12 用定数バッファ構造体
/// @details Renderer3D_DX12 の GPU 定数バッファの CPU 側構造体を定義する。

#ifdef _WIN32

#include <cstdint>

namespace mitiru::render
{

// ─────────────────────────────────────────────────────────────
//  DX12 用定数バッファ構造体（256 バイトアラインメント必須）
// ─────────────────────────────────────────────────────────────

/// @brief トランスフォーム用定数バッファ (register(b0))
/// @details D3D12 の CBV は 256 バイトアラインメントを要求する。
struct alignas(256) DX12CbTransform
{
	float world[4][4]{};       ///< ワールド行列
	float view[4][4]{};        ///< ビュー行列
	float projection[4][4]{};  ///< 射影行列
};

/// @brief ライティング用定数バッファ (register(b1))
/// @details D3D12 の CBV は 256 バイトアラインメントを要求する。
struct alignas(256) DX12CbLighting
{
	float lightDir[4]{};          ///< ライト方向 (xyz) + パディング
	float lightColor[4]{};        ///< ライト色 (xyz) + パディング
	float ambientColor[4]{};      ///< アンビエント色 (xyz) + パディング
	float cameraPos[4]{};         ///< カメラ位置 (xyz) + パディング
	float materialDiffuse[4]{};   ///< マテリアル拡散色 (rgba)
	float materialSpecular[4]{};  ///< マテリアル鏡面色 (rgba)
	float materialShininess = 32.0f;  ///< マテリアル光沢度
	/// 影部の色 (トゥーン時のみ使用)。暗くするだけでなく色相を寄せるため rgb で持つ。
	float shadowTint[3]{0.60f, 0.64f, 0.76f};
	float fogColor[4]{};              ///< 距離フォグの色 (a は未使用)
	/// x=かかり始める距離 y=完全に染まる距離 z=有効フラグ (0/1)
	float fogParams[4]{};
	/// マテリアル由来の描画指定。x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
	float materialParams[4]{0.5f, 0.0f, 0.0f, 0.0f};
	/// トゥーンの段 (v40)。x=段数 (1 = 従来の 2 トーン) y=境の幅 z=ハイライト強さ w=ハイライト指数。
	/// 末尾追記なので toon 以外の PS が宣言する短い CbLighting とも整合する
	float toonParams[4]{1.0f, 0.12f, 0.0f, 32.0f};
	float toonMidTint[4]{0.78f, 0.80f, 0.88f, 1.0f};  ///< 3 段以上の中間の帯の色
	/// 半球アンビエント (v43)。法線の y で ground↔sky を混ぜる。半球を使わないときは CPU 側が
	/// 両方に ambientColor を入れるので、シェーダーの lerp は分岐なしで平坦な色になる (w は未使用)
	float ambientSky[4]{};
	float ambientGround[4]{};
	/// 縁光 (v43)。xyz = 色 × 強さ (強さを掛け込んであるので 0 なら黒 = 無効)、w = 1-NdotV の指数
	float rimParams[4]{0.0f, 0.0f, 0.0f, 3.0f};
	/// 材質由来の段付きハイライト (v43)。xyz = ハイライト色 (誘電体は白、金属は自分の色)、
	/// w = toonParams.w に掛ける指数の係数 (roughness が小さいほど鋭い)
	float toonMaterial[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

// 1 draw ごとに ring へ積むので、alignas(256) で 512 にならないよう 256 byte ちょうどに収めておく
static_assert(sizeof(DX12CbLighting) == 256, "DX12CbLighting は CBV の 256 byte 単位ちょうど");

/// @brief 材質のマップと PBR の係数、描画ごとの色の調整 (register(b2)、DX12LitShaders.hpp の CbDrawEx)
struct alignas(256) DX12CbDrawEx
{
	float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};  ///< PBR の基本色 (tint 込み)
	float emissive[4]{};                          ///< rgb = 自発光の係数
	float mapFlags[4]{};                          ///< x = 法線マップ y = 法線の強さ z = 金属・粗さマップ w = 自発光マップ
	float pbr[4]{0.0f, 1.0f, 0.0f, 0.0f};         ///< x = metallic y = roughness z = 遮蔽の強さ
	float tintAdd[4]{};                           ///< rgb = 照明の後に足す色
};

/// @brief froxel と IBL とスポットの影のフレーム定数 (register(b4)、DX12LitShaders.hpp の CbCluster)
struct alignas(256) DX12CbCluster
{
	std::uint32_t grid[4]{};   ///< xyz = froxel の数 w = 局所光の数
	float depth[4]{};          ///< x = near y = far z = Z / log(far/near) w = -Z log(near) / log(far/near)
	float screen[4]{};         ///< x = タイル数 X / 画面幅 y = タイル数 Y / 画面高さ
	float forward[4]{};        ///< xyz = 視線
	float ibl[4]{};            ///< x = 環境マップ有無 y = 環境光の強さ z = prefiltered の最大 mip w = 1 なら副ビュー (デカールを読まない)
	std::uint32_t spotShadowLight[4]{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};  ///< 枠 k の影を使う局所光の番号
	float spotShadowParams[4]{};          ///< x = アトラスの texel の幅 (u) y = 高さ (v)
	float spotShadowViewProj[4][4][4]{};  ///< 枠 k の光の view * proj (column-major)
};

/// @brief 局所光の割り当ての compute の定数
struct alignas(256) DX12CbClusterBuild
{
	std::uint32_t grid[4]{};   ///< xyz = froxel の数 w = 光の数
	float frustum[4]{};        ///< x = tan(水平半角) y = tan(垂直半角) z = near w = far
};

static_assert(sizeof(DX12CbDrawEx) == 256 && sizeof(DX12CbCluster) == 512 && sizeof(DX12CbClusterBuild) == 256);

} // namespace mitiru::render

#endif // _WIN32
