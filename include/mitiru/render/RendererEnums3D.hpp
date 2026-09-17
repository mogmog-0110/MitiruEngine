#pragma once

/// @file RendererEnums3D.hpp
/// @brief IRenderer3D / ISceneFx / IExperimentalRenderer3D が共有する列挙型
/// @details 3 インターフェースに分割 (§6-2) した際、ISceneFx.hpp が IRenderer3D.hpp に
///          依存すると include 循環になるため、両方から参照される列挙型だけをここへ出した。

#include <cstdint>

namespace mitiru::render
{

/// @brief シェーダーモード
enum class ShaderMode3D : uint8_t
{
	Phong = 0,     ///< 標準Phongシェーディング
	Toon,          ///< セルシェーディング
	Unlit,         ///< ライティングなし
	Flat,          ///< フラットシェーディング
	Posterize,     ///< ポスタリゼーション
	Halftone,      ///< ハーフトーン
	Hatching,      ///< ハッチング
	GradientMap,   ///< グラデーションマップ
	Silhouette,    ///< シルエット
	Watercolor,    ///< 水彩風
	PBR,           ///< Cook-Torrance + IBL（DX12のみ実装、DX11はPhongへwarnOnceフォールバック）
};

/// @brief ポストプロセスアウトラインのモード
/// @details ISceneFx::setOutlineMode() で切り替える。
///          DX12では全モードが実装され、DX11ではno-opとなる。
enum class OutlineMode : int
{
	DepthSobel     = 0,  ///< 深度Sobel（線形化）— デフォルト
	DepthLaplacian = 1,  ///< 深度Laplacian 3x3
	DepthSobelNdotV = 2, ///< 深度Sobel + NdotVフィルタ（凹面除外）
	ColorEdge      = 3,  ///< 色エッジ（輝度Sobel）
	DepthColorCombo = 4, ///< 深度+色 複合（両方にエッジがある場合のみ）
	Fresnel        = 5,  ///< Fresnel（メインシェーダー内N.Vリム効果、ポストプロセスなし）
};

/// @brief アウトラインモードの総数
constexpr int OUTLINE_MODE_COUNT = 6;

} // namespace mitiru::render
