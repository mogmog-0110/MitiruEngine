#pragma once

/// @file Renderer3D_Dawn.hpp
/// @brief Dawn（WebGPU）3D レンダラーの宣言。MITIRU_WITH_DAWN=ON のビルドだけで使う
/// @details OOZE DRIVE が使う前方描画の部分集合。メッシュ、トゥーンの段と描画ごとの ToonStyle、半球の環境光、フォグ、縦グラデーションの空、4x MSAA、深度、resolve、中立の肩のトーンマップを扱う

#include <cstddef>
#include <cstdint>
#include <memory>

#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>

namespace mitiru::render
{

/// @brief device が DawnDevice のとき Dawn の 3D レンダラーを作る。それ以外は nullptr を返す
/// @param needs ゲームが宣言した機能 (ADR 0090)。nullptr は旧体制で、今までの時期に作る
[[nodiscard]] std::unique_ptr<IRenderer3D> createDawnRenderer3D(gfx::IDevice* device, const features::RenderNeeds* needs = nullptr);

/// @brief hot reload で宣言が増えたとき、増えた機能をフレームの境目で作る。減った分は手放さない。Dawn のレンダラーでなければ何もしない
void dawnDeclareFeatures(IRenderer3D& renderer, const features::RenderNeeds& needs);

/// @brief WBOIT の蓄積先を作ってあるか。Dawn のレンダラーでなければ false。試験が、宣言したときだけ初期化で作っていることを見る
[[nodiscard]] bool dawnWboitReady(const IRenderer3D& renderer);

/// @brief 副ビュー slot の出力。最終色と同じ形式 (sRGB の値を持つ 8bit) の Dawn の生のハンドルで、UI の `view3d:N` と試験が読む。
///        slot を作っていなければ textureView が nullptr。作ってから 1 度も描いていない間の中身は決まっていない
struct DawnViewImage
{
	void* textureView = nullptr;   ///< WGPUTextureView
	void* texture = nullptr;       ///< WGPUTexture
	int width = 0;
	int height = 0;
};

[[nodiscard]] DawnViewImage dawnViewSlotImage(const IRenderer3D& renderer, int slot);

/// @brief 主ビューの bind group の組の登録簿の数え (ADR 0091)。試験と診断が読む。Dawn のレンダラーでなければ 0
struct DawnBindGroupStats
{
	std::uint64_t sweeps = 0;         ///< 写す物を見直した回数
	std::uint64_t rewrites = 0;       ///< 写す物が違って作り直した組の延べ数 (最初の作成を含む)
	std::uint64_t lateRewrites = 0;   ///< 記録の前に初めて違いが見つかった組の延べ数。作り替えた直後の見直しを忘れた経路があると増える
};

[[nodiscard]] DawnBindGroupStats dawnBindGroupStats(const IRenderer3D& renderer);

/// @brief スキンの頂点の変形の数え (直前に finalizeFrame したフレーム)。試験と診断が読む。Dawn のレンダラーでなければ 0
struct DawnSkinFrameStats
{
	std::uint32_t gpuBodies = 0;     ///< GPU で変形した 1 体 1 prim の数
	std::uint32_t dispatches = 0;    ///< 同じ源をまとめた compute の dispatch の数
	std::uint32_t vertices = 0;      ///< GPU で変形した頂点の延べ数
	std::uint32_t cpuBodies = 0;     ///< GPU で変形できず CPU に回った数
	bool computeReady = false;       ///< スキンの compute を使える
};

[[nodiscard]] DawnSkinFrameStats dawnSkinStats(const IRenderer3D& renderer);

/// @brief Dawn のモデルの読み込みの数え。Engine::streamingReport が読む。Dawn のレンダラーでなければ 0
struct DawnLoadStats
{
	std::uint32_t pending = 0;     ///< 頼んでまだ登録していない読み込み
	std::uint64_t finished = 0;
	std::uint64_t failed = 0;
	double maxFinishMs = 0.0;      ///< 登録 1 件にかかった最大の時間 (メインスレッド)
};

[[nodiscard]] DawnLoadStats dawnLoadStats(const IRenderer3D& renderer);

/// @brief モデル (glTF / GLB / FBX / VRM) を描く前に読み始める。待たない。forceModel は行の `model:` の印。
///        Dawn のレンダラーでなく、モデルと分かる拡張子でも印でもなければ false
bool dawnPreloadAsset(IRenderer3D& renderer, const char* path, bool forceModel);

/// @brief モデルを読み終えて描けるか。読めなかった物は false
[[nodiscard]] bool dawnAssetReady(const IRenderer3D& renderer, const char* path, bool forceModel);

/// @brief 頼んだ読み込みを全部終える。登録した数を返す
std::size_t dawnSettleLoads(IRenderer3D& renderer);

/// @brief 前半を終えた読み込みの後半を、フレームの予算の分だけ進める。登録した数を返す
std::size_t dawnPumpLoads(IRenderer3D& renderer);

/// @brief true にすると、読み終わっていないモデルは描かずに先へ進む (EngineConfig::asyncLoads)。既定は描く前に待つ
void dawnSetAsyncLoads(IRenderer3D& renderer, bool on);

/// @brief 色の仕上げの宣言 (ColorLook.hpp) を読む。組があれば true。Dawn のレンダラーでなければ false
bool dawnDeclareColorLooks(IRenderer3D& renderer, const char* manifestPath);

/// @brief 色の仕上げの粒のシードにする決定論のフレーム番号。Dawn のレンダラーでなければ何もしない
void dawnSetColorFinishSeed(IRenderer3D& renderer, std::uint32_t frame);

/// @brief 名前の組を作り直した回数。名前が無ければ 0
[[nodiscard]] std::uint64_t dawnBindGroupRewrites(const IRenderer3D& renderer, const char* table);

} // namespace mitiru::render
