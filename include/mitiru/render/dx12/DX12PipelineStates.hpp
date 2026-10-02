#pragma once

/// @file DX12PipelineStates.hpp
/// @brief Renderer3D_DX12 の PSO 生成・リソース生成メソッド群
/// @details このファイルは Renderer3D_DX12 クラス定義内でインクルードされる。
///          単独でインクルードしないこと。

// NOTE: このファイルは Renderer3D_DX12 の private セクション内で
//       #include される設計のため、#pragma once 以外のガードは不要。
//
// Renderer3D_DX12 の pipeline state メンバー関数を集める detail-include のハブ。
// Renderer3D_DX12.hpp にある class Renderer3D_DX12 の本体の中から include される。
// 各サブファイルは .inl 形式の class body の断片で、単独のヘッダではない。

// NOLINTBEGIN(build/include)
#include <mitiru/render/dx12/detail/DX12PipelineStates_Setup.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_ForwardRender.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Overlay.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_PostProcess.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Ssao.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Bloom.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Dof.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Shadow.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Occlusion.inl>
// NOLINTEND(build/include)
