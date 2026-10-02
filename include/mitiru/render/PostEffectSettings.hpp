#pragma once

/// @file PostEffectSettings.hpp
/// @brief 3D の後処理の選択肢 (AA の方式、環境遮蔽の方式、GPU 時間を測るパス)

#include <cstdint>

namespace mitiru::render
{

/// @brief 3D の AA の方式。MSAA 4x は主パスの形なので常にかかり、その後に何を足すかを選ぶ
enum class AntiAliasing3D : std::uint8_t
{
	MsaaFxaa,   ///< MSAA 4x + FXAA (既定)
	Msaa,       ///< MSAA 4x だけ
	Taa,        ///< MSAA 4x + TAA (射影を画素内でずらし、動きベクトルで履歴を重ねる)
};

/// @brief 環境遮蔽の方式。どちらも setAmbientOcclusion の半径 (ワールド単位) と強さで動く
enum class AmbientOcclusionMethod : std::uint8_t
{
	Ssao,   ///< 法線の半球に 16 点 (既定。トゥーンの絵に合わせて調整済み、ADR 0042 / 0046)
	Gtao,   ///< XeGTAO (Intel、MIT)。地平線の角から余弦で重みを付けて積む
};

/// @brief GPU 時間を測る後処理のパス (Renderer3D_DX12::postGpuMilliseconds の引数)
enum class PostGpuPass : std::uint32_t
{
	Velocity,           ///< 動きベクトル (動いた物の描き直し + 画面全体の解決)
	Trails,             ///< 剣筋の帯
	AmbientOcclusion,   ///< SSAO または GTAO
	AntiAliasing,       ///< FXAA または TAA
	MotionBlur,         ///< 動きのぼけ (タイル最大 → 近傍最大 → 集め)
	Sky,                ///< 空の LUT・空気遠近の froxel・空の描画
	VolumetricFog,      ///< 体積フォグの注入と積分
	AerialComposite,    ///< 空気遠近とフォグを HDR に掛ける合成
	Upscale,            ///< TAAU か FSR 3.1 と鮮鋭化
	UpscaleInputs,      ///< FSR に渡す 1 標本の深度と反応マスク (半透明・剣筋)
	Particles,          ///< GPU パーティクルの刻みと描画
	HitFeel,            ///< 当たった瞬間の画面の演出
	Count
};

} // namespace mitiru::render
