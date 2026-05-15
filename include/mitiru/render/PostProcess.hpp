#pragma once

/// @file PostProcess.hpp
/// @brief DX11ポストプロセッシングパイプライン（アンブレラヘッダー）
/// @details レンダーターゲットへの描画結果に対してシェーダーベースの
///          スクリーンエフェクトをチェーン実行する。
///          ピンポンバッファによるマルチパス処理をサポートする。

#ifdef _WIN32

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>
#include <mitiru/render/postprocess/GaussianBlurPass.hpp>
#include <mitiru/render/postprocess/BloomPass.hpp>
#include <mitiru/render/postprocess/ColorGradingPass.hpp>
#include <mitiru/render/postprocess/AtmosphericEffects.hpp>
#include <mitiru/render/postprocess/TransitionEffects.hpp>
#include <mitiru/render/postprocess/PostProcessChain.hpp>

#endif // _WIN32
